#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
// #include <limits.h> // WARN: remove if not used
#include <assert.h>

#include "xxhash.h"
#include "utils.h"
#include "hedley.h"

#define BUCKET_CNT_BITS 3
#define BUCKET_CNT      (1 << BUCKET_CNT_BITS)

// Maximum average load of a bucket that triggers growth is 6.5.
// Represent as loadFactorNum/loadFactorDen, to allow integer math.
#define LOAD_FACTOR_NUM 13
#define LOAD_FACTOR_DEN 2

enum tophash_t {
	EMPTY_REST      = 0,
	EMPTY_ONE       = 1,
	EVACUATED_X     = 2,
	EVACUATED_Y     = 3,
	EVACUATED_EMPTY = 4,
	MIN_TOP_HASH    = 5,
};

// Flags
#define ITERATOR       1
#define OLD_ITERATOR   2
#define SAME_SIZE_GROW 8

// TODO: rename to "string_t" or something
typedef struct string_t {
	size_t len;
	char   *str;
} string_t;

typedef struct bmap bmap;

struct bmap {
	uint8_t  tophash[BUCKET_CNT];
	string_t keys[BUCKET_CNT];
	void     *values[BUCKET_CNT];
	bmap     *overflow;
};

typedef struct mapextra {
	bmap **overflow;
	bmap **oldoverflow;
	bmap *next_overflow;
} mapextra;

typedef void (*free_fn)(void*);

