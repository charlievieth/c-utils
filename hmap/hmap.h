#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// typedef unsigned int ht_key_t;
typedef uint32_t hmap_key;

// TODO: use a macro for this
typedef bool (*hmap_iter_func)(hmap_key key, void *value, void *userdata);

typedef void (*hmap_free_func)(void* data);

typedef struct {
	void     *value;
	hmap_key key;
	uint32_t probe;
} hmap_entry;

// TODO: record load factor - at least while testing
typedef struct {
	size_t max_insert_ops;
	size_t max_probe;
	size_t insert_ops;
	size_t insert_count;
	double max_load;
} hmap_stats;

typedef struct {
	size_t         count; // live cells
	uint32_t       seed;  // hash seed
	uint8_t        b;     // log_2 of # of buckets (can hold up to loadFactor * 2^B items)
	hmap_entry     *buckets;
	hmap_entry     *oldbuckets;
	hmap_free_func free_func;

	// WARN
	hmap_stats     *stats;
} hmap;

size_t hmap_size(const hmap *h);
size_t hmap_capacity(const hmap *h);

void hmap_clear(hmap *h);
void hmap_destroy(hmap *h);

void hmap_assign(hmap *h, hmap_key key, void *value);
void *hmap_get(const hmap *h, hmap_key key);
bool hmap_delete(hmap *h, hmap_key key);
