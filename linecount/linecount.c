#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <sys/stat.h>

#include "linecount.h"
#include "cdefs.h"
#include "slice.h"
#include "opts.h"

static inline int count_newlines(const unsigned char *buf, const int len) {
	buf = (const unsigned char *)__builtin_assume_aligned(buf, 512);
	int count = 0;
	for (int i = 0; i < len; i++) {
		if (buf[i] == '\n') {
			count++;
		}
	}
	return count;
}

bool lc_setbuf(FILE *fp, unsigned char **buf, int *buf_len) {
	struct stat st;
	if (fstat(fileno(fp), &st) == -1) {
		perror("fstat");
		return false;
	}
	if (unlikely(st.st_blksize > INT_MAX)) {
		fprintf(stderr, "st_blksize (%zu) exceeds INT_MAX (%zu)\n",
			(size_t)st.st_blksize, (size_t)INT_MAX);
		return false;
	}
	int blksize = (int)st.st_blksize;
	if (*buf_len < blksize) {
		free(*buf);
		*buf_len = ALIGN_UP(blksize, 4096);
		if (posix_memalign((void **)buf, 4096, (size_t)*buf_len)) {
			return false;
		}
	}
	if (setvbuf(fp, (char *)*buf, _IOFBF, blksize) != 0) {
		perror("setvbuf");
		return false;
	}
	return true;
}

FILE * open_file(const char *filename, unsigned char **buf, int *buf_len) {
	FILE *fp = fopen(filename, "r");
	if(!fp) {
		perror("fopen");
		return NULL;
	}
	if (!lc_setbuf(fp, buf, buf_len)) {
		fclose(fp);
		return NULL;
	}
	return fp;
}

bool count_lines(const char *filename, unsigned char *buf, const int buf_len) {
	buf = (unsigned char *)__builtin_assume_aligned(buf, 512);

	FILE *fp = fopen(filename, "r");
	if(!fp) {
		perror("fopen");
		return false;
	}

	struct stat stats;
	if (fstat(fileno(fp), &stats) == -1) {
		perror("fstat");
		goto Error;
	}

	int count = 0;
	int ret;
	while((ret = fread((void *)buf, 1, buf_len, fp)) == buf_len) {
		count += count_newlines(buf, buf_len);
	}
	if (ferror(fp)) {
		perror("fread");
		goto Error;
	}
	if (!feof(fp)) {
		fprintf(stderr, "unexpected EOF\n");
		goto Error;
	}
	count += count_newlines(buf, ret);

	fclose(fp);
	return true;

Error:
	fclose(fp);
	return false;
}

int main(int argc, char *argv[]) {
	parse_options(argc, argv);

	printf("Names:\n");
	for (int i = 0; i < opts.names.len; i++) {
		printf("  %d: %s\n", i, opts.names.str[i]);
	}
	printf("Exts:\n");
	for (int i = 0; i < opts.exts.len; i++) {
		printf("  %d: %s\n", i, opts.exts.str[i]);
	}
	printf("Paths:\n");
	for (int i = 0; i < opts.paths.len; i++) {
		printf("  %d: %s\n", i, opts.paths.str[i]);
	}
	/* code */
	return 0;
}
