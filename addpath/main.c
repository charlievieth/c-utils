#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

typedef struct {
	int  len;
	int  cap;
	char **data;
} string_slice;

static void string_slice_init(string_slice *a) {
	a->len = 0;
	a->cap = 0;
	a->data = NULL;
}

static void string_slice_free(string_slice *a, bool free_strings) {
	if (a->data) {
		if (free_strings) {
			for (int i = 0; i < a->len; i++) {
				if (a->data[i]) {
					free(a->data[i]);
					a->data[i] = NULL;
				}
			}
		}
		free(a->data);
	}
	string_slice_init(a); // zero everything
}

static bool string_slice_do_grow(string_slice *a, int n) {
	int cap = a->cap > 0 ? 2*a->cap + n : n;
	char **data = realloc(a->data, sizeof(char *) * (size_t)cap);
	if (!data) {
		string_slice_free(a, true);
		assert(a->data); // blow up here if assert is enabled
		return false;
	}
	a->cap = cap;
	a->data = data;
	return true;
}

static inline bool string_slice_grow(string_slice *a, int n) {
	assert(n >= 0);
	if (n <= a->cap - a->len) {
		return true;
	}
	return string_slice_do_grow(a, n);
}

static bool string_slice_append(string_slice *a, char *s) {
	assert(s);
	if (!string_slice_grow(a, 1)) {
		return false;
	}
	a->data[a->len++] = s;
	return true;
}

static bool string_slice_contains(string_slice *a, char *s) {
	assert(s);
	for (int i = 0; i < a->len; i++) {
		if (strcmp(a->data[i], s) == 0) {
			return true;
		}
	}
	return false;
}

char * string_slice_join(string_slice *a, char *sep) {
	assert(sep);
	if (a->len == 0) {
		return strdup("\0");
	}
	if (a->len == 1) {
		return strdup(a->data[0]);
	}
	size_t slen = strlen(sep);
	size_t n = slen * (a->len - 1);
	for (int i = 0; i < a->len; i++) {
		n += strlen(a->data[i]);
	}
	char *s = malloc(n + 1);
	assert(strcat(s, a->data[0]));
	for (int i = 1; i < a->len; i++) {
		assert(strcat(s, sep)); // lazy and dangerous
		assert(strcat(s, a->data[i]));
	}
	return s;
}

int count_path_separators(const char *s) {
	const unsigned char *p = (const unsigned char *)s;
	int n = 0;
	do {
		if (*p == ':') {
			n++;
		}
	} while (*++p);
	return n;
}

char ** split_path(const char *s) {
	const int n = count_path_separators(s);
	char **a = calloc(n + 2, sizeof(char *)); // leave last slot empty
	int i = 0;
	while (i < n) {
		char *p = strchr(s, ':');
		if (!p) {
			break;
		}
		a[i] = strndup(s, p - s);
		s = p + 1;
		i++;
	};
	a[i] = strdup(s);
	a[i + 1] = NULL;
	return a;
}

static bool empty_string(const char *s) {
	return !s || *s == '\0';
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "missing PATH...\n");
		return 1;
	}

	string_slice *all = alloca(sizeof(string_slice));
	assert(all);
	string_slice_init(all);

	for (int i = 1; i < argc; i++) {
		char **paths = split_path(argv[i]);
		if (!paths) {
			fprintf(stderr, "error (split_path): out of memory\n");
			return 1;
		}
		char *path;
		while ((path = *paths++) != NULL) {
			if (empty_string(path) || string_slice_contains(all, path)) {
				continue;
			}
			if (!string_slice_append(all, path)) {
				fprintf(stderr, "error (string_slice_append): out of memory\n");
				return 1;
			}
		}
	}

	char *path = string_slice_join(all, ":");
	size_t len = strlen(path);
	fwrite(path, 1, len, stdout);
	return 0;
}
