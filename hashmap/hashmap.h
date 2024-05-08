#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hedley.h"

#define BUCKET_CNT_BITS 3
#define BUCKET_CNT      (1 << BUCKET_CNT_BITS)

// TODO: rename to "string_t" or something
typedef struct {
	size_t len;
	char   *str;
} string_t;

typedef struct {
	void *ptr;
} bmap_entry;

typedef struct bmap bmap;

struct bmap {
	uint8_t    tophash[BUCKET_CNT];
	string_t   keys[BUCKET_CNT];
	bmap_entry values[BUCKET_CNT];
	bmap       *overflow;
};

// WARN: use or remove
typedef struct {
	int32_t len;
	int32_t cap;
	bmap    **buckets;
} bmap_array;

// TODO: implement this (if possible)
typedef struct {
	// bmap **overflow;
	// bmap **oldoverflow;
	bmap_array *overflow;
	bmap_array *oldoverflow;
	bmap       *next_overflow;
} mapextra;

typedef void (*free_fn)(void*);

// noop_free is a no-op free function and should be used when
// the hmap does not own and should not free it's values.
static inline void noop_free(void *p) {
	(void)p;
}

// TODO: implement map iteration
// TODO: make a const value (not pointer)
typedef bool (*iter_fn)(const char* key, const string_t* val);

// TODO: allow users to specify custom allocators.
typedef void *(*malloc_fn)(size_t);          // malloc
typedef void *(*calloc_fn)(size_t, size_t);  // calloc
typedef void *(*realloc_fn)(size_t, size_t); // realloc

// TODO: allow users to specify custom allocators.
typedef struct {
	malloc_fn  malloc;
	calloc_fn  calloc;
	realloc_fn realloc;
	// free_fn    free; // TODO: allow this ?
} hmap_allocators;

// TODO: make this opaque
typedef struct {
	size_t    count;       // # live cells == size of map.
	uint8_t   flags;
	uint8_t   b;           // log_2 of # of buckets (can hold up to loadFactor * 2^B items)
	uint16_t  noverflow;   // approximate number of overflow buckets; see incrnoverflow for details
	uint32_t  hash0;       // hash seed
	bmap      *buckets;    // array of 2^B Buckets. may be nil if count==0.
	bmap      *oldbuckets; // previous bucket array of half the size, non-nil only when growing
	size_t    nevacuate;   // progress counter for evacuation (buckets less than this have been evacuated)
	mapextra  *extra;      // optional fields
	free_fn   free;        // function to free values
} hmap;

void hmap_init_seed(hmap *h, free_fn free, uint32_t seed)
	HEDLEY_NON_NULL(1);

void hmap_init(hmap *h, free_fn free)
	HEDLEY_NON_NULL(1);

hmap *hmap_new_seed(free_fn free, uint32_t seed)
	HEDLEY_RETURNS_NON_NULL;

hmap *hmap_new(free_fn free)
	HEDLEY_RETURNS_NON_NULL;

// TODO: maybe rename to hmap_free ???
void hmap_destroy(hmap *h);

void hmap_assign_strlen(hmap *h, const char *str, size_t len, void *value)
	HEDLEY_NON_NULL(1, 2);

void hmap_assign_str(hmap *h, const char *str, void *value)
	HEDLEY_NON_NULL(1, 2);

bool hmap_access_strlen(hmap *h, const char *str, size_t len, void **value)
	HEDLEY_NON_NULL(1);

bool hmap_access_str(hmap *h, const char *str, void **value)
	HEDLEY_NON_NULL(1);

bool hmap_delete_strlen(hmap *h, const char *str, size_t len)
	HEDLEY_NON_NULL(1, 2);

bool hmap_delete_str(hmap *h, const char *str)
	HEDLEY_NON_NULL(1, 2);

size_t hmap_len(const hmap *h) {
	return h != NULL ? h->count : 0;
}

#endif /* HASHMAP_H */
