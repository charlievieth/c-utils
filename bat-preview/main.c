#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>

// #define BAT_DEBUG

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x),0)
#endif

static void usage() {
	fprintf(stderr, "bat-preview: [BAT_OPTIONS] -- FILE...\n");
}

static int flag_terminator(const int argc, char *argv[]) {
	for (int i = 0; i < argc - 1; i++) {
		if (strcmp(argv[i], "--") == 0) {
			return i + 1;
		}
	}
	return -1;
}

static int readable_file(const char *name) {
	struct stat buf;
	if (stat(name, &buf) != 0) {
		perror(name);
		return 0;
	}
	if ((buf.st_mode & S_IFREG) != S_IFREG) {
	#ifdef BAT_DEBUG
			fprintf(stderr, "%s: is a directory\n", name);
	#endif
		return 0;
	}
	return 1;
}

int main(int argc, char *argv[]) {
	int i = flag_terminator(argc, argv);
	if (unlikely(i == -1 || i >= argc)) {
		usage();
		return 0;
	}
	if (unlikely(argc > 32)) {
		fprintf(stderr, "too many arguments: %d\n", argc);
		return 1;
	}

	int j = i - 1;
	char **args = calloc(argc, sizeof(char *));
	memcpy(args, &argv[1], sizeof(char *) * j);

#ifdef BAT_DEBUG
	char **p;
	printf("args (pre):\n");
	p = args;
	while (*p) {
		printf("  %s\n", *p++);
	}
#endif

	int n = j;
	do {
		if (readable_file(argv[i])) {
			args[j++] = argv[i];
		}
	} while (++i < argc);

#ifdef BAT_DEBUG
	printf("args (post):\n");
	p = args;
	while (*p) {
		printf("  %s\n", *p++);
	}
#endif

	if (j > n) {
		execvp(args[0], &args[0]);
	}
 	return 0; // no valid arguments
}
