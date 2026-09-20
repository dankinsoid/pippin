// @ai-generated(solo)
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#endif

#include "clj/compiled.h"
#include "clj/lock.h"
#include "shadow_internal.h"
#include "trace_internal.h"

// ---- the frame table: every compiled fn's code start, all units merged, sorted, replaced as a whole

// A fn's code runs to the next entry's start: only frame functions live in the __cljframe section (compiled_internal.h).
typedef struct {
	uintptr_t       start;
	const clj_node *stub;
} frame_range;

typedef struct {
	size_t      n;
	frame_range ranges[];
} frame_table;

// A call site marker (CLJC_SITE): the code address right after a call and the stub of the fn whose body made it.
typedef struct {
	uintptr_t       text;
	const clj_node *stub;
} site_entry;

// One per image with compiled units: its text and frame section bounds and its call site markers, sorted by address.
typedef struct image {
	uintptr_t     text_lo, text_hi;
	uintptr_t     frames_lo, frames_hi;
	site_entry   *sites;
	size_t        nsites;
	struct image *next;
} image;

static clj_lock                lock = CLJ_LOCK_INIT;
static _Atomic(frame_table *)  frames;
static _Atomic(image *)        images;

static int by_start(const void *a, const void *b) {
	const frame_range *x = a, *y = b;
	return x->start < y->start ? -1 : x->start > y->start ? 1 : 0;
}

static int by_text(const void *a, const void *b) {
	const site_entry *x = a, *y = b;
	return x->text < y->text ? -1 : x->text > y->text ? 1 : 0;
}

#ifdef __APPLE__
// The layout CLJC_SITE emits: two self-relative 32-bit offsets, so the section needs no relocation at load.
typedef struct {
	int32_t text, stub;
} site_raw;

static image *image_of(uintptr_t addr) {
	for (image *im = atomic_load_explicit(&images, memory_order_acquire); im; im = im->next) {
		if (addr >= im->text_lo && addr < im->text_hi) return im;
	}
	return NULL;
}

// Registers the image holding addr once: its text segment and its site section. Under the lock.
static image *register_image(const void *addr) {
	Dl_info info;
	if (!dladdr(addr, &info) || !info.dli_fbase) return NULL;
	const struct mach_header_64 *header = info.dli_fbase;
	unsigned long                size = 0;
	uint8_t                     *text = getsegmentdata(header, "__TEXT", &size);
	if (!text) return NULL;
	if (image_of((uintptr_t)addr)) return image_of((uintptr_t)addr);
	image *im = calloc(1, sizeof *im);
	if (!im) clj_fatal("out of memory");
	im->text_lo = (uintptr_t)text;
	im->text_hi = (uintptr_t)text + size;
	unsigned long fsize = 0;
	uint8_t      *fr = getsectiondata(header, "__TEXT", "__cljframe", &fsize);
	im->frames_lo = (uintptr_t)fr;
	im->frames_hi = (uintptr_t)fr + fsize;
	unsigned long   ssize = 0;
	const site_raw *raw = (const site_raw *)getsectiondata(header, "__TEXT", "__cljsite", &ssize);
	if (raw && ssize) {
		size_t n = ssize / sizeof *raw;
		im->sites = malloc(n * sizeof *im->sites);
		if (!im->sites) clj_fatal("out of memory");
		for (size_t i = 0; i < n; i++) {
			im->sites[i].text = (uintptr_t)&raw[i].text + (uintptr_t)(intptr_t)raw[i].text;
			im->sites[i].stub = (const clj_node *)((uintptr_t)&raw[i].stub + (uintptr_t)(intptr_t)raw[i].stub);
		}
		// Sorting is only for safety: the assembler writes them in program order, which is address order.
		qsort(im->sites, n, sizeof *im->sites, by_text);
		im->nsites = n;
	}
	im->next = atomic_load_explicit(&images, memory_order_relaxed);
	atomic_store_explicit(&images, im, memory_order_release);
	return im;
}
#else
static image *image_of(uintptr_t addr) {
	(void)addr;
	return NULL;
}

static image *register_image(const void *addr) {
	(void)addr;
	return NULL;
}
#endif

void clj_trace_register_image(const void *addr) {
	clj_lock_lock(&lock);
	register_image(addr);
	clj_lock_unlock(&lock);
}

