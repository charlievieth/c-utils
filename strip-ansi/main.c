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

#include <pcre2.h>

static const char *const strip_pattern = "(?:\x1b[\\[()][0-9;]*[a-zA-Z@]|\x1b][0-9];[[:print:]]+(?:\x1b\\\\|\x07)|\x1b.|[\x0e\x0f]|.\x08)";
static const size_t strip_pattern_size = strlen(strip_pattern);

char *escape_string(const char *s) {
	const unsigned char *p = (const unsigned char *)s;
	int n = strlen(s);
	int bufsize = n * 2;
	char *buf = malloc(bufsize);
	char *b = buf;

	#define grow(m)                                  \
		do {                                         \
			ptrdiff_t sz = b - buf;                  \
			if (sz <= (ptrdiff_t)m) {                \
				size_t new_size = (bufsize + m) * 2; \
				buf = realloc(buf, new_size);        \
				assert(buf);                         \
				bufsize = new_size;                  \
				b = &buf[sz];                        \
			}                                        \
		} while(0)

	#define append(s)             \
		do {                      \
			size_t m = strlen(s); \
			grow(m);              \
			strcpy(b, s);         \
			b += m;               \
		} while(0)

	#define append_char(c) \
		do {               \
			grow(1);       \
			*b++ = c;      \
		} while(0);

	for (int i = 0; i < n; i++) {
		unsigned char c = p[i];
		switch (c) {
		case '\\':
			append("\\\\");
			break;
		case '\t':
			append("\\t");
			break;
		case '\v':
			append("\\v");
			break;
		case '\r':
			append("\\r");
			break;
		case '\n':
			append("\\n");
			break;
		default:
			if (iscntrl(c)) {
				grow(5);
				int m = sprintf(b, "\\x%.2x", c);
				assert(m >= 0);
				b += m;
			} else {
				append_char(c);
			}
		}
	}
	append_char('\0');

	#undef append_char
	#undef append
	#undef grow

	return buf;
}

int print_pcre2_error(int errcode, const char *format, ...) {
	int return_code = 0;
	char *errbuf = NULL;
	char *bufp = NULL;

	errbuf = calloc(256, 1);
	assert(errbuf);
	if (!errbuf) {
		goto error;
	}
	int n = pcre2_get_error_message(errcode, (unsigned char *)errbuf, 256);
	switch (n) {
	case PCRE2_ERROR_NOMEMORY:
		strcpy(errbuf, "PCRE2_ERROR_NOMEMORY");
		// n = strlen("PCRE2_ERROR_NOMEMORY");
		break;
	case PCRE2_ERROR_BADDATA:
		strcpy(errbuf, "PCRE2_ERROR_BADDATA");
		// n = strlen("PCRE2_ERROR_BADDATA");
		break;
	case 1:
		strcpy(errbuf, "INVALID ERROR CODE");
		// n = strlen("INVALID ERROR CODE");
		break;
	}

	bufp = NULL;
	size_t sizep;
	FILE *mstream = open_memstream(&bufp, &sizep);
	assert(mstream);
	if (!mstream) {
		goto error;
	}

	fprintf(mstream, "Error (%d): %s: ", errcode, errbuf);

	va_list args;
	va_start(args, format);
	assert(vfprintf(mstream, format, args) >= 0);
	va_end(args);

	assert(fflush(mstream) == 0);
	if (sizep > 0 && bufp[sizep - 1] != '\n') {
		fputc('\n', mstream);
	}
	assert(fclose(mstream) == 0);

	fwrite(bufp, 1, sizep, stderr);

exit:
	if (errbuf) {
		free(errbuf);
	}
	if (bufp) {
		free(bufp);
	}

	return return_code;

error:
	return_code = 1;
	goto exit;
}

static pcre2_code *re;
static pcre2_match_data *match_data;
static pcre2_match_context *mcontext;
static pcre2_jit_stack *jit_stack;

int compile_pattern() {
	int errcode;
	PCRE2_SIZE erroffset;
	uint32_t options = PCRE2_MULTILINE | PCRE2_JIT_COMPLETE;
	re = pcre2_compile((const unsigned char*)strip_pattern, options,
		strip_pattern_size, &errcode, &erroffset, NULL);
	if (!re) {
		print_pcre2_error(errcode, "invalid pattern at index %zu: `%s`",
			erroffset, strip_pattern);
		assert(re);
		return 1;
	}

	int rc = pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
	assert(rc == 0);

	mcontext = pcre2_match_context_create(NULL);
	assert(mcontext);

	jit_stack = pcre2_jit_stack_create(32*1024, 1024*1024, NULL);
	assert(jit_stack);

	match_data = pcre2_match_data_create_from_pattern(re, NULL);
	assert(match_data);

	return 0;
}