typedef struct hmap {
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

static inline bool memequal(const void *p1, const void *p2, size_t size) {
	return p1 == p2 || memcmp(p1, p2, size) == 0;
}

static inline bool string_equal(const string_t *s, const char *str, size_t len) {
	// TODO: use our own memequal for short strings
	return s->len == len && *s->str == *str && memequal(s->str, str, len);
}

HEDLEY_NON_NULL(1, 2)
static void string_assign(string_t *key, const char *str, size_t len) {
	assert(key);
	assert(str);
	// TODO: shrink very large keys
	if (key->len < len) {
		key->str = xrealloc(key->str, len + 1);
	}
	memmove(key->str, str, len + 1);
	key->len = len;
}

static inline size_t hmap_is_empty(uint8_t x) {
	return x <= EMPTY_ONE;
}

static inline size_t hmap_bucket_shift(uint8_t b) {
	return (size_t)1 << (b & sizeof(void*)*8 - 1);
}

static inline size_t hmap_bucket_mask(uint8_t b) {
	return hmap_bucket_shift(b) - 1;
}

static inline uint8_t tophash(uint64_t hash) {
	uint8_t top = (uint8_t)(hash >> (sizeof(void*)*8 - 8));
	if (top < MIN_TOP_HASH) {
		top += MIN_TOP_HASH;
	}
	return top;
}

// overLoadFactor reports whether count items placed in 1<<B buckets is over loadFactor.
static bool hmap_overload_factor(size_t count, uint8_t b) {
	return count > BUCKET_CNT && count > LOAD_FACTOR_NUM*(hmap_bucket_shift(b)/LOAD_FACTOR_DEN);
}

// tooManyOverflowBuckets reports whether noverflow buckets is too many for a
// map with 1<<B buckets.
//
// Note that most of these overflow buckets must be in sparse use;
// if use was dense, then we'd have already triggered regular map growth.
static bool hmap_too_many_overflow_buckets(uint16_t noverflow, uint8_t b) {
	// If the threshold is too low, we do extraneous work.
	// If the threshold is too high, maps that grow and shrink can hold on to
	// lots of unused memory. "too many" means (approximately) as many overflow
	// buckets as regular buckets.
	//
	// See incrnoverflow for more details.
	if (b > 15) {
		b = 15;
	}
	return noverflow >= (uint16_t)(1<<(b&15));
}

static inline bool hmap_growing(const hmap *h) {
	return h->oldbuckets != NULL;
}

static void hmap_grow(hmap *h) {
	uint8_t bigger = 1;
	if (!hmap_overload_factor(h->count+1, h->b)) {
		bigger = 0;
		h->flags |= SAME_SIZE_GROW;
	}
	// WARN WARN WARN
	bmap *oldbuckets = h->buckets;
	assert(false);
}

static void hmap_grow_work(hmap *h, size_t bucket) {
	(void)h;
	(void)bucket;
	assert(false);
}

static void hmap_free(hmap *h, void **p) {
	if (*p) {
		if (h->free) {
			h->free(*p);
		} else {
			free(*p);
		}
		*p = NULL;
	}
}

// incrnoverflow increments h.noverflow.
// noverflow counts the number of overflow buckets.
// This is used to trigger same-size map growth.
// See also tooManyOverflowBuckets.
// To keep hmap small, noverflow is a uint16.
// When there are few buckets, noverflow is an exact count.
// When there are many buckets, noverflow is an approximate count.
static void hmap_incoverflow(hmap *h) {
	// We trigger same-size map growth if there are
	// as many overflow buckets as buckets.
	// We need to be able to count to 1<<h.B.
	if (h->b < 16) {
		h->noverflow++;
		return;
	}
	// Increment with probability 1/(1<<(h.B-15)).
	// When we reach 1<<15 - 1, we will have approximately
	// as many overflow buckets as buckets.
	uint32_t mask = (uint32_t)(1<<(h->b-15)) - 1;

	// WARN: seed rand!!!
	//
	// Example: if h.B == 18, then mask == 7,
	// and fastrand & 7 == 0 with probability 1/8.
	uint32_t rand_val = random();
	if ((rand_val&mask) == 0) {
		h->noverflow++;
	}
}

static bmap *hmap_newoverflow(hmap *h, bmap *b) {
	bmap *ovf = NULL;
	if (h->extra != NULL && h->extra->next_overflow != NULL) {
		// We have preallocated overflow buckets available.
		// See makeBucketArray for more details.
		ovf = h->extra->next_overflow;
		if (ovf->overflow == NULL) {
			// We're not at the end of the preallocated overflow buckets.
			// Bump the pointer.
			h->extra->next_overflow = ovf;
		} else {
			// This is the last preallocated overflow bucket.
			// Reset the overflow pointer on this bucket,
			// which was set to a non-nil sentinel value.
			ovf->overflow = NULL;
			h->extra->next_overflow = NULL;
		}
	} else {
		ovf = xcalloc(1, sizeof(bmap));
	}
	hmap_incoverflow(h);
	b->overflow = ovf;
	return ovf;
}

HEDLEY_NON_NULL()
int hmap_assign_str(hmap *h, const char *str, void *value) {
	assert(h);
	assert(str);
	if (h->buckets == NULL) {
		h->buckets = xcalloc(1, sizeof(bmap));
	}
	size_t len = strlen(str);
	uint64_t hash = XXH64(str, len, (XXH64_hash_t)h->hash0);
	uint8_t top = tophash(hash);

	size_t bucket;
	bmap *b;

	bmap *insertb;
	size_t inserti;
	string_t *insertk;

again:
	bucket = hash & hmap_bucket_mask(h->b);
	if (hmap_growing(h)) {
		// WARN WARN WARN
		hmap_grow_work(h, bucket);
	}
	b = &h->buckets[bucket];
	assert(b);

	insertb = NULL;
	inserti = 0;
	insertk = NULL;

	for (;;) {
		for (int i = 0; i < BUCKET_CNT; i++) {
			if (b->tophash[i] != top) {
				if (hmap_is_empty(b->tophash[i]) && insertb == NULL) {
					insertb = b;
					inserti = i;
				}
				if (b->tophash[i] == EMPTY_REST) {
					goto exit_bucketloop;
				}
			}
			string_t *k = &b->keys[i];
			if (!string_equal(k, str, len)) {
				continue;
			}
			// already have a mapping for key. Update it.
			inserti = i;
			insertb = b;
			// Overwrite existing key.
			string_assign(k, str, len);
			goto done;
		}
		if (!b->overflow) {
			break;
		}
		b = b->overflow;
	}
exit_bucketloop:

	// Did not find mapping for key. Allocate new cell & add entry.

	// If we hit the max load factor or we have too many overflow buckets,
	// and we're not already in the middle of growing, start growing.
	if (!hmap_growing(h) && (hmap_overload_factor(h->count+1, h->b) ||
		hmap_too_many_overflow_buckets(h->noverflow, h->b))) {

		// WARN WARN WARN WARN WARN WARN
		hmap_grow(h);
		goto again;
	}

	if (insertb == NULL) {
		// The current bucket and all the overflow buckets connected to it are
		// full, allocate a new one.
		insertb = hmap_newoverflow(h, b);
		inserti = 0; // not necessary, but avoids needlessly spilling inserti
	}
	insertb->tophash[inserti] = top;

	// store new key at insert position
	insertk = &insertb->keys[inserti];
	string_assign(insertk, str, len);
	h->count++;

done:
	hmap_free(h, &insertb->values[inserti]);
	insertb->values[inserti] = value;
	return 0;
}

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;

	printf("N: %lu\n", lrand48());
	printf("N: %li\n", random());
	// printf("N: %zu\n", SIZE_MAX);
	// printf("N: %zu\n", __builtin_object_size(ptrdiff_t, 0));
	return 0;
}
