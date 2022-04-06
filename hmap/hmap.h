#ifndef HMAP_H
#define HMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef HMAP_PURE
#define HMAP_PURE __attribute__((pure))
#endif

// typedef unsigned int ht_key_t;
typedef uint32_t hmap_key;

// TODO: use a macro for this
typedef bool (*hmap_iter_func)(hmap_key key, void *value, void *userdata);

// hmap_free_func is the function signature of a custom `free` function
typedef void (*hmap_free_func)(void* data);

// noop_free is no-op free
static inline void noop_free(void* data) {
	(void)data;
	return;
}

// TODO: record load factor - at least while testing
typedef struct {
	size_t max_insert_ops;
	size_t max_probe;
	size_t insert_ops;
	size_t insert_count;
	double max_load;
} hmap_stats;

typedef struct {
	void     *value;
	hmap_key key;
	uint32_t probe;
} hmap_entry;

typedef struct {
	size_t         count;       // live cells
	uint32_t       seed;        // hash seed
	uint8_t        b;           // log_2 of # of buckets (can hold up to loadFactor * 2^B items)
	hmap_entry     *buckets;    // TOOD: rename to "bucket"
	hmap_entry     *oldbuckets; // TOOD: rename to "oldbucket"
	hmap_free_func free_func;

	// WARN
	hmap_stats     *stats;
} hmap;

hmap *new_hmap(size_t hint);

size_t HMAP_PURE hmap_size(const hmap *h);
size_t HMAP_PURE hmap_capacity(const hmap *h);

void hmap_clear(hmap *h);
void hmap_destroy(hmap *h);

void hmap_assign(hmap *h, hmap_key key, void *value);
bool hmap_find(const hmap *h, hmap_key key, void **value);
void *hmap_get(const hmap *h, hmap_key key);
bool hmap_delete(hmap *h, hmap_key key);

void hmap_print_stats(const hmap *h);

/*
 * hashmap__for_each_entry - iterate over all entries in hmap
 * @map: hmap to iterate
 * @cur: hmap_entry * used as a loop cursor
 * @bkt: unsigned integer used as a bucket loop cursor
 */
// #define hmap_for_each_entry(map, cur, bkt)
// 	const size_t _size = hmap_size(map);
// 	for (bkt = 0; bkt < _size; bkt++)
// 		cur = map->buckets[bkt];

#endif /* HMAP_H */
