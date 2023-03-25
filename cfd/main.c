// vim: ts=4 sw=4

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

// TODO: consider using: getprogname()
#define PROGRAM_NAME "cfd"

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

static void __attribute__((__noreturn__)) *xalloc_die(void);
static void __attribute__((__malloc__)) *xmalloc(size_t n);
static void __attribute__((__malloc__)) *xrealloc(void *p, size_t n);

static void *xalloc_die(void) {
	fflush(stdout);
	fprintf(stderr, "memory exhausted\n");
	abort();
}

static void *xmalloc(size_t n) {
	void *p = malloc(n);
	if (!p && n != 0) {
		xalloc_die();
	}
	return p;
}

static void *xrealloc(void *p, size_t n) {
	if (!n && p) {
		free(p);
		return NULL;
	}
	p = realloc(p, n);
	if (!p && n) {
		xalloc_die();
	}
	return p;
}

#if !defined(SIZE_T_MAX) && !defined(SIZE_MAX)
#error "missing SIZE_T_MAX and SIZE_MAX"
#endif

#ifndef SSIZE_MAX
#define SSIZE_MAX PTRDIFF_MAX
#endif

// WARN: use or remove
static char *trim_prefix_inplace(char *buf, size_t buf_len, size_t *new_len) {
	if (buf_len > 2) {
		if (memcmp(buf, "./", 2) == 0) {
			*new_len = buf_len - 2;
			return &buf[2];
		}
		if (memcmp(buf, "\x1b[", 2) == 0) {
			const char *p = memchr(buf, 'm', buf_len);
			if (p) {
				ssize_t i = p - buf + 1;
				if (strncmp(&buf[i], "./\x1b[0m", strlen("./\x1b[0m")) == 0) {
					i += strlen("./\x1b[0m");
					*new_len = buf_len - i;
					return &buf[i];
				}
				if (strncmp(&buf[i], "./", strlen("./")) == 0) {
					memmove(&buf[i], &buf[i + 2], buf_len - i - 1);
					*new_len = buf_len - 2;
					return buf;
				}
			}
		}
	}
	*new_len = buf_len;
	return buf;
}

// TODO: use of remove
static inline bool contains_ansi_escape_code(const char *str, size_t str_len) {
	const size_t esc_len = strlen("\x1b[");

	return str_len >= esc_len && (
		memcmp(str, "\x1b[", esc_len) == 0 || memmem(str, str_len, "\x1b[", esc_len) != NULL
	);
}

static void strip_ansi(char **dst, size_t *dst_len, const char *str, size_t str_len) {
	unsigned const char *p = (unsigned const char *)str;
	unsigned char c;
	size_t n = 0;

	// Count the number of non-ANSI bytes
	while ((c = *p++)) {
		if (c == '\x1b' && *p == '[') {
			while ((c = *p++)) {
				if (c == 'm') {
					break;
				}
			}
			if (!c) {
				break; // bad sequence
			}
		} else {
			n++;
		}
	}

	// All ANSI
	if (n == 0) {
		// WARN: all ANSI
		if (dst_len) {
			*dst_len = 0;
		}
		if (*dst) {
			*dst[0] = '\0';
		}
		return;
	}

	// No ANSI
	if (n == str_len) {
		*dst = xrealloc(*dst, str_len + 1);
		memcpy(*dst, str, str_len + 1);
		if (dst_len) {
			*dst_len = str_len;
		}
		return;
	}

	if (dst_len) {
		*dst_len = n;
	}
	*dst = xrealloc(*dst, n + 1);
	unsigned char *d = *(unsigned char **)dst;

	p = (unsigned const char *)str;
	while ((c = *p++)) {
		if (c == '\x1b' && *p == '[') {
			while ((c = *p++)) {
				if (c == 'm') {
					break;
				}
			}
			if (!c) {
				break; // bad sequence
			}
		} else {
			*d++ = c;
		}
	}
	*d = '\0';
}

typedef struct {
	char   *line;
	size_t line_len;
	char   *comp;
	size_t comp_len; // WARN
} line_buffer;

#define CONCATX(x, y)      x##y
#define CONCAT(x, y)       CONCATX(x, y)
#define UNIQUE_ALIAS(name) CONCAT(name, __COUNTER__)

#define _line_buffer_for_each(lbuf, len, value, __p, __e)               \
	line_buffer *(__e) = &(lbuf)[(len)];                                \
	for ((value) = (lbuf); (value) < (__e); (value)++)

#define line_buffer_for_each(lbuf, len, value)                          \
		_line_buffer_for_each(lbuf, len, value, UNIQUE_ALIAS(__p), UNIQUE_ALIAS(__e))

#define for_each_if(condition) if (!(condition)) {} else