void clj_c_register_frames(const clj_frame_entry *entries, size_t n) {
	if (n == 0) return;
	frame_range *fresh = malloc(n * sizeof *fresh);
	if (!fresh) clj_fatal("out of memory");
	for (size_t i = 0; i < n; i++) fresh[i] = (frame_range){(uintptr_t)entries[i].fn, entries[i].stub};
	qsort(fresh, n, sizeof *fresh, by_start);
	clj_lock_lock(&lock);
	const image *im = register_image((const void *)entries[0].fn);
	for (size_t i = 0; im && i < n; i++) {
		if (fresh[i].start < im->frames_lo || fresh[i].start >= im->frames_hi) clj_fatal("compiled unit: a frame function is outside its section");
	}
	frame_table *old = atomic_load_explicit(&frames, memory_order_relaxed);
	size_t       total = (old ? old->n : 0) + n;
	frame_table *merged = malloc(sizeof *merged + total * sizeof *merged->ranges);
	if (!merged) clj_fatal("out of memory");
	merged->n = total;
	size_t i = 0, j = 0, k = 0;
	while (i < (old ? old->n : 0) || j < n) {
		if (j == n || (old && i < old->n && old->ranges[i].start < fresh[j].start)) merged->ranges[k++] = old->ranges[i++];
		else merged->ranges[k++] = fresh[j++];
	}
	// A throw on another thread may still be reading the old table: it stays allocated, one per unit ever loaded.
	atomic_store_explicit(&frames, merged, memory_order_release);
	clj_lock_unlock(&lock);
	free(fresh);
}

// The frame whose code holds pc, with its end: the next entry's start, or the end of the image's frame section.
static const frame_range *frame_of(uintptr_t pc, uintptr_t *end) {
	const frame_table *t = atomic_load_explicit(&frames, memory_order_acquire);
	if (!t) return NULL;
	const image *im = image_of(pc);
	if (!im || pc < im->frames_lo || pc >= im->frames_hi) return NULL;
	size_t lo = 0, hi = t->n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (t->ranges[mid].start <= pc) lo = mid + 1;
		else hi = mid;
	}
	if (lo == 0) return NULL;
	const frame_range *r = &t->ranges[lo - 1];
	*end = lo < t->n && t->ranges[lo].start < im->frames_hi ? t->ranges[lo].start : im->frames_hi;
	return r;
}

// The first marker at or past a return address, inside the frame that holds it: the call the address returns to.
static const clj_node *site_stub(uintptr_t ra, uintptr_t end) {
	const image *im = image_of(ra);
	if (!im || !im->nsites) return NULL;
	size_t lo = 0, hi = im->nsites;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (im->sites[mid].text < ra) lo = mid + 1;
		else hi = mid;
	}
	if (lo == im->nsites || im->sites[lo].text >= end) return NULL;
	return im->sites[lo].stub;
}

bool clj_trace_code_known(uintptr_t pc) { return image_of(pc) != NULL; }

// ---- the real stack

#if defined(__has_feature)
#if __has_feature(ptrauth_returns)
#include <ptrauth.h>
#define STRIP(ra) ((uintptr_t)ptrauth_strip((void *)(ra), ptrauth_key_return_address))
#endif
#endif
#ifndef STRIP
#define STRIP(ra) (ra)
#endif

static inline uintptr_t strip(uintptr_t ra) { return STRIP(ra); }

static bool frame_in(uintptr_t fp, uintptr_t lo, uintptr_t hi) { return fp >= lo && fp + 2 * sizeof(uintptr_t) <= hi && (fp & 7) == 0; }

typedef struct {
	const clj_node *fn;
	uintptr_t       key; // the frame's stack address: lower is deeper
} compiled_frame;

// Appends the frames a return address stands for: the inlined callee a marker names, then the frame's own fn.
static size_t frames_at(uintptr_t ra, uintptr_t key, compiled_frame *out, size_t n, size_t cap) {
	uintptr_t          end;
	const frame_range *r = frame_of(ra, &end);
	if (!r) return n;
	const clj_node *inlined = site_stub(ra, end);
	if (inlined && inlined != r->stub && n < cap) out[n++] = (compiled_frame){inlined, key};
	if (n < cap) out[n++] = (compiled_frame){r->stub, key};
	return n;
}

