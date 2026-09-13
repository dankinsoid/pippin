// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "alloc.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

enum {
	SLAB_SIZE   = 64 * 1024,
	SLAB_HEADER = 64,
	MAX_SMALL   = 1024,
	// 32, 40, 48, 56, 64, then four classes per doubling up to 1024.
	NCLASSES = 21,
};

typedef struct heap heap;

// Found from any cell by masking its address; the slab is aligned to its size.
typedef struct slab {
	heap           *owner;
	struct slab    *next;    // owner's slab list for this class
	void           *free;    // owner-only intrusive free list
	_Atomic(void *) foreign; // frees from other threads; the owner drains it
	uint32_t        cls;
	uint32_t        cell;
	uint32_t        bump;    // offset of the next never-used cell; its memory is still zero
	uint32_t        used;    // handed out and not on the local free list
} slab;

// One per thread, never freed: an exited thread's slabs are abandoned, cells freed but never reused (v1).
// Empty slabs are never returned to the OS (v1).
struct heap {
	slab *current[NCLASSES];
	slab *slabs[NCLASSES];
};

static _Thread_local heap *tls_heap;

#if CLJ_DEBUG
// One process-wide counter, contended across threads; debug-only, so acceptable until profiles say otherwise.
static _Atomic int64_t live_objects;
int64_t clj_debug_live_objects(void) { return atomic_load(&live_objects); }
#define LIVE_ADD(n) atomic_fetch_add_explicit(&live_objects, (n), memory_order_relaxed)
#else
int64_t clj_debug_live_objects(void) { return -1; }
#define LIVE_ADD(n) ((void)0)
#endif

// CLJ_SYSTEM_ALLOC=1 routes every object through calloc/realloc/free so ASan sees object boundaries.
static bool use_system_alloc(void) {
	static _Atomic int mode; // 0 unknown, 1 pool, 2 system; a racing first read computes the same value
	int m = atomic_load_explicit(&mode, memory_order_relaxed);
	if (__builtin_expect(m == 0, 0)) {
		const char *e = getenv("CLJ_SYSTEM_ALLOC");
		m = (e && *e && *e != '0') ? 2 : 1;
		atomic_store_explicit(&mode, m, memory_order_relaxed);
	}
	return m == 2;
}

static uint32_t size_class(size_t size) {
	if (size <= 64) return size <= 32 ? 0 : (uint32_t)((size - 1) / 8) - 3;
	uint32_t log2 = 32 - (uint32_t)__builtin_clz((uint32_t)size - 1);
	uint32_t step = 1u << (log2 - 3);
	uint32_t q = (uint32_t)((size + step - 1) / step);
	return 5 + 4 * (log2 - 7) + (q - 5);
}

static uint32_t class_cell(uint32_t cls) {
	if (cls <= 4) return 32 + 8 * cls;
	uint32_t g = (cls - 5) / 4;
	return (16u << g) * (5 + (cls - 5) % 4);
}

static inline slab *slab_of(void *p) {
	return (slab *)((uintptr_t)p & ~(uintptr_t)(SLAB_SIZE - 1));
}

// A dead cell's first word is the free-list link; memcpy keeps clear of aliasing rules.
static inline void *cell_next(void *p) {
	void *n;
	memcpy(&n, p, sizeof n);
	return n;
}

static inline void cell_set_next(void *p, void *n) { memcpy(p, &n, sizeof n); }

static heap *my_heap(void) {
	if (!tls_heap) {
		tls_heap = calloc(1, sizeof *tls_heap);
		if (!tls_heap) clj_fatal("out of memory");
	}
	return tls_heap;
}

// mmap gives zeroed pages but only page alignment; map twice the size and trim to an aligned slab.
static slab *slab_new(heap *h, uint32_t cls) {
	size_t span = 2 * SLAB_SIZE;
	char *p = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) clj_fatal("out of memory");
	char *base = (char *)(((uintptr_t)p + SLAB_SIZE - 1) & ~(uintptr_t)(SLAB_SIZE - 1));
	if (base > p) munmap(p, (size_t)(base - p));
	if (base + SLAB_SIZE < p + span) munmap(base + SLAB_SIZE, (size_t)((p + span) - (base + SLAB_SIZE)));
	slab *s = (slab *)base;
	s->owner = h;
	s->cls = cls;
	s->cell = class_cell(cls);
	s->bump = SLAB_HEADER;
	atomic_init(&s->foreign, NULL);
	return s;
}

