// DEV ONLY HEADERS
#include <stdio.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include "timer.h"

#if !defined(XOSHIRO_NO_THREAD_LOCALS)
#  if defined(__cplusplus) &&  __cplusplus >= 201103L
#    define XOSHIRO_THREAD_LOCAL       thread_local
#  elif defined(__GNUC__) && __GNUC__ < 5
#    define XOSHIRO_THREAD_LOCAL       __thread
#  elif defined(_MSC_VER)
#    define XOSHIRO_THREAD_LOCAL       __declspec(thread)
#  elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#    define XOSHIRO_THREAD_LOCAL       _Thread_local
#  endif
#  ifndef XOSHIRO_THREAD_LOCAL
#    if defined(__GNUC__)
#      define XOSHIRO_THREAD_LOCAL     __thread
#    endif
#  endif
#endif

#if !defined(XOSHIRO_RANDOM_DEVICE)
#  define XOSHIRO_RANDOM_DEVICE "/dev/urandom"
#endif

#define xo_unlikely(x) __builtin_expect(!!(x), 0)

// xoshiro256ss_state implements the "xoshiro256**" algorithm.
// https://en.wikipedia.org/wiki/Xorshift#xoshiro256**
// https://nullprogram.com/blog/2017/09/21/
typedef struct {
	uint64_t s[4];
} xoshiro256ss_state;

static XOSHIRO_THREAD_LOCAL xoshiro256ss_state _xoshiro_next;

static inline uint64_t _xoshiro_rol64(uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

uint64_t xoshiro256ss(xoshiro256ss_state *state) {
	uint64_t *s = state->s;
	uint64_t const result = _xoshiro_rol64(s[1] * 5, 7) * 9;
	uint64_t const t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;
	s[3] = _xoshiro_rol64(s[3], 45);

	return result;
}

// xoshiro256ss_is_zero checks if xoshiro256ss_state n is all zeros and needs
// to be seeded.
static inline int xoshiro256ss_is_zero(xoshiro256ss_state *n) {
	return (n->s[0]|n->s[1]|n->s[2]|n->s[3]) == 0;
}

static inline uint64_t _xoshiro_urand(void) {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
	return ((uint64_t)getpid() << 16) ^ tp.tv_sec ^ tp.tv_nsec;
}

#define XOSHIRO_INIT_BETTER (xoshiro256ss_state){                     \
	.s = {                                                            \
		(_xoshiro_urand() * (uint64_t)(__COUNTER__ + 1)) * 4832413LU, \
		(_xoshiro_urand() * (uint64_t)(__COUNTER__ + 1)) * 9939073LU, \
		(_xoshiro_urand() * (uint64_t)(__COUNTER__ + 1)) * 2238779LU, \
		(_xoshiro_urand() * (uint64_t)(__COUNTER__ + 1)) * 5643241LU, \
	}                                                                 \
}

// xoshiro256ss_seed_weak seeds state with a random seed generated from
// the process's pid and a mix of clock readings.
//
// This function is thread-safe and faster than xoshiro256ss_seed_strong
// as it does not read from the random device.
void xoshiro256ss_seed_weak(xoshiro256ss_state *state) {
	*state = XOSHIRO_INIT_BETTER;
}

// xoshiro256ss_seed_strong seeds state with a strong random seed from
// "/dev/random". If reading from "/dev/random" fails the state is seeded
// using xoshiro256ss_seed_weak.
void xoshiro256ss_seed_strong(xoshiro256ss_state *state) {
	// Ignore errors due to EINTR
	#define ignore_eintr(_n) while((_n) < 0 && errno == EINTR) {}

	int fd;
	ignore_eintr(fd = open(XOSHIRO_RANDOM_DEVICE, O_RDONLY|O_CLOEXEC, 0));
	int done = 0;
	if (fd >= 0) {
		ssize_t n;
		ignore_eintr(n = read(fd, state->s, sizeof(state->s)));
		if (n == sizeof(state->s) && !xoshiro256ss_is_zero(state)) {
			done = 1;
		}
		ignore_eintr(close(fd));
	}
	// Fallback to using pid mixed with clock readings.
	if (!done) {
		xoshiro256ss_seed_weak(state);
	}
	#undef ignore_eintr
}

