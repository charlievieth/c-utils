#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>

// TODO: consider using: getprogname()
#define PROGRAM_NAME "cfd"

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

static void __attribute__((__noreturn__)) *xalloc_die();
static void __attribute__((__malloc__)) *xmalloc(size_t n);
static void __attribute__((__malloc__)) *xrealloc(void *p, size_t n);

static void *xalloc_die() {
	fflush(stdout);
	fprintf(stderr, "memory exhausted\n");
	abort();
}

static void *xmalloc(size_t n) {
	void *p = malloc(n);
	if (!p && n != 0) {
		xalloc_die();
	}
	return p;
}

static void *xrealloc(void *p, size_t n) {
	if (!n && p) {
		free(p);
		return NULL;
	}
	p = realloc(p, n);
	if (!p && n) {
		xalloc_die();
	}
	return p;
}

#ifndef powerof2
#define powerof2(x) ((((x)-1)&(x))==0)
#endif

static inline size_t p2roundup(size_t n) {
	if (!powerof2(n)) {
		n--;
		n |= n >> 1;
		n |= n >> 2;
		n |= n >> 4;
		n |= n >> 8;
		n |= n >> 16;
#if SIZE_T_MAX > 0xffffffffU
		n |= n >> 32;
#endif
		n++;
	}
	return (n);
}

static inline int expand_buffer(char **buf, size_t *cap, size_t len) {
	if (unlikely(len > (size_t)SSIZE_MAX + 1)) {
		errno = EOVERFLOW;
		return -1;
	}
	if (unlikely(*cap <= len)) {
		// avoid overflow
		size_t newcap;
		if (len == (size_t)SSIZE_MAX + 1) {
			newcap = (size_t)SSIZE_MAX + 1;
		} else {
			newcap = p2roundup(len + *cap);
		}
		char *newline = xrealloc(*buf, newcap);
		*cap = newcap;
		*buf = newline;
	}
	return 0;
}

#define prefix_len 2
static const char *const slash_prefix = "./";
static const char *const ansii_prefix = "\x1b[";
static_assert(strlen(slash_prefix) == prefix_len, "slash_prefix");
static_assert(strlen(ansii_prefix) == prefix_len, "ansii_prefix");

static ssize_t trim_prefix(char **dst, size_t *dst_cap, const char *buf, const size_t buf_len) {
	if (unlikely(expand_buffer(dst, dst_cap, buf_len) != 0)) {
		return -1;
	}
	char *d = *dst;
	if (buf_len > prefix_len) {
		if (memcmp(buf, slash_prefix, prefix_len) == 0) {
			size_t n = buf_len - prefix_len;
			memcpy(d, &buf[prefix_len], n);
			return n;
		}
		// TODO: remove and write ANSII prefix then use the
		// above logic to strip the prefix.
		if (memcmp(buf, ansii_prefix, prefix_len) == 0) {
			const char *p = memchr(buf, 'm', buf_len);
			if (p) {
				ssize_t i = p - buf;
				memcpy(d, buf, i + 1);
				memcpy(&d[i + 1], &buf[i + prefix_len + 1], buf_len - i - prefix_len);
				return buf_len - prefix_len;;
			}
		}
	}
	memcpy(d, buf, buf_len);
	return buf_len;
}

static int consume_stdin(const unsigned char delim) {
	size_t buf_cap = 256;
	size_t dst_cap = 256;
	char *buf = xmalloc(256);
	char *dst = xmalloc(256);

	ssize_t i;
	while ((i = getdelim(&buf, &buf_cap, delim, stdin)) != -1) {
		ssize_t n = trim_prefix(&dst, &dst_cap, buf, i);
		if (unlikely(n < 0)) {
			goto fatal_error;
		}
		if (unlikely((ssize_t)fwrite(dst, 1, n, stdout) < n)) {
			perror("fwrite");
			goto fatal_error;
		}
	}
	if (ferror(stdin)) {
		if (errno != 0) {
			perror("getdelim");
		}
		goto fatal_error;
	}
	free(buf);
	free(dst);
	fflush(stdout);
	return 0;

fatal_error:
	fprintf(stderr, "fatal error");
	return 1;
}

static void print_usage(bool print_error) {
	if (print_error) {
		fprintf(stderr, "Usage: %s [OPTION]...\n", PROGRAM_NAME);
		fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
	} else {
		printf("Usage: %s [OPTION]... [FILE]...\n", PROGRAM_NAME);
		fputs("\n\
Strip './' filename prefixes from the output of fd (fd-find).\n\
\n\
Options:\n\
  -0, --print0  Line delimiter is NUL, not newline\n\
  -h, --help    Print this help message and exit.\n", stdout);
	}
}

int main(int argc, char const *argv[]) {
	bool null_terminate = false;
	bool invalid_flag = false;
	bool print_help = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-0") == 0 || strcmp(argv[i], "--print0") == 0) {
			if (null_terminate) {
				fprintf(stderr,"%s: null terminate flag ['-0', '--print0'] specified twice\n",
					PROGRAM_NAME);
				invalid_flag = true;
			}
			null_terminate = true;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help = true;
			break;
		} else {
			fprintf(stderr, "%s: unrecognized option: '%s'\n", PROGRAM_NAME, argv[i]);
			invalid_flag = true;
		}
	}
	if (invalid_flag) {
		print_usage(true);
		return 2;
	}
	if (print_help) {
		print_usage(false);
		return 0;
	}

	const unsigned char delim = null_terminate ? 0 : '\n';

	return consume_stdin(delim);
}
