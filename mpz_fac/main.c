#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>

int main(int argc, char const *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "USAGE: INT\n");
		return 1;
	}
	long n = atoll(argv[1]);
	if (n < 0) {
		fprintf(stderr, "USAGE: INT\n");
		return 1;
	}

	mpz_t rop;
	mpz_init(rop);
	mpz_fac_ui(rop, (unsigned long)n);

	char *s = mpz_get_str(NULL, 10, rop);
	size_t len = strlen(s);
	fwrite(s, 1, len, stdout);
	// printf("length: %zu\n", strlen(s));

	mpz_clear(rop);
	free(s);
	return 0;
}