// xoshiro256ss_seed seeds state using the global seed.
void xoshiro256ss_seed(xoshiro256ss_state *state) {
	xoshiro256ss_state *n = &_xoshiro_next;
	if (xo_unlikely(xoshiro256ss_is_zero(n))) {
		xoshiro256ss_seed_strong(n);
	}
	state->s[0] = xoshiro256ss(n);
	state->s[1] = xoshiro256ss(n);
	state->s[2] = xoshiro256ss(n);
	state->s[3] = xoshiro256ss(n);
}

#define _xo_line  (uint64_t)(__LINE__ ? __LINE__ : 1)
#define _xo_count (uint64_t)(__COUNTER__ + 1)

// XOSHIRO_INIT initializes an xoshiro256ss_state with semi-random values based
// on the __FILE__ and __COUNTER__ macros.
//
// Warning: multiple copies of the same program will start with the same
// random seed. This is a potential issue in distributed systems.
#define XOSHIRO_INIT (xoshiro256ss_state){                                    \
	.s = {                                                                    \
		_xoshiro_rol64((_xo_line * 4832413LU) * (_xo_count * 9295157LU), 7),  \
		_xoshiro_rol64((_xo_line * 9939073LU) * (_xo_count * 6256933LU), 45), \
		_xoshiro_rol64((_xo_line * 2238779LU) * (_xo_count * 3364397LU), 7),  \
		_xoshiro_rol64((_xo_line * 5643241LU) * (_xo_count * 9088697LU), 45), \
	}                                                                         \
}

#undef xo_unlikely

int benchmark_rand(long N, void *data) {
	xoshiro256ss_state *n = (xoshiro256ss_state *)data;
	for (long i = 0; i < N; i++) {
		xoshiro256ss(n);
	}
	return 0;
}

int benchmark_init_strong(long N, void *data) {
	xoshiro256ss_state *n = (xoshiro256ss_state *)data;
	for (long i = 0; i < N; i++) {
		xoshiro256ss_seed_strong(n);
	}
	return 0;
}

void xo_print_state(xoshiro256ss_state *n, const char *prefix) {
	for (int i = 0; i < 4; i++) {
		printf("%s%d: %llu\n", prefix, i, n->s[i]);
	}
}

int main() {
	{
		xoshiro256ss_state state = XOSHIRO_INIT_BETTER;
		printf("state:\n");
		xo_print_state(&state, "  ");
		for (int i = 0; i < 4; i++) {
			printf("%d: %llu\n", i, xoshiro256ss(&state));
			xo_print_state(&state, "  ");
		}
		return 0;
	}
	{
		// xoshiro256ss_state state = { .s = {0, 0, 0, 0} };
		xoshiro256ss_state state = XOSHIRO_INIT_BETTER;
		printf("state:\n");
		xo_print_state(&state, "  ");
		// return 0;
		// xoshiro256ss_seed(&state);
		ns_benchmark_t b = ns_benchmark(NS_SECOND, benchmark_init_strong, &state);
		if (b.failed) {
			printf("FAIL\n");
			return 1;
		}
		ns_fprintf_benchmark(stdout, "is_zero", b);
			printf("PASS\n");
		return 0;
	}

	// xoshiro256ss_state state = { 1, 0, 0, 0 };


	xoshiro256ss_state state = XOSHIRO_INIT;
	// xoshiro256ss_seed(&state);

	// for (int i = 0; i < 4; i++) {
	//     printf("%d: %zu\n", i, (size_t)state.s[i]);
	// }
	// printf("\n");

	for (int i = 0; i < 10; i++) {
		uint64_t r = xoshiro256ss(&state);
		// for (int j = 0; j < 4; j++) {
		// 	printf("  %d: %zu\n", j, (size_t)state.s[j]);
		// }
		printf("%d: %zu\n", i, (size_t)r);
	}
}