// Frame pointers from an origin up to the stack's top: {fp, return address} records, the caller's key its fp.
static size_t walk(const clj_trace_origin *o, uintptr_t lo, uintptr_t hi, intptr_t bias, compiled_frame *out, size_t cap) {
	// Records are read `bias` bytes from their addresses: an evacuated stack is walked in its blob (coro.c).
	size_t    n = 0;
	uintptr_t fp = o->fp;
	if (o->pc) {
		n = frames_at(o->pc, o->sp, out, n, cap);
		// A fault in a prologue or a leaf has the caller's fp and its return address still in lr.
		uintptr_t          end_pc, end_lr;
		const frame_range *at_pc = frame_of(o->pc, &end_pc), *at_lr = frame_of(strip(o->lr), &end_lr);
		uintptr_t          first_ra = frame_in(fp, lo, hi) ? strip(((uintptr_t *)(fp + bias))[1]) : 0;
		bool               prologue = at_pc && o->pc - at_pc->start < 16;
		if (o->lr && strip(o->lr) != first_ra && (prologue || !at_pc || at_lr != at_pc)) n = frames_at(strip(o->lr), fp, out, n, cap);
	}
	while (n < cap && frame_in(fp, lo, hi)) {
		uintptr_t next = ((uintptr_t *)(fp + bias))[0], ra = strip(((uintptr_t *)(fp + bias))[1]);
		if (!ra) break;
		n = frames_at(ra, next, out, n, cap);
		if (next <= fp) break;
		fp = next;
	}
	return n;
}

static const clj_node *position_of(const clj_shadow_frame *f) {
	return f->call_site && f->call_site->line ? f->call_site : f->fn_node;
}

// The compiled frames of a walk merged with a ring's frames by stack address, innermost first.
static size_t merge(const clj_shadow_stack *s, const clj_shadow_frame *frames, const compiled_frame *compiled, size_t nc, clj_trace_frame *out, size_t cap) {
	size_t held = s->depth < s->mask + 1 ? s->depth : s->mask + 1;
	size_t n = 0, i = 0, j = 0;
	while (n < cap && (i < nc || j < held)) {
		const clj_shadow_frame *sf = j < held ? &frames[(s->depth - 1 - j) & s->mask] : NULL;
		if (i < nc && (!sf || compiled[i].key < (uintptr_t)sf->sp)) {
			out[n++] = (clj_trace_frame){compiled[i].fn, compiled[i].fn};
			i++;
		} else {
			out[n++] = (clj_trace_frame){sf->fn_node, position_of(sf)};
			j++;
		}
	}
	return n;
}

size_t clj_trace_collect(clj_trace_frame *out, size_t cap, const clj_trace_origin *origin) {
	const clj_shadow_stack *s = clj_shadow_stack_current();
	clj_trace_origin        here;
	if (!origin) {
		// A throw, not a signal: a thread that has run only compiled code gets its stack now.
		if (!s) s = clj_shadow_stack_init();
		here = (clj_trace_origin){0, 0, (uintptr_t)__builtin_frame_address(0), 0};
		origin = &here;
	}
	if (!s) return 0;
	enum { COMPILED_MAX = 512 };
	compiled_frame compiled[COMPILED_MAX];
	size_t         nc = walk(origin, (uintptr_t)s->stack_lo, (uintptr_t)s->stack_hi, 0, compiled, cap < COMPILED_MAX ? cap : COMPILED_MAX);
	return merge(s, s->frames, compiled, nc, out, cap);
}

size_t clj_trace_collect_parked(const void *ring, const void *frames, const clj_trace_origin *origin, intptr_t bias, clj_trace_frame *out, size_t cap) {
	const clj_shadow_stack *s = ring;
	enum { COMPILED_MAX = 512 };
	compiled_frame compiled[COMPILED_MAX];
	size_t         nc = walk(origin, origin->sp, (uintptr_t)s->stack_hi, bias, compiled, cap < COMPILED_MAX ? cap : COMPILED_MAX);
	return merge(s, frames, compiled, nc, out, cap);
}

bool clj_trace_compiled_on_stack(void) {
	const clj_shadow_stack *s = clj_shadow_tls;
	if (!s || !atomic_load_explicit(&frames, memory_order_acquire)) return false;
	clj_trace_origin here = {0, 0, (uintptr_t)__builtin_frame_address(0), 0};
	compiled_frame   one;
	return walk(&here, (uintptr_t)s->stack_lo, (uintptr_t)s->stack_hi, 0, &one, 1) > 0;
}
