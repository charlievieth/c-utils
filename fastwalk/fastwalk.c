#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include <stdatomic.h>

// WARN: remove if not used
#include <semaphore.h>

//  TODO: Check GCC or Clang
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define printd(msg)                                                                       \
	do {                                                                                  \
		fprintf(stderr, "\033[0;32m# %s:%d: %s\033[0m;\n", __FILE_NAME__, __LINE__, msg); \
	} while(0);

#define debug_pthread(_errnum, _op)                                    \
	do {                                                               \
		fprintf(stderr, "%s:%d error (%d): %s: %s\n",                  \
			__FILE_NAME__, __LINE__, _errnum, strerror(_errnum), _op); \
		assert(0);                                                     \
	} while(0);

typedef struct alist_element alist_element;

typedef struct {
	alist_element *ptr;
	size_t        count;
} alist_pointer;

static inline bool alist_pointer_equal(alist_pointer p1, alist_pointer p2) {
	return p1.ptr == p2.ptr && p1.count == p2.count;
}

// WARN:
struct alist_element {
	void *value;
	_Atomic alist_pointer next;
};

alist_pointer new_alist_element(void *val) {
	alist_element * node = malloc(sizeof(alist_element));
	if (unlikely(!node)) {
		assert(node);
		return (alist_pointer){ 0 };
	}
	node->value = val;
	node->next = (alist_pointer){ .ptr = NULL };
	return (alist_pointer){ .ptr = node }; // count is uninitialized
}

// Michael and Scott Lock-Free FIFO Queue.
//
// TODO: fix the use-after-free bug
// https://stackoverflow.com/questions/40818465/explain-michael-scott-lock-free-queue-alorigthm
typedef struct {
	atomic_size_t len;
	_Atomic alist_pointer head;
	_Atomic alist_pointer tail;
} aqueue;

size_t aqueue_len(aqueue *q) {
	atomic_size_t n = atomic_load(&q->len);
	return (size_t)n;
}

int aqueue_init(aqueue *q) {
	q->len = 0;
	alist_pointer node = new_alist_element(NULL);
	// node.ptr = NULL;
	q->head = node;
	q->tail = node;
	return 0;
}

int aqueue_push(aqueue *q, void *val) {
	// TODO: try to use "weak" CAS
	alist_pointer node = new_alist_element(val);
	alist_pointer tail;
	for (;;) {
		tail = q->tail;
		alist_pointer next = tail.ptr->next;
		if (alist_pointer_equal(tail, q->tail)) {
			if (next.ptr == NULL) {
				node.count = next.count + 1;
				if (atomic_compare_exchange_strong(&tail.ptr->next, &next, node)) {
					break;
				}
			} else {
				tail.count++;
				atomic_compare_exchange_strong(&q->tail, &tail, next);
			}
		}
	}
	atomic_compare_exchange_strong(&q->tail, &tail, node);
	q->len++; // TODO: remove len
	return 0;
}

// TODO: pass in value
void *aqueue_pop(aqueue *q) {
	void *val = NULL;
	alist_pointer head;
	for (;;) {
		head = q->head;
		alist_pointer tail = q->tail;
		alist_pointer next = head.ptr->next; // WARN: crash if head free'd
		if (alist_pointer_equal(head, q->head)) {
			if (head.ptr == tail.ptr) {
				if (next.ptr == NULL) {
					return NULL;
				}
				next.count = tail.count + 1;
				atomic_compare_exchange_strong(&q->tail, &tail, next);
			} else {
				val = next.ptr->value;
				next.count = head.count + 1;
				if (atomic_compare_exchange_strong(&q->head, &head, next)) {
					q->len--; // TODO: remove this
					break;
				}
			}
		}
	}
	free(head.ptr);
	return val;
}

typedef struct {
	int id;
	aqueue *q;
	char *val;
	atomic_int *count;
} thread_arg;

void *thread_test(void *p) {
	thread_arg *a = p;
	printf("%d: len (start): %zu\n", a->id, aqueue_len(a->q));
	for (int i = 0; i < 100000; i++) {
		if (i&1) {
			aqueue_pop(a->q);
			atomic_fetch_add(a->count, -1);
		} else {
			aqueue_push(a->q, a->val);
			atomic_fetch_add(a->count, 1);
		}
	}
	printf("%d: len (end): %zu - %d\n", a->id, aqueue_len(a->q), atomic_load(a->count));
	return NULL;
}

