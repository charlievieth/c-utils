#ifndef TIMER_H
#define TIMER_H

// WARN: DEBUG ONLY
#include <stdio.h>

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

#if defined(__APPLE__) && defined(__MACH__)
#    include <mach/mach_time.h>
#    define HAVE_MACH_TIME
#else
#    include <time.h>
#endif

// ns_time_t represents an instant in time with nanosecond precision.
typedef struct {
	uint64_t ext; // monotonic clock reading in nanoseconds
} ns_time_t;

// A ns_duration_t represents the elapsed time between two instants
// as an int64 nanosecond count. The representation limits the
// largest representable duration to approximately 290 years.
typedef int64_t ns_duration_t;

// Duration constants
#define NS_NANOSECOND  (ns_duration_t)(1)
#define NS_MICROSECOND (ns_duration_t)(1000 * NS_NANOSECOND)
#define NS_MILLISECOND (ns_duration_t)(1000 * NS_MICROSECOND)
#define NS_SECOND      (ns_duration_t)(1000 * NS_MILLISECOND)
#define NS_MINUTE      (ns_duration_t)(60 * NS_SECOND)
#define NS_HOUR        (ns_duration_t)(60 * NS_MINUTE)

// A ns_benchmark_t is the result of benchmarking a function.
typedef struct {
	ns_duration_t dur;       // benchmark duration
	ns_duration_t ns_per_op; // nanoseconds per op
	int32_t       nops;      // number of times the benchmark function was called
	bool          failed;    // true if the benchmark return non-zero
} ns_benchmark_t;

// ns_clock_nanoseconds returns the monotonic clock reading in nanoseconds.
static inline uint64_t ns_clock_nanoseconds(ns_time_t t) {
	return t.ext;
}

// ns_time_now returns a ns_time_t that stores the current time.
ns_time_t ns_time_now(void) {
#if defined(HAVE_MACH_TIME)
	static __thread mach_timebase_info_data_t tb_info;
	if (tb_info.denom == 0) {
		mach_timebase_info(&tb_info);
	}
	uint64_t ns = mach_absolute_time();
	ns = (ns * tb_info.numer) / tb_info.denom;
#else
	// TODO: check for _POSIX_TIMERS > 0 and _POSIX_MONOTONIC_CLOCK
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp); // TODO: check error
	uint64_t ns = (tp.tv_sec * NS_SECOND) + tp.tv_nsec;
#endif
	return (ns_time_t){ns};
}

static inline ns_duration_t ns_time_since(ns_time_t t) {
	int64_t end = ns_clock_nanoseconds(ns_time_now());
	return (ns_duration_t){end - (int64_t)ns_clock_nanoseconds(t)};
}

static inline int64_t ns_duration_ns(ns_duration_t d) {
	return d;
}

double ns_duration_ms(ns_duration_t d) {
	return (double)ns_duration_ns(d) / (double)NS_MILLISECOND;
}

ns_duration_t ns_time_func(int64_t iterations, void (*funcp)(void*), void *data) {
	ns_time_t t = ns_time_now();
	for (int64_t i = 0; i < iterations; i++) {
		funcp(data);
	}
	return ns_time_since(t);
}

static int32_t _ns_round_down_10(int32_t n) {
	int tens = 0;
	while (n >= 10) {
		n = n / 10;
		tens++;
	}
	int32_t result = 1;
	for (int i = 0; i < tens; i++) {
		result *= 10;
	}
	return result;
}

static int32_t _ns_round_up_10(int32_t n) {
	int32_t base = _ns_round_down_10(n);
	if (n <= base) {
		return base;
	}
	if (n <= (2 * base)) {
		return 2 * base;
	}
	if (n <= (3 * base)) {
		return 3 * base;
	}
	if (n <= (5 * base)) {
		return 5 * base;
	}
	return 10 * base;
}

ns_benchmark_t ns_benchmark(ns_duration_t exp, int (*fn)(long, void*), void *data) {
	#define min(_x, _y) ((_x) <= (_y) ? (_x) : (_y))
	#define max(_x, _y) ((_x) >= (_y) ? (_x) : (_y))
	const int32_t n_max = 1000000000;

	ns_time_t t;
	ns_duration_t d;

	t = ns_time_now();
	if (fn(1, data) != 0) {
		return (ns_benchmark_t){ .failed = true };
	}
	d = ns_time_since(t);

	int32_t n = 1;
	while (ns_duration_ns(d) < ns_duration_ns(exp)) {
 		int32_t last = n;
		int64_t ns_per_op = ns_duration_ns(d) / n;
		n = ns_duration_ns(exp) / ns_per_op;
		n = max(min(n+n/5, last*100), last+1);
		n = _ns_round_up_10(n);
		if (n > n_max) {
			break;
		}
		t = ns_time_now();
		if (fn(n, data) != 0) {
			return (ns_benchmark_t){ .nops = n, .failed = true };
		}
		d = ns_time_since(t);
	}

	return (ns_benchmark_t){
		.dur = d,
		.ns_per_op = ns_duration_ns(d) / n,
		.nops = n,
		.failed = false
	};

	#undef min
	#undef max
}

void fprintf_benchmark(FILE *restrict out, const char *name, ns_benchmark_t b) {
	fprintf(out, "%s        %+"PRId32"        %+"PRId64" ns/op\n", name, b.nops, b.ns_per_op);
}

#if defined(HAVE_MACH_TIME)
#    undef HAVE_MACH_TIME
#endif
#endif /* TIMER_H */

