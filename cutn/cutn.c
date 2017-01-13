#include <stdio.h>
#include <stdlib.h>

int main(void) {
	const int SIZE = 4096;
	char buf[SIZE];
	int n = 0;
	while ((n = fread(buf, sizeof(*buf), SIZE, stdin)) == SIZE) {
		for (int i = 0; i < SIZE; ++i) {
			int c = buf[i];
			if (c != '\n') {
				fputc(c, stdout);
			}
		}
	}
	if (feof(stdin)) {
		for (int i = 0; i < n; ++i) {
			int c = buf[i];
			if (c != '\n') {
				fputc(c, stdout);
			}
		}
	} else if (ferror(stdin)) {
		perror("Error: reading stdin");
	}
	return 0;
}
