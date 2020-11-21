#include <stdio.h>
#include <glob.h>

// TODO: consider using ftw.h and fnmatch.h

int main(int argc, char const *argv[]) {
	if (argc <= 1) {
		fprintf(stderr, "error: missing pattern\n");
		return 1;
	}

	// TODO:
	//  1. print per-iteration
	//  2. error func

	int flags =  GLOB_MARK | GLOB_TILDE | GLOB_BRACE;
	glob_t g = { 0 };
	for (int i = 1; i < argc; i++) {
		if (i == 2) {
			flags |= GLOB_APPEND;
		}
		glob(argv[i], flags, NULL, &g);
	}
	for (size_t i = 0; i < g.gl_pathc; i++) {
		printf("%s\n", g.gl_pathv[i]);
	}
	globfree(&g);
	return 0;
}
