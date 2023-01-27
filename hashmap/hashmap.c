#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

// WARN: dev only
#include <malloc/malloc.h>

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

HEDLEY_NON_NULL(1, 2)
static void string_move(string_t *dst, string_t *src) {
	*dst = *src;
	src->len = 0;
	src->str = NULL;
}

static void string_free(string_t *s) {
	if (s && s->str) {
		free(s->str);
	}
#ifndef NDEBUG
	// Zero when debug enabled
	if (s) memset(s, 0, sizeof(string_t));
#endif
}

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

typedef struct mapextra {
	bmap **overflow;
	bmap **oldoverflow;
	bmap *next_overflow;
} mapextra;

typedef void (*free_fn)(void*);

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

////////////////////////////////////////////////////////////////////////////////
// WARN: debug only
void print_bucket(const bmap *buckets, int nbuckets);
//
////////////////////////////////////////////////////////////////////////////////

HEDLEY_RETURNS_NON_NULL
static hmap *hmap_new(free_fn free) {
	uint32_t seed = random() & UINT32_MAX;
	hmap *h = xmalloc(sizeof(hmap));
	*h = (hmap){ .hash0 = seed, .free = free };
	return h;
}

static void hmap_free_value(hmap *h, bmap_entry *e) {
	if (e) {
		if (h->free) {
			h->free(e->ptr);
		} else {
			free(e->ptr);
		}
#ifndef NDEBUG
		e->ptr = NULL;
#endif
	}
}

static void hmap_destroy_bucket(hmap *h, bmap *b) {
	assert(b);
	for (int i = 0; i < BUCKET_CNT; i++) {
		string_free(&b->keys[i]);
		hmap_free_value(h, &b->values[i]);
	}
	memset(b->tophash, 0, sizeof(b->tophash));
}

static void hmap_destroy_bucket_array(hmap *h, bmap *buckets, size_t nbuckets) {
	if (!buckets) {
		return;
	}
	assert(nbuckets < SIZE_MAX);
	for (size_t i = 0; i < nbuckets; i++) {
		hmap_destroy_bucket(h, &buckets[i]);
	}
#ifndef NDEBUG
	memset(buckets, 0, sizeof(bmap) * nbuckets);
#endif
}

static inline size_t tophash_is_empty(uint8_t x) {
	return x <= EMPTY_ONE;
}

static inline size_t hmap_bucket_shift(uint8_t b) {
	return (size_t)1 << (b & sizeof(void*)*8 - 1);
}

static inline size_t hmap_bucket_mask(uint8_t b) {
	return hmap_bucket_shift(b) - 1;
}

static void hmap_destroy(hmap *h) {
	if (h == NULL) {
		return;
	}
	hmap_destroy_bucket_array(h, h->buckets, hmap_bucket_shift(h->b));
	if (h->oldbuckets != NULL) {
		assert(h->b > 0);
		hmap_destroy_bucket_array(h, h->oldbuckets, hmap_bucket_shift(h->b - 1));
	}
	// WARN: need to figure out how to free this!!!
	assert(h->extra == NULL);
}

static inline uint8_t tophash(uint64_t hash) {
	uint8_t top = (uint8_t)(hash >> (sizeof(void*)*8 - 8));
	if (top < MIN_TOP_HASH) {
		top += MIN_TOP_HASH;
	}
	return top;
}

static inline bool bmap_evacuated(const bmap *b) {
	uint8_t h = b->tophash[0];
	return h > EMPTY_ONE && h < MIN_TOP_HASH;
}