#define _line_bufferp_for_each(pbuf, plen, value, __p, __e)             \
	line_buffer **(__p) = (pbuf);                                       \
	line_buffer *const *(__e) = &(pbuf)[(plen)];                        \
	while ((__p) < (__e))                                               \
		for_each_if((value = *(__p++)) || true)

#define line_bufferp_for_each(pbuf, plen, value)                        \
		_line_bufferp_for_each(pbuf, plen, value, UNIQUE_ALIAS(__p), UNIQUE_ALIAS(__e))

static void line_buffer_free(line_buffer *b) {
	if (b && b->line) {
		free(b->line);
	}
#ifndef NDEBUG
	if (b) {
		*b = (line_buffer){ 0 };
	}
#endif
}

static line_buffer **line_buffer_to_ptr(line_buffer *in, size_t in_len) {
	assert(in_len > 0);
	if (in_len == 0) {
		return NULL;
	}

	line_buffer **plines = xmalloc(sizeof(line_buffer*) * in_len);
	line_buffer **p = plines;
	line_buffer *v;
	line_buffer_for_each(in, in_len, v) {
		*p++ = v;
	}

#ifndef NDEBUG
	assert(plines[0] == &in[0]);
	assert(plines[in_len-1] == &in[in_len-1]);
#endif
	return plines;
}

// TODO: use strcoll if non-ASCII chars are present
static inline int line_buffer_compare_strings(const void* p1, const void* p2) {
    const line_buffer *b1 = *(const line_buffer* const*)p1;
    const line_buffer *b2 = *(const line_buffer* const*)p2;
    int ret = memcmp(b1->comp, b2->comp, b1->comp_len < b2->comp_len
    	? b1->comp_len : b2->comp_len);
    return ret != 0 ? ret : (ssize_t)b1->comp_len - (ssize_t)b2->comp_len;
}

static bool has_upper_case_chars(char *s, size_t n) {
	const unsigned char *end = (const unsigned char *)(&s[n]);
	unsigned char *p = (unsigned char *)s;
	while (p < end) {
		unsigned char c = *p++;
		if ('A' <= c && c <= 'Z') {
			return true;
		}
	}
	return false;
}

static void str_to_lower(char *s, size_t n) {
	const unsigned char *end = (const unsigned char *)(&s[n]);
	unsigned char *p = (unsigned char *)s;
	while (p < end) {
		unsigned char c = *p;
		if ('A' <= c && c <= 'Z') {
			*p += 'a' - 'A';
		}
		p++;
	}
}

static int consume_stream_sort(FILE *istream, FILE *ostream, const unsigned char delim, bool ignore_case) {
	size_t buf_cap = 128;
	size_t line_cap = 8;
	char *buf = xmalloc(128);
	char *comp = xmalloc(128);
	line_buffer *lines = xmalloc(sizeof(line_buffer) * line_cap);
	line_buffer **plines = NULL;
	size_t li = 0;

	bool has_ansi = false;
	ssize_t buf_len;
	while ((buf_len = getdelim(&buf, &buf_cap, delim, istream)) != -1) {
		size_t dlen;
		char *dst = trim_prefix_inplace(buf, buf_len, &dlen);
		if (!has_ansi) {
			has_ansi = contains_ansi_escape_code(dst, dlen);
		}

		if (li == line_cap) {
			line_cap *= 2;
			lines = xrealloc(lines, sizeof(line_buffer) * line_cap);
		}
		lines[li] = (line_buffer){ 0 }; // zero
		line_buffer *line = &lines[li++];

		if (has_ansi) {
			size_t comp_len;
			strip_ansi(&comp, &comp_len, dst, dlen);
			if (ignore_case) {
				str_to_lower(comp, comp_len);
			}
			// Use one alloc for both the line and comp buffers
			line->line = xmalloc(dlen + comp_len + 2);
			line->line_len = dlen;
			line->comp = &line->line[dlen + 1];
			line->comp_len = comp_len; // WARN
			memcpy(line->line, dst, dlen + 1);
			memcpy(line->comp, comp, comp_len + 1);
		} else {
			if (ignore_case && has_upper_case_chars(dst, dlen)) {
				line->line = xmalloc((dlen * 2) + 2);
				line->line_len = dlen;
				line->comp_len = dlen;
				line->comp = &line->line[dlen + 1];
				memcpy(line->line, dst, dlen + 1);
				memcpy(line->comp, dst, dlen + 1);
				str_to_lower(line->comp, dlen);
			} else {
				line->line = xmalloc(dlen + 1);
				memcpy(line->line, dst, dlen + 1);
				line->line_len = dlen;
				line->comp = line->line; // WARN: sharing pointers
				line->comp_len = dlen; // WARN
			}
		}
	}
	if (ferror(stdin)) {
		if (errno != 0) {
			perror("getdelim");
		}
		goto fatal_error;
	}
	if (li == 0) {
		goto exit_cleanup;
	}

	plines = line_buffer_to_ptr(lines, li);
	// NB: I tried using an inlined version of glibc's qsort but this is faster.
	qsort(plines, li, sizeof(line_buffer *), line_buffer_compare_strings);

	line_buffer *p;
	line_bufferp_for_each(plines, li, p) {
		ssize_t ret = fwrite(p->line, 1, p->line_len, ostream);
		if (unlikely(ret < (ssize_t)p->line_len)) {
			perror("fwrite");
			goto fatal_error;
		}
	}

exit_cleanup:

	fflush(stdout);
	free(buf);
	free(comp);
	if (plines) {
		free(plines);
	}
	// TODO: measure how long it takes to free lines
	if (li > 0) {
		line_buffer_for_each(lines, li, p) {
			line_buffer_free(p);
		}
	}
	free(lines);
	return 0;

fatal_error:
	fprintf(stderr, "fatal error");
	return 1;
}

