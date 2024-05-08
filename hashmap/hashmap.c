// Required for random(3)
#if defined(__GNUC__) && !defined(_DEFAULT_SOURCE)
#    define _DEFAULT_SOURCE 1
#endif
#include <stdlib.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdarg.h>

#define XXH_INLINE_ALL 1
#define XXH_NO_STREAM 1
#include "xxhash.h"

#include "utils.h"
#include "hedley.h"

// WARN: dev only
#include "timer.h"
#include "hashmap.h"

#define BUCKET_CNT_BITS 3
#define BUCKET_CNT      (1 << BUCKET_CNT_BITS)

// Maximum average load of a bucket that triggers growth is 6.5.
// Represent as LOAD_FACTOR_NUM/LOAD_FACTOR_DEN, to allow integer math.
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

static inline bool memcmp_small(const void *p1, const void *p2, size_t n) {
	const unsigned char *s1 = (const unsigned char *)p1;
	const unsigned char *s2 = (const unsigned char *)p2;
	for (; n && *s1++ == *s2++; n--) {
	}
	return n == 0;
}

static inline bool memequal(const char *p1, const char *p2, size_t n) {
	// TODO: tune this value on amd64.
	// Both clang and GCC unroll memcmp_small when n <= 16.
	static const size_t small_cutoff = 8;
	if (p1 == p2) {
		return true;
	}
	if (n <= small_cutoff) {
		return memcmp_small(p1, p2, n);
	}
	return memcmp(p1, p2, 8) == 0 && memcmp(&p1[8], &p2[8], n-8) == 0;
}

HEDLEY_NON_NULL(1, 2)
static inline bool string_equal(const string_t *s, const char *str, size_t len) {
	// NB: this is faster on macOS but needs to be tested on Linux with glibc
	return s->len == len && s->str[0] == str[0] && memequal(s->str, str, len);
}

HEDLEY_NON_NULL(1, 2)
static inline void string_move(string_t *dst, const string_t *src) {
	*dst = *src;
}

HEDLEY_NON_NULL(1, 2)
static void string_assign(string_t *key, const char *str, size_t len) {
	assert(key);
	assert(str);
	// TODO: shrink very large keys
	// static const size_t MiB = 1024 * 1024;
	// if (key->len > MiB && len < key->len / 4) {
	// 	/* code */
	// }
	if (key->len < len) {
		key->str = xrealloc(key->str, len + 1);
	}
	memmove(key->str, str, len + 1);
	key->len = len;
}

// TODO: allow the hmap to *not* own the string.
static void string_free(string_t *s) {
	if (s && s->str) {
		free(s->str);
		memset(s, 0, sizeof(string_t));
	}
}

////////////////////////////////////////////////////////////////////////////////
// WARN: debug only
void print_bucket(const bmap *buckets, int nbuckets, const char *prefix);
//
////////////////////////////////////////////////////////////////////////////////

// TODO: use this:
// #include <time.h>
// #include <unistd.h>
// #include <fcntl.h>
//
// #if !defined(RAND_CLOCK_ID)
// #	if defined(CLOCK_MONOTONIC_RAW)
// #		define RAND_CLOCK_ID CLOCK_MONOTONIC_RAW
// #	elif defined(CLOCK_MONOTONIC)
// #		define RAND_CLOCK_ID CLOCK_MONOTONIC
// #	elif defined(CLOCK_REALTIME)
// #		define RAND_CLOCK_ID CLOCK_REALTIME
// #	else
// #		error "no compatible clockid_t defined!"
// #	endif
// #endif
//
// static inline uint32_t generate_random_seed(void) {
// 	uint32_t seed;
// 	int fd = open("/dev/random", O_RDONLY | O_CLOEXEC, 0);
// 	if (fd >= 0) {
// 		if (read(fd, (void *)&seed, sizeof(seed))) {
// 			return seed;
// 		}
// 		close(fd);
// 	}
//
// 	// Fallback to pid and clock time.
// 	uint32_t pid = getpid();
// 	struct timespec tp;
// 	do {
// 		clock_gettime(RAND_CLOCK_ID, &tp);
// 		seed = (uint32_t)((pid << 16) ^ tp.tv_sec ^ tp.tv_nsec);
// 	} while (seed == 0);
// 	return seed;
// }

