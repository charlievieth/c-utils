#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include <ftw.h>

#include <stdatomic.h>

//  TODO: Check GCC or Clang
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

typedef struct queue_node queue_node;

struct queue_node {
	queue_node *next;
	void      *value;
};

queue_node *new_queue_node(void *value) {
	queue_node *node = malloc(sizeof(queue_node));
	if (unlikely(!node)) {
		assert(node);
		return NULL;
	}
	node->next = NULL;
	node->value = value;
	return node;
}

typedef struct {
	pthread_mutex_t lock;
	size_t          len;
	queue_node      *head;
	queue_node      *tail;
	pthread_cond_t  cond; // TODO: move to top ???
	int             closed; // TODO: implement this
} queue;

#define __queue_pthread_fatal(_errnum, _op)                             \
	do {                                                                \
		((void)fprintf(stderr, "%s:%d error (%d): %s: %s\n",            \
			__FILE_NAME__, __LINE__, _errnum, strerror(_errnum), _op)); \
		assert(0);                                                      \
		exit(1);                                                        \
	} while(0);

#define queue_lock(_q)                                                 \
	do {                                                               \
		int _ret;                                                      \
		if (unlikely((_ret = pthread_mutex_lock(&(_q)->lock)) != 0)) { \
			__queue_pthread_fatal(_ret, "pthread_mutex_lock");         \
		}                                                              \
	} while (0)

#define queue_unlock(_q)                                                 \
	do {                                                                 \
		int _ret;                                                        \
		if (unlikely((_ret = pthread_mutex_unlock(&(_q)->lock)) != 0)) { \
			__queue_pthread_fatal(_ret, "pthread_mutex_unlock");         \
		}                                                                \
	} while (0)

int queue_init(queue *q) {
	int res;

	pthread_mutexattr_t mattr;
	if ((res = pthread_mutexattr_init(&mattr) != 0)) {
		__queue_pthread_fatal(res, "pthread_mutexattr_init");
		return res;
	}
	*q = (queue){ .lock = { 0 }, .cond = { 0 } };
	if ((res = pthread_mutex_init(&q->lock, &mattr) != 0)) {
		__queue_pthread_fatal(res, "pthread_mutex_init");
		return res;
	}
	if ((res = pthread_mutexattr_destroy(&mattr) != 0)) {
		__queue_pthread_fatal(res, "pthread_mutexattr_destroy");
		return res;
	}

	pthread_condattr_t cattr;
	if ((res = pthread_condattr_init(&cattr) != 0)) {
		__queue_pthread_fatal(res, "pthread_condattr_init");
		return res;
	}
	if ((res = pthread_cond_init(&q->cond, &cattr) != 0)) {
		__queue_pthread_fatal(res, "pthread_cond_init");
		return res;
	}
	if ((res = pthread_condattr_destroy(&cattr)) != 0) {
		__queue_pthread_fatal(res, "pthread_condattr_destroy");
		return res;
	}

	queue_node *node = new_queue_node(NULL);
	if (!node) {
		assert(node);
		return ENOMEM;
	}
	q->head = node;
	q->tail = node;
	return 0;
}

bool queue_is_closed(queue *q) {
	queue_lock(q);
	bool closed = q->closed != 0;
	queue_unlock(q);
	return closed;
}

int queue_close(queue *q) {
	queue_lock(q);
	int ret = q->closed ? EINVAL : 0;
	q->closed++;
	queue_unlock(q);
	if (ret == 0) {
		pthread_cond_broadcast(&q->cond);
	}
	return ret;
}

// static inline int __queue_open(queue *q) { return q->closed == 0; }

// int queue_open(queue *q) {
// 	queue_lock(q);
// 	int open = __queue_open(q);
// 	queue_unlock(q);
// 	return open;
// }

size_t queue_len(queue *q) {
	queue_lock(q);
	size_t len = q->len;
	queue_unlock(q);
	return len;
}

