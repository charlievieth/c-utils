#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <locale.h>
#include <wchar.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>

#include "hedley.h"

// Simple buffer implementation

struct buffer {
	char   *buf;
	size_t cap;
	size_t len;
};

HEDLEY_NON_NULL(1)
static void buffer_reset(struct buffer *b) {
	b->cap = 0;
	b->len = 0;
}

HEDLEY_NON_NULL(1)
static void buffer_free(struct buffer *b) {
	free(b->buf);
	b->buf = NULL;
	buffer_reset(b);
}

HEDLEY_NON_NULL(1)
static int buffer_grow(struct buffer *b, size_t len) {
	if (len > b->cap-b->len) {
		size_t cap = (b->cap*2) + len;
		char *buf = realloc(b->buf, cap);
		assert(buf);
		if (!buf) {
			buffer_free(b);
			return -1;
		}
		b->buf = buf;
		b->cap = cap;
	}
	return 0;
}

HEDLEY_NON_NULL(1, 2)
static int buffer_write(struct buffer *b, const void *buf, size_t len) {
	if (buffer_grow(b, len) == -1) {
		return -1;
	}
	memcpy(&b->buf[b->len], buf, len);
	b->len += len;
	return 0;
}

/*
static int buffer_append_char(struct buffer *b, unsigned char c) {
	if (buffer_grow(b, 1) == -1) {
		return -1;
	}
	b->buf[b->len++] = c;
	return 0;
}

static const char *buffer_str(struct buffer *b) {
	if (!b->buf || b->len == 0) {
		return NULL;
	}
	if (b->buf[b->len-1] != '\0') {
		if (buffer_append_char(b, '\0') == -1) {
			return NULL;
		}
	}
	return b->buf;
}
*/

// ANSI Parsing

#define RUNE_SELF 0x80

static inline bool is_numeric(unsigned char c) {
	return '0' <= c && c <= '9';
}

static inline bool is_print(unsigned char c) {
	return '\x20' <= c && c <= '\x7e';
}

static inline bool is_crtl_seq_start(unsigned char c) {
	return c == '\\' || c == '[' || c == '(' || c == ')';
}

HEDLEY_NON_NULL(1)
static inline int match_operating_system_command(const unsigned char *p, int64_t length) {
	int64_t i = 5;
	for ( ; i < length && is_print(p[i]); i++) {
	}
	if (i < length) {
		if (p[i] == '\x07') {
			return i + 1;
		}
		if (p[i] == '\x1b' && i < length-1 && p[i+1] == '\\') {
			return i + 2;
		}
	}
	return -1;
}

HEDLEY_NON_NULL(1)
static inline int match_control_sequence(const unsigned char *p, int64_t length) {
	int64_t i = 2;
	for ( ; i < length; i++) {
		if (!is_numeric(p[i]) && p[i] != ';') {
			break;
		}
	}
	if (i < length) {
		unsigned char c = p[i];
		if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '@') {
			return i + 1;
		}
	}
	return -1;
}

HEDLEY_NON_NULL(1)
static inline int64_t has_ansi(const unsigned char *s) {
	const unsigned char *p = s;
	unsigned char c;
	if (p) {
		while ((c = *p++) != '\0') {
			switch (c) {
			case '\x0e':
			case '\x0f':
			case '\x1b':
			case '\x08':
				return p - s - 1;
			}
		}
	}
	return -1;
}

static inline bool rune_start(unsigned char c) {
	return (c&0xC0) != 0x80;
}

HEDLEY_NON_NULL(1)
static int64_t decode_rune_in_string(const unsigned char *p, int64_t length) {
	if (length < 1) {
		return 0;
	}
	mbstate_t mb = { 0 };
	int64_t size = mbrlen((const char *)p, length, &mb);
	return size > 0 ? size : 1;
}

HEDLEY_NON_NULL(1)
static int64_t decode_last_rune_in_string(const unsigned char *p, int64_t length) {
	const int utf_max = 4;

	int64_t end = length;
	if (end == 0) {
		return -1;
	}
	int64_t start = end - 1;
	if (p[start] < RUNE_SELF) {
		return 1;
	}
	int64_t lim = end - utf_max;
	if (lim < 0) {
		lim = 0;
	}
	for (start--; start >= lim; start--) {
		if (rune_start(p[start])) {
			break;
		}
	}
	if (start < 0) {
		start = 0;
	}
	int64_t size = decode_rune_in_string((&p[start]), end-start);
	if (start+size != end) {
		return 1;
	}
	return size;
}

