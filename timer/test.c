#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "timer.h"

int test_time_since(void) {
	ns_time_t start = ns_time_now();
	usleep(10);
	ns_duration_t dur = ns_time_since(start);
	if (dur <= 0) {
		printf("%s:%d error: non-positive duration: %+"PRId64"\n",
			__FILE__, __LINE__, dur);
		return 1;
	}
	return 0;
}

int benchmark_time_now_fn(long N, void *data) {
	(void)data;
	ns_time_t start;
	for (long i = 0; i < N; i++) {
		start = ns_time_now();
	}
	assert(ns_clock_nanoseconds(start) > 0);
	return 0;
}

int benchmark_time_since_fn(long N, void *data) {
	(void)data;
	ns_time_t start;
	ns_duration_t dur = 0;
	for (long i = 0; i < N; i++) {
		start = ns_time_now();
		dur = ns_time_since(start);
	}
	assert(dur >= 0);
	return 0;
}

int benchmark_time_now(void) {
	ns_benchmark_t b = ns_benchmark(NS_SECOND, benchmark_time_now_fn, NULL);
	ns_fprintf_benchmark(stdout, "time_now", b);
	return 0;
}

int benchmark_time_since(void) {
	ns_benchmark_t b = ns_benchmark(NS_SECOND, benchmark_time_since_fn, NULL);
	ns_fprintf_benchmark(stdout, "time_since", b);
	return 0;
}

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;
	assert(test_time_since() == 0);

	if (argc == 2 && strcmp(argv[1], "-bench") == 0) {
		printf("Benchmarks:\n");
		assert(benchmark_time_now() == 0);
		assert(benchmark_time_since() == 0);
	}

	printf("PASS\n");
	return 0;
}