int queue_push(queue *q, void *val) {
	queue_node *node = new_queue_node(val);
	queue_lock(q);
	q->tail->next = node;
	q->tail = node;
	q->len++;
	queue_unlock(q);
	pthread_cond_signal(&q->cond);
	return 0;
}

void *queue_pop(queue *q) {
	void *val = NULL;
	queue_lock(q);
	queue_node *head = q->head;
	if (head != q->tail) {
		val = head->value;
		q->head = head->next;
		q->len--;
		free(head);
	}
	queue_unlock(q);
	// TODO: signal waiters
	return val;
}

void *queue_pop_wait(queue *q) {
	queue_lock(q);
	while (q->head == q->tail && !q->closed) {
		pthread_cond_wait(&q->cond, &q->lock);
	}
	// WARN: figure out what close should look like
	void *val = NULL;
	if (q->head != q->tail) {
		queue_node *head = q->head;
		val = head->value;
		q->head = head->next;
		q->len--;
		free(head);
	}
	queue_unlock(q);
	return val;
}

enum walker_status {
	W_SKIP_DIR      = -1,
	W_SKIP_FILES    = -2,
	W_TRAVERSE_LINK = -3,
};

typedef int (*walk_func)(const char *path, int typ);

typedef struct {
	walk_func fn;
	queue     *workc;
	queue     *enqueuec;
	atomic_bool done;
} walker;

typedef struct {
	walker *w;
	const char *dir;
} walk_context;

typedef struct {
	const char *dir;
} walk_item;

static void walker_enqueue(walker *w, char *dir) {
	if (!w->done) {
		queue_push(w->enqueuec, dir);
	}
}

static char *walker_join_paths(const char *dir, size_t dlen, struct dirent *dp) {
	size_t nlen = dp->d_namlen; // TODO: support systems without d_namlen
	char *joined = malloc(dlen + strlen("/") + nlen + 1);
	if (unlikely(!joined)) {
		fprintf(stderr, "walker_join_paths: OOM\n");
		assert(joined);
		exit(1);
	}
	memcpy(joined, dir, dlen);
	memcpy(&joined[dlen], "/", 1);
	memcpy(&joined[dlen + 1], dp->d_name, nlen + 1);
	return joined;
}

int walker_on_dirent(walker *w, const char *dir, size_t dlen, struct dirent *dp) {
	char *joined = walker_join_paths(dir, dlen, dp);
	if (dp->d_type == DT_DIR) {
		walker_enqueue(w, joined);
		return 0;
	}
	// TODO: support W_TRAVERSE_LINK
	return w->fn(joined, dp->d_type);
}

int walker_do_walk(walker *w, const char *dirname) {
	DIR *dir = opendir(dirname);
	if (!dir) {
		perror(dirname);
		return 0; // TODO: check different error types
	}

	int ret = 0;
	struct dirent *dp;
	const size_t dirlen = strlen(dirname);
	bool skip_files = false;

	while ((dp = readdir(dir))) {
		if (dp->d_type == DT_UNKNOWN) {
			continue;
		}
		if (skip_files && dp->d_type == DT_REG) {
			continue;
		}
		if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
			continue;
		}
		ret = walker_on_dirent(w, dirname, dirlen, dp);
		if (ret != 0) {
			if (ret == W_SKIP_FILES) {
				skip_files = true;
				ret = 0;
				continue;
			}
			break;
		}
	}

	closedir(dir);
	return ret;
}

void *walker_do_work(void *data) {
	assert(data);
	walker *w = (walker *)data;
	while (!w->done) {
		char *dir = (char *)queue_pop_wait(w->workc);
		if (!dir || w->done) {
			break;
		}
		int ret = walker_do_walk(w, dir);
		(void)ret; // WARN: handle this

		// WARN WARN
		// use a union or something for the return value
		// WARN WARN

		if (dir) {
			free(dir);
		}
	};
	return NULL;
}

