#ifndef LC_SLICE_H
#define LC_SLICE_H

#include <stdbool.h>

typedef struct {
	int  len;
	int  cap;
	char **str;
} string_array;

void string_array_init(string_array *a);
bool string_array_append(string_array *a, const char *s);

#endif /* LC_SLICE_H */