// Local free list, then foreign frees, then untouched memory. `zero` bytes are cleared on reuse;
// untouched cells are already zero.
static void *slab_take(slab *s, size_t zero) {
	void *p = s->free;
	if (!p && atomic_load_explicit(&s->foreign, memory_order_relaxed)) {
		p = atomic_exchange_explicit(&s->foreign, NULL, memory_order_acquire);
		for (void *q = p; q; q = cell_next(q)) s->used--;
	}
	if (p) {
		s->free = cell_next(p);
		s->used++;
		memset(p, 0, zero);
		return p;
	}
	if (s->bump + s->cell <= SLAB_SIZE) {
		p = (char *)s + s->bump;
		s->bump += s->cell;
		s->used++;
		return p;
	}
	return NULL;
}

static void *pool_alloc(uint32_t cls, size_t zero) {
	heap *h = my_heap();
	slab *s = h->current[cls];
	void *p = s ? slab_take(s, zero) : NULL;
	if (p) return p;
	for (slab *t = h->slabs[cls]; t; t = t->next) {
		if (t == s) continue;
		p = slab_take(t, zero);
		if (p) {
			h->current[cls] = t;
			return p;
		}
	}
	s = slab_new(h, cls);
	s->next = h->slabs[cls];
	h->slabs[cls] = s;
	h->current[cls] = s;
	return slab_take(s, zero);
}

static void pool_free(void *p) {
	slab *s = slab_of(p);
	if (s->owner == tls_heap) {
		cell_set_next(p, s->free);
		s->free = p;
		s->used--;
		return;
	}
	void *old = atomic_load_explicit(&s->foreign, memory_order_relaxed);
	do {
		cell_set_next(p, old);
	} while (!atomic_compare_exchange_weak_explicit(&s->foreign, &old, p, memory_order_release, memory_order_relaxed));
}

void *clj_alloc(const clj_type *type, size_t size) {
	CLJ_ASSERT(size >= sizeof(clj_header), "object smaller than its header");
	clj_header *h;
	if (size > MAX_SMALL || use_system_alloc()) {
		h = calloc(1, size);
		if (!h) clj_fatal("out of memory");
		h->flags = CLJ_FLAG_LARGE;
	} else {
		h = pool_alloc(size_class(size), size);
	}
	atomic_init(&h->rc, 1);
	h->type = type;
	LIVE_ADD(1);
	return h;
}

void *clj_realloc(void *obj, size_t size) {
	clj_header *h = obj;
	CLJ_ASSERT(!(h->flags & CLJ_FLAG_IMMORTAL) && atomic_load_explicit(&h->rc, memory_order_relaxed) == 1,
	           "realloc of a non-unique object");
	CLJ_ASSERT(size >= sizeof(clj_header), "object smaller than its header");
	if (h->flags & CLJ_FLAG_LARGE) {
		if (size > MAX_SMALL || use_system_alloc()) {
			h = realloc(h, size);
			if (!h) clj_fatal("out of memory");
			return h;
		}
		clj_header *n = pool_alloc(size_class(size), 0);
		memcpy(n, h, size);
		n->flags &= ~CLJ_FLAG_LARGE;
		free(h);
		return n;
	}
	slab *s = slab_of(h);
	if (size <= MAX_SMALL && size_class(size) == s->cls) return h;
	clj_header *n;
	if (size > MAX_SMALL) {
		n = malloc(size);
		if (!n) clj_fatal("out of memory");
		memcpy(n, h, s->cell);
		n->flags |= CLJ_FLAG_LARGE;
	} else {
		n = pool_alloc(size_class(size), 0);
		memcpy(n, h, s->cell < size ? s->cell : size);
	}
	pool_free(h);
	return n;
}

void clj_dealloc(clj_header *h) {
	LIVE_ADD(-1);
	if (h->flags & CLJ_FLAG_LARGE) free(h);
	else pool_free(h);
}

bool clj_debug_pool_enabled(void) { return !use_system_alloc(); }

size_t clj_debug_cell_size(size_t size) {
	if (size > MAX_SMALL || use_system_alloc()) return 0;
	return class_cell(size_class(size));
}