// static inline void debug_zero_memory(void *ptr, size_t len) {
// #ifndef NDEBUG
// 	memset(ptr, 0, len);
// #else
// 	(void)ptr;
// 	(void)len;
// #endif
// }

static inline uint32_t rand_uint32(void) {
	return (uint32_t)(random()&UINT32_MAX);
}

static uint32_t hmap_gen_seed(void) {
	return rand_uint32();
}

void hmap_init_seed(hmap *h, free_fn free, uint32_t seed) {
	memset(h, 0, sizeof(hmap));
	h->free = free;
	h->hash0 = seed;
}

void hmap_init(hmap *h, free_fn free) {
	hmap_init_seed(h, free, hmap_gen_seed());
}

hmap *hmap_new_seed(free_fn free, uint32_t seed) {
	hmap *h = xmalloc(sizeof(hmap));
	hmap_init_seed(h, free, seed);
	return h;
}

hmap *hmap_new(free_fn free) {
	return hmap_new_seed(free, hmap_gen_seed());
}

static void hmap_free_value(hmap *h, bmap_entry *e) {
	if (e) {
		if (h->free) {
			h->free(e->ptr);
		} else {
			free(e->ptr);
		}
		e->ptr = NULL;
	}
}

static inline void hmap_free_bucket_elements(hmap *h, bmap *b) {
	for (int i = 0; i < BUCKET_CNT; i++) {
		string_free(&b->keys[i]);
		hmap_free_value(h, &b->values[i]);
	}
}

static void hmap_destroy_bucket(hmap *h, bmap *b) {
	assert(b);
	hmap_free_bucket_elements(h, b);
	for (bmap *o = b->overflow; o != NULL; ) {
		hmap_free_bucket_elements(h, o);
		bmap *next = o->overflow;
		free(o);
		o = next;
	}
	b->overflow = NULL;
}

static void hmap_destroy_bucket_array(hmap *h, bmap *buckets, size_t nbuckets) {
	if (buckets == NULL) {
		return;
	}
	for (bmap *b = buckets ; nbuckets != 0; nbuckets--, b++) {
		hmap_destroy_bucket(h, b);
	}
#ifndef NDEBUG
	memset(buckets, 0, sizeof(bmap) * nbuckets);
#endif
	free(buckets);
}

static bmap_array *bmap_array_new(void) {
	bmap_array *a = xcalloc(1, sizeof(bmap_array));
	return a;
}

static void bmap_array_free(hmap *h, bmap_array *a) {
	if (a == NULL) {
		return;
	}
	for (int32_t i = 0; i < a->len; i++) {
		hmap_destroy_bucket(h, a->buckets[i]);
	}
}

