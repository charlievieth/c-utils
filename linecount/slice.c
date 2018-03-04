#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "slice.h"

void string_array_init(string_array *a) {
	a->len = 0;
	a->cap = 0;
	a->str = NULL;
}

bool string_array_append(string_array *a, const char *s) {
	assert(a);
	assert(s);
	if (a->cap < a->len + 1) {
		// TODO: check if alloc failed
		if (a->cap == 0) {
			a->cap = 8;
			a->str = malloc(sizeof(char *) * (size_t)a->cap);
		} else {
			a->cap *= 2;
			a->str = realloc(a->str, sizeof(char *) * (size_t)a->cap);
		}
	}
	// TODO: limit max length
	size_t len = strlen(s);
	char *copy = malloc(len);
	memcpy(copy, s, len);
	a->str[a->len++] = copy;
	return true; // TODO: return false on error
}
