#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void usage() {
	fprintf(stderr, "bat-preview-history: [HISTORY LINE]\n");
}

static inline bool is_space(const unsigned char c) {
	return c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r' ||
		c == ' ' || c == 0x85 || c == 0xA0;
}

static inline bool is_digit(const unsigned char c) {
	return '0' <= c && c <= '9';
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		usage();
		return 1;
	}
	const char *s = argv[1];
	while (s && is_space(*s)) {
		s++;
	}
	while (s && is_digit(*s)) {
		s++;
	}
	while (s && is_space(*s)) {
		s++;
	}
	printf("%s\n", s);
 	return 0;
}
