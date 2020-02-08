#include <stdio.h>

int main() {
	static unsigned char buf[32 * 1024];
	static const int size = sizeof(buf);

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
