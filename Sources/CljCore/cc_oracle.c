// @ai-generated(solo)
// The frame oracle (design §7, «Фаза 3», «Оракул»): a borrowed word on the stack adds no count, so it raises no alarm.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "cc_internal.h"
#include "coro_internal.h"

#if CLJ_DEBUG

enum { ORACLE_SLOTS = 1 << 14, ORACLE_MAX = ORACLE_SLOTS / 2, FRAMES_MAX = 4096, WORDS_MAX = 4 };

typedef struct {
	const clj_header *h;
	uint32_t          named; // references the frames' enumeration named
	uint32_t          held;  // edges of the coroutine's unshared objects and its own fields
	uintptr_t         at[WORDS_MAX]; // stack words naming it: the innermost ones, then the outermost; 0 past them
} entry;

struct clj_cc_oracle {
	const clj_coro    *c;
	uint32_t           owner;
	entry             *table;
	uint32_t           n;
	bool               full;
	const clj_header **work;
	uint32_t           nwork;
};

static int enabled = -1;

static bool ours(const clj_cc_oracle *o, const clj_header *h) {
	uint32_t f = h->flags;
	return !(f & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) && (f >> CLJ_OWNER_SHIFT) == o->owner;
}

static entry *find(clj_cc_oracle *o, const clj_header *h) {
	uint32_t i = (uint32_t)((((uintptr_t)h >> 3) * 0x9E3779B97F4A7C15u) >> 50) & (ORACLE_SLOTS - 1);
	while (o->table[i].h) {
		if (o->table[i].h == h) return &o->table[i];
		i = (i + 1) & (ORACLE_SLOTS - 1);
	}
	if (o->n >= ORACLE_MAX) {
		o->full = true;
		return NULL;
	}
	o->n++;
	o->table[i].h = h;
	o->work[o->nwork++] = h;
	return &o->table[i];
}

clj_cc_oracle *clj_cc_oracle_begin(const clj_coro *c) {
	if (enabled < 0) {
		const char *e = getenv("CLJ_CC_ORACLE");
		enabled = !(e && e[0] == '0');
	}
	if (!enabled || !c->debug_owner) return NULL;
	clj_cc_oracle *o = calloc(1, sizeof *o);
	entry         *table = calloc(ORACLE_SLOTS, sizeof *table);
	const clj_header **work = malloc(ORACLE_MAX * sizeof *work);
	if (!o || !table || !work) clj_fatal("out of memory");
	o->c = c;
	o->owner = c->debug_owner;
	o->table = table;
	o->work = work;
	return o;
}

void clj_cc_oracle_named(clj_cc_oracle *o, clj_value v) {
	if (!o || !clj_is_ptr(v) || !ours(o, clj_header_of(v))) return;
	entry *e = find(o, clj_header_of(v));
	if (e) e->named++;
}

static void held(clj_value v, void *ctx) {
	clj_cc_oracle *o = ctx;
	if (!clj_is_ptr(v) || !ours(o, clj_header_of(v))) return;
	entry *e = find(o, clj_header_of(v));
	if (e) e->held++;
}

// A frame's return address sits in its callee's frame record; the innermost one's, in the switch's save area.
static uintptr_t function_at(const uintptr_t *words, size_t n, uintptr_t sp, uintptr_t fp, uintptr_t pc, uintptr_t addr) {
	uintptr_t hi = sp + n * sizeof *words;
	for (int k = 0; k < FRAMES_MAX && fp >= sp && fp + 2 * sizeof *words <= hi && !(fp & 7); k++) {
		if (addr < fp + 2 * sizeof *words) return pc;
		uintptr_t next = words[(fp - sp) / sizeof *words], ra = words[(fp - sp) / sizeof *words + 1];
		if (next <= fp) break;
		pc = ra;
		fp = next;
	}
	return pc;
}

