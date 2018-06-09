#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>

typedef struct {
	char *data;
	int  len;
	int  cap;
} byte_slice;

void byte_slice_init(byte_slice *b) {
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

// bool byte_slice_do_grow(byte_slice *b, const int len) {
// 	if (b->len+len < b->cap) {
// 		char *data = NULL;
// 		if (b->data) {
// 			b->cap = 2*b->cap + len;
// 			data = realloc(b->data, b->cap);
// 		} else {
// 			b->len = 0;
// 			b->cap = len;
// 			data = malloc(len);
// 		}
// 		if (!data) {
// 			if (b->data) {
// 				free(b->data);
// 				b->data = NULL;
// 			}
// 			assert(data);
// 			return false;
// 		}
// 		b->data = data;
// 	}
// 	return true;
// }

bool byte_slice_do_grow(byte_slice *a, int n) {
	int cap = a->cap > 0 ? 2*a->cap + n : n;
	char *data = realloc(a->data, (size_t)cap);
	if (!data) {
		free(a->data);
		assert(a->data);
		return false;
	}
	a->cap = cap;
	a->data = data;
	return true;
}

static inline bool byte_slice_grow(byte_slice *a, const int n) {
	return n <= a->cap - a->len ? true : byte_slice_do_grow(a, n);
}

void byte_slice_append(byte_slice *dst, char *src, const int len) {
	assert(len >= 0);
	if (len > 0) {
		byte_slice_grow(dst, len);
		memcpy(dst->data + dst->len, src, len);
		dst->len += len;
	}
}

typedef struct {
	int  len;
	int  cap;
	char *data;
} char_slice;

void char_slice_init(char_slice *a) {
	a->len = 0;
	a->cap = 0;
	a->data = NULL;
}

bool char_slice_do_grow(char_slice *a, int n) {
	int cap = a->cap > 0 ? 2*a->cap + n : n;
	char *data = realloc(a->data, (size_t)cap);
	if (!data) {
		free(a->data);
		assert(a->data);
		return false;
	}
	a->cap = cap;
	a->data = data;
	return true;
}

static inline bool char_slice_grow(char_slice *a, int n) {
	if (n <= a->cap - a->len) {
		return true;
	}
	return char_slice_do_grow(a, n);
}

int char_slice_write_char(char_slice *a, char c) {
	if (!char_slice_grow(a, 1)) {
		return 0;
	}
	a->data[a->len++] = c;
	return 1;
}

int char_slice_unread_char(char_slice *a) {
	if (a->len > 0) {
		a->data[--a->len] = '\0';
	}
	return 1;
}

int char_slice_write_string(char_slice *a, const char *s) {
	int n = strlen(s);
	if (!char_slice_grow(a, n+1)) {
		return 0;
	}
	memcpy(a->data + a->len, s, n+1);
	a->len += n;
	return 1;
}

typedef enum {
	BUFIO_ERR_NONE,
	BUFIO_ERR_BUFFER_FULL,
	BUFIO_ERR_READ,
	BUFIO_ERR_NO_PROGRESS,
	BUFIO_ERR_EOF
} bufio_error;

typedef struct {
	char      *buf;
	FILE      *rd;
	int       len; // buffer length (constant)
	int       r;   // read pos
	int       w;   // write pos
	bufio_error err;
} bufio_reader;

int bufio_buffered(bufio_reader *b) { return b->w - b->r; }

void bufio_fill(bufio_reader *b) {
	if (b->r > 0) {
		memmove(b->buf, b->buf + b->r, b->r - b->w);
		b->w -= b->r;
		b->r = 0;
	}
	assert(b->w < b->len);

	int limit = 100;
	while (limit--) {
		int size = b->len - b->w;
		int n = fread(b->buf+b->w, 1, size, b->rd);
		assert(n >= 0);
		b->w += n;
		if (n != size) {
			if (feof(b->rd)) {
				b->err = BUFIO_ERR_EOF;
				return;
			}
			if (ferror(b->rd)) {
				b->err = BUFIO_ERR_READ;
				return;
			}
		}
		if (n > 0) {
			return;
		}
	}
	b->err = BUFIO_ERR_NO_PROGRESS;
}

// TODO: use 'const unsigned char' for delim ???
bufio_error bufio_read_slice(bufio_reader *b, byte_slice *line, const int delim) {
	bufio_error err = BUFIO_ERR_NONE;
	for (;;) {
		// Search buffer.
		char *p = memchr(b->buf+b->r, delim, b->w);
		if (p) {
			line->data = b->buf + b->r;
			line->len = (p - b->buf) + 1;
			line->cap = line->len;
			assert(line->len >= 0);
			break;
		}

		// Pending error?
		if (b->err != BUFIO_ERR_NONE) {
			line->data = b->buf + b->r;
			line->len = b->w - b->r;
			line->cap = line->len;
			assert(line->len >= 0);
			b->r = b->w;
			err = b->err;
			break;
		}

		// Buffer full?
		if (bufio_buffered(b) >= b->len) {
			b->r = b->w;
			line->data = b->buf;
			line->len = b->len;
			line->cap = line->len;
			err = BUFIO_ERR_BUFFER_FULL;
			break;
		}

		bufio_fill(b); // buffer is not full
	}

	return err;
}

int main() {
	char_slice a;
	char_slice_init(&a);
	char_slice_write_string(&a, "'Hello, World!'\n");
	char_slice_write_string(&a, "' 1 Hello, World!'\n");
	char_slice_write_string(&a, "' 2 Hello, World!'\n");
	printf("Ok: %s\n", a.data);
	// fwrite(a.data, 1, a.len, stdout);
	// printf("Hello, World!\n");
	return 0;
}

// int main() {
// 	byte_slice b = { 0 };
// 	byte_slice_append(&b, "Hello, World!\n", strlen("Hello, World!\n"));
// 	byte_slice_append(&b, " 1 Hello, World!\n", strlen(" 1 Hello, World!\n"));
// 	byte_slice_append(&b, " 2 Hello, World!\n", strlen(" 2 Hello, World!\n"));
// 	fwrite(b.data, 1, b.len, stdout);
// 	// printf("Hello, World!\n");
// 	return 0;
// }

/*
byte_slice byte_slice_grow(byte_slice b, const int len) {
	if (b.data == NULL) {
		b.len = 0;
		b.cap = len;
		b.data = malloc(len);
		assert(b.data);
	} else if (b.len+len < b.cap) {
		b.cap = 2*b.cap + len;
		char *data = realloc(b.data, b.cap);
		assert(data);
	}
	return b;
}

byte_slice byte_slice_append(byte_slice dst, char *src, const int len) {
	dst = byte_slice_grow(dst, len);
	memcpy(dst.data + dst.len, src, len);
	dst.len += len;
	return dst;
}

int main(int argc, char const *argv[]) {
	byte_slice b = { 0 };
	b = byte_slice_append(b, "Hello, World!\n", strlen("Hello, World!\n"));
	b = byte_slice_append(b, " 1 Hello, World!\n", strlen(" 1 Hello, World!\n"));
	b = byte_slice_append(b, " 2 Hello, World!\n", strlen(" 2 Hello, World!\n"));
	fwrite(b.data, 1, b.len, stdout);
	// printf("Hello, World!\n");
	return 0;
}
*/
