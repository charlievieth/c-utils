#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "hedley.h"

#define unlikely HEDLEY_UNLIKELY

HEDLEY_NO_RETURN HEDLEY_NEVER_INLINE
static void oom_error(const char *function, size_t size);

static void oom_error(const char *function, size_t size) {
	printf("%s: unable to allocate %zu bytes: %m\n", function, size);
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