static int consume_stream(FILE *istream, FILE *ostream, const unsigned char delim) {
	size_t buf_cap = 128;
	char *buf = xmalloc(128);

	ssize_t buf_len;
	while ((buf_len = getdelim(&buf, &buf_cap, delim, istream)) != -1) {
		size_t dlen;
		char *dst = trim_prefix_inplace(buf, buf_len, &dlen);
		if (unlikely(dlen < 0)) {
			goto fatal_error;
		}
		if (unlikely((ssize_t)fwrite(dst, 1, dlen, ostream) < (ssize_t)dlen)) {
			perror("fwrite");
			goto fatal_error;
		}
	}
	if (ferror(stdin)) {
		if (errno != 0) {
			perror("getdelim");
		}
		goto fatal_error;
	}
	free(buf);
	fflush(stdout);
	return 0;

fatal_error:
	fprintf(stderr, "fatal error");
	return 1;
}

static void print_usage(bool print_error) {
	if (print_error) {
		fprintf(stderr, "Usage: %s [OPTION]...\n", PROGRAM_NAME);
		fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
	} else {
		printf("Usage: %s [OPTION]... [FILE]...\n", PROGRAM_NAME);
		fputs("\n\
Strip './' filename prefixes from the output of fd (fd-find).\n\
\n\
Options:\n\
  -0, --print0  Line delimiter is NUL, not newline\n\
  -s, --sort    Sort lines before printing\n\
  -i, --isort   Sort lines case-insensitive before printing\n\
  -v, --verbose Print debug information\n\
  -h, --help    Print this help message and exit.\n", stdout);
	}
}

static inline bool streq(const char *s1, const char *s2) {
	return strcmp(s1, s2) == 0;
}

static inline bool arg_equal(const char *argv, const char *short_name, const char *long_name) {
	return (short_name && streq(argv, short_name)) || (long_name && streq(argv, long_name));
}

static int64_t timespec_nanos(struct timespec ts) {
	int64_t sec = ts.tv_sec;
	int64_t nsec = ts.tv_nsec;
	return (sec * 1e9) + nsec;
}

