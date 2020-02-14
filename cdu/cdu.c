#ifdef __STDC_ALLOC_LIB__
#define __STDC_WANT_LIB_EXT2__ 1
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include <pthread.h>

// uncomment to print debug messages
// #define QUEUE_DEBUG

#include "rpa_queue.h"

//  TODO: Check GCC or Clang
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

typedef struct {
	rpa_queue_t         *queue;
	atomic_int_fast64_t size;
	pthread_t           thread_id;
} thread_info;

static int64_t file_size(const char *restrict path) {
	struct stat st;
	if (unlikely(lstat(path, &st) != 0)) {
		perror(path);
		return 0;
	}
	return (int64_t)st.st_size;
}

void *worker(void *arg) {
	fprintf(stderr, "worker: start\n");
	thread_info *info = arg;

	// WARN: this is super shitty
	int x = 0;
	for (;;) {
		char *path;
		if (!rpa_queue_pop(info->queue, (void**)&path)) {
			x++;
			printf("worker: pop: %i\n", x);
			if (x >= 5) {
				printf("worker: pop: EXIT\n");
				goto worker_exit;
			}
			continue; // WARN: this is shity
		}
		if (!path) {
			printf("worker: null\n");
			goto worker_exit; // signal to quit
		}
		int64_t size = file_size(path);
		atomic_fetch_add_explicit(&info->size, size, memory_order_relaxed);
		free(path);
	}

worker_exit:
	fprintf(stderr, "worker: exit\n");
	return NULL;
}

#ifndef CDU_NUM_CPU
#define CDU_NUM_CPU 12
#endif


static char *memdup(const char *restrict p, size_t n) {
	char *b = malloc(n + 1);
	if (!b) {
		return NULL;
	}
	memcpy(b, p, n);
	b[n-1] = '\0';
	return b;
}

char *human_size(int64_t n) {
	static const int64_t KB = 1024;
	static const int64_t MB = KB * 1024;
	static const int64_t GB = MB * 1024;
	static const int64_t TB = GB * 1024;
	// static const int64_t PB = TB * 1024;

	char *s = malloc(22); // enough space for max int + letter + null
	if (!s) {
		return NULL;
	}
	assert(n >= 0); // TODO: better check

	// TODO: this is a mess: we could just set the divisor and char

	int ret;
	if (n < KB) {
		ret = sprintf(s, "%"PRId64"B", n);
		goto done;
	}
	if (n < MB) {
		if (n < 10 * KB) {
			double f = (double)n / (double)KB;
			ret = sprintf(s, "%.1fK", f);
		} else {
			ret = sprintf(s, "%"PRId64"K", n / KB);
		}
		goto done;
	}
	if (n < GB) {
		if (n < 10 * MB) {
			double f = (double)n / (double)MB;
			ret = sprintf(s, "%.1fM", f);
		} else {
			ret = sprintf(s, "%"PRId64"M", n / MB);
		}
		goto done;
	}
	if (n < TB) {
		if (n < 10 * GB) {
			double f = (double)n / (double)GB;
			ret = sprintf(s, "%.1fG", f);
		} else {
			ret = sprintf(s, "%"PRId64"G", n / GB);
		}
		goto done;
	}
	if (n < 10 * TB) {
		double f = (double)n / (double)TB;
		ret = sprintf(s, "%.1fT", f);
	} else {
		ret = sprintf(s, "%"PRId64"T", n / TB);
	}

done:
	if (ret < 0) {
		fprintf(stderr, "sprintf(): failed\n");
		assert(ret >= 0);
		return NULL;
	}

	return s;
}

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;

	static const int thread_count = CDU_NUM_CPU;
	static const int queue_size = thread_count * 2;

	rpa_queue_t *queue = NULL;
	if (!rpa_queue_create(&queue, queue_size)) {
		assert(false);
	}

	// TODO: consider setting thread attributes
	thread_info *infos = calloc(thread_count, sizeof(thread_info));
	assert(infos);

	for (int i = 0; i < thread_count; i++) {
		atomic_init(&infos[i].size, 0);
		infos[i].queue = queue;
		int s = pthread_create(&infos[i].thread_id, NULL, &worker, &infos[i]);
		if (s != 0) {
			fprintf(stderr, "error: pthread_create(): %d\n", s);
			assert(false);
		}
	}

	// TODO: set the buffer size of STDIN
	size_t cap = 4096;
	char *line = malloc(cap);
	assert(line);

	for (;;) {
		ssize_t n = getdelim(&line, &cap, '\0', stdin);
		if (n < 0) {
			fprintf(stderr, "getdelim: done\n");
			break;
		}
		char *buf = memdup(line, n);
		assert(buf);
		if (!rpa_queue_push(queue, buf)) {
			fprintf(stderr, "rpa_queue_push() failed\n");
			assert(false);
		}
	}

	// TODO: use rpa_queue_interrupt_all() and make this cleaner

	fprintf(stderr, "sending NULL paths\n");
	for (int i = 0; i < queue_size; i++) {
		if (!rpa_queue_push(queue, NULL)) {
			fprintf(stderr, "rpa_queue_push() failed\n");
			assert(false);
		}
	}

	fprintf(stderr, "destroying queue\n");
	if (!rpa_queue_term(queue)) {
		fprintf(stderr, "rpa_queue_term(): failed\n");
		assert(false);
	}

	int64_t total_size = 0;
	for (int i = 0; i < thread_count; i++) {
		int n = pthread_join(infos[i].thread_id, NULL);
		if (n != 0) {
			fprintf(stderr, "pthread_join(): failed\n");
			assert(false);
		}
		total_size += infos[i].size;
	}

	// TODO: free resources
	char *out = human_size(total_size);
	if (!out) {
		fprintf(stderr, "human_size(): failed\n");
		assert(out);
		return 1;
	}
	printf("size: %s\n", out);

	return 0;
}
