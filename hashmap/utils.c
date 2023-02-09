#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "hedley.h"

#define unlikely HEDLEY_UNLIKELY

HEDLEY_NO_RETURN HEDLEY_NEVER_INLINE
static void oom_error(const char *function, size_t size);

static void oom_error(const char *function, size_t size) {
#if defined(__GNUC__)
	// NB: '%m' prints the string corresponding to the error code in errno
	// but is a GNU extension and not supported by ISO C.
	printf("%s: unable to allocate %zu bytes: %m\n", function, size);
#else
	printf("%s: unable to allocate %zu bytes: %s\n", function, size, strerror(errno));
#endif
	exit(1);
	HEDLEY_UNREACHABLE();
}

void *xmalloc(size_t n) {
	void *p = malloc(n);
	if (unlikely(p == NULL)) {
		oom_error("malloc", n);
	}
	return p;
}

void *xcalloc(size_t count, size_t size) {
	void *p = calloc(count, size);
	if (unlikely(p == NULL)) {
		oom_error("calloc", count * size);
	}
	return p;
}

void *xrealloc(void *ptr, size_t size) {
	void *p = realloc(ptr, size);
	if (unlikely(p == NULL)) {
		if (ptr) {
			free(ptr);
		}
		oom_error("realloc", size);
	}
	return p;
}

void xdie_impl(const char *msg, const char *file, int line) {
	fprintf(stderr, "%s:%i %s\n", file, line, msg);
	exit(1);
	HEDLEY_UNREACHABLE();
}