int walker_walk(const char *root) {
	const int nprocs = 12; // TODO: don't hard code this
	(void)root;   // WARN
	(void)nprocs; // WARN

	walker *w = malloc(sizeof(walker));
	if (!w) {
		fprintf(stderr, "error: OOM\n");
		assert(0);
		return 1;
	}
	w->workc = malloc(sizeof(queue));
	w->enqueuec = malloc(sizeof(queue));
	if (!w->workc || !w->enqueuec) {
		fprintf(stderr, "error: OOM\n");
		assert(0);
		return 1;
	}

	int ret;
	if ((ret = queue_init(w->workc)) != 0) {
		fprintf(stderr, "error: failed to initialize: workc: %d\n", ret);
		assert(0);
		return ret;
	}
	if ((ret = queue_init(w->enqueuec)) != 0) {
		fprintf(stderr, "error: failed to initialize: enqueuec: %d\n", ret);
		assert(0);
		return ret;
	}

	pthread_t *threads = calloc(nprocs, sizeof(pthread_t));

	for (int i = 0; i < nprocs; i++) {
		if ((ret = pthread_create(&threads[i], NULL, walker_do_work, w)) != 0) {
			assert(ret == 0);
			goto err_cleanup;
		}
	}

err_cleanup:
	queue_close(w->workc);
	queue_close(w->enqueuec);

	for (int i = 0; i < nprocs; i++) {
		if (!threads[i]) {
			continue;
		}
		pthread_cancel(threads[i]);
	}
	free(threads);
	free(w->workc);
	free(w->enqueuec);
	free(w);

	return 1;
}

typedef struct {
	int id;
	queue *q;
	char *val;
	atomic_int *count;
} thread_arg;

void *thread_test(void *p) {
	thread_arg *a = p;
	printf("%d: len (start): %zu\n", a->id, queue_len(a->q));
	for (int i = 0; i < 100000; i++) {
		if (i&1) {
			queue_pop(a->q);
			// queue_pop_wait(a->q);
			atomic_fetch_add(a->count, -1);
		} else {
			queue_push(a->q, a->val);
			atomic_fetch_add(a->count, 1);
		}
	}
	queue_push(a->q, a->val);
	atomic_fetch_add(a->count, 1);
	printf("%d: len (end): %zu - %d\n", a->id, queue_len(a->q), atomic_load(a->count));
	return NULL;
}

void *drain_queue(void *p) {
	queue *q = (queue *)p;
	for (;;) {
		if (queue_pop_wait(q) == NULL && queue_is_closed(q)) {
			break;
		}
	}
	printf("### EXIT\n");
	return NULL;
}

queue *global_queue = NULL;
atomic_size_t global_count = 0;

int count_lines(const unsigned char *s, int size) {
	int n = 0;
	for (int i = 0; i < size; i++) {
		if (s[i] == '\n') {
			n++;
		}
	}
	return n;
}

void *ftw_worker(void *p) {
	(void)p;
	const int size = 1024 * 1024;
	unsigned char *buffer = malloc(size);
	for (;;) {
		char *path = (char *)queue_pop_wait(global_queue);
		if (!path) {
			if (queue_is_closed(global_queue)) {
				break;
			}
			continue;
		}
		FILE *fp = fopen(path, "r");
		if (!fp) {
			continue;
		}
		int n;
		int count = 0;
		while ((n = fread(buffer, 1, size, fp)) == size) {
			count += count_lines(buffer, n);
		}
		if (n > 0) {
			// TODO: check error
			count += count_lines(buffer, n);
		}
		fclose(fp);
		free(path);
		global_count += count;
	}
	return NULL;
}

// int (*fn)(const char *, const struct stat *ptr, int flag)
int walk_ftw(const char *name, const struct stat *ptr, int flag) {
	(void)ptr;
	(void)flag;
	if (flag == FTW_F) {
		queue_push(global_queue, strdup(name));
	}
	return 0;
}

