#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>

#include <assert.h>


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
	if (buf_len > prefix_len) {
		if (unlikely(expand_buffer(dst, dst_cap, buf_len - prefix_len) != 0)) {
			return -1;
		}
		if (memcmp(buf, slash_prefix, prefix_len) == 0) {
			size_t n = buf_len - prefix_len;
			memcpy(*dst, &buf[prefix_len], n);
			return n;
		}
		if (memcmp(buf, ansii_prefix, prefix_len) == 0) {
			const char *p = memchr(buf, 'm', buf_len);
			if (p) {
				ssize_t i = p - buf;
				char *d = *dst;
				memcpy(d, buf, i + 1);
				memcpy(&d[i + 1], &buf[i + prefix_len + 1], buf_len - i - prefix_len);
				return buf_len - prefix_len;;
			}
		}
	}
	memcpy(dst, buf, buf_len);
	return buf_len;
}

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;

	const int delim = '\n';

	size_t buf_cap = 512;
	size_t dst_cap = 512;
	char *buf = xmalloc(512);
	char *dst = xmalloc(512);

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