// typedef struct {
// 	int32_t  depth;
// 	int32_t  max_depth;
// 	int32_t  path_offset;
// 	// WARN: this may be too short for some relative paths!!!
// 	char     pathbuf[PATH_MAX];
// } context;

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;


	aqueue *q = malloc(sizeof(aqueue));
	int res = aqueue_init(q);
	assert(res == 0);

	const int thread_count = 4;
	pthread_t threads[thread_count];
	atomic_int count = 0;
	thread_arg thread_args[] = {
		{0, q, "a", &count},
		{1, q, "b", &count},
		{2, q, "c", &count},
		{3, q, "d", &count},
	};

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

	alist_pointer head = q->head;
	alist_pointer tail = q->tail;
	printf("count: %d\n", count);
	printf("head: %p\n", (void *)head.ptr);
	printf("tail: %p\n", (void *)tail.ptr);
	printf("len: %zu\n", aqueue_len(q));

	int n = 0;
	while (aqueue_len(q) > 0) {
		printf("%d: pop (%zu): %s\n", n++, aqueue_len(q), (char *)aqueue_pop(q));
		if (n >= 20) {
			printf("error N: %d\n", n);
			return 1;
		}
	}

	// aqueue_push(q, "a");
	// aqueue_push(q, "b");
	// aqueue_push(q, "c");
	// printf("len: %zu\n", aqueue_len(q));
	// printf("len: %s\n", (char *)aqueue_pop(q));
	// printf("len: %s\n", (char *)aqueue_pop(q));
	// printf("len: %s\n", (char *)aqueue_pop(q));
	// printf("len: %zu\n", aqueue_len(q));
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

/*
typedef struct list_element list_element;

struct list_element {
	list_element *next;
	void         *value;
};

typedef struct {
	pthread_mutex_t lock;
	size_t          len;
	list_element    *root;
	list_element    *tail;
	sem_t           *sem;
	// WARN: new
} queue;

int queue_init(queue *q) {
	// WARN: do we need to keep a reference to this or
	// destroy it ???
	pthread_mutexattr_t attr;
	int res;
	if (unlikely((res = pthread_mutexattr_init(&attr)) != 0)) {
		debug_pthread(res, "pthread_mutexattr_init");
		return res;
	}
	*q = (queue){ .lock = { 0 } };
	if (unlikely((res = pthread_mutex_init(&q->lock, &attr)) != 0)) {
		debug_pthread(res, "pthread_mutex_init");
		return res;
	}
	if (unlikely((res = pthread_mutexattr_destroy(&attr)) != 0)) {
		debug_pthread(res, "pthread_mutexattr_destroy");
		return res;
	}
	return 0;
}

size_t queue_len(queue *q) {
	int res;
	if (unlikely((res = pthread_mutex_lock(&q->lock)) != 0)) {
		debug_pthread(res, "pthread_mutex_lock");
		return 0; // WARN: handle
	}
	size_t len = q->len;
	if (unlikely((res = pthread_mutex_unlock(&q->lock)) != 0)) {
		debug_pthread(res, "pthread_mutex_unlock");
		return 0; // WARN: handle
	}
	return len;
}

int queue_push(queue *q, void *val) {
	int res;
	if (unlikely((res = pthread_mutex_lock(&q->lock)) != 0)) {
		debug_pthread(res, "pthread_mutex_lock");
		return res;
	}
	list_element *elem = malloc(sizeof(list_element));
	if (unlikely(!elem)) {
		assert(elem);
		return ENOMEM;
	}
	elem->next = NULL;
	elem->value = val;
	q->len++;
	if (likely(q->tail)) {
		q->tail->next = elem;
		q->tail = elem;
	} else {
		q->root = elem;
		q->tail = elem;
	}
	if (unlikely((res = pthread_mutex_unlock(&q->lock)) != 0)) {
		debug_pthread(res, "pthread_mutex_unlock");
		return res;
	}
	return 0;
}

void *queue_pop(queue *q) {
	int res;
	if (unlikely((res = pthread_mutex_lock(&q->lock)) != 0)) {
		debug_pthread(res, "pthread_mutex_lock");
		return NULL; // WARN: handle
	}
	void *val = NULL;
	if (likely(q->root)) {
		q->len--;
		list_element *elem = q->root;
		q->root = elem->next;
		val = elem->value;
		free(elem);
	}
	if (unlikely((res = pthread_mutex_unlock(&q->lock)) != 0)) {
		debug_pthread(res, "pthread_mutex_unlock");
		return NULL; // WARN: handle
	}
	return val;
}
*/

/*
#include <stdatomic.h>

typedef struct {
	atomic_size_t len;
	list_element *root;
	list_element *tail;
} aqueue;

size_t aqueue_len(aqueue *q) {
	atomic_size_t n = atomic_load(&q->len);
	return (size_t)n;
}

int aqueue_push(aqueue *q, void *val) {
	list_element * _Atomic elem = malloc(sizeof(list_element));
	if (unlikely(!elem)) {
		assert(elem);
		return 2;
	}
	elem->next = NULL;
	elem->value = val;
	for (;;) {
		if (atomic_compare_exchange_weak(&q->tail, &q->tail->next, elem)) {
			// code
		}
	}
	atomic_fetch_add(&q->len, 1);

	// if (likely(q->tail)) {
	// 	q->tail->next = elem;
	// 	q->tail = elem;
	// } else {
	// 	q->root = elem;
	// 	q->tail = elem;
	// }
	return 0;
}
*/
