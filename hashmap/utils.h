#ifndef HMAP_UTILS_H
#define HMAP_UTILS_H

#include <stddef.h>
#include "hedley.h"

void *xmalloc(size_t n)
	HEDLEY_MALLOC HEDLEY_RETURNS_NON_NULL;

void *xcalloc(size_t count, size_t size)
	HEDLEY_MALLOC HEDLEY_RETURNS_NON_NULL;

void *xrealloc(void *ptr, size_t size)
	HEDLEY_MALLOC HEDLEY_RETURNS_NON_NULL;

HEDLEY_NO_RETURN HEDLEY_NEVER_INLINE HEDLEY_PRIVATE
void xdie_impl(const char *msg, const char *file, int line);

#define xdie(msg) xdie_impl(msg, __FILE__, __LINE__)

#endif /* HMAP_UTILS_H */
