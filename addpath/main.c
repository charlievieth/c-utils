#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

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

/*
func genSplit(s, sep []byte, sepSave, n int) [][]byte {
	if n == 0 {
		return nil
	}
	if len(sep) == 0 {
		return explode(s, n)
	}
	if n < 0 {
		n = Count(s, sep) + 1
	}

	a := make([][]byte, n)
	n--
	i := 0
	for i < n {
		m := Index(s, sep)
		if m < 0 {
			break
		}
		a[i] = s[: m+sepSave : m+sepSave]
		s = s[m+len(sep):]
		i++
	}
	a[i] = s
	return a[:i+1]
}
*/

char ** split_path(const char *s) {
	int n = count_path_separators(s);
	char **a = calloc(n + 1, sizeof(char *)); // leave last slot empty
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
	a[i] = a[i] = strdup(s);
	return a;
}

// static bool add_path_before = false;
// static bool add_path_after = false;

// void proc_args(int argc, char *argv[]) {
// 	int a = 0;
// 	while((a = getopt(argc, argv, "ab")) != -1) {
// 		switch (a) {
// 		case 'a':
// 			add_path_after = true;
// 			break;
// 		case 'b':
// 			add_path_before = true;
// 			break;
// 		}
// 	}

// 	argc -= optind;
// 	argv += optind;
// 	if (argc < 1) {
// 		/* code */
// 	}
// }

int main(int argc, char *argv[]) {
	assert(argc > 1);
	int a = 0;
	bool add_path_after = false;
	bool add_path_before = false;
	while((a = getopt(argc, argv, "ab")) != -1) {
		switch (a) {
		case 'a':
			add_path_after = true;
			break;
		case 'b':
			add_path_before = true;
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1) {
		fprintf(stderr, "usage: [OPTION] PATHS...\n");
		return 1;
	}
	if (add_path_after && add_path_before) {
		fprintf(stderr, "cannot specify both -a and -b flags\n");
		return 1;
	}
	if (!add_path_after && !add_path_before) {
		fprintf(stderr, "must specify either -a or -b flags\n");
		return 1;
	}

	char **a = split_path(argv[1]);
	char *s;
	int i = 0;
	while ((s = *a++) != NULL) {
		printf("%i: '%s'\n", ++i, s);
	}
	return 0;
}
