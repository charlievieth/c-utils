#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static int trim_space(char *buf, const int size) {
	int n = 0;
	for (int i = 0; i < size; i++) {
		if (!isspace(buf[i])) {
			buf[n] = buf[i];
			n++;
		}
	}
	return n;
}

static int write_buffer(const char *buf, const size_t size) {
	return fwrite(buf, sizeof(char), size, stdout) == size;
}

int main(void) {
	char buf[BUFSIZ];
	int n = 0;

	while ((n = fread(buf, sizeof(*buf), BUFSIZ, stdin)) == BUFSIZ) {
		int o = trim_space(buf, BUFSIZ);
		if (!write_buffer(buf, o)) {
			perror("error: writing to stdout");
			goto Error;
		}
	}

	if (feof(stdin)) {
		int o = trim_space(buf, n);
		if (!write_buffer(buf, o)) {
			perror("error: writing to stdout");
			goto Error;
		}
	} else if (ferror(stdin)) {
		perror("error: reading from stdin");
		goto Error;
	} else {
		fprintf(stderr, "error: unhandled\n");
		goto Error;
	}

	return 0;

Error:
	return 1;
}