int test_dirent() {
	const char *dirname = "/Users/cvieth/Projects/C/utils/fastwalk";
	DIR *dir = opendir(dirname);
	if (!dir) {
		perror(dirname);
		return 1; // TODO: check different error types
	}

	struct dirent *dp;
	while ((dp = readdir(dir))) {
		// TODO: support "skip files"
		printf("%s (%d): %i %zu\n", dp->d_name, dp->d_type, dp->d_namlen, strlen(dp->d_name));
		char *s = walker_join_paths(dirname, strlen(dirname), dp);
		printf("\t'%s'\n", s);
		free(s);
	}

	closedir(dir);
	return 0;
}

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;
	// assert(test_dirent() == 0);
	// return 0;

	global_queue = malloc(sizeof(queue));
	assert(queue_init(global_queue) == 0);

	const int thread_count = 4;
	pthread_t threads[thread_count];

	for (int i = 0; i < thread_count; i++) {
		int res = pthread_create(&threads[i], NULL, ftw_worker, global_queue);
		assert(res == 0);
	}
	int ret = ftw("/Users/cvieth/Projects/C", walk_ftw, 10000);
	printf("RET: %d\n", ret);
	queue_close(global_queue);

	for (int i = 0; i < thread_count; i++) {
		assert(pthread_join(threads[i], NULL) == 0);
	}
	printf("LINES: %zu\n", global_count);
	printf("DONE\n");

	return 0;
}

/*
int xmain(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;

	queue *q = malloc(sizeof(queue));
	int res = queue_init(q);
	assert(res == 0);

	const int thread_count = 4;
	pthread_t threads[thread_count];
	atomic_int count = 0;
	thread_arg thread_args[] = {
		{0, q, "a", &count},
		{1, q, "b", &count},
		{2, q, "c", &count},
		{3, q, "d", &count},
		// {4, q, "d", &count},
		// {5, q, "d", &count},
		// {6, q, "d", &count},
		// {7, q, "d", &count},
	};
	assert(sizeof(thread_args) / sizeof(thread_args[0]) == thread_count);

	pthread_t xthread;
	pthread_create(&xthread, NULL, drain_queue, q);

	for (int i = 0; i < thread_count; i++) {
		res = pthread_create(&threads[i], NULL, thread_test, &thread_args[i]);
		assert(res == 0);
	}
	for (int i = 0; i < thread_count; i++) {
		res = pthread_join(threads[i], NULL);
		assert(res == 0);
		printf("%d: joined\n", thread_args[i].id);
	}
	printf("\n");

	printf("killing drain\n");
	printf("queue_close: %d\n", queue_close(q));
	// queue_close(q);
	pthread_join(xthread, NULL);
	printf("kill complete\n");

	queue_node *head = q->head;
	queue_node *tail = q->tail;
	printf("count: %d\n", count);
	printf("head: %p\n", (void *)head);
	printf("tail: %p\n", (void *)tail);
	printf("len: %zu\n", queue_len(q));
	printf("closed: %d\n", q->closed);

	int n = 0;
	while (queue_len(q) > 0) {
		printf("%d: pop (%zu): %s\n", n++, queue_len(q), (char *)queue_pop(q));
		if (n >= 20) {
			printf("error N: %d\n", n);
			return 1;
		}
	}

	// queue_push(q, "a");
	// queue_push(q, "b");
	// queue_push(q, "c");
	// printf("len: %zu\n", queue_len(q));
	// printf("len: %s\n", (char *)queue_pop(q));
	// printf("len: %s\n", (char *)queue_pop(q));
	// printf("len: %s\n", (char *)queue_pop(q));
	// printf("len: %zu\n", queue_len(q));
	// printf("Ok\n");

	// queue q;
	// int res = queue_init(&q);
	// assert(res == 0);
	// queue_push(&q, "a");
	// queue_push(&q, "b");
	// queue_push(&q, "c");
	// printf("len: %zu\n", queue_len(&q));
	// printf("len: %s\n", (char *)queue_pop(&q));
	// printf("len: %s\n", (char *)queue_pop(&q));
	// printf("len: %s\n", (char *)queue_pop(&q));
	// printf("len: %zu\n", queue_len(&q));
	// printf("Ok\n");

	return 0;
}
*/
