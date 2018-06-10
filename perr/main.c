#include <stdio.h>

static const int size = 4096;
static unsigned char buf[4096];

int main() {
	size_t n;
	while ((n = fread(buf, 1, size, stdin)) == size) {
		if ((fwrite(buf, 1, size, stderr)) != size) {
			goto ErrStderr;
		}
		if ((fwrite(buf, 1, size, stdout)) != size) {
			goto ErrStdout;
		}
	}
	if (!feof(stdin) && ferror(stdin)) {
		fprintf(stderr, "perr: reading from stdin)\n");
		return 1;
	}
	if ((fwrite(buf, 1, n, stderr)) != n) {
		goto ErrStderr;
	}
	if ((fwrite(buf, 1, n, stdout)) != n) {
		goto ErrStdout;
	}
	return 0;

ErrStdout:
	fprintf(stderr, "perr: writing to stdout)\n");
	return 1;

ErrStderr:
	fprintf(stderr, "perr: writing to stderr)\n");
	return 1;
}