static void bmap_array_append(bmap_array *a, bmap *b) {
	if (a->cap < a->len + 1) {
		int32_t cap = (a->cap + 1) * 2;
		bmap **buckets = xrealloc(a->buckets, sizeof(bmap*) * cap);
		a->cap = cap;
		a->buckets = buckets;
	}
	a->buckets[a->len++] = b;
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

void hmap_destroy(hmap *h) {
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
#ifndef NDEBUG
	memset(h, 0, sizeof(hmap));
#endif
	// WARN: maybe we should not free since this prevents the use
	// of stack allocated hmaps.
	free(h);
}

static inline uint8_t tophash(uint64_t hash) {
	uint8_t top = (uint8_t)(hash >> (sizeof(uint64_t)*8 - 8));
	if (top < MIN_TOP_HASH) {
		top += MIN_TOP_HASH;
	}
	return top;
}

static inline bool bmap_evacuated(const bmap *b) {
	uint8_t h = b->tophash[0];
	return h > EMPTY_ONE && h < MIN_TOP_HASH;
}

// hmap_overload_factor reports whether count items placed in 1<<B buckets is over loadFactor.
static inline bool hmap_overload_factor(size_t count, uint8_t b) {
	return count > BUCKET_CNT && count > LOAD_FACTOR_NUM*(hmap_bucket_shift(b)/LOAD_FACTOR_DEN);
}

// hmap_too_many_overflow_buckets reports whether noverflow buckets is too many
// for a map with 1<<B buckets.
//
// Note that most of these overflow buckets must be in sparse use;
// if use was dense, then we'd have already triggered regular map growth.
static inline bool hmap_too_many_overflow_buckets(uint16_t noverflow, uint8_t b) {
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
		if (h->extra->oldoverflow != NULL) {
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
	uint32_t rand_val = rand_uint32();
	if ((rand_val&mask) == 0) {
		h->noverflow++;
	}
}

static void hmap_create_overflow(hmap *h) {
	if (h->extra == NULL) {
		h->extra = xcalloc(1, sizeof(mapextra));
	}
	if (h->extra->overflow == NULL) {
		h->extra->overflow = bmap_array_new();
	}
}

static bmap *hmap_newoverflow(hmap *h, bmap *b) {
	bmap *ovf = NULL;
	// WARN: this is always false
	if (h->extra != NULL && h->extra->next_overflow != NULL) {
		// WARN WARN WARN: this is never hit because we never assign
		// to hmap->extra
		assert(0);

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

static inline void hmap_advance_evacuation_mark(hmap *h, size_t newbit) {
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
		if (h->oldbuckets != NULL) {
			hmap_destroy_bucket_array(h, h->oldbuckets, hmap_bucket_shift(h->b - 1));
			h->oldbuckets = NULL;
		}
		// Can discard old overflow buckets as well.
		// If they are still referenced by an iterator,
		// then the iterator holds a pointers to the slice.
		if (h->extra != NULL) {
			// WARN: free this
			h->extra->oldoverflow = NULL;
		}
		h->flags &= ~(SAME_SIZE_GROW);
	}
}

// WARN: issue could be here
static inline void bmap_clear(bmap *b) {
	// Preserve b.tophash because the evacuation
	// state is maintained there.
	static const size_t key_offset = offsetof(bmap, keys);
	memset((char *)b+key_offset, 0, sizeof(bmap)-key_offset);
}

HEDLEY_NON_NULL(1, 2)
static inline uint64_t hmap_hash_str(const hmap *h, const char *s, size_t n) {
	// WARN: we're only using a 32 bit seed when the hash
	// function wants 64 bits.
	return XXH3_64bits_withSeed((const void*)s, n, (XXH64_hash_t)h->hash0);
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
					uint64_t hash = hmap_hash_str(h, k->str, k->len);
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
			// Free overflow buckets
			bmap *oldb = &h->oldbuckets[oldbucket];
			for (bmap *o = oldb->overflow; o != NULL; ) {
				bmap *cur = o;
				o = o->overflow;
				free(cur);
				// bmap *next = o->overflow;
				// free(o);
				// o = next;
			}
			bmap_clear(oldb);
		}
	}

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

HEDLEY_ALWAYS_INLINE
static void hmap_assign_strlen_impl(
	hmap *h,
	const char *str,
	size_t len,
	void *value
) {
	uint64_t hash = hmap_hash_str(h, str, len);
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
}

// TODO: how should we handle NULL strings ???
// TODO: do we really need hmap_assign_strlen_impl() ???
HEDLEY_NON_NULL(1, 2)
void hmap_assign_strlen(hmap *h, const char *str, size_t len, void *value) {
	assert(h);
	assert(str);
	hmap_assign_strlen_impl(h, str, len, value);
}

// TODO: how should we handle NULL strings ???
HEDLEY_NON_NULL(1, 2)
void hmap_assign_str(hmap *h, const char *str, void *value) {
	assert(h);
	assert(str);
	size_t len = strlen(str);
	hmap_assign_strlen_impl(h, str, len, value);
}

static inline void hmap_set_value(void **dst, const bmap_entry *e) {
	if (dst) {
		*dst = e ? e->ptr : NULL;
	}
}

static inline void hmap_not_found(void **dst) {
	hmap_set_value(dst, NULL);
}

bool hmap_access_strlen(hmap *h, const char *str, size_t len, void **value) {
	if (str == NULL) {
		hmap_not_found(value);
		return false;
	}
	if (h->b == 0) {
		// One bucket table
		const bmap *b = &h->buckets[0];
		if (len < 32) {
			for (int i = 0; i < BUCKET_CNT; i++) {
				const string_t *k = &b->keys[i];
				if (k->len != len || tophash_is_empty(b->tophash[i])) {
					if (b->tophash[i] == EMPTY_REST) {
						break;
					}
					continue;
				}
				if (string_equal(k, str, len)) {
					hmap_set_value(value, &b->values[i]);
					return true;
				}
			}
			hmap_not_found(value);
			return false;
		}
		// long key, try not to do more comparisons than necessary
		assert(len >= 32);
		size_t keymaybe = BUCKET_CNT;
		for (int i = 0; i < BUCKET_CNT; i++) {
			const string_t *k = &b->keys[i];
			if (k->len != len || tophash_is_empty(b->tophash[i])) {
				if (b->tophash[i] == EMPTY_REST) {
					break;
				}
				continue;
			}
			// check first 4 bytes
			if (memcmp(k->str, str, 4) != 0) {
				continue;
			}
			// check last 4 bytes
			if (memcmp(&k->str[len-4], &str[len-4], 4) != 0) {
				continue;
			}
			if (keymaybe != BUCKET_CNT) {
				// Two keys are potential matches. Use hash to distinguish them.
				goto dohash;
			}
			keymaybe = i;
		}
		if (keymaybe != BUCKET_CNT) {
			const string_t *k = &b->keys[keymaybe];
			if (string_equal(k, str, len)) {
				hmap_set_value(value, &b->values[keymaybe]);
				return true;
			}
		}
		hmap_not_found(value);
		return false;
	}

	uint64_t hash;
	size_t m;
	uint8_t top;
	bmap *b;
dohash:
	hash = hmap_hash_str(h, str, len);
	m = hmap_bucket_mask(h->b);
	b = &h->buckets[hash&m];
	if (h->oldbuckets != NULL) {
		if (!hmap_same_size_grow(h)) {
			m >>= 1;
		}
		bmap *oldb = &h->oldbuckets[hash&m];
		if (!bmap_evacuated(oldb)) {
			b = oldb;
		}
	}
	top = tophash(hash);
	for (; b != NULL; b = b->overflow) {
		for (int i = 0; i < BUCKET_CNT; i++) {
			const string_t *k = &b->keys[i];
			if (k->len != len || b->tophash[i] != top) {
				continue;
			}
			if (string_equal(k, str, len)) {
				hmap_set_value(value, &b->values[i]);
				return true;
			}
		}
	}

	hmap_not_found(value);
	return false;
}

bool hmap_access_str(hmap *h, const char *str, void **value) {
	return hmap_access_strlen(h, str, str != NULL ? strlen(str) : 0, value);
}

static HEDLEY_ALWAYS_INLINE bool hmap_delete_strlen_impl(
	hmap *h,
	const char *str,
	size_t len
) {
	if (str == NULL) {
		return false;
	}
	uint64_t hash = hmap_hash_str(h, str, len);

	size_t bucket = hash & hmap_bucket_mask(h->b);
	if (hmap_growing(h)) {
		// WARN WARN WARN
		hmap_grow_work(h, bucket);
	}
	bmap *b = &h->buckets[bucket];
	bmap *b_orig = b;
	uint8_t top = tophash(hash);
	string_t *k;
	for (; b != NULL; b = b->overflow) {
		for (int i = 0; i < BUCKET_CNT; i++) {
			k = &b->keys[i];
			if (k->len != len || b->tophash[i] != top) {
				continue;
			}
			if (!string_equal(k, str, len)) {
				continue;
			}
			string_free(k);
			hmap_free_value(h, &b->values[i]);
			b->tophash[i] = EMPTY_ONE;
			// If the bucket now ends in a bunch of emptyOne states,
			// change those to emptyRest states.
			if (i == BUCKET_CNT-1) {
				if (b->overflow != NULL && b->overflow->tophash[0] != EMPTY_REST) {
					goto notlast;
				}
			} else {
				if (b->tophash[i+1] != EMPTY_REST) {
					goto notlast;
				}
			}
			for (;;) {
				b->tophash[i] = EMPTY_REST;
				if (i == 0) {
					if (b == b_orig) {
						break; // beginning of initial bucket, we're done.
					}
					// Find previous bucket, continue at its last entry.
					bmap *c = b;
					for (b = b_orig; b->overflow != c; b = b->overflow) {
					}
					i = BUCKET_CNT - 1;
				} else {
					i--;
				}
				if (b->tophash[i] != EMPTY_ONE) {
					break;
				}
			}
		notlast:
			h->count--;
			// Reset the hash seed to make it more difficult for attackers to
			// repeatedly trigger hash collisions. See issue 25237.
			if (h->count == 0) {
				h->hash0 = hmap_gen_seed();
			}
			goto search_end;
		}
	}
search_end:
	return false;
}

bool hmap_delete_strlen(hmap *h, const char *str, size_t len) {
	return hmap_delete_strlen_impl(h, str, len);
}

bool hmap_delete_str(hmap *h, const char *str) {
	return hmap_delete_strlen_impl(h, str, strlen(str));
}

// type hiter struct {
// 	key         unsafe.Pointer // Must be in first position.  Write nil to indicate iteration end (see cmd/compile/internal/walk/range.go).
// 	elem        unsafe.Pointer // Must be in second position (see cmd/compile/internal/walk/range.go).
// 	t           *maptype
// 	h           *hmap
// 	buckets     unsafe.Pointer // bucket ptr at hash_iter initialization time
// 	bptr        *bmap          // current bucket
// 	overflow    *[]*bmap       // keeps overflow buckets of hmap.buckets alive
// 	oldoverflow *[]*bmap       // keeps overflow buckets of hmap.oldbuckets alive
// 	startBucket uintptr        // bucket iteration started at
// 	offset      uint8          // intra-bucket offset to start from during iteration (should be big enough to hold bucketCnt-1)
// 	wrapped     bool           // already wrapped around from end of bucket array to beginning
// 	B           uint8
// 	i           uint8
// 	bucket      uintptr
// 	checkBucket uintptr
// }

// TESTS
////////////////////////////////////////////////////////////////////////////////

static const char *words[] = {
	"overannotate",
	"medioccipital",
	"balden",
	"noctivagant",
	"Latimeria",
	"difficultly",
	"unadorn",
	"nonchokable",
	"hemicylindrical",
	"unwiliness",
	"unsecurity",
	"chalcites",
	"nosophyte",
	"nonmoral",
	"unnationalized",
	"ascarides",
	"hyphaeresis",
	"speechification",
	"medimno",
	"mesameboid",
	"Hippuris",
	"admire",
	"wraithlike",
	"polychloride",
	"pulmotracheal",
	"unaveraged",
	"iridadenosis",
	"unimmerged",
	"portable",
	"anoine",
	"selachoid",
	"bootstrap",
	"associational",
	"fibrinemia",
	"dogship",
	"Cocos",
	"Sphyrna",
	"beletter",
	"scandalization",
	"tergant",
	"buzane",
	"forcing",
	"dygogram",
	"bugled",
	"nonstationary"
};
static const int words_len = sizeof(words) / sizeof(words[0]);

void print_bucket(const bmap *buckets, int nbuckets, const char *prefix) {
	const char *pfx = prefix ? prefix : "";
	for (int j = 0; j < nbuckets; j++) {
		const bmap *b = &buckets[j];
		// assert(b);
		for ( ; b != NULL; b = b->overflow) {
			bool newline = false;
			for (int i = 0; i < BUCKET_CNT; i++) {
				if (tophash_is_empty(b->tophash[i])) {
					continue;
				}
				newline = true;
				if (b->values[i].ptr) {
					int v = *((int *)b->values[i].ptr);
					printf("%s%i.%i: %hhu: %s: %i\n", pfx, j, i, b->tophash[i], b->keys[i].str, v);
				} else {
					printf("%s%i.%i: %hhu: %s: (null)\n", pfx, j, i, b->tophash[i], b->keys[i].str);
				}
			}
			if (newline) {
				printf("\n");
			}
		}
	}
}

void print_hmap(const hmap *h) {
	printf("# hmap: %p\n", (const void*)h);
	printf("  count:       %zu\n", h->count);
	printf("  flags:       %hhu\n", h->flags);
	printf("  b:           %hhu\n", h->b);
	printf("  noverflow:   %hu\n", h->noverflow);
	printf("  hash0:       %u\n", h->hash0);
	printf("  nevacuate:   %zu\n", h->nevacuate);
	printf("  ndbuckets:   %zu\n", hmap_bucket_shift(h->b));
	printf("  noldbuckets: %zu\n", hmap_noldbuckets(h));
	printf("\n");
	printf("# buckets:\n");
	print_bucket(h->buckets, hmap_bucket_shift(h->b), "  ");
	printf("# old buckets:\n");
	print_bucket(h->oldbuckets, hmap_noldbuckets(h), "  ");
}

typedef struct {
	int  failed;
	bool verbose;
	bool fail_fast;
} test_runner;

static int test_failure_count(const test_runner *t) {
	return t->failed;
}

static void test_mark_failure(test_runner *t) {
	t->failed++;
	if (t->fail_fast) {
		fprintf(stderr, "FAIL\n");
		exit(1);
	}
}

static bool test_failed(const test_runner *t) {
	return test_failure_count(t) > 0;
}

static int test_exit_code(const test_runner *t) {
	if (test_failed(t)) {
		fprintf(stderr, "FAIL\n");
		return 1;
	}
	fprintf(stderr, "PASS\n");
	return 0;
}

HEDLEY_PRINTF_FORMAT(2, 3)
static void terror(test_runner *t, const char *format, ...) {
	test_mark_failure(t);
	fprintf(stdout, "Error: ");
	va_list args;
	va_start(args, format);
	if (vfprintf(stdout, format, args) < 0) {
		xdie("FATAL: vfprintf");
	}
	va_end(args);
}

static void _test_assert_count(test_runner *t, const hmap *h, size_t count,
	                           const char *file, int line) {
	if (h->count != count) {
		terror(t, "%s:%d count: got: %zu; want: %zu\n", file, line, h->count, count);
	}
}

static void test_dump_map(const test_runner *t, const hmap *h) {
	if (!t->verbose) {
		return;
	}
	printf("################################################\n");
	print_hmap(h);
	printf("################################################\n\n");
}

static void _test_assert_value(test_runner *t, hmap *h, const char *key,
	                           int *value, const char *file, int line) {
	int *p = NULL;
	if (!hmap_access_str(h, key, (void**)&p)) {
		terror(t, "%s:%d missing key '%s'\n", file, line, key);
		test_dump_map(t, h);
		return;
	}
	if ((!value && !p) || (value && p && *value == *p)) {
		return;
	}
	if (value == NULL || p == NULL) {
		terror(t, "%s:%d key: %s got value %p want %p\n", file, line, key, (void*)p, (void*)value);
	} else {
		terror(t, "%s:%d key: %s got value %d want %d\n", file, line, key, *p, *value);
	}
}

static void _test_assert_contains(test_runner *t, hmap *h, const char *key,
	                              bool want, const char *file, int line) {
	int *p = NULL;
	bool got = hmap_access_str(h, key, (void**)&p);
	if (got != want) {
		const char *msg = got
			? "does not contain key"
			: "unexpected key";
		terror(t, "%s:%d %s: '%s'\n", file, line, msg, key);
		test_dump_map(t, h);
		return;
	}
}

#define test_assert_count(t, h, count) \
	_test_assert_count(t, h, count, __FILE__, __LINE__)

#define test_assert_value(t, h, key, value) \
	_test_assert_value(t, h, key, value, __FILE__, __LINE__)

#define test_assert_has_key(t, h, key) \
	_test_assert_contains(t, h, key, true, __FILE__, __LINE__)

#define test_assert_missing_key(t, h, key) \
	_test_assert_contains(t, h, key, false, __FILE__, __LINE__)

bool test_hmap(test_runner *t) {
	hmap *h = hmap_new(NULL);
	for (int i = 0; i < words_len; i++) {
		test_assert_count(t, h, (size_t)i);

		int *p = xmalloc(sizeof(int));
		*p = i;
		hmap_assign_str(h, words[i], p);
		test_assert_value(t, h, words[i], p);
		test_assert_has_key(t, h, words[i]);

		test_assert_count(t, h, (size_t)i + 1);
	}

	int fail_count = test_failure_count(t);
	int *p = xmalloc(sizeof(int));
	for (int i = 0; i < words_len; i++) {
		*p = i;
		test_assert_value(t, h, words[i], p);
	}
	free(p);
	p = NULL;

	for (int i = 0; i < words_len; i++) {
		hmap_delete_str(h, words[i]);
		test_assert_missing_key(t, h, words[i]);
		test_assert_count(t, h, words_len - i - 1);
	}
	test_assert_count(t, h, 0);

	if (test_failed(t) && test_failure_count(t) > fail_count) {
		printf("\n################################\n");
		print_hmap(h);
		printf("################################\n\n");
	}

	hmap_destroy(h);
	return true;
}

bool test_hmap_web2(test_runner *t) {
	int len = 0;
	int cap = 1024;
	string_t *list = xcalloc(cap, sizeof(string_t));
	size_t n = 0;
	char *line = NULL;
	FILE *f = fopen("testdata/web2", "r");
	assert(f);
	while (getline(&line, &n, f) > 0) {
		if (len == cap) {
			cap = (cap + 1) * 2;
			list = xrealloc(list, sizeof(string_t) * cap);
		}
		assert(line);
		const char *s = strdup(line);
		assert(s);
		string_assign(&list[len++], s, strlen(s));
	}
	free(line);
	if (len == 0) {
		terror(t, "failed to read any lines from testdata/web2\n");
		return false;
	}

	hmap *h = hmap_new(NULL);
	ns_time_t start = ns_time_now();
	for (int i = 0; i < len; i++) {
		test_assert_count(t, h, (size_t)i);

		int *p = xmalloc(sizeof(int));
		*p = i;
		hmap_assign_strlen(h, list[i].str, list[i].len, p);
		test_assert_value(t, h, list[i].str, p);
		test_assert_has_key(t, h, list[i].str);

		test_assert_count(t, h, (size_t)i + 1);
	}

	for (int i = 0; i < len; i++) {
		hmap_delete_str(h, list[i].str);
		test_assert_missing_key(t, h, list[i].str);
		test_assert_count(t, h, len - i - 1);
	}
	ns_duration_t dur = ns_time_since(start);
	printf("web: %.2fms - %zu ns/op\n", ns_duration_ms(dur), (size_t)dur/(size_t)len);

	hmap_destroy(h);
	for (int i = 0; i < len; i++) {
		string_free(&list[i]);
	}
	free(list);
	return true;
}

static int benchmark_hmap_access_fn(long N, void *data) {
	string_t *strlist = (string_t *)data;

	hmap *h = hmap_new(NULL);
	for (int i = 0; i < words_len; i++) {
		hmap_assign_str(h, words[i], NULL);
	}

	for (long j = 0; j < N; j++) {
		// for (int i = 0; i < words_len; i++) {
		// 	hmap_assign_strlen(h, strlist[i].str, strlist[i].len, NULL);
		// }
		for (int i = 0; i < words_len; i++) {
			if (!hmap_access_strlen(h, strlist[i].str, strlist[i].len, NULL)) {
				return 1;
			}
		}
		// for (int i = 0; i < words_len; i++) {
		// 	hmap_delete_strlen(h, strlist[i].str, strlist[i].len);
		// }
	}

	hmap_destroy(h);
	return 0;
}

static int benchmark_hmap_assign_access_delete_fn(long N, void *data) {
	string_t *strlist = (string_t *)data;

	hmap *h = hmap_new(NULL);

	for (long j = 0; j < N; j++) {
		for (int i = 0; i < words_len; i++) {
			hmap_assign_strlen(h, strlist[i].str, strlist[i].len, NULL);
		}
		for (int i = 0; i < words_len; i++) {
			if (!hmap_access_strlen(h, strlist[i].str, strlist[i].len, NULL)) {
				return 1;
			}
		}
		for (int i = 0; i < words_len; i++) {
			hmap_delete_strlen(h, strlist[i].str, strlist[i].len);
		}
	}

	hmap_destroy(h);
	return 0;
}

string_t *strlist = NULL;

void init_bench_strlist(void) {
	if (strlist != NULL) {
		return;
	}
	strlist = calloc(words_len, sizeof(string_t));
	for (int i = 0; i < words_len; i++) {
		string_assign(&strlist[i], words[i], strlen(words[i]));
	}
}

int benchmark_hmap_access(ns_duration_t bench_dur) {
	init_bench_strlist();
	ns_benchmark_t result = ns_benchmark(bench_dur, benchmark_hmap_access_fn, strlist);
	if (result.failed) {
		fprintf(stderr, "benchmark: failed\n");
		return 1;
	}
	ns_fprintf_benchmark(stdout, "hmap_access", result);
	return 0;
}

int benchmark_hmap_assign_access_delete(ns_duration_t bench_dur) {
	init_bench_strlist();
	ns_benchmark_t result = ns_benchmark(bench_dur, benchmark_hmap_assign_access_delete_fn, strlist);
	if (result.failed) {
		fprintf(stderr, "benchmark: failed\n");
		return 1;
	}
	ns_fprintf_benchmark(stdout, "hmap_assign_access_delete", result);
	return 0;
}

// #define bool_str(b) (b ? "true" : false)

int main(int argc, char const *argv[]) {
	(void)argc;
	(void)argv;

	// WARN WARN WARN
	srandomdev();

	// const ns_duration_t bench_dur = NS_SECOND * 5;
	const ns_duration_t bench_dur = NS_SECOND;

	// Benchmarks
	if (argc == 2 && strcmp(argv[1], "bench") == 0) {
		// printf("Benchmarks:\n");
		int exit_code = 0;
		if (benchmark_hmap_access(bench_dur) != 0) {
			fprintf(stderr, "FAIL: benchmark_hmap_access\n");
			exit_code = 1;
		}
		if (benchmark_hmap_assign_access_delete(bench_dur) != 0) {
			fprintf(stderr, "FAIL: benchmark_hmap_assign_access_delete\n");
			exit_code = 1;
		}
		free(strlist);
		return exit_code;
	}

	// Tests
	bool verbose = true;
	test_runner t = { .verbose = verbose };
	test_hmap(&t);
	if (test_exit_code(&t) != 0) {
		return test_exit_code(&t);
	}
	test_hmap_web2(&t);
	if (test_exit_code(&t) != 0) {
		return test_exit_code(&t);
	}

	// // int **values = xmalloc(sizeof(int) * words_len);
	// // for (int i = 0; i < words_len; i++) {
	// // 	int *p = malloc(sizeof(int));
	// // 	*p = i;
	// // 	values[i] = p;
	// // }

	// string_t *strlist = calloc(words_len, sizeof(string_t));
	// for (int i = 0; i < words_len; i++) {
	// 	string_assign(&strlist[i], words[i], strlen(words[i]));
	// }

	// ns_benchmark_t result = ns_benchmark((ns_duration_t){SECOND}, benchmark_hmap_fn, strlist);
	// if (result.failed) {
	// 	fprintf(stderr, "benchmark: failed\n");
	// 	return 1;
	// }

	// printf("%d: %.2fms: %zu ns/op\n", result.n, ns_duration_ms(result.d),
	// 	(size_t)(ns_duration_ns(result.d) / (int64_t)result.n));
	// // printf("x: %zu\n", (size_t)x);
	// return 0;

	// free(strlist);
	// return 0;
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