/*
int main(void) {
	const int SIZE = 4096;
	char buf[SIZE];
	int n = 0;
	while ((n = fread(buf, sizeof(*buf), SIZE, stdin)) == SIZE) {
		for (int i = 0; i < SIZE; ++i) {
			int c = buf[i];
			if (c != '\n') {
				fputc(c, stdout);
			}
		}
	}
	if (feof(stdin)) {
		for (int i = 0; i < n; ++i) {
			int c = buf[i];
			if (c != '\n') {
				fputc(c, stdout);
			}
		}
	} else if (ferror(stdin)) {
		perror("Error: reading stdin");
	}
	return 0;
}
*/


static int read_ignoring_eintrio(int fd, void *buf, size_t size) {
	int n;
	do {
		n = read(fd, buf, size);
	} while (errno == EINTR);
	return n;
}

int replace_all(const char *subject, size_t length) {
	size_t bufsize = length + 512;
	char *buf = malloc(bufsize);
	assert(buf);
	if (!buf) {
		goto error;
	}

	/*
	int pcre2_substitute(
		const pcre2_code *code,
		PCRE2_SPTR subject,
		PCRE2_SIZE length,
		PCRE2_SIZE startoffset,
		uint32_t options,
		pcre2_match_data *match_data,
		pcre2_match_context *mcontext,
		PCRE2_SPTR replacement,
		PCRE2_SIZE rlength,
		PCRE2_UCHAR *outputbuffer,
		PCRE2_SIZE *outlengthptr,
	);
	*/

	uint32_t options = PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_LITERAL;
	int rc = pcre2_substitute(
		re,                             // pcre2_code *code
		(const unsigned char *)subject, // subject
		length,                         // length
		0,                              // startoffset
		options,                        // options
		// TODO: not needed
		// match_data,                     // *match_data
		NULL,                     // *match_data
		// TODO: not needed
		// mcontext,                       // *mcontext
		NULL,                       // *mcontext
		(const unsigned char *)"",      // replacement
		0,                              // rlength
		(unsigned char *)buf,           // *outputbuffer
		&bufsize                        // *outlengthptr
	);
	if (rc < 0) {
		print_pcre2_error(rc, "pcre2_substitute failed");
		goto error;
	}

	printf("rc: %d\n", rc);
	printf("length: %zu\n", length);
	printf("bufsize: %zu\n", bufsize);

	// fwrite(buf, 1, bufsize, stdout);

	return 0;

error:
	free(buf);
	return 1;
}

#define RUNE_SELF 0x80

static inline bool is_numeric(unsigned char c) {
	return '0' <= c && c <= '9';
}

static inline bool is_print(unsigned char c) {
	return '\x20' <= c && c <= '\x7e';
}

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

static inline bool is_crtl_seq_start(unsigned char c) {
	return c == '\\' || c == '[' || c == '(' || c == ')';
}

// static inline int64_t has_ansi(const unsigned char *p, int64_t length) {
// 	int64_t i;
// 	for (i = 0; i < length; i++) {
// 		switch (p[i]) {
// 		case '\x0e':
// 		case '\x0f':
// 		case '\x1b':
// 		case '\x08':
// 			return i;
// 		}
// 	}
// 	return -1;
// }

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
				return p - s;
			}
		}
	}
	return -1;
}

static inline bool rune_start(unsigned char c) {
	return (c&0xC0) != 0x80;
}

static int64_t decode_rune_in_string(const unsigned char *p, int64_t length) {
	if (length < 1) {
		return 0;
	}
	mbstate_t mb = { 0 };
	int64_t size = mbrlen((const char *)p, length, &mb);
	return size > 0 ? size : 1;
}

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

// typedef struct {
// 	size_t start;
// 	size_t end;
// } char_range;