// static ssize_t trim_prefix(char **dst, size_t *dst_cap, const char *buf, const size_t buf_len) {
int main(int argc, char const *argv[]) {
	// TODO: use a MODE bitmask or something
	bool null_terminate = false;
	bool sort_lines = false;
	bool sort_lines_case = false;
	bool invalid_flag = false;
	bool print_help = false;
	bool verbose = false;

	bool run_benchmarks = false;
	char *bench_filename = NULL;
	long bench_count = 10;

	for (int i = 1; i < argc; i++) {
		if (arg_equal(argv[i], "-0", "--print0")) {
			if (null_terminate) {
				fprintf(stderr,"%s: null terminate flag ['-0', '--print0'] specified twice\n",
					PROGRAM_NAME);
				invalid_flag = true;
			}
			null_terminate = true;
		} else if (arg_equal(argv[i], "-s", "--sort")) {
			if (sort_lines) {
				fprintf(stderr,"%s: sort lines flag ['-s', '--sort'] specified twice\n",
					PROGRAM_NAME);
				invalid_flag = true;
			}
			sort_lines = true;
		} else if (arg_equal(argv[i], "-i", "--isort")) {
			if (sort_lines_case) {
				fprintf(stderr,"%s: sort lines case-insensitive flag ['-i', '--isort'] specified twice\n",
					PROGRAM_NAME);
				invalid_flag = true;
			}
			sort_lines_case = true;
		} else if (arg_equal(argv[i], "-v", "--verbose")) {
			verbose = true;
		} else if (arg_equal(argv[i], "-h", "--help")) {
			print_help = true;
			break;

		// Benchmarks
		} else if (streq(argv[i], "--benchfile")) {
			if (i + 1 == argc) {
				fprintf(stderr,"%s: missing argument to '--benchfile' flag\n",
					PROGRAM_NAME);
				invalid_flag = true;
			} else {
				bench_filename = strdup(argv[++i]);
				run_benchmarks = true;
			}
		} else if (streq(argv[i], "--bench")) {
			if (i + 1 < argc) {
				bench_count = strtol(argv[++i], NULL, 10);
			}
			run_benchmarks = true;

		} else {
			fprintf(stderr, "%s: unrecognized option: '%s'\n", PROGRAM_NAME, argv[i]);
			invalid_flag = true;
		}
	}
	if (unlikely(verbose)) {
		#define format_bool(ok) (ok) ? "true" : "false"

		fprintf(stderr, "# debug: command line arguments:\n");
		fprintf(stderr, "#   null_terminate: %s\n", format_bool(null_terminate));
		fprintf(stderr, "#   sort_lines: %s\n", format_bool(sort_lines));
		fprintf(stderr, "#   sort_lines_case: %s\n", format_bool(sort_lines_case));
		fprintf(stderr, "#   invalid_flag: %s\n", format_bool(invalid_flag));
		fprintf(stderr, "#   print_help: %s\n", format_bool(print_help));
		fprintf(stderr, "#   verbose: %s\n", format_bool(verbose));
		fprintf(stderr, "#   run_benchmarks: %s\n", format_bool(run_benchmarks));
		fprintf(stderr, "#   bench_filename: %s\n", bench_filename ? bench_filename : "NULL");
		fprintf(stderr, "#   bench_count: %li\n", bench_count);
		fprintf(stderr, "\n");

		#undef format_bool
	}
	if (invalid_flag || print_help) {
		free(bench_filename);
		print_usage(invalid_flag); // print to stderr if there is an invalid flag
		return invalid_flag ? 2 : 0;
	}

	const unsigned char delim = null_terminate ? 0 : '\n';

	if (run_benchmarks) {
		struct timespec start;
		FILE *ostream = NULL;
		int exit_code = 1;

		FILE *istream = fopen(bench_filename, "r");
		if (!istream) {
			perror("fopen (bench file)");
			goto bench_exit;
		}

		// Get file size
		if (fseek(istream, 0, SEEK_END) != 0) {
			perror("fseek (bench file)");
			goto bench_exit;
		}
		const int64_t file_bytes = ftello(istream);
		if (fseek(istream, 0, SEEK_SET) != 0) {
			perror("fseek (bench file)");
			goto bench_exit;
		}
		const double file_mbs = (double)file_bytes / (double)(1024 * 1024);
		fprintf(stderr, "benchmark: n: %li file: %s size: %.2f\n",
				bench_count, bench_filename, file_mbs);

		timespec_get(&start, TIME_UTC);

		ostream = fopen("/dev/null", "w");
		if (!ostream) {
			perror("fopen (/dev/null)");
			goto bench_exit;
		}

		if (sort_lines || sort_lines_case) {
			for (long i = 0; i < bench_count; i++) {
				int ret = consume_stream_sort(istream, ostream, delim, sort_lines_case);
				if (unlikely(ret != 0)) {
					fprintf(stderr, "consume_stream_sort\n");
					goto bench_exit;
				}
				if (unlikely(fseek(istream, 0, SEEK_SET) != 0)) {
					perror("fseek");
					goto bench_exit;
				}
			}
		} else {
			for (long i = 0; i < bench_count; i++) {
				int ret = consume_stream(istream, ostream, delim);
				if (unlikely(ret != 0)) {
					fprintf(stderr, "consume_stream\n");
					goto bench_exit;
				}
				if (unlikely(fseek(istream, 0, SEEK_SET) != 0)) {
					perror("fseek");
					goto bench_exit;
				}
			}
		}

		struct timespec end;
		timespec_get(&end, TIME_UTC);

		// ns and secs are total
		int64_t ns = timespec_nanos(end) - timespec_nanos(start);
		double secs = (double)(ns / 1e9);
		fprintf(stderr, "duration:   %.3fs\n", secs);
		fprintf(stderr, "average:    %.3fs\n", secs/(double)bench_count);
		fprintf(stderr, "throughput: %.3f MB/s\n", (1 / secs) * (file_mbs * (double)bench_count));
		exit_code = 0;

bench_exit:
		if (istream) {
			fclose(istream);
		}
		if (ostream) {
			fclose(ostream);
		}
		if (bench_filename) {
			free(bench_filename);
		}
		return exit_code;
	}

	int exit_code = 0;
	if (sort_lines || sort_lines_case) {
		exit_code = consume_stream_sort(stdin, stdout, delim, sort_lines_case);
	} else {
		exit_code = consume_stream(stdin, stdout, delim);
	}
	if (bench_filename) {
		free(bench_filename);
	}
	return exit_code;
}