// overLoadFactor reports whether count items placed in 1<<B buckets is over loadFactor.
static inline bool hmap_overload_factor(size_t count, uint8_t b) {
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

static void hmap_make_bucket_array(uint8_t b, bmap **buckets, bmap **next_overflow, bmap *dirty_alloc) {
	// TODO: see if we need to implement this
	assert(dirty_alloc == NULL);
	assert(buckets);
	assert(next_overflow);

	// TODO: calculate the size of the actuall allocation and use that for overflow.
	// size_t nbuckets = base;

	size_t nbuckets = hmap_bucket_shift(b);
	*buckets = xcalloc(nbuckets, sizeof(bmap));
	*next_overflow = NULL;
}

static inline bool hmap_same_size_grow(const hmap *h) {
	return !!(h->flags&SAME_SIZE_GROW);
}

static inline size_t hmap_noldbuckets(const hmap *h) {
	uint8_t old_b = h->b;
	if (!hmap_same_size_grow(h)) {
		old_b--;
	}
	return hmap_bucket_shift(old_b);
}

// WARN: there needs to be a better way to do this
static inline size_t hmap_old_bucket_mask(const hmap *h) {
	return hmap_noldbuckets(h) - 1;
}

static void hmap_grow(hmap *h) {
	uint8_t bigger = 1;
	if (!hmap_overload_factor(h->count+1, h->b)) {
		bigger = 0;
		h->flags |= SAME_SIZE_GROW;
	}
	bmap *oldbuckets = h->buckets;
	bmap *newbuckets;
	bmap *next_overflow;
	hmap_make_bucket_array(h->b+bigger, &newbuckets, &next_overflow, NULL);

	// Free old buckets, if any
	if (h->oldbuckets) {
		assert(h->b > 0);
		hmap_destroy_bucket_array(h, h->oldbuckets, hmap_noldbuckets(h));
	}

	uint8_t flags = h->flags & ~(ITERATOR | OLD_ITERATOR);
	if ((flags&ITERATOR) != 0) {
		flags |= OLD_ITERATOR;
	}
	h->b += bigger;
	h->flags = flags;
	h->oldbuckets = oldbuckets;
	h->buckets = newbuckets;
	h->nevacuate = 0;
	h->noverflow = 0;

	if (h->extra != NULL && h->extra->overflow != NULL) {
		// Promote current overflow buckets to the old generation.
		if (h->extra->overflow != NULL) {
			xdie("oldoverflow is not nil");
		}
		h->extra->oldoverflow = h->extra->overflow;
		h->extra->overflow = NULL;
	}
	if (next_overflow != NULL) {
		if (h->extra == NULL) {
			h->extra = xcalloc(1, sizeof(mapextra));
		}
		h->extra->next_overflow = next_overflow;
	}

	// the actual copying of the hash table data is done incrementally
	// by growWork() and evacuate().
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
	uint32_t rand_val = random()|UINT32_MAX;
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
			h->extra->next_overflow = &ovf[1];
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

static void hmap_advance_evacuation_mark(hmap *h, size_t newbit) {
	h->nevacuate++;
	// Experiments suggest that 1024 is overkill by at least an order of magnitude.
	// Put it in there as a safeguard anyway, to ensure O(1) behavior.
	size_t stop = h->nevacuate + 1024;
	if (stop > newbit) {
		stop = newbit;
	}
	while (h->nevacuate != stop && bmap_evacuated(&h->oldbuckets[h->nevacuate])) {
		h->nevacuate++;
	}
	if (h->nevacuate == newbit) { // newbit == # of oldbuckets
		// Growing is all done. Free old main bucket array.
		h->oldbuckets = NULL;
		// Can discard old overflow buckets as well.
		// If they are still referenced by an iterator,
		// then the iterator holds a pointers to the slice.
		if (h->extra != NULL) {
			h->extra->oldoverflow = NULL;
		}
		h->flags &= ~(SAME_SIZE_GROW);
	}
}

static inline void bmap_clear(bmap *b) {
#ifndef NDEBUG
	static const size_t key_offset = offsetof(bmap, keys);
	// Preserve b.tophash because the evacuation
	// state is maintained there.
	assert(b);
	memset((char *)b+key_offset, 0, sizeof(bmap)-key_offset);
#else
	(void)b;
#endif
}

typedef struct {
	bmap       *b; // current destination bucket
	size_t     i;  // key/elem index into b
	string_t   *k; // pointer to current key storage
	bmap_entry *e; // pointer to current elem storage
} evac_dest;

static void hmap_evacuate(hmap *h, size_t oldbucket) {
	assert(h->oldbuckets);
	assert(oldbucket <= hmap_noldbuckets(h));

	bmap *b = &h->oldbuckets[oldbucket];
	size_t newbit = hmap_noldbuckets(h);
	if (!bmap_evacuated(b)) {
		// TODO: simplify xy
		// xy contains the x and y (low and high) evacuation destinations.
		evac_dest xy[2] = { 0 };
		evac_dest *x = &xy[0];
		x->b = &h->buckets[oldbucket];
		x->k = &h->buckets[oldbucket].keys[0];
		x->e = &h->buckets[oldbucket].values[0];;

		if (!hmap_same_size_grow(h)) {
			// Only calculate y pointers if we're growing bigger.
			evac_dest *y = &xy[1];
			y->b = &h->buckets[oldbucket+newbit];
			y->k = &h->buckets[oldbucket+newbit].keys[0];
			y->e = &h->buckets[oldbucket+newbit].values[0];;
		}

		for ( ; b != NULL; b = b->overflow) {
			for (int i = 0; i < BUCKET_CNT; i++) {
				uint8_t top = b->tophash[i];
				if (tophash_is_empty(top)) {
					b->tophash[i] = EVACUATED_EMPTY;
					continue;
				}
				if (top < MIN_TOP_HASH) {
					xdie("bad map state");
				}
				uint8_t use_y = 0;
				string_t *k = &b->keys[i];
				if (!hmap_same_size_grow(h)) {
					uint64_t hash = XXH64(k->str, k->len, (XXH64_hash_t)h->hash0);
					if ((hash&newbit) != 0) {
						use_y = 1;
					}
				}

				// evacuatedX + 1 == evacuatedY, enforced in makemap
				b->tophash[i] = EVACUATED_X + use_y;
				evac_dest *dst = &xy[use_y]; // evacuation destination

				if (dst->i == BUCKET_CNT) {
					dst->b = hmap_newoverflow(h, dst->b);
					dst->i = 0;
					dst->k = &dst->b->keys[0];
					dst->e = &dst->b->values[0];
				}
				// dst->b->tophash[dst->i&(BUCKET_CNT-1)] = top;
				dst->b->tophash[dst->i] = top;

				// Move key
				string_move(dst->k, k);
				*dst->e = b->values[i];
				dst->i++;

				// These updates might push these pointers past the end of the
				// key or elem arrays.  That's ok, as we have the overflow pointer
				// at the end of the bucket to protect against pointing past the
				// end of the bucket.
				dst->k++;
				dst->e++;
			}
		}

		// Unlink the overflow buckets & clear key/elem.
		if ((h->flags&OLD_ITERATOR) == 0) {
			// This only clears the map when NDEBUG is not defined.
			bmap_clear(&h->oldbuckets[oldbucket]);
		}
	}

	// WARN: this breaks everything !!!
	if (oldbucket == h->nevacuate) {
		hmap_advance_evacuation_mark(h, newbit);
	}
}

static void hmap_grow_work(hmap *h, size_t bucket) {
	// make sure we evacuate the oldbucket corresponding
	// to the bucket we're about to use
	hmap_evacuate(h, bucket&hmap_old_bucket_mask(h));

	// evacuate one more oldbucket to make progress on growing
	if (hmap_growing(h)) {
		hmap_evacuate(h, h->nevacuate);
	}
}

HEDLEY_NON_NULL(1, 2)
int hmap_assign_str(hmap *h, const char *str, void *value) {
	assert(h);
	assert(str);

	size_t len = strlen(str);
	uint64_t hash = XXH64(str, len, (XXH64_hash_t)h->hash0);

	if (h->buckets == NULL) {
		h->buckets = xcalloc(1, sizeof(bmap));
	}
	size_t bucket;
	bmap *b;

	bmap *insertb;
	size_t inserti;
	string_t *insertk;
	uint8_t top;

again:
	bucket = hash & hmap_bucket_mask(h->b);
	if (hmap_growing(h)) {
		// WARN WARN WARN
		hmap_grow_work(h, bucket);
	}
	b = &h->buckets[bucket];
	top = tophash(hash);

	insertb = NULL;
	inserti = 0;
	insertk = NULL;

	for (;;) {
		for (int i = 0; i < BUCKET_CNT; i++) {
			if (b->tophash[i] != top) {
				if (tophash_is_empty(b->tophash[i]) && insertb == NULL) {
					insertb = b;
					inserti = i;
				}
				if (b->tophash[i] == EMPTY_REST) {
					goto exit_bucketloop;
				}
				continue;
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

		hmap_grow(h); // Growing the table invalidates everything, so try again
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
	hmap_free_value(h, &insertb->values[inserti]);
	insertb->values[inserti].ptr = value;
	return 0;
}

static const char *words[] = {
	"abac",
	"abaca",
	"abacate",
	"abacay",
	"abacinate",
	"abacination",
	"abaciscus",
	"abacist",
	"aback",
	"abactinal",
	"abactinally",
	"abaction",
	"abactor",
	"abaculus",
	"abacus",
	"Abadite",
	"abaff",
	"abaft",
	"abaisance",
	"abaiser",
	"abaissed",
	"abalienate",
	"abalienation",
	"abalone",
	"Abama",
	"abampere",
	"abandon",
	"abandonable",
	"abandoned",
	"abandonedly",
	"abandonee",
	"abandoner",
	"abandonment",
	"Abanic",
	"Abantes",
	"abaptiston",
	"Abarambo",
	"Abaris",
	"abarthrosis",
	"abarticular",
	"abarticulation",
	"abas",
	"abase",
	"abased",
	"abasedly",
};
static const int words_len = sizeof(words) / sizeof(words[0]);

void print_bucket(const bmap *buckets, int nbuckets) {
	for (int j = 0; j < nbuckets; j++) {
		const bmap *b = &buckets[j];
		// assert(b);
		for ( ; b != NULL; b = b->overflow) {
			for (int i = 0; i < BUCKET_CNT; i++) {
				if (tophash_is_empty(b->tophash[i])) {
					continue;
				}
				if (b->values[i].ptr) {
					int v = *((int *)b->values[i].ptr);
					printf("%i.%i: %hhu: %s: %i\n", j, i, b->tophash[i], b->keys[i].str, v);
				} else {
					printf("%i.%i: %hhu: %s: (null)\n", j, i, b->tophash[i], b->keys[i].str);
				}
			}
			printf("\n");
		}
	}
}

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;

	// WARN WARN WARN
	srandomdev();
	printf("rand: %li\n", random());

	hmap *h = hmap_new(NULL);
	assert(h);
	for (int i = 0; i < words_len; i++) {
		int *p = malloc(sizeof(int));
		*p = i;
		hmap_assign_str(h, words[i], p);
	}

	printf("b: %i\n", (int)h->b);
	printf("shift: %zu\n", hmap_bucket_shift(h->b));
	printf("count: %zu\n", h->count);
	printf("noverflow: %u\n", (unsigned int)h->noverflow);
	printf("nevacuate: %zu\n", h->nevacuate);

	printf("\n# buckets:\n");
	print_bucket(h->buckets, hmap_bucket_shift(h->b));

	printf("# old buckets:\n");
	print_bucket(h->oldbuckets, hmap_noldbuckets(h));

	hmap_destroy(h);

	// for (int i = 0; i < 8; i++) {
	// 	printf("%i: %zu\n", i, hmap_bucket_shift(i));
	// }
	return 0;
}

// static const uint64_t rand_numbers[] = {
// 	5577006791947779410,
// 	8674665223082153551,
// 	6129484611666145821,
// 	4037200794235010051,
// 	3916589616287113937,
// 	6334824724549167320,
// 	605394647632969758,
// 	1443635317331776148,
// };
// static const int rand_numbers_len = sizeof(rand_numbers) / sizeof(rand_numbers[0]);