HEDLEY_NON_NULL(1,3,4)
static int next_ansi_escape_sequence(const char *s, int64_t length, int64_t *start, int64_t *end) {
	const unsigned char *p = (const unsigned char *)s;
	assert(start);
	assert(end);
	*start = -1;
	*end = -1;

	int64_t i = has_ansi(p);
	if (i < 0) {
		return -1;
	}

	for ( ; i < length; i++) {
		switch (p[i]) {
		case '\x08':
			// backtrack to match: `.\x08`
			if (i > 0 && p[i-1] != '\n') {
				if (p[i-1] < RUNE_SELF) {
					*start = i - 1;
					*end = i + 1;
					return 0;
				}

				int64_t n = decode_last_rune_in_string(p, i);
				assert(n > 0);
				if (n < 0) {
					n = 0; // WARN
				}
				*start = i - n;
				*end = i + 1;
				return 0;
			}
			break;
		case '\x1b':
			// match: `\x1b[\\[()][0-9;]*[a-zA-Z@]`
			if (i+2 < length && is_crtl_seq_start(p[i+1])) {
				int64_t j = match_control_sequence(&p[i], length-i);
				if (j != -1) {
					*start = i;
					*end = i + j;
					return 0;
				}
			}

			// match: `\x1b][0-9];[[:print:]]+(?:\x1b\\\\|\x07)`
			if (i+5 < length && p[i+1] == ']' && is_numeric(p[i+2]) && p[i+3] == ';' && is_print(p[i+4])) {
				int64_t j = match_operating_system_command(&p[i], length-i);
				if (j != -1) {
					*start = i;
					*end = i + j;
					return 0;
				}
			}

			// match: `\x1b.`
			if (i+1 < length && p[i+1] != '\n') {
				if (p[i+1] < RUNE_SELF) {
					*start = i;
					*end = i + 2;
					return 0;
				}

				int64_t n = decode_rune_in_string(&p[i+1], length-(i+1));
				*start = i;
				*end = i + n + 1;
				return 0;
			}
			break;
		case '\x0e':
		case '\x0f':
			// match: `[\x0e\x0f]`
			*start = i;
			*end = i + 1;
			return 0;
		}
	}

	return -1;
}

HEDLEY_NON_NULL(1,3)
static int strip_ansi(const char *s, int64_t length, struct buffer *b) {
	int64_t start, end;
	int64_t prev = 0;
	int i = 0;
	while (i < length) {
		int rc = next_ansi_escape_sequence(&s[i], length-i, &start, &end);
		// fprintf(stderr, "i: %i start: %lli end: %lli\n", i, start, end); // WARN
		if (rc == -1 || start == -1) {
			break;
		}
		start += i;
		i += end;
		if (start-prev > 0) {
			// fprintf(stderr, "writing: %lli\n", start); // WARN
			if (buffer_write(b, &s[prev], start-prev) == -1) {
				return -1;
			}
		}
		prev = i;
	}
	if (prev == 0) {
		// no ANSI escape sequences found
		buffer_write(b, s, length); // copy whole string
	} else {
		buffer_write(b, &s[prev], length-prev);
	}
	return 0;
}

HEDLEY_NON_NULL(2)
static int read_ignoring_eintrio(int fd, void *buf, size_t size) {
	int n;
	do {
		n = read(fd, buf, size);
	} while (errno == EINTR);
	return n;
}

HEDLEY_NON_NULL(2)
static int write_ignoring_eintrio(int fd, void *buf, size_t size) {
	int n;
	do {
		n = write(fd, buf, size);
	} while (errno == EINTR);
	return n;
}

static int process_fildes(int fd_in, int fd_out, int benchmark) {
	struct buffer b = { 0 };

	const int64_t min_read = 512;
	int64_t cap = 32 * 1024;
	assert(cap > min_read); // sanity check

	char *buf = malloc(cap);
	assert(buf);
	char *p = buf;

	ssize_t n;
	while (1) {
		int64_t len = p - buf;
		if ((cap - len) < min_read) {
			char *new_buf = realloc(buf, cap * 2);
			assert(new_buf);
			if (!new_buf) {
				goto error;
			}
			cap *= 2;
			buf = new_buf;
			p = &buf[len];
		}
		if ((n = read_ignoring_eintrio(fd_in, p, cap-len)) <= 0) {
			break;
		}
		p += n;
	};
	if (n < 0) {
		perror("read");
		goto error;
	}
	p[0] = '\0';

	buffer_grow(&b, p-buf);

	if (benchmark <= 0) {
		if (strip_ansi(buf, p-buf, &b) == -1) {
			goto error;
		}
		size_t off = 0;
		while (off < b.len) {
			n = write_ignoring_eintrio(fd_out, &b.buf[off], b.len-off);
			if (n == -1) {
				perror("write");
				goto error;
			}
			off += n;
		}
	} else {
		for (int i = 0; i < benchmark; i++) {
			buffer_reset(&b);
			if (strip_ansi(buf, p-buf, &b) == -1) {
				goto error;
			}
			size_t off = 0;
			while (off < b.len) {
				n = write_ignoring_eintrio(fd_out, &b.buf[off], b.len-off);
				if (n == -1) {
					perror("write");
					goto error;
				}
				off += n;
			}
		}
	}

	free(buf);
	buffer_free(&b);
	return 0;

error:
	free(buf);
	buffer_free(&b);
	return 1;
}

int main(int argc, char const *argv[]) {
	// TODO: close stdout on exit
	setlocale(LC_ALL, "");
	const int benchmark = 10000;

	if (argc <= 1 || strcmp(argv[1], "-") == 0) {
		if (process_fildes(STDIN_FILENO, STDOUT_FILENO, benchmark) == -1) {
			return 1;
		}
	}
	for (int i = 1; i < argc; i++) {
		 int fd = open(argv[i], O_RDONLY);
		 if (fd < 0) {
		 	perror("open");
		 	fprintf(stderr, "error opening file: %s\n", argv[i]);
		 	return 1;
		 }
		 if (process_fildes(fd, STDOUT_FILENO, benchmark) == -1) {
		 	fprintf(stderr, "error processing file: %s\n", argv[i]);
		 	return 1;
		 }
	}
	return 0;
}