int next_ansi_escape_sequence(const char *s, int64_t length, int64_t *start, int64_t *end) {
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

int strip_ansi(const char *s, int64_t length, char **buf, int64_t *bufsize) {

	return -1;
}

int process_stdin() {
	const int64_t min_read = 512;
	int64_t cap = 32 * 1024;
	assert(cap > min_read); // sanity check

	char *buf = malloc(cap);
	assert(buf);
	char *p = buf;

	int n;
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
		if ((n = read_ignoring_eintrio(STDIN_FILENO, p, cap-len)) <= 0) {
			break;
		}
		p += n;
	};
	if (n < 0) {
		perror("read");
		goto error;
	}
	p[0] = '\0';

	// printf("##########\n");
	// fwrite(buf, 1, p-buf, stdout);
	// // printf("%s", buf);
	// printf("##########\n");

	// int pcre2_match(
	// 	const pcre2_code *code,
	// 	PCRE2_SPTR subject,
	// 	PCRE2_SIZE length,
	// 	PCRE2_SIZE startoffset,
	// 	uint32_t options,
	// 	pcre2_match_data *match_data,
	// 	pcre2_match_context *mcontext
	// );
	uint32_t options = PCRE2_NO_UTF_CHECK;
	int rc = pcre2_match(
		re,
		(const unsigned char *)buf,
		p-buf,
		0,
		options,
		match_data,
		mcontext
	);
	printf("rc: %d\n", rc);
	if (rc < 0) {
		print_pcre2_error(rc, "match failed");
	}

	// replace_all(buf, p-buf);
	free(buf);

	return 0;

error:
	free(buf);
	return 1;
}

int main(int argc, char const *argv[]) {
	setlocale(LC_ALL, "");

	// const char *test = "\x1b[1mhello \x1b[mw\x1b.7o\x1b.8r\x1b(Bl\x1b[2@d";
	const char *test = "\x1b[1mhello \x1b[Kworld";
	const int test_len = strlen(test);
	// char_range range = { 0 };

	int i = 0;
	int64_t prev = 0;
	int64_t start, end;
	while (i < test_len) {
		int rc = next_ansi_escape_sequence(&test[i], test_len-i, &start, &end);
		if (rc == -1 || start == -1) {
			break;
		}
		start += i;
		i += end;
		printf("i: %d start: %d end: %d\n", i, start, end);
		printf("'%.*s'\n", start, &test[prev]);

		prev = i;
	}
	if (prev > 0) {
		printf("'%.*s'\n", test_len, &test[prev]);
	}
	return 0;


	// const char *test = "日a本b語ç日ð本Ê語þ日¥本¼語i日";
	// const size_t test_len = strlen(test);
	// int n = decode_last_rune_in_string((const unsigned char *)test, test_len);
	// printf("%d: %s\n", n, &test[test_len - n]);
	// return 0;

	// char *s = escape_string(strip_pattern);
	// printf("pattern: `%s`\n", s);
	// free(s);
	// return 0;

	int ret = compile_pattern();
	printf("ret: %d\n", ret);

	return process_stdin();
}

/*
int process_stdin() {
	const int SIZE = 8192;
	char *buf = malloc(SIZE);
	assert(buf);

	int64_t cap = 32 * 1024;
	int64_t len = 0;
	char *data = malloc(cap);
	assert(data);

	int n;
	while ((n = fread(buf, sizeof(*buf), SIZE, stdin)) > 0) {
		if (SIZE > cap-len) {
			int64_t new_cap = (2 * cap) + SIZE;
			char *new_data = realloc(data, new_cap);
			assert(new_data);
			if (!new_data) {
				free(data);
				return 1;
			}
			data = new_data;
			cap = new_cap;
		}
		memcpy(&data[len], buf, n);
		if (n != SIZE) {
			break;
		}
	}
	if (feof(stdin)) {
		for (int i = 0; i < n; ++i) {
			int c = buf[i];
			if (c != '\n') {
				fputc(c, stdout);
			}
		}
	} else if (ferror(stdin)) {
		perror("Error: reading stdin");
	}
	free(buf);
	return 0;
}
*/

// void example() {
// 	int rc;
// 	pcre2_code *re;
// 	pcre2_match_data *match_data;
// 	pcre2_match_context *mcontext;
// 	pcre2_jit_stack *jit_stack;

// 	re = pcre2_compile("pattern", PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroffset, NULL);
// 	rc = pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
// 	mcontext = pcre2_match_context_create(NULL);
// 	jit_stack = pcre2_jit_stack_create(32*1024, 512*1024, NULL);
// 	pcre2_jit_stack_assign(mcontext, NULL, jit_stack);
// 	match_data = pcre2_match_data_create(re, 10);
// 	rc = pcre2_match(re, subject, length, 0, 0, match_data, mcontext);
// 	/* Process result */

// 	pcre2_code_free(re);
// 	pcre2_match_data_free(match_data);
// 	pcre2_match_context_free(mcontext);
// 	pcre2_jit_stack_free(jit_stack);
// }
