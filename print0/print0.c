#include <stdio.h>
#include <string.h>

// static void null_terminate(char *buf, int size) {
// 	char *p;
// 	while ((size > 0 && (p = memchr(buf, '\n', size)))) {
// 		*p = '\0';
// 		buf = p;
// 		size -= (int)(p - buf);
// 	}
// }

static void null_terminate(char *buf, int size) {
	for (int i = 0; i < size; i++) {
		if (buf[i] == '\n') {
			buf[i] = 0;
		}
	}
}

static int write_buffer(const char *buf, const size_t size) {
	return fwrite(buf, sizeof(char), size, stdout) == size;
}

static char buf[BUFSIZ];

int main(void) {
	int n = 0;

	while ((n = fread(buf, sizeof(*buf), BUFSIZ, stdin)) == BUFSIZ) {
		null_terminate(buf, BUFSIZ);
		if (!write_buffer(buf, BUFSIZ)) {
			perror("error: writing to stdout");
			return 1;
		}
	}

	if (feof(stdin)) {
		null_terminate(buf, n);
		if (!write_buffer(buf, n)) {
			perror("error: writing to stdout");
			return 1;
		}
	} else if (ferror(stdin)) {
		perror("error: reading from stdin");
		return 1;
	} else {
		fprintf(stderr, "error: feof\n");
		return 1;
	}

	return 0;
}
