#include <stdio.h>
#include <string.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define BUFSIZE (1024 * 1024)

int main() {
	char buf[BUFSIZE] __attribute__((aligned(4096))); // page align
	char out[BUFSIZE] __attribute__((aligned(4096)));

	memset(buf, 0, BUFSIZE);
	if (setvbuf(stdout, out, _IOFBF, 1024 * 1024) != 0) {
		fprintf(stderr, "setvbuf failed\n");
		return 1;
	}
	while (1) {
		if (unlikely(fwrite(buf, 1, BUFSIZE, stdout) < BUFSIZE)) {
			break;
		}
	}
	return 0;
}