static void describe(char *out, size_t cap, uintptr_t pc) {
	Dl_info info;
	if (pc && dladdr((const void *)pc, &info) && info.dli_sname)
		snprintf(out, cap, "%s+%#lx (%p)", info.dli_sname, (unsigned long)(pc - (uintptr_t)info.dli_saddr), (void *)pc);
	else if (pc && info.dli_fname)
		snprintf(out, cap, "%s+%#lx", info.dli_fname, (unsigned long)(pc - (uintptr_t)info.dli_fbase));
	else
		snprintf(out, cap, "%p", (void *)pc);
}

// The stack holds redzones and words of returned frames: read as plain words, never as the sanitizer's objects.
__attribute__((no_sanitize("address", "thread"))) static void scan(clj_cc_oracle *o, const uintptr_t *words, size_t n, uintptr_t sp) {
#if defined(__has_feature) && __has_feature(thread_sanitizer)
	// A stale word names any execution's object, read while its owner writes it: TSan seeds from the named alone.
	(void)o, (void)words, (void)n, (void)sp;
	return;
#endif
	uintptr_t hi = sp + n * sizeof *words;
	for (size_t i = 0; i < n && !o->full; i++) {
		uintptr_t w = words[i];
		if (w >= sp && w < hi) continue;
		const clj_header *h = clj_debug_object_at(w);
		if (!h || !ours(o, h) || !(atomic_load_explicit(&h->rc, memory_order_relaxed) & CLJ_RC_COUNT_MASK)) continue;
		entry *e = find(o, h);
		if (!e) continue;
		int k = 0;
		while (k < WORDS_MAX - 1 && e->at[k]) k++;
		e->at[k] = sp + i * sizeof *words; // the last keeps the outermost: the owner's frame is usually above its lenders'
	}
}

void clj_cc_oracle_end(clj_cc_oracle *o) {
	if (!o) return;
	const clj_coro *c = o->c;
	clj_coro_type.each_child((void *)c, held, o);
	held(c->pending, o);
	held(c->pending_trace, o);
	for (size_t i = 0; i < c->nretired; i++) held(c->retired[i], o);
	for (uint32_t i = 0; i < c->nspawn; i++) held(c->spawn_trace[i].name, o);
	const uintptr_t *words;
	size_t           n;
	uintptr_t        sp, fp, pc;
	bool             stack = clj_coro_parked_stack(c, &words, &n, &sp, &fp, &pc);
	if (stack) scan(o, words, n, sp);
	while (o->nwork && !o->full) {
		const clj_header *h = o->work[--o->nwork];
		if (h->type->each_child) h->type->each_child((void *)h, held, o);
	}
	for (uint32_t i = 0; i < ORACLE_SLOTS && !o->full; i++) {
		entry            *e = &o->table[i];
		const clj_header *h = e->h;
		// Only what may lie on a cycle: a reference to anything else keeps no garbage alive.
		if (!h || !(h->flags & (CLJ_FLAG_REACH | CLJ_FLAG_REACH_LOCAL))) continue;
		uint32_t rc = atomic_load_explicit(&h->rc, memory_order_relaxed);
		uint32_t count = rc & CLJ_RC_COUNT_MASK, known = e->named + e->held + ((rc & CLJ_RC_BUFFERED) ? 1 : 0);
		if (count == known) continue;
		char   where[1024] = "no stack word names it";
		size_t len = 0;
		for (int k = 0; k < WORDS_MAX && e->at[k] && stack; k++) {
			char fn[200];
			describe(fn, sizeof fn, function_at(words, n, sp, fp, pc, e->at[k]));
			len += (size_t)snprintf(where + len, sizeof where - len, "%sa word at %p in %s", k ? "; " : "", (void *)e->at[k], fn);
			if (len >= sizeof where) break;
		}
		if (count > known)
			clj_ice("cc/frame-missed", NULL, "a parked coroutine owns %u references to a %s, its frames name %u and its objects hold %u: %s",
			        count, h->type->name, e->named, e->held, where);
		clj_ice("cc/frame-overnamed", NULL, "a parked coroutine's frames name %u references to a %s and its objects hold %u, of %u: %s",
		        e->named, h->type->name, e->held, count, where);
	}
	free(o->work);
	free(o->table);
	free(o);
}

#endif
