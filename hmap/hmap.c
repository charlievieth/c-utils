#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

#include <errno.h>     // EINTR

#include "hmap.h"

////////////////////////////////////////////////////////////////////////

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

static void __attribute__((__noreturn__)) __attribute__((cold))
	*xalloc_die();
static void __attribute__((__malloc__)) __attribute__((returns_nonnull))
	*xmalloc(size_t n);
static void __attribute__((__malloc__)) __attribute__((returns_nonnull))
	*xcalloc(size_t count, size_t size);
// static void __attribute__((__malloc__)) *xrealloc(void *p, size_t n);

static void *xalloc_die() {
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

static void *xcalloc(size_t count, size_t size) {
	void *p = calloc(count, size);
	if (!p && (count * size != 0)) {
		xalloc_die();
	}
	return p;
}

// static void *xrealloc(void *p, size_t n) {
// 	if (!n && p) {
// 		free(p);
// 		return NULL;
// 	}
// 	p = realloc(p, n);
// 	if (!p && n) {
// 		xalloc_die();
// 	}
// 	return p;
// }

char *xasprintf(const char *format, ...)
	__attribute__((format (printf, 1, 2)));

char *xasprintf(const char *format, ...) {
	va_list args;
	va_start(args, format);
	char *ret = NULL;
	if (unlikely(vasprintf(&ret, format, args) < 0)) {
		fflush(stdout);
		fprintf(stderr, "error: vasprintf\n");
		abort();
	}
	va_end(args);
	assert(ret);
	return ret;
}

static void xassertf_impl(const char *filename, int line, const char *assertion, const char *format, ...)
	__attribute__((format (printf, 4, 5)));

static void xassertf_impl(const char *filename, int line, const char *assertion, const char *format, ...) {
	fflush(stdout);
	fprintf(stderr, "%s:%d: failed assertion: %s\n", filename, line, assertion);
	fprintf(stderr, "  ");
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	abort();
}

static void xassert_impl(const char *filename, int line, const char *assertion) {
	fflush(stdout);
	fprintf(stderr, "%s:%d: failed assertion: %s\n", filename, line, assertion);
	abort();
}

#define xassertf(e, format, ...) \
	(__builtin_expect(!(e), 0) ? xassertf_impl(__FILE__, __LINE__, #e, format, ##__VA_ARGS__) : (void)0)

#define xassert(e) \
	(__builtin_expect(!(e), 0) ? xassert_impl(__FILE__, __LINE__, #e) : (void)0)

////////////////////////////////////////////////////////////////////////

static inline uint32_t rotl32(uint32_t x, int8_t r) {
	return (x << r) | (x >> (32 - r));
}

static inline uint32_t fmix32(uint32_t h) {
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

static inline uint32_t murmurhash32(uint32_t key, uint32_t seed) {
	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;
	const uint32_t len = sizeof(uint32_t);

	uint32_t h1 = seed;
	uint32_t k1 = key;

	k1 *= c1;
	k1 = rotl32(k1,15);
	k1 *= c2;

	h1 ^= k1;
	h1 = rotl32(h1,13);
	h1 = h1*5+0xe6546b64;

	h1 ^= len;
	h1 = fmix32(h1);

	return h1;
}

////////////////////////////////////////////////////////////////////////

/* USABLE_FRACTION is the maximum dictionary load.
 * Currently set to 2/3 capacity.
 */
#define USABLE_FRACTION(n) (((n) << 1)/3)

/* GROWTH_RATE. Growth rate upon hitting maximum load.
 * Currently set to items*3.
 * This means that hashes double in size when growing without deletions,
 * but have more head room when the number of deletions is on a par with the
 * number of insertions.
 */
#define GROWTH_RATE(h) ((h)->items*3)

/* SHRINK_THRESHOLD. To prevent thrashing, we require the size of the hash
 * to be consistently below the target size. The SHRINK_THRESHOLD is the
 * number of times the hash size must be observed below the target size
 * before we resize the hash.
 *
 * If the number of items in the hash exceeds 1/3 of capacity we reset the
 * shrink_requests counter as this indicates we may be growing.
 */
#define SHRINK_THRESHOLD(h) ((h)->size < SIZE_MAX ? (h)->size + 1 : SIZE_MAX)

////////////////////////////////////////////////////////////////////////

static void noop_free(void* data) {
	(void)data;
	return;
}

// TODO: require users to specify EMPTY_VALUE on insert or
// automaticaly convert NULL values to EMPTY_VALUE ???
static size_t _empty = 0;
static size_t *EMPTY_VALUE = &_empty;

// TODO: change "bucket_" prefix to be consistent

static inline size_t bucket_size(uint8_t b) {
	return (size_t)1 << (b & (sizeof(size_t)*8 - 1));
}

static inline size_t bucket_mask(uint8_t b) {
	return bucket_size(b) - 1;
}

// TODO: rename
// bucket_size_for returns the size of the bucket required to store hint elements.
static inline size_t bucket_size_for(size_t hint) {
	if (hint > 0) {
		for (size_t i = 0; i < (sizeof(size_t) * 8) - 1; i++) {
			size_t n = 1ULL << i;
			if (USABLE_FRACTION(n) >= hint) {
				return i;
			}
		}
	}
	return 3; // 8
}

size_t hmap_size(const hmap *h) {
	return h->count;
}

size_t hmap_capacity(const hmap *h) {
	return bucket_size(h->b);
}

static inline size_t hmap_inc_index(const hmap *h, size_t index) {
	return ++index != bucket_size(h->b) ? index : 0;
}

static inline bool hmap_growing(const hmap *h) {
	return h->oldbuckets != NULL;
}

// WARN: use this
static bool hmap_needs_to_grow(const hmap *h) {
	// grow if empty or more than 75% filled
	return h->b == 0 || (h->count + 1) * 4 / 3 > bucket_size(h->b);
}

static inline void hmap_free(const hmap *h, void **p) {
	if (*p && *p != EMPTY_VALUE) {
		if (h->free_func) {
			h->free_func(*p);
		} else {
			free(*p); // WARN: getting a clang warning here
		}
#ifndef NDEBUG
		*p = NULL;
#endif // NDEBUG
	}
}

#ifndef NDEBUG

static void hmap_dump(const hmap *h, bool omitempty) {
	fprintf(stderr, "hmap %p: cap=%zu len=%zu b=%i\n",
		(const void *)h, bucket_size(h->b), h->count, (int)h->b);
	size_t count = 0;
	size_t probe_max = 0;
	for (size_t i = 0; i < bucket_size(h->b); i++) {
		hmap_entry e = h->buckets[i];
		if (!e.value && omitempty) {
			continue;
		}
		if (e.value) {
			count++;
		}
		if (e.probe > probe_max) {
			probe_max = e.probe;
		}
		fprintf(stderr, "  item %5zu: key = %10u probe = %2u value = %p\n",
			i, e.key, e.probe, e.value);
	}
	fprintf(stderr, "Hashtable %p: size=%zu items=%zu counted=%zu probe_max=%zu\n",
		(const void*)h, bucket_size(h->b), h->count, count, probe_max);
}

static bool hmap_is_consistent(const hmap *h) {
	size_t count = 0;
	for (size_t i = 0; i < bucket_size(h->b); i++) {
		if (h->buckets[i].value) {
			count++;
		}
	}
	bool ok = count == h->count;
	if (!ok) {
		hmap_dump(h, true);
	}
	return ok;
}

#else

static bool hmap_is_consistent(const hmap *h) {
	return true;
}

static void hmap_dump(const hmap *h, bool omitempty) {
	(void)h;
	(void)omitempty;
}

#endif // NDEBUG

static void hmap_free_entries(const hmap *h) {
	assert(hmap_is_consistent(h));
	if (h->count > 0 && (h->free_func == NULL || h->free_func != noop_free)) {
		if (h->buckets) {
			hmap_entry *b = h->buckets;
			for (size_t i = 0; i < bucket_size(h->b); i++) {
				if (b[i].value) {
					hmap_free(h, &b[i].value);
				}
			}
		}
		if (h->oldbuckets) {
			hmap_entry *b = h->oldbuckets;
			for (size_t i = 0; i < bucket_size(h->b - 1); i++) {
				if (b[i].value) {
					hmap_free(h, &b[i].value);
				}
			}
		}
	}
}

void hmap_clear(hmap *h) {
	hmap_free_entries(h);
	if (h->oldbuckets) {
		free(h->oldbuckets);
		h->oldbuckets = NULL;
	}
	if (h->stats) {
		memset(h->stats, 0, sizeof(hmap_stats));
	}
	h->count = 0;
	memset(h->buckets, 0, sizeof(hmap_entry) * bucket_size(h->b));
}

void hmap_destroy(hmap *h) {
	hmap_free_entries(h);
	if (h->oldbuckets) {
		free(h->oldbuckets);
	}
	if (h->buckets) {
		free(h->buckets);
	}
	if (h->stats) {
		free(h->stats);
	}
#ifndef NDEBUG
	memset(h, 0, sizeof(hmap));
#endif // NDEBUG
	free(h);
}

static inline void hmap_record_insert_stats(hmap *h, size_t ops, uint32_t probe) {
	if (h->stats) {
		h->stats->insert_count++;
		h->stats->insert_ops += ops;
		if (ops > h->stats->max_insert_ops) {
			h->stats->max_insert_ops = ops;
		}
		if (probe > h->stats->max_probe) {
			h->stats->max_probe = probe;
		}
		double load = (double)h->count / (double)bucket_size(h->b);
		if (load > h->stats->max_load) {
			h->stats->max_load = load;
		}
	}
}

static void insert(hmap *h, hmap_key key, void *value) {
	uint32_t probe = 0;
	uint32_t hash = murmurhash32(key, h->seed);
	size_t index = hash & bucket_mask(h->b);
	size_t ops = 0;

	hmap_entry *e = &h->buckets[index];
	for (;;) {
		ops++;
		if (!e->value) {
			h->count++;
			*e = (hmap_entry){
				.value = value,
				.key = key,
				.probe = probe,
			};
			break;
		}
		if (e->key == key) {
			if (e->value != value) {
				hmap_free(h, &e->value);
			}
			e->value = value;
			break;
		}
		if (probe > e->probe) {
			hmap_entry tmp = *e;
			*e = (hmap_entry){
				.value = value,
				.key = key,
				.probe = probe,
			};
			value = tmp.value;
			key = tmp.key;
			probe = tmp.probe;
		}
		assert(index + 1 <= bucket_size(h->b));
		// TODO: we may be able to use the bucket_mask here
		index++;
		if (index == bucket_size(h->b)) {
			index = 0;
		}
		e = &h->buckets[index]; // TODO: bump pointer
		probe++;
	}

	hmap_record_insert_stats(h, ops, probe);
}

static void hmap_grow(hmap *h) {
	const size_t oldcount = h->count;
	hmap_entry *ob = h->buckets;
	const hmap_entry *oldbuckets = h->buckets;
	const uint8_t oldb = h->b;

	h->b++;
	h->buckets = xcalloc(bucket_size(h->b), sizeof(hmap_entry));
	h->count = 0;

	for (size_t i = 0; i < bucket_size(oldb); i++) {
		if (oldbuckets[i].value) {
			insert(h, oldbuckets[i].key, oldbuckets[i].value);
		}
	}

	free(ob);
	assert(h->count == oldcount);
}

void hmap_assign(hmap *h, hmap_key key, void *value) {
	assert(h);
	assert(value);
	assert(hmap_is_consistent(h));

	insert(h, key, value);
	if (h->count >= USABLE_FRACTION(bucket_size(h->b))) {
		hmap_grow(h);
	}

	assert(hmap_is_consistent(h));
}

void *hmap_get(const hmap *h, hmap_key key) {
	assert(h);

	uint32_t probe = 0;
	uint32_t hash = murmurhash32(key, h->seed);
	size_t index = hash & bucket_mask(h->b);

	const hmap_entry *e = &h->buckets[index];
	while (e->value) {
		if (e->key == key) {
			return e->value;
		}
		if (e->probe < probe) {
			break;
		}
		index = hmap_inc_index(h, index);
		e = &h->buckets[index];
		probe++;
	}

	return NULL;
}

// TODO: move, ownership (return value in that case)
//
bool hmap_delete(hmap *h, hmap_key key) {
	uint32_t probe = 0;
	uint32_t hash = murmurhash32(key, h->seed);
	size_t index = hash & bucket_mask(h->b);

	hmap_entry *e = &h->buckets[index];
	const hmap_entry *last = &h->buckets[bucket_size(h->b)-1];

	bool found = false;
	while (e->value) {
		if (e->key == key) {
			// TODO: ownership
			hmap_free(h, &e->value);
			found = true;

			// shift buckets: n == next entry
			hmap_entry *n = e != last ? e + 1 : &h->buckets[0];
			while (n->value && n->probe > 0) {
				*e = *n;
				e->probe--;

				e = n;
				n = n != last ? n + 1 : &h->buckets[0];
			}

			// set empty after backward shifting
			e->value = NULL;
			h->count--;
			break;
		}

		if (e->probe < probe) {
			break;
		}

		e = e != last ? e + 1 : &h->buckets[0];
		probe++;
	}

	assert(hmap_is_consistent(h));
	assert(hmap_get(h, key) == NULL);

	return found;
}

static void hmap_iter(const hmap *h, hmap_iter_func fn, void *data) {
	for (size_t i = 0; i < bucket_size(h->b); i++) {
		if (h->buckets[i].value) {
			if (!fn(h->buckets[i].key, h->buckets[i].value, data)) {
				break;
			}
		}
	}
}

hmap *new_hmap(size_t hint) {
	// TODO: use calloc
	hmap *h = xmalloc(sizeof(hmap));
	h->count = 0;
	h->seed = (uint32_t)random();
	h->b = bucket_size_for(hint);

	// TODO: lazily allocate
	h->buckets = xcalloc(bucket_size(h->b), sizeof(hmap_entry));
	h->oldbuckets = NULL;
	h->free_func = NULL; // TODO: set to: `free`

	// WARN
	h->stats = xcalloc(1, sizeof(hmap_stats));
	return h;
}

typedef struct {
	uint32_t key;
	void     *value;
	bool     seen;
} key_value_pair;

static bool kvs_contains(const key_value_pair *keys, size_t size, uint32_t key) {
	for (size_t i = 0; i < size; i++) {
		if (keys[i].key == key) {
			return true;
		}
	}
	return false;
}

static uint32_t kvs_random_key(const key_value_pair *keys, size_t size) {
	for (int i = 0; i < 1024; i++) {
		uint32_t key = (uint32_t)random();
		if (!kvs_contains(keys, size, key)) {
			return key;
		}
	}
	xassertf(false, "failed to find a random key not contained in kvs: %p", (const void*)keys);
	abort();
}

static void test_hmap_n(int n) {
	hmap *h = new_hmap(0);
	h->free_func = noop_free;
	key_value_pair *keys = calloc(n, sizeof(key_value_pair));

	for (int i = 0; i < n; i++) {
		// don't allow for duplicate keys!
		uint32_t key = (uint32_t)random();
		while (hmap_get(h, key) != NULL) {
			key = (uint32_t)random();
		}
		keys[i].key = key;
		keys[i].value = EMPTY_VALUE;
		hmap_assign(h, keys[i].key, EMPTY_VALUE);
	}
	xassertf(hmap_size(h) == (size_t)n, "%i: hmap_size=%zu want: %i", n, hmap_size(h), n);

	for (int i = 0; i < n; i++) {
		void *p = hmap_get(h, keys[i].key);
		xassert(p == keys[i].value);
	}

	double avg_ops = (double)h->stats->insert_ops /(double)h->stats->insert_count;
	fprintf(stderr,
		"%8i: max_ops = %3zu  probe = %2zu  insert_ops = %8zu  insert_count = %8zu  insert_avg = %.2f max_load = %.2f\n",
		n, h->stats->max_insert_ops, h->stats->max_probe, h->stats->insert_ops,
		h->stats->insert_count, avg_ops, h->stats->max_load);

	double load = (double)h->count / (double)bucket_size(h->b);
	fprintf(stderr, "          size=%zu count=%zu load=%.2f\n",
		bucket_size(h->b), h->count, load);


	// Test delete / add
	size_t exp_count = h->count;
	for (int i = 0; i < n; i++) {
		xassert(hmap_delete(h, keys[i].key));
		hmap_assign(h, keys[i].key, EMPTY_VALUE);
	}
	xassertf(h->count == exp_count, "count=%zu want: %zu", h->count, exp_count);
	for (int i = 0; i < n; i++) {
		void *p = hmap_get(h, keys[i].key);
		xassertf(p == keys[i].value, "%i: missing key: %u", n, keys[i].key);
	}

	// Test delete unknown key
	int num_delete_tests = n <= 128 ? n : 128;

	hmap_entry *exp_buckets = xmalloc(bucket_size(h->b) * sizeof(hmap_entry));
	memcpy(exp_buckets, h->buckets, bucket_size(h->b) * sizeof(hmap_entry));

	for (int i = 0; i < num_delete_tests; i++) {
		uint32_t key = keys[i].key + 1;
		if (!kvs_contains(keys, n, key)) {
			xassert(hmap_delete(h, key) == false);
		}
		key = kvs_random_key(keys, n);
		xassert(hmap_get(h, key) == NULL);
		xassert(hmap_delete(h, key) == false);
	}
	xassertf(h->count == exp_count, "count=%zu want: %zu", h->count, exp_count);
	if (memcmp(exp_buckets, h->buckets, bucket_size(h->b) * sizeof(hmap_entry)) != 0) {
		fprintf(stderr, "error: random deletes modified the hmap->buckets\n");
		hmap_dump(h, true);
	}
	free(exp_buckets);

	// Test delete
	for (int i = 0; i < n; i++) {
		bool deleted = hmap_delete(h, keys[i].key);
		if (!deleted) {
			hmap_dump(h, true);
		}
		xassertf(deleted, "%i: failed to delete key: %u count=%zu i=%i",
			n, keys[i].key, h->count, i);
	}
	for (int i = 0; i < n; i++) {
		xassertf(hmap_get(h, keys[i].key) == NULL, "%i: failed to delete key: %u count=%zu i=%i",
			n, keys[i].key, h->count, i);
	}
	xassertf(h->count == 0, "count=%zu want: %i", h->count, 0);

	hmap_destroy(h);
	free(keys);
}

static void test_hmap() {
	srandomdev();

	int test_sizes[] = {
		1,
		3,
		4,
		7,
		9,
		8,
		17,
		1023,
		8191,
		8193,
		293, 347, 419, 499, 593, 709, 853, 1021, 1229, 1471, 1777, 2129, 2543, 3049,
		3659, 4391, 5273, 6323, 7589, 9103, 10937, 13109, 15727, 18899, 22651,
	};
	const int count = sizeof(test_sizes) / sizeof(test_sizes[0]);
	for (int i = 0; i < count; i++) {
		// printf("%i\n", test_sizes[i]);
		test_hmap_n(test_sizes[i]);
	}

	printf("pass\n");
}

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;

	for (int i = 0; i < 128; i++) {
		/* code */
		test_hmap();
	}
	return 0;

	srandomdev();

	for (int i = 0; i < 8192; i++) {
		hmap *h = new_hmap(0);
		// int value = 0;
		for (uint32_t j = 0; j < 22651; j++) {
			hmap_assign(h, random(), EMPTY_VALUE);
		}
		free(h->buckets);
		free(h);
	}

	// hmap_dump(h, true);

	// int *a = calloc(5, sizeof(int));
	// for (int i = 0; i < 4; i++) {
	// 	a[i] = i;
	// }
	// int *p = &a[1];
	// p += 4;
	// printf("p: %i %i\n", a[1], *p);
}
