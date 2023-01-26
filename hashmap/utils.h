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

#endif
