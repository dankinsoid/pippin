// @ai-generated(guided)
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/core.h"
#include "clj/epoch.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/intrinsics.h"
#include "clj/list.h"
#include "clj/long.h"
#include "clj/map.h"
#include "clj/printer.h"
#include "clj/proto.h"
#include "clj/set.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "epoch_internal.h"
#include "guard_internal.h"
#include "load_internal.h"
#include "profile_internal.h"
#include "proto_internal.h"
#include "shadow_internal.h"
#include "shape_internal.h"
#include "coro_internal.h"
#include "specialize_internal.h"
#include "trace_internal.h"

enum {
	SMALL_SLOTS = 16, // frame slots kept on the C stack
	SMALL_ARGS  = 8,  // argument buffers kept on the C stack
	// Covers the C frames between two guard checks: a few eval steps plus one native (printer and reader are iterative).
	STACK_MARGIN = 64 * 1024,
};

static clj_value *buf_alloc(clj_value *small, size_t n) {
	if (n <= SMALL_ARGS) return small;
	clj_value *p = malloc(n * sizeof *p);
	if (!p) clj_fatal("out of memory");
	return p;
}

static void buf_free(clj_value *small, clj_value *p) {
	if (p != small) free(p);
}

static inline clj_value eval_child(const clj_node *n, clj_frame *f) { return f->exec->nodes[n->id].eval(n, f); }

// ---- cooperative deadline
// Checked at a closure call and at a loop turn, one clock read per DEADLINE_CHECK_EVERY of them.
enum {
	DEADLINE_CHECK_EVERY = 1024,
	// Calls a handler gets before the expired deadline throws again: enough for clojure.test's reporting.
	DEADLINE_UNWIND_CALLS = 1 << 16,
	// Unwind budgets one deadline hands out. Past them every check throws, so a handler that catches the
	// timeout inside a loop of its own runs at most this many turns instead of for ever.
	DEADLINE_MAX_UNWINDS = 64,
};

static inline bool deadline_reached(clj_shadow_stack *s) {
	if (--s->countdown) return false;
	s->countdown = DEADLINE_CHECK_EVERY;
	return clj_profile_now() >= clj_shadow_deadline(s);
}

// The branch every call pays when no deadline is set.
static inline bool deadline_hit(clj_shadow_stack *s) {
	return __builtin_expect(clj_shadow_deadline(s) != 0, 0) && deadline_reached(s);
}

// The deadline stays set, so a handler that catches :cancelled and keeps going is stopped again.
// A cancelled coroutine takes the same path (coro.h).
static clj_value deadline_throw(clj_shadow_stack *s) {
	if (s->unwinds) {
		s->unwinds--;
		s->countdown = DEADLINE_UNWIND_CALLS;
	} else {
		s->countdown = 1;
	}
	if (atomic_load_explicit(&s->cancelled, memory_order_relaxed)) return clj_throw_cancelled(clj_coro_cancel_is_deadline(clj_coro_current()));
	return clj_throw_cancelled(true);
}

// The expiry is a cancellation by the timer thread (sched.c), so a coroutine parked past its deadline is woken too;
// the tick's own clock read catches a running one first.
static void deadline_apply(uint64_t deadline) {
	clj_coro         *c = clj_coro_current();
	clj_shadow_stack *s = c->shadow;
	atomic_store_explicit(&s->deadline, deadline, memory_order_relaxed);
	s->countdown = DEADLINE_CHECK_EVERY;
	s->unwinds = DEADLINE_MAX_UNWINDS;
	if (deadline) clj_coro_deadline_arm(c);
	else clj_coro_deadline_cleared(c);
}

void clj_deadline_set_ms(uint64_t ms) { deadline_apply(ms ? clj_profile_now() + ms * 1000000u : 0); }

uint64_t clj_deadline_get(void) {
	clj_shadow_stack *s = clj_shadow_tls;
	return s ? clj_shadow_deadline(s) : 0;
}

void clj_deadline_restore(uint64_t deadline) { deadline_apply(deadline); }

bool clj_deadline_expired(void) {
	clj_shadow_stack *s = clj_shadow_tls;
	return s && clj_shadow_deadline(s) && clj_profile_now() >= clj_shadow_deadline(s);
}

// ---- call-site caches

typedef struct {
	const clj_type     *receiver;
	clj_value           impl;  // borrowed from the type's table: alive while the epoch holds (proto.h)
	const clj_fn_arity *arity; // of impl at the site's argument count when impl is a closure, else NULL
} proto_ic_entry;

// The protocol cache of a site: the method fn (by serial) it saw last and the impl per receiver type found
// for it at `epoch`. Nothing is retained: a retained impl would pin what the last call reached (a user type
// through `map`'s site in core.clj) and cycle through a recursive method (impl -> exec -> site -> impl).
// The impls are borrowed from the tables, which only an epoch bump retires (extend, a dying type); a hit
// retains one inside a dispatch window after seeing the epoch unchanged, so a concurrent extend cannot free
// it. Several threads run one exec: fills are serialized by the seqlock and a reader that sees a fill in
// progress or a changed sequence takes the generic path.
typedef struct {
	_Atomic uint64_t seq; // odd while a fill runs
	uint64_t         method_serial, epoch;
	uint32_t         n, next; // entries filled; the one an eviction replaces
	proto_ic_entry   e[CLJ_PROTO_IC_ENTRIES];
} proto_ic;

// A closure needs no cache: its fn node carries the arity table, one load away. The site keeps the protocol
// cache (made on the first protocol dispatch) and, in debug builds, counts the calls its fast paths took
// (hits) against those that went through the generic invoke (misses).
struct clj_call_site {
	_Atomic(proto_ic *) proto;
#if CLJ_DEBUG
	_Atomic uint64_t hits, misses;
#endif
};

#if CLJ_DEBUG
#define IC_COUNT(site, which) atomic_fetch_add_explicit(&(site)->which, 1, memory_order_relaxed)
#else
#define IC_COUNT(site, which) ((void)0)
#endif

// The body of f's arity for n arguments, NULL when none takes n.
static inline const clj_fn_arity *arity_for(const clj_node *code, size_t n) {
	const clj_fn_arity *arity = n <= CLJ_FN_MAX_FIXED ? code->u.fn.fixed[n] : NULL;
	if (!arity && code->u.fn.variadic && n >= code->u.fn.variadic->nparams) arity = code->u.fn.variadic;
	return arity;
}

// Old fn roots a def replaced while this execution was evaluating. A fn root is read at +0 (eval_borrowed), so
// the release waits until nothing on the execution can still hold such a read: no closure frame (shadow depth)
// and no clj_exec_run. Unbounded while it stays in flight; a def on another execution is not covered (NOTES.md).
// Per coroutine, not per carrier: a parked coroutine keeps its +0 reads across the carriers it migrates over.
#define execution (*clj_coro_current())

// A compiled fn pushes no frame, so the real stack answers for it: a walk, paid only at a rebind or a drain.
static bool in_flight(void) {
	clj_coro *c = clj_coro_current();
	return c->exec_depth > 0 || c->shadow->depth > 0 || clj_trace_compiled_on_stack();
}

// @ai-generated(guided)
bool clj_eval_retire_root(clj_value old) {
	if (!clj_is_fn(old) || !in_flight()) return false;
	if (execution.nretired == execution.cretired) {
		execution.cretired = execution.cretired ? execution.cretired * 2 : 8;
		execution.retired = realloc(execution.retired, execution.cretired * sizeof *execution.retired);
		if (!execution.retired) clj_fatal("out of memory");
	}
	execution.retired[execution.nretired++] = old;
	return true;
}

// Called where a depth returned to zero; the other one may still be up.
static void drain_retired(void) {
	if (in_flight()) return;
	while (execution.nretired) clj_release(execution.retired[--execution.nretired]);
}

// A finished coroutine holds nothing any more: everything it retired goes (sched.c).
void clj_eval_drain_retired(clj_coro *c) {
	while (c->nretired) clj_release(c->retired[--c->nretired]);
	free(c->retired);
	c->retired = NULL;
	c->cretired = 0;
}

size_t clj_debug_retired_roots(void) { return execution.nretired; }

// A local, captured or constant read without a retain: its home (the frame, the closure's env, the tree)
// outlives the consumer. A local's last use (optimizer.c) hands the frame's own reference over instead: the
// slot is niled so teardown, a rebind and a debug reader see nothing there, and the consumer gets a +1 it may
// update in place; a slot the frame only borrows (a fixed param, the self slot) is borrowed on as before. The
// mark is a flag inside the LOCAL case, not a case of its own: a fifth case turns the switch into a jump table
// in every inlined copy and cost ~3 ns per iteration of the counting loop (so does dispatching the read through
// the exec table). An OUTER read stays owned for the same reason.
// A var's root is borrowed when immortal (every root bound by boot) or a fn (a rebind
// parks the old fn until the thread is idle, clj_eval_retire_root); a data root is retained, since a rebind
// releases it at once. Every other kind evaluates owned.
// @ai-generated(guided)
// The frame `depth` static links up; a direct fn's frame links to its defining one, alive for the call.
static inline const clj_frame *outer_frame(const clj_frame *f, uint32_t depth) {
	while (depth--) f = f->outer;
	return f;
}

static inline bool slot_owned(const clj_frame *f, uint32_t i) { return i >= 64 || (f->owned >> i) & 1; }

// Forced inline in optimized builds only: with the hand-over in the LOCAL case the compiler stopped inlining it
// on its own, and the call it emitted cost the counting loop 5 ns per iteration; at -O0 a forced copy per
// eval_all site grows the sanitizer frames past the 50 nested calls the tests rely on.
#ifdef __OPTIMIZE__
__attribute__((always_inline))
#endif
static inline clj_value eval_borrowed(const clj_node *n, clj_frame *f, bool *owned) {
	*owned = false;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: {
		uint32_t  i = n->u.local.index;
		clj_value v = f->slots[i];
		if (__builtin_expect(n->u.local.last, 0) && slot_owned(f, i)) {
			f->slots[i] = CLJ_NIL;
			if (i < 64) f->owned &= ~((uint64_t)1 << i);
			*owned = true;
		}
		return v;
	}
	case CLJ_NODE_CAPTURED: return f->captured[n->u.index];
	case CLJ_NODE_CONST: return n->u.value;
	case CLJ_NODE_VAR: {
		if (__builtin_expect(clj_var_of(n->u.var)->dynamic, 0)) {
			*owned = true;
			return clj_var_deref(n->u.var);
		}
		clj_value root = clj_var_root(n->u.var);
		if (!clj_is_ptr(root)) {
			if (root == CLJ_UNBOUND) {
				*owned = true;
				return clj_var_deref(n->u.var);
			}
			return root;
		}
		const clj_header *h = clj_header_of(root);
		if ((h->flags & CLJ_FLAG_IMMORTAL) || h->type == &clj_fn_type) return root;
		*owned = true;
		return clj_retain(root);
	}
	default:
		*owned = true;
		return eval_child(n, f);
	}
}

// Stores an owned value; the old one is released only when the frame owned it.
static inline void slot_set(clj_frame *f, uint32_t i, clj_value v) {
	clj_value old = f->slots[i];
	f->slots[i] = v;
	if (slot_owned(f, i)) {
		clj_release(old);
	} else {
		f->owned |= (uint64_t)1 << i;
	}
}

static inline void release_all(const clj_value *vals, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) clj_release(vals[i]);
}

// Releases the values whose bit is set; n > 64 means every one.
static inline void release_owned(const clj_value *vals, uint32_t n, uint64_t owned) {
	if (n > 64) {
		release_all(vals, n);
		return;
	}
	while (owned) {
		clj_release(vals[__builtin_ctzll(owned)]);
		owned &= owned - 1;
	}
}

static void slots_release(const clj_frame *f, uint32_t n) { release_owned(f->slots, n, f->owned); }

// Evaluates nodes into out; on failure releases what was already evaluated. With `owned` the reads that can
// be borrowed are, and the mask says which results the caller must release (n > 64: every one); NULL evaluates
// everything owned, for values that are stored.
// @ai-generated(guided)
// Always inlined: the borrow/owned branch folds away per call site.
static inline __attribute__((always_inline)) bool eval_all(const clj_node *const *nodes, uint32_t n, clj_frame *f, clj_value *out, uint64_t *owned) {
	uint64_t mask = 0;
	bool     borrow = owned && n <= 64;
	for (uint32_t i = 0; i < n; i++) {
		bool      own = true;
		clj_value v = borrow ? eval_borrowed(nodes[i], f, &own) : eval_child(nodes[i], f);
		if (v == CLJ_THROWN) {
			if (borrow) release_owned(out, i, mask);
			else release_all(out, i);
			return false;
		}
		if (borrow && own) mask |= (uint64_t)1 << i;
		out[i] = v;
	}
	if (owned) *owned = borrow ? mask : UINT64_MAX;
	return true;
}

static clj_value eval_const(const clj_node *n, clj_frame *f) {
	(void)f;
	return clj_retain(n->u.value);
}

// An owned read: the last use of an owned slot is a hand-over, as in eval_borrowed, and retains nothing.
static clj_value eval_local(const clj_node *n, clj_frame *f) {
	uint32_t  i = n->u.local.index;
	clj_value v = f->slots[i];
	if (n->u.local.last && slot_owned(f, i)) {
		f->slots[i] = CLJ_NIL;
		if (i < 64) f->owned &= ~((uint64_t)1 << i);
		return v;
	}
	return clj_retain(v);
}

static clj_value eval_captured(const clj_node *n, clj_frame *f) { return clj_retain(f->captured[n->u.index]); }

static clj_value eval_outer(const clj_node *n, clj_frame *f) { return clj_retain(outer_frame(f, n->u.outer.depth)->slots[n->u.outer.index]); }

static clj_value eval_var(const clj_node *n, clj_frame *f) {
	(void)f;
	return clj_var_deref(n->u.var);
}

static clj_value eval_if(const clj_node *n, clj_frame *f) {
	bool      owned;
	clj_value test = eval_borrowed(n->u.if_.test, f, &owned);
	if (test == CLJ_THROWN) return CLJ_THROWN;
	bool truthy = clj_truthy(test);
	if (owned) clj_release(test);
	const clj_node *branch = truthy ? n->u.if_.then : n->u.if_.else_;
	return branch ? eval_child(branch, f) : CLJ_NIL;
}

static clj_value eval_do(const clj_node *n, clj_frame *f) {
	uint32_t last = n->u.seq.n - 1;
	for (uint32_t i = 0; i < last; i++) {
		bool      owned;
		clj_value v = eval_borrowed(n->u.seq.items[i], f, &owned);
		if (v == CLJ_THROWN) return CLJ_THROWN;
		if (owned) clj_release(v);
	}
	return eval_child(n->u.seq.items[last], f);
}

static bool bind_all(const clj_node *n, clj_frame *f) {
	for (uint32_t i = 0; i < n->u.let.n; i++) {
		clj_value v = eval_child(n->u.let.inits[i], f);
		if (v == CLJ_THROWN) return false;
		slot_set(f, n->u.let.slots[i], v);
	}
	return true;
}

static clj_value eval_let(const clj_node *n, clj_frame *f) {
	if (!bind_all(n, f)) return CLJ_THROWN;
	return eval_child(n->u.let.body, f);
}

// recur has already rebound the slots when the body yields CLJ_RECUR; the loop is a C loop, not a call.
// The ring is read once before the loop: the pointer is the execution's own across a park, a TLS re-read would not be.
static clj_value eval_loop(const clj_node *n, clj_frame *f) {
	if (!bind_all(n, f)) return CLJ_THROWN;
	clj_shadow_stack *s = clj_shadow_stack_init();
	for (;;) {
		clj_value v = eval_child(n->u.let.body, f);
		if (v != CLJ_RECUR) return v;
		if (deadline_hit(s)) return deadline_throw(s);
	}
}

// All arguments are evaluated before any slot changes, so (recur b a) swaps.
static clj_value eval_recur(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *vals = buf_alloc(small, n->u.recur.n);
	bool ok = eval_all(n->u.recur.args, n->u.recur.n, f, vals, NULL);
	if (ok) {
		for (uint32_t i = 0; i < n->u.recur.n; i++) slot_set(f, n->u.recur.slots[i], vals[i]);
	}
	buf_free(small, vals);
	return ok ? CLJ_RECUR : CLJ_THROWN;
}

static clj_value eval_fn(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *env = buf_alloc(small, n->u.fn.ncaptures);
	for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
		const clj_capture *c = &n->u.fn.captures[i];
		switch (c->kind) {
		case CLJ_CAPTURE_LOCAL: env[i] = f->slots[c->index]; break;
		case CLJ_CAPTURE_CAPTURED: env[i] = f->captured[c->index]; break;
		case CLJ_CAPTURE_OUTER: env[i] = outer_frame(f, c->depth)->slots[c->index]; break;
		}
	}
	clj_value fn = clj_fn_closure(clj_from_ptr((void *)f->exec), n, n->u.fn.name, env, n->u.fn.ncaptures);
	buf_free(small, env);
	return fn;
}

// A closure gets the call site for its shadow frame; anything else goes through the invoke slot.
static inline clj_value invoke_at(clj_value fn, const clj_value *args, uint32_t n, const clj_node *site) {
	if (clj_is_fn(fn) && clj_fn_of(fn)->kind == CLJ_FN_CLOSURE) return clj_closure_invoke_at(fn, args, n, site);
	return clj_invoke(fn, args, n);
}

// A plain native straight from the site: the arity check fn_invoke would make, then its C function. Natives
// are leaves of the shadow stack, so a throw inside reports the same frames as through clj_invoke.
static inline clj_value call_native(const clj_fn *nf, clj_value fn, const clj_value *args, uint32_t n) {
	if (n < nf->min_arity || (nf->max_arity != CLJ_ARITY_ANY && n > nf->max_arity)) return clj_arity_error(fn, n);
	return nf->u.native.fn(args, n);
}

// The guard reads its limit from the shadow stack, the one thread-local a call touches; the limit is computed
// on the thread's first call.
static char *stack_limit_of(clj_shadow_stack *s) {
#if defined(__APPLE__)
	pthread_t self = pthread_self();
	char     *top = pthread_get_stackaddr_np(self);
	size_t    size = pthread_get_stacksize_np(self);
	size_t    margin = size / 4 < STACK_MARGIN ? size / 4 : STACK_MARGIN;
	s->stack_limit = top - size + margin;
#else
	// No portable stack bounds: assume the thread's default and that we are near the top.
	char here;
	s->stack_limit = &here - (512 * 1024 - STACK_MARGIN);
#endif
	return s->stack_limit;
}

// Runs arity's body of the fn node `code` (a fn or a direct fn) in a frame the caller filled: every owned slot
// is released after. The guard, the shadow frame, the instrumentation and the recur loop of every call.
// @ai-generated(guided)
static inline __attribute__((always_inline)) clj_value run_body(const clj_node *code, const clj_fn_arity *arity, clj_frame *frame, const clj_node *site) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (__builtin_expect(!s, 0)) s = clj_shadow_stack_init();
	char *limit = s->stack_limit;
	if (__builtin_expect(!limit, 0)) limit = stack_limit_of(s);
	char here;
	if (__builtin_expect(&here < limit, 0)) {
		slots_release(frame, arity->nslots);
		return clj_throw_msg("Stack overflow");
	}
	if (deadline_hit(s)) {
		slots_release(frame, arity->nslots);
		return deadline_throw(s);
	}
	s->frames[s->depth & s->mask] = (clj_shadow_frame){code, site, __builtin_frame_address(0)};
	s->depth++;
	// Read once: a profiler started or stopped mid-body counts only calls timed from their entry.
	uint8_t  instrument = clj_instrument;
	uint64_t t0 = 0;
#ifdef __APPLE__
	os_signpost_id_t signpost = 0;
#endif
	if (__builtin_expect(instrument, 0)) {
		if (instrument & CLJ_INSTRUMENT_PROFILE) t0 = clj_profile_now();
#ifdef __APPLE__
		if (instrument & CLJ_INSTRUMENT_SIGNPOSTS) signpost = clj_signpost_begin(code->u.fn.name);
#endif
	}
	clj_value v;
	for (;;) {
		v = eval_child(arity->body, frame);
		if (v != CLJ_RECUR) break;
	}
	if (__builtin_expect(instrument, 0)) {
#ifdef __APPLE__
		if (signpost) clj_signpost_end(signpost);
#endif
		if (instrument & CLJ_INSTRUMENT_PROFILE) clj_profile_record(code, clj_profile_now() - t0);
	}
	if (__builtin_expect(--s->depth == 0, 0) && execution.nretired) drain_retired();
	slots_release(frame, arity->nslots);
	return v;
}

// A closure call: the fixed params (and the self slot) borrowed, the rest owned as `owned` says; the frame's
// exec and environment are the closure's own.
static inline __attribute__((always_inline)) clj_value run_frame(clj_value f, const clj_fn_arity *arity, clj_value *slots, uint64_t owned, const clj_node *site) {
	const clj_fn *fn = clj_fn_of(f);
	clj_frame     frame = {slots, (clj_value *)fn->env, clj_exec_of(fn->code), owned, NULL};
	return run_body(fn->u.node, arity, &frame, site);
}

// A call with the arguments already in a buffer: the frame copies them. Frames past SMALL_SLOTS live on the
// heap, past 64 slots they retain every param (eval.h).
// @ai-generated(guided)
static clj_value closure_run(clj_value f, const clj_fn_arity *arity, const clj_value *args, size_t n, const clj_node *site) {
	clj_value  small[SMALL_SLOTS];
	clj_value *slots = small;
	if (arity->nslots > SMALL_SLOTS) {
		slots = malloc(arity->nslots * sizeof *slots);
		if (!slots) clj_fatal("out of memory");
	}
	bool     big = arity->nslots > 64;
	uint64_t owned = big ? UINT64_MAX : 0;
	uint32_t filled = arity->nparams;
	for (uint32_t i = 0; i < arity->nparams; i++) slots[i] = big ? clj_retain(args[i]) : args[i];
	if (arity->variadic) {
		slots[filled++] = n > arity->nparams ? clj_list_from_array(args + arity->nparams, n - arity->nparams) : CLJ_NIL;
		owned |= (uint64_t)1 << arity->nparams;
	}
	for (uint32_t i = filled; i < arity->nslots; i++) slots[i] = CLJ_NIL;
	if (arity->self_slot >= 0) slots[arity->self_slot] = big ? clj_retain(f) : f;
	clj_value v = run_frame(f, arity, slots, owned, site);
	if (slots != small) free(slots);
	return v;
}

static clj_value call_impl(clj_value impl, const clj_fn_arity *arity, const clj_value *args, uint32_t n, const clj_node *at) {
	if (arity) return closure_run(impl, arity, args, n, at);
	return invoke_at(impl, args, n, at);
}

// A consistent snapshot of the entry for t under the seqlock, or false.
static bool proto_ic_find(const proto_ic *ic, uint64_t serial, const clj_type *t, proto_ic_entry *out, uint64_t *epoch) {
	uint64_t s0 = atomic_load_explicit(&ic->seq, memory_order_acquire);
	if ((s0 & 1) || ic->method_serial != serial) return false;
	bool found = false;
	for (uint32_t i = 0; i < ic->n && i < CLJ_PROTO_IC_ENTRIES; i++) {
		if (ic->e[i].receiver == t) {
			*out = ic->e[i];
			found = true;
			break;
		}
	}
	*epoch = ic->epoch;
	atomic_thread_fence(memory_order_acquire);
	return found && atomic_load_explicit(&ic->seq, memory_order_relaxed) == s0;
}

// Records what a dispatch under `epoch` found; a fill racing with another gives up (the other one's
// entry is as good), one under a moved epoch or another method empties the cache first.
// @ai-generated(guided)
static void proto_ic_fill(clj_call_site *site, uint64_t serial, uint64_t epoch, const clj_type *t, clj_value impl, const clj_fn_arity *arity) {
	proto_ic *ic = atomic_load_explicit(&site->proto, memory_order_acquire);
	if (!ic) {
		proto_ic *fresh = calloc(1, sizeof *fresh);
		if (!fresh) clj_fatal("out of memory");
		proto_ic *expected = NULL;
		if (atomic_compare_exchange_strong_explicit(&site->proto, &expected, fresh, memory_order_acq_rel, memory_order_acquire)) {
			ic = fresh;
		} else {
			free(fresh);
			ic = expected;
		}
	}
	uint64_t s0 = atomic_load_explicit(&ic->seq, memory_order_relaxed);
	if ((s0 & 1) || !atomic_compare_exchange_strong_explicit(&ic->seq, &s0, s0 + 1, memory_order_acq_rel, memory_order_relaxed)) return;
	if (ic->method_serial != serial || ic->epoch != epoch) {
		ic->method_serial = serial;
		ic->epoch = epoch;
		ic->n = ic->next = 0;
	}
	uint32_t i = ic->n < CLJ_PROTO_IC_ENTRIES ? ic->n++ : ic->next++ % CLJ_PROTO_IC_ENTRIES;
	ic->e[i] = (proto_ic_entry){t, impl, arity};
	atomic_store_explicit(&ic->seq, s0 + 2, memory_order_release);
}

// The method's own arity check comes first, as through clj_invoke, so the error names the method rather
// than the impl. A hit calls the cached impl; a miss dispatches through the tables under their reader window
// and records what it found.
// @ai-generated(guided)
static clj_value invoke_protocol(clj_call_site *site, clj_value method, const clj_value *args, uint32_t n, const clj_node *at) {
	const clj_fn *mf = clj_fn_of(method);
	if (n < mf->min_arity || (mf->max_arity != CLJ_ARITY_ANY && n > mf->max_arity)) return clj_arity_error(method, n);
	uint64_t        serial = clj_method_ctx_of(method)->serial;
	const clj_type *t = clj_dispatch_type_inline(args[0]);
	proto_ic       *ic = atomic_load_explicit(&site->proto, memory_order_acquire);
	proto_ic_entry  entry;
	uint64_t        epoch;
	if (__builtin_expect(ic && proto_ic_find(ic, serial, t, &entry, &epoch), 1)) {
		// The window makes the epoch check meaningful: a concurrent extend either bumped before this read or
		// waits for the close, so an impl the check admits is retained from a live table.
		clj_proto_reader *window = clj_proto_window_open_inline();
		bool              live = clj_epoch_load() == epoch;
		if (live) clj_retain(entry.impl);
		clj_proto_window_close_inline(window);
		if (live) {
			IC_COUNT(site, hits);
			clj_value r = call_impl(entry.impl, entry.arity, args, n, at);
			clj_release(entry.impl);
			return r;
		}
	}
	IC_COUNT(site, misses);
	epoch = clj_epoch_load();
	clj_value impl = clj_protocol_method_impl(method, args[0]);
	if (clj_is_nil(impl)) return clj_protocol_no_impl(method, args[0]);
	const clj_fn_arity *arity = clj_is_fn(impl) && clj_fn_of(impl)->kind == CLJ_FN_CLOSURE ? arity_for(clj_fn_of(impl)->u.node, n) : NULL;
	// The lookup ran between two reads of the same epoch, so the impl belongs to the tables of that epoch.
	if (clj_epoch_load() == epoch) proto_ic_fill(site, serial, epoch, t, impl, arity);
	clj_value r = call_impl(impl, arity, args, n, at);
	clj_release(impl);
	return r;
}

// A closure with a fixed arity for the call and a small frame gets its arguments evaluated straight into its
// slots: no argument buffer, no copy, and the owned mask of the evaluation is the frame's. Everything else
// evaluates into a buffer: a plain native is called from it directly, the rest dispatches through the slot.
// @ai-generated(guided)
static clj_value eval_invoke(const clj_node *n, clj_frame *f) {
	bool      fn_owned;
	clj_value fn = eval_borrowed(n->u.invoke.fn, f, &fn_owned);
	if (fn == CLJ_THROWN) return CLJ_THROWN;
	uint32_t            nargs = n->u.invoke.n;
	clj_call_site      *site = &((clj_exec *)f->exec)->sites[n->site];
	clj_value           result = CLJ_THROWN;
	uint64_t            owned;
	const clj_fn_arity *arity = NULL;
	const clj_fn       *fnp = clj_is_fn(fn) ? clj_fn_of(fn) : NULL;
	if (__builtin_expect(fnp && fnp->kind == CLJ_FN_CLOSURE && nargs <= CLJ_FN_MAX_FIXED, 1)) arity = fnp->u.node->u.fn.fixed[nargs];
	if (__builtin_expect(arity != NULL, 1) && arity->nslots <= SMALL_SLOTS) {
		IC_COUNT(site, hits);
		clj_value slots[SMALL_SLOTS];
		if (eval_all(n->u.invoke.args, nargs, f, slots, &owned)) {
			for (uint32_t i = nargs; i < arity->nslots; i++) slots[i] = CLJ_NIL;
			if (arity->self_slot >= 0) slots[arity->self_slot] = fn;
			result = run_frame(fn, arity, slots, owned, n);
		}
	} else {
		clj_value  small[SMALL_ARGS];
		clj_value *args = buf_alloc(small, nargs);
		if (eval_all(n->u.invoke.args, nargs, f, args, &owned)) {
			if (fnp && fnp->kind == CLJ_FN_NATIVE) {
				IC_COUNT(site, hits);
				result = call_native(fnp, fn, args, nargs);
			} else if (nargs && clj_is_protocol_method(fn)) {
				result = invoke_protocol(site, fn, args, nargs, n);
			} else {
				IC_COUNT(site, misses);
				result = invoke_at(fn, args, nargs, n);
			}
			release_owned(args, nargs, owned);
		}
		buf_free(small, args);
	}
	if (fn_owned) clj_release(fn);
	return result;
}

#if CLJ_DEBUG
static _Atomic int64_t direct_calls;
#define DIRECT_COUNT() atomic_fetch_add_explicit(&direct_calls, 1, memory_order_relaxed)
#else
#define DIRECT_COUNT() ((void)0)
#endif

int64_t clj_debug_direct_calls(void) {
#if CLJ_DEBUG
	return atomic_load_explicit(&direct_calls, memory_order_relaxed);
#else
	return -1;
#endif
}

// A direct fn's slot is a placeholder: every use of the binding is a DIRECT_CALL.
static clj_value eval_direct_fn(const clj_node *n, clj_frame *f) {
	(void)n;
	(void)f;
	return CLJ_NIL;
}

// The arguments are evaluated straight into a fresh frame, as for a closure with a small frame, and the frame
// links to the defining one `depth` links up: a free variable of the body is a slot there, alive for the call
// and unchanged during it (only a recur of an enclosing loop rebinds it, and that re-evaluates the let).
// Recursion gets a frame per activation. Past 64 slots every param is retained, as closure_run does.
// @ai-generated(guided)
static clj_value eval_direct_call(const clj_node *n, clj_frame *f) {
	const clj_fn_arity *arity = n->u.direct.arity;
	uint32_t            nargs = n->u.direct.n;
	clj_value           small[SMALL_SLOTS];
	clj_value          *slots = small;
	if (arity->nslots > SMALL_SLOTS) {
		slots = malloc(arity->nslots * sizeof *slots);
		if (!slots) clj_fatal("out of memory");
	}
	uint64_t  owned;
	clj_value result = CLJ_THROWN;
	if (eval_all(n->u.direct.args, nargs, f, slots, &owned)) {
		if (arity->nslots > 64) {
			for (uint32_t i = 0; i < nargs; i++) {
				if (!((owned >> i) & 1)) clj_retain(slots[i]);
			}
			owned = UINT64_MAX;
		}
		for (uint32_t i = nargs; i < arity->nslots; i++) slots[i] = CLJ_NIL;
		clj_frame frame = {slots, f->captured, f->exec, owned, outer_frame(f, n->u.direct.depth)};
		DIRECT_COUNT();
		result = run_body(n->u.direct.fn, arity, &frame, n);
	}
	if (slots != small) free(slots);
	return result;
}

// The var's root is compared with the fn the table resolved at boot before every call: a rebound var
// ((def + ...) in clojure.core, with-redefs) takes the generic path, so the rewrite is invisible to the
// program. A relaxed load suffices: a match calls a C function that reads nothing the bind published.
// @ai-generated(guided)
static inline bool intrinsic_guard(const clj_node *n) {
	return clj_var_root_relaxed(n->u.intrinsic.var) == clj_intrinsic_builtin(n->u.intrinsic.op);
}

// The table's function or the var's current root over evaluated arguments; the consuming bit leaves the mask.
static clj_value intrinsic_apply(const clj_node *n, clj_value *args, uint64_t *owned) {
	const clj_intrinsic *op = n->u.intrinsic.op;
	if (__builtin_expect(intrinsic_guard(n), 1)) {
		// A collection this site owns (a last-use local, a nested conj) goes to the consuming core as is: at rc 1
		// it is updated in place, and the bit leaves the mask since the core took the reference.
		if (__builtin_expect(clj_intrinsic_consumes(op), 0) && (*owned & 1)) {
			*owned &= ~(uint64_t)1;
			return clj_intrinsic_call_consuming(op, args);
		}
		return clj_intrinsic_call(op, args);
	}
	clj_value fn = clj_var_deref(n->u.intrinsic.var);
	clj_value result = fn == CLJ_THROWN ? CLJ_THROWN : invoke_at(fn, args, n->u.intrinsic.n, n);
	clj_release(fn);
	return result;
}

static clj_value eval_intrinsic(const clj_node *n, clj_frame *f) {
	clj_value args[3];
	uint64_t  owned;
	if (!eval_all(n->u.intrinsic.args, n->u.intrinsic.n, f, args, &owned)) return CLJ_THROWN;
	clj_value result = intrinsic_apply(n, args, &owned);
	release_owned(args, n->u.intrinsic.n, owned);
	return result;
}

// The specialized arithmetic entries (specialize.c installs one where the facts say what every argument is): the
// tag of every argument and the boot-root guard are the whole check, then the operation runs inline on the untagged
// values — for fixnums with the overflow check of arith2 and the canonical re-tag of clj_long_new, for doubles as
// IEEE arithmetic into a fresh box, for a fixnum beside a double as arith2 converts —; anything else, a boxed long
// included, takes the generic path above. A fact that went stale is therefore slower here, never wrong.
// @ai-generated(solo)
#define SPEC_ENTRY(name, nargs, check, body)                                                                    \
	static clj_value name(const clj_node *n, clj_frame *f) {                                                   \
		clj_value args[nargs];                                                                                 \
		uint64_t  owned;                                                                                       \
		if (!eval_all(n->u.intrinsic.args, nargs, f, args, &owned)) return CLJ_THROWN;                         \
		clj_value result;                                                                                      \
		if (__builtin_expect((check) && intrinsic_guard(n), 1)) {                                              \
			body                                                                                               \
		} else {                                                                                               \
			result = intrinsic_apply(n, args, &owned);                                                         \
		}                                                                                                      \
		release_owned(args, nargs, owned);                                                                     \
		return result;                                                                                         \
	}

#define FIXNUM_ARITH(name, nargs, check, op, a, b)                                                             \
	SPEC_ENTRY(name, nargs, check, {                                                                           \
		int64_t r;                                                                                             \
		if (__builtin_expect(op((a), (b), &r), 0)) result = clj_throw_msg("integer overflow");                \
		else result = clj_long_new(r);                                                                         \
	})

#define FIX1 (args[0] & 1)
#define FIX2 ((args[0] & args[1]) & 1)
#define DBL1 clj_is_double(args[0])
#define DBL2 (clj_is_double(args[0]) && clj_is_double(args[1]))
#define FIX_DBL ((args[0] & 1) && clj_is_double(args[1]))
#define DBL_FIX (clj_is_double(args[0]) && (args[1] & 1))
#define F0 clj_fixnum_val(args[0])
#define F1 clj_fixnum_val(args[1])
#define D0 clj_double_val(args[0])
#define D1 clj_double_val(args[1])

FIXNUM_ARITH(eval_fix_add, 2, FIX2, __builtin_add_overflow, F0, F1)
FIXNUM_ARITH(eval_fix_sub, 2, FIX2, __builtin_sub_overflow, F0, F1)
FIXNUM_ARITH(eval_fix_mul, 2, FIX2, __builtin_mul_overflow, F0, F1)
FIXNUM_ARITH(eval_fix_inc, 1, FIX1, __builtin_add_overflow, F0, (int64_t)1)
FIXNUM_ARITH(eval_fix_dec, 1, FIX1, __builtin_sub_overflow, F0, (int64_t)1)
SPEC_ENTRY(eval_fix_lt, 2, FIX2, { result = clj_bool(F0 < F1); })
SPEC_ENTRY(eval_fix_le, 2, FIX2, { result = clj_bool(F0 <= F1); })
SPEC_ENTRY(eval_fix_gt, 2, FIX2, { result = clj_bool(F0 > F1); })
SPEC_ENTRY(eval_fix_ge, 2, FIX2, { result = clj_bool(F0 >= F1); })
SPEC_ENTRY(eval_fix_eq, 2, FIX2, { result = clj_bool(args[0] == args[1]); })
SPEC_ENTRY(eval_fix_zero, 1, FIX1, { result = clj_bool(args[0] == clj_fixnum(0)); })
SPEC_ENTRY(eval_fix_pos, 1, FIX1, { result = clj_bool(F0 > 0); })
SPEC_ENTRY(eval_fix_neg, 1, FIX1, { result = clj_bool(F0 < 0); })

// IEEE semantics as arith2 and compare2: (/ 1.0 0.0) is ##Inf, every comparison with a NaN is false, = included.
SPEC_ENTRY(eval_dbl_add, 2, DBL2, { result = clj_double_new(D0 + D1); })
SPEC_ENTRY(eval_dbl_sub, 2, DBL2, { result = clj_double_new(D0 - D1); })
SPEC_ENTRY(eval_dbl_mul, 2, DBL2, { result = clj_double_new(D0 * D1); })
SPEC_ENTRY(eval_dbl_div, 2, DBL2, { result = clj_double_new(D0 / D1); })
SPEC_ENTRY(eval_dbl_inc, 1, DBL1, { result = clj_double_new(D0 + 1.0); })
SPEC_ENTRY(eval_dbl_dec, 1, DBL1, { result = clj_double_new(D0 - 1.0); })
SPEC_ENTRY(eval_dbl_lt, 2, DBL2, { result = clj_bool(D0 < D1); })
SPEC_ENTRY(eval_dbl_le, 2, DBL2, { result = clj_bool(D0 <= D1); })
SPEC_ENTRY(eval_dbl_gt, 2, DBL2, { result = clj_bool(D0 > D1); })
SPEC_ENTRY(eval_dbl_ge, 2, DBL2, { result = clj_bool(D0 >= D1); })
SPEC_ENTRY(eval_dbl_eq, 2, DBL2, { result = clj_bool(D0 == D1); })
SPEC_ENTRY(eval_dbl_zero, 1, DBL1, { result = clj_bool(D0 == 0); })
SPEC_ENTRY(eval_dbl_pos, 1, DBL1, { result = clj_bool(D0 > 0); })
SPEC_ENTRY(eval_dbl_neg, 1, DBL1, { result = clj_bool(D0 < 0); })

// A fixnum beside a double: the Numbers ladder makes the result a double and the fixnum converts as arith2 does.
SPEC_ENTRY(eval_fd_add, 2, FIX_DBL, { result = clj_double_new((double)F0 + D1); })
SPEC_ENTRY(eval_fd_sub, 2, FIX_DBL, { result = clj_double_new((double)F0 - D1); })
SPEC_ENTRY(eval_fd_mul, 2, FIX_DBL, { result = clj_double_new((double)F0 * D1); })
SPEC_ENTRY(eval_fd_div, 2, FIX_DBL, { result = clj_double_new((double)F0 / D1); })
SPEC_ENTRY(eval_fd_lt, 2, FIX_DBL, { result = clj_bool((double)F0 < D1); })
SPEC_ENTRY(eval_fd_le, 2, FIX_DBL, { result = clj_bool((double)F0 <= D1); })
SPEC_ENTRY(eval_fd_gt, 2, FIX_DBL, { result = clj_bool((double)F0 > D1); })
SPEC_ENTRY(eval_fd_ge, 2, FIX_DBL, { result = clj_bool((double)F0 >= D1); })
SPEC_ENTRY(eval_df_add, 2, DBL_FIX, { result = clj_double_new(D0 + (double)F1); })
SPEC_ENTRY(eval_df_sub, 2, DBL_FIX, { result = clj_double_new(D0 - (double)F1); })
SPEC_ENTRY(eval_df_mul, 2, DBL_FIX, { result = clj_double_new(D0 * (double)F1); })
SPEC_ENTRY(eval_df_div, 2, DBL_FIX, { result = clj_double_new(D0 / (double)F1); })
SPEC_ENTRY(eval_df_lt, 2, DBL_FIX, { result = clj_bool(D0 < (double)F1); })
SPEC_ENTRY(eval_df_le, 2, DBL_FIX, { result = clj_bool(D0 <= (double)F1); })
SPEC_ENTRY(eval_df_gt, 2, DBL_FIX, { result = clj_bool(D0 > (double)F1); })
SPEC_ENTRY(eval_df_ge, 2, DBL_FIX, { result = clj_bool(D0 >= (double)F1); })

#undef FIXNUM_ARITH
#undef SPEC_ENTRY
#undef FIX1
#undef FIX2
#undef DBL1
#undef DBL2
#undef FIX_DBL
#undef DBL_FIX
#undef F0
#undef F1
#undef D0
#undef D1

typedef struct {
	const char *name;
	uint32_t    arity;
	clj_eval_fn fix, dbl, fd, df;
} spec_entry;

static const spec_entry spec_entries[] = {
	{"clojure.core/+", 2, eval_fix_add, eval_dbl_add, eval_fd_add, eval_df_add},
	{"clojure.core/-", 2, eval_fix_sub, eval_dbl_sub, eval_fd_sub, eval_df_sub},
	{"clojure.core/*", 2, eval_fix_mul, eval_dbl_mul, eval_fd_mul, eval_df_mul},
	// an integer quotient may be a ratio: only the double forms
	{"clojure.core//", 2, NULL, eval_dbl_div, eval_fd_div, eval_df_div},
	{"clojure.core/inc", 1, eval_fix_inc, eval_dbl_inc, NULL, NULL},
	{"clojure.core/dec", 1, eval_fix_dec, eval_dbl_dec, NULL, NULL},
	{"clojure.core/<", 2, eval_fix_lt, eval_dbl_lt, eval_fd_lt, eval_df_lt},
	{"clojure.core/<=", 2, eval_fix_le, eval_dbl_le, eval_fd_le, eval_df_le},
	{"clojure.core/>", 2, eval_fix_gt, eval_dbl_gt, eval_fd_gt, eval_df_gt},
	{"clojure.core/>=", 2, eval_fix_ge, eval_dbl_ge, eval_fd_ge, eval_df_ge},
	// = between a fixnum and a double is false by type: the generic path answers it
	{"clojure.core/=", 2, eval_fix_eq, eval_dbl_eq, NULL, NULL},
	{"clojure.core/zero?", 1, eval_fix_zero, eval_dbl_zero, NULL, NULL},
	{"clojure.core/pos?", 1, eval_fix_pos, eval_dbl_pos, NULL, NULL},
	{"clojure.core/neg?", 1, eval_fix_neg, eval_dbl_neg, NULL, NULL},
};

static const spec_entry *spec_entry_of(const clj_intrinsic *op) {
	for (size_t i = 0; i < sizeof spec_entries / sizeof *spec_entries; i++) {
		if (spec_entries[i].arity == op->arity && strcmp(spec_entries[i].name, op->name) == 0) return &spec_entries[i];
	}
	return NULL;
}

clj_eval_fn clj_eval_specialized_entry(const clj_intrinsic *op, clj_spec_kind kind) {
	const spec_entry *e = spec_entry_of(op);
	if (!e) return NULL;
	switch (kind) {
	case CLJ_SPEC_FIXNUM: return e->fix;
	case CLJ_SPEC_DOUBLE: return e->dbl;
	case CLJ_SPEC_FIXNUM_DOUBLE: return e->fd;
	case CLJ_SPEC_DOUBLE_FIXNUM: return e->df;
	}
	return NULL;
}

// The arguments become the frame of whichever program runs, at +0 for its whole evaluation: the programs
// hold only local reads, calls and literals, so nothing in them stores to a slot.
// @ai-generated(guided)
static clj_value eval_fused(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	uint32_t   nargs = n->u.fused.nargs;
	clj_value *vals = buf_alloc(small, nargs);
	uint64_t   owned;
	clj_value  result = CLJ_THROWN;
	if (eval_all(n->u.fused.args, nargs, f, vals, &owned)) {
		bool      fuse = clj_fusion_guard(n->u.fused.guards, n->u.fused.nguards);
		clj_frame inner = {vals, NULL, f->exec, 0, NULL};
		result = eval_child(fuse ? n->u.fused.fused : n->u.fused.original, &inner);
		release_owned(vals, nargs, owned);
	}
	buf_free(small, vals);
	return result;
}

// Root first, then meta, then the flags, as DefExpr.eval does.
// ---- keyword-lookup sites (NOTES.md "Shapes", the cache)

// Entries are appended under one lock and never rewritten, so a reader that loads n sees whole entries below it.
typedef struct {
	_Atomic uint32_t n; // CLJ_KW_IC_MEGA once the site gave up
	clj_kw_entry     e[CLJ_KW_IC_ENTRIES];
#if CLJ_DEBUG
	_Atomic uint64_t hits, misses;
#endif
} kw_ic;

static clj_lock kw_fill_lock = CLJ_LOCK_INIT;

#if CLJ_DEBUG
#define KW_COUNT(ic, which) atomic_fetch_add_explicit(&(ic)->which, 1, memory_order_relaxed)
#else
#define KW_COUNT(ic, which) ((void)0)
#endif

static void kw_ic_fill(kw_ic *ic, clj_value m, clj_value key) {
	clj_kw_entry e;
	if (!clj_kw_entry_make(&e, m, key)) return;
	clj_lock_lock(&kw_fill_lock);
	uint32_t n = atomic_load_explicit(&ic->n, memory_order_relaxed);
	bool     seen = false;
	for (uint32_t i = 0; i < n && i < CLJ_KW_IC_ENTRIES; i++) seen = seen || ic->e[i].key == e.key;
	if (!seen && n < CLJ_KW_IC_ENTRIES) {
		ic->e[n] = e;
		atomic_store_explicit(&ic->n, n + 1, memory_order_release);
	} else if (!seen && n == CLJ_KW_IC_ENTRIES) {
		atomic_store_explicit(&ic->n, CLJ_KW_IC_MEGA, memory_order_relaxed);
	}
	clj_lock_unlock(&kw_fill_lock);
}

// (get m key not_found) through the site's cache: a hit is the guard and one load, a miss the generic lookup and a fill.
// @ai-generated(solo)
static inline clj_value kw_lookup(kw_ic *ic, clj_value m, clj_value key, clj_value not_found) {
	uint32_t n = atomic_load_explicit(&ic->n, memory_order_acquire);
	if (__builtin_expect(n <= CLJ_KW_IC_ENTRIES, 1)) {
		bool        record;
		const void *k = clj_kw_key_of(m, &record);
		for (uint32_t i = 0; i < n; i++) {
			if (clj_kw_entry_hit(&ic->e[i], k, key)) {
				KW_COUNT(ic, hits);
				return clj_retain(clj_kw_entry_read(&ic->e[i], m, not_found));
			}
		}
		if (k) kw_ic_fill(ic, m, key);
	}
	KW_COUNT(ic, misses);
	return clj_get(m, key, not_found);
}

// (:k m) and (:k m nf): the keyword is the node's constant, so the call is the lookup itself (keyword_invoke).
static clj_value eval_kw_invoke(const clj_node *n, clj_frame *f) {
	clj_value args[2];
	uint64_t  owned;
	if (!eval_all(n->u.invoke.args, n->u.invoke.n, f, args, &owned)) return CLJ_THROWN;
	clj_value r = kw_lookup(f->exec->nodes[n->id].ic, args[0], n->u.invoke.fn->u.value, n->u.invoke.n == 2 ? args[1] : CLJ_NIL);
	release_owned(args, n->u.invoke.n, owned);
	return r;
}

// (get m :k) and (get m :k nf) under the intrinsic guard; a rebound `get` takes the generic path.
static clj_value eval_get_kw(const clj_node *n, clj_frame *f) {
	clj_value args[3];
	uint64_t  owned;
	if (!eval_all(n->u.intrinsic.args, n->u.intrinsic.n, f, args, &owned)) return CLJ_THROWN;
	clj_value r;
	if (__builtin_expect(intrinsic_guard(n), 1)) r = kw_lookup(f->exec->nodes[n->id].ic, args[0], args[1], n->u.intrinsic.n == 3 ? args[2] : CLJ_NIL);
	else r = intrinsic_apply(n, args, &owned);
	release_owned(args, n->u.intrinsic.n, owned);
	return r;
}

static bool kw_invoke_site(const clj_node *n) {
	return n->kind == CLJ_NODE_INVOKE && n->u.invoke.fn->kind == CLJ_NODE_CONST && clj_is_keyword(n->u.invoke.fn->u.value) &&
	       (n->u.invoke.n == 1 || n->u.invoke.n == 2);
}

static bool kw_get_site(const clj_node *n) {
	return n->kind == CLJ_NODE_INTRINSIC && clj_intrinsic_is_get(n->u.intrinsic.op) && n->u.intrinsic.args[1]->kind == CLJ_NODE_CONST &&
	       clj_is_keyword(n->u.intrinsic.args[1]->u.value);
}

static clj_value eval_def(const clj_node *n, clj_frame *f) {
	if (n->u.def.init) {
		clj_value v = eval_child(n->u.def.init, f);
		if (v == CLJ_THROWN) return CLJ_THROWN;
		clj_var_bind_root(n->u.def.var, v);
		clj_release(v);
	}
	clj_value m = eval_child(n->u.def.meta, f);
	if (m == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_map(m)) {
		clj_value e = clj_throw_msg("def metadata must be a map, got: %s", clj_type_name(m));
		clj_release(m);
		return e;
	}
	clj_var_set_meta(n->u.def.var, m);
	clj_release(m);
	clj_var_set_macro(n->u.def.var, n->u.def.macro);
	clj_var_set_dynamic(n->u.def.var, n->u.def.dynamic);
	return clj_retain(n->u.def.var);
}

static clj_value eval_vector(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *items = buf_alloc(small, n->u.seq.n);
	clj_value  result = CLJ_THROWN;
	uint64_t   owned;
	if (eval_all(n->u.seq.items, n->u.seq.n, f, items, &owned)) {
		result = clj_vector_from_array(items, n->u.seq.n);
		release_owned(items, n->u.seq.n, owned);
	}
	buf_free(small, items);
	return result;
}

// The cache of a MAP literal whose keys are constant keywords: its shape and the slot each key of the literal takes.
typedef struct {
	const clj_shape *shape;
	uint8_t          slot[CLJ_SHAPE_MAX_KEYS];
} map_ic;

// Such a literal builds straight into its shape: one allocation, no transitions.
// @ai-generated(solo)
static clj_value eval_map_shaped(const clj_node *n, clj_frame *f) {
	const map_ic  *ic = f->exec->nodes[n->id].ic;
	uint32_t       nkeys = n->u.seq.n / 2;
	clj_value      m = clj_shape_map_alloc(ic->shape);
	clj_shape_map *sm = clj_shape_map_of(m);
	for (uint32_t i = 0; i < nkeys; i++) {
		clj_value v = eval_child(n->u.seq.items[2 * i + 1], f);
		if (v == CLJ_THROWN) {
			clj_release(m);
			return CLJ_THROWN;
		}
		sm->slots[ic->slot[i]] = v;
	}
	return m;
}

// The cache of a literal whose keys are constant keywords without a duplicate, else NULL (the generic entry throws).
static map_ic *map_literal_ic(const clj_node *n) {
	uint32_t  nkeys = n->u.seq.n / 2;
	clj_value keys[CLJ_SHAPE_MAX_KEYS];
	if (nkeys == 0 || nkeys > CLJ_SHAPE_MAX_KEYS) return NULL;
	for (uint32_t i = 0; i < nkeys; i++) {
		const clj_node *k = n->u.seq.items[2 * i];
		if (k->kind != CLJ_NODE_CONST || !clj_is_keyword(k->u.value)) return NULL;
		keys[i] = k->u.value;
	}
	const clj_shape *shape = clj_shape_for_keys(keys, nkeys);
	if (!shape) return NULL;
	map_ic *ic = calloc(1, sizeof *ic);
	if (!ic) clj_fatal("out of memory");
	ic->shape = shape;
	clj_shape_slots_of(shape, keys, ic->slot);
	return ic;
}

static clj_value eval_map(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *items = buf_alloc(small, n->u.seq.n);
	clj_value  result = CLJ_THROWN;
	uint64_t   owned;
	if (eval_all(n->u.seq.items, n->u.seq.n, f, items, &owned)) {
		result = clj_map_empty();
		for (uint32_t i = 0; i < n->u.seq.n; i += 2) {
			if (clj_map_contains(result, items[i])) {
				clj_value text = clj_pr_str_max(items[i], CLJ_ERROR_PRINT_MAX);
				clj_release(result);
				result = text == CLJ_THROWN ? CLJ_THROWN : clj_throw_msg("Duplicate key: %s", clj_string_bytes(text));
				clj_release(text);
				break;
			}
			result = clj_map_assoc(result, items[i], items[i + 1]);
		}
		release_owned(items, n->u.seq.n, owned);
	}
	buf_free(small, items);
	return result;
}

static clj_value eval_set(const clj_node *n, clj_frame *f) {
	clj_value  small[SMALL_ARGS];
	clj_value *items = buf_alloc(small, n->u.seq.n);
	clj_value  result = CLJ_THROWN;
	uint64_t   owned;
	if (eval_all(n->u.seq.items, n->u.seq.n, f, items, &owned)) {
		result = clj_set_empty();
		for (uint32_t i = 0; i < n->u.seq.n; i++) {
			if (clj_set_contains(result, items[i])) {
				clj_value text = clj_pr_str_max(items[i], CLJ_ERROR_PRINT_MAX);
				clj_release(result);
				result = text == CLJ_THROWN ? CLJ_THROWN : clj_throw_msg("Duplicate key: %s", clj_string_bytes(text));
				clj_release(text);
				break;
			}
			result = clj_set_conj(result, items[i]);
		}
		release_owned(items, n->u.seq.n, owned);
	}
	buf_free(small, items);
	return result;
}

// @ai-generated(guided)
static clj_value eval_throw(const clj_node *n, clj_frame *f) {
	clj_value v = eval_child(n->u.throw_, f);
	if (v == CLJ_THROWN) return CLJ_THROWN;
	return clj_throw(v);
}

// :default also catches non-errors, so its :cancelled exclusion stays explicit (design.md §4).
static bool catch_matches(const clj_catch *c, clj_value ex) {
	switch (c->kind) {
	case CLJ_CATCH_ALL: return !clj_ex_isa(ex, clj_cancelled_keyword());
	case CLJ_CATCH_ERROR: return clj_is_exception(ex);
	default: return clj_ex_isa(ex, c->keyword);
	}
}

// Unwinding is the ordinary return path: the body has already released its temporaries when CLJ_THROWN
// arrives here. finally runs on both paths; when it throws, its exception replaces the in-flight one.
// @ai-generated(guided)
static clj_value eval_try(const clj_node *n, clj_frame *f) {
	clj_value v = eval_child(n->u.try_.body, f);
	CLJ_ASSERT(v != CLJ_RECUR, "recur escaped a try body");
	if (v == CLJ_THROWN && n->u.try_.ncatches) {
		clj_value trace = clj_take_pending_trace();
		clj_value ex = clj_take_pending();
		bool      handled = false;
		for (uint32_t i = 0; i < n->u.try_.ncatches && !handled; i++) {
			const clj_catch *c = &n->u.try_.catches[i];
			if (!catch_matches(c, ex)) continue;
			slot_set(f, c->slot, ex);
			v = eval_child(c->handler, f);
			handled = true;
		}
		if (handled) clj_release(trace);
		else clj_throw_traced(ex, trace);
	}
	if (n->u.try_.finally_) {
		// The pending slot is free while finally runs; the in-flight exception is parked here.
		clj_value parked_trace = v == CLJ_THROWN ? clj_take_pending_trace() : CLJ_NIL;
		clj_value parked = v == CLJ_THROWN ? clj_take_pending() : CLJ_NIL;
		clj_value fv = eval_child(n->u.try_.finally_, f);
		if (fv == CLJ_THROWN) {
			clj_release(v == CLJ_THROWN ? parked : v);
			clj_release(parked_trace);
			return CLJ_THROWN;
		}
		clj_release(fv);
		if (v == CLJ_THROWN) clj_throw_traced(parked, parked_trace);
	}
	return v;
}

clj_eval_fn clj_node_eval_fn(clj_node_kind kind) {
	switch (kind) {
	case CLJ_NODE_CONST: return eval_const;
	case CLJ_NODE_LOCAL: return eval_local;
	case CLJ_NODE_CAPTURED: return eval_captured;
	case CLJ_NODE_VAR: return eval_var;
	case CLJ_NODE_IF: return eval_if;
	case CLJ_NODE_DO: return eval_do;
	case CLJ_NODE_LET: return eval_let;
	case CLJ_NODE_LOOP: return eval_loop;
	case CLJ_NODE_RECUR: return eval_recur;
	case CLJ_NODE_FN: return eval_fn;
	case CLJ_NODE_INVOKE: return eval_invoke;
	case CLJ_NODE_DEF: return eval_def;
	case CLJ_NODE_VECTOR: return eval_vector;
	case CLJ_NODE_MAP: return eval_map;
	case CLJ_NODE_SET: return eval_set;
	case CLJ_NODE_TRY: return eval_try;
	case CLJ_NODE_THROW: return eval_throw;
	case CLJ_NODE_INTRINSIC: return eval_intrinsic;
	case CLJ_NODE_FUSED: return eval_fused;
	case CLJ_NODE_OUTER: return eval_outer;
	case CLJ_NODE_DIRECT_FN: return eval_direct_fn;
	case CLJ_NODE_DIRECT_CALL: return eval_direct_call;
	}
	clj_fatal("unknown node kind");
}

// The exec is const to the frame only by convention; the table is its own mutable state.
// @ai-generated(guided)
static clj_value eval_counting(const clj_node *n, clj_frame *f) {
	((clj_exec *)f->exec)->nodes[n->id].hits++;
	return clj_node_eval_fn(n->kind)(n, f);
}

static void count_on(const clj_node *n, void *ctx) {
	clj_exec *e = ctx;
	e->nodes[n->id].eval = eval_counting;
	clj_node_children(n, count_on, e);
}

clj_eval_fn clj_eval_site_entry(const clj_exec *e, const clj_node *n) {
	if (!e->nodes[n->id].ic) return clj_node_eval_fn(n->kind);
	if (n->kind == CLJ_NODE_INVOKE) return eval_kw_invoke;
	if (n->kind == CLJ_NODE_INTRINSIC) return eval_get_kw;
	return eval_map_shaped;
}

static void count_off(const clj_node *n, void *ctx) {
	clj_exec *e = ctx;
	e->nodes[n->id].eval = clj_eval_site_entry(e, n);
	clj_node_children(n, count_off, e);
}

void clj_exec_count(clj_value exec, bool on) {
	clj_exec *e = clj_exec_of(exec);
	(on ? count_on : count_off)(e->root, e);
	if (!on) clj_exec_reapply(exec);
}

uint64_t clj_exec_hits(clj_value exec, uint32_t id) { return clj_exec_of(exec)->nodes[id].hits; }

typedef struct {
	uint32_t        id;
	const clj_node *found;
} find_ctx;

// Ids are pre-order and a subtree's are contiguous: one child per level contains the target.
static void find_node(const clj_node *n, void *ctx) {
	find_ctx *c = ctx;
	if (c->found || c->id < n->id || c->id >= n->id + n->nnodes) return;
	if (n->id == c->id) c->found = n;
	else clj_node_children(n, find_node, c);
}

static const clj_call_site *site_of(clj_value exec, uint32_t id) {
	const clj_exec *e = clj_exec_of(exec);
	find_ctx        c = {id, NULL};
	find_node(e->root, &c);
	CLJ_ASSERT(c.found && c.found->kind == CLJ_NODE_INVOKE, "not an invoke node of this exec");
	return &e->sites[c.found->site];
}

int64_t clj_debug_exec_ic_hits(clj_value exec, uint32_t id) {
#if CLJ_DEBUG
	return (int64_t)atomic_load_explicit(&site_of(exec, id)->hits, memory_order_relaxed);
#else
	(void)exec;
	(void)id;
	return -1;
#endif
}

int64_t clj_debug_exec_ic_misses(clj_value exec, uint32_t id) {
#if CLJ_DEBUG
	return (int64_t)atomic_load_explicit(&site_of(exec, id)->misses, memory_order_relaxed);
#else
	(void)exec;
	(void)id;
	return -1;
#endif
}

typedef struct {
	uint32_t        site;
	const clj_node *found;
} find_site_ctx;

static void find_site(const clj_node *n, void *ctx) {
	find_site_ctx *c = ctx;
	if (c->found) return;
	if (n->kind == CLJ_NODE_INVOKE && n->site == c->site) c->found = n;
	else clj_node_children(n, find_site, c);
}

uint32_t clj_debug_exec_invoke_id(clj_value exec, uint32_t site) {
	find_site_ctx c = {site, NULL};
	find_site(clj_exec_of(exec)->root, &c);
	CLJ_ASSERT(c.found, "no invoke node with that ordinal");
	return c.found->id;
}

uint32_t clj_debug_exec_ic_proto_entries(clj_value exec, uint32_t id) {
	const proto_ic *ic = atomic_load_explicit(&site_of(exec, id)->proto, memory_order_acquire);
	return ic ? ic->n : 0;
}

static const kw_ic *kw_ic_of(clj_value exec, uint32_t id) {
	const clj_exec *e = clj_exec_of(exec);
	find_ctx        c = {id, NULL};
	find_node(e->root, &c);
	if (!c.found || (c.found->kind != CLJ_NODE_INVOKE && c.found->kind != CLJ_NODE_INTRINSIC)) return NULL;
	return e->nodes[id].ic;
}

uint32_t clj_debug_exec_kw_entries(clj_value exec, uint32_t id) {
	const kw_ic *ic = kw_ic_of(exec, id);
	return ic ? atomic_load_explicit(&ic->n, memory_order_acquire) : UINT32_MAX;
}

int64_t clj_debug_exec_kw_hits(clj_value exec, uint32_t id) {
#if CLJ_DEBUG
	const kw_ic *ic = kw_ic_of(exec, id);
	return ic ? (int64_t)atomic_load_explicit(&ic->hits, memory_order_relaxed) : -1;
#else
	(void)exec;
	(void)id;
	return -1;
#endif
}

int64_t clj_debug_exec_kw_misses(clj_value exec, uint32_t id) {
#if CLJ_DEBUG
	const kw_ic *ic = kw_ic_of(exec, id);
	return ic ? (int64_t)atomic_load_explicit(&ic->misses, memory_order_relaxed) : -1;
#else
	(void)exec;
	(void)id;
	return -1;
#endif
}

typedef struct {
	uint32_t        ordinal, seen;
	const clj_node *found;
} find_kw_ctx;

static void find_kw_site(const clj_node *n, void *ctx) {
	find_kw_ctx *c = ctx;
	if (c->found) return;
	if ((kw_invoke_site(n) || kw_get_site(n)) && c->seen++ == c->ordinal) {
		c->found = n;
		return;
	}
	clj_node_children(n, find_kw_site, c);
}

uint32_t clj_debug_exec_kw_site_id(clj_value exec, uint32_t ordinal) {
	find_kw_ctx c = {ordinal, 0, NULL};
	find_kw_site(clj_exec_of(exec)->root, &c);
	return c.found ? c.found->id : UINT32_MAX;
}

bool clj_debug_exec_map_shaped(clj_value exec, uint32_t id) {
	const clj_exec *e = clj_exec_of(exec);
	return id < e->root->nnodes && e->nodes[id].eval == eval_map_shaped;
}

static void exec_each_child(void *self, clj_visitor visit, void *ctx) {
	const clj_exec *e = self;
	visit(clj_from_ptr((void *)e->root), ctx);
}

static void free_ic(const clj_node *n, void *ctx) {
	clj_exec *e = ctx;
	free(e->nodes[n->id].ic);
	clj_node_children(n, free_ic, e);
}

// The dependents index (specialize.c) holds execs without a reference: the entry leaves under its lock while the
// header still says how many references the exec has, so a rebind that meets a dying one can see it die.
static void exec_unlink(void *self) { clj_exec_forget(self); }

static void exec_finalize(void *self) {
	clj_exec *e = self;
	free_ic(e->root, e);
	for (uint32_t i = 0; i < e->nsites; i++) free(atomic_load_explicit(&e->sites[i].proto, memory_order_relaxed));
	free(e->sites);
}

const clj_type clj_exec_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "exec",
	.each_child = exec_each_child,
	.finalize = exec_finalize,
	.unlink = exec_unlink,
};

typedef struct {
	clj_exec *exec;
	bool      top; // outside every fn: these slots belong to the root's frame
} build_ctx;

static void note_slot(build_ctx *b, uint32_t slot) {
	if (b->top && slot >= b->exec->nslots) b->exec->nslots = slot + 1;
}

static void *kw_ic_new(void) {
	kw_ic *ic = calloc(1, sizeof *ic);
	if (!ic) clj_fatal("out of memory");
	return ic;
}

// @ai-generated(guided)
static void build(const clj_node *n, void *ctx) {
	build_ctx *b = ctx;
	b->exec->nodes[n->id].eval = clj_node_eval_fn(n->kind);
	switch (n->kind) {
	case CLJ_NODE_INVOKE:
		if (n->site >= b->exec->nsites) b->exec->nsites = n->site + 1;
		if (kw_invoke_site(n)) {
			b->exec->nodes[n->id].ic = kw_ic_new();
			b->exec->nodes[n->id].eval = eval_kw_invoke;
		}
		break;
	case CLJ_NODE_INTRINSIC:
		if (kw_get_site(n)) {
			b->exec->nodes[n->id].ic = kw_ic_new();
			b->exec->nodes[n->id].eval = eval_get_kw;
		}
		break;
	case CLJ_NODE_MAP: {
		map_ic *ic = map_literal_ic(n);
		if (ic) {
			b->exec->nodes[n->id].ic = ic;
			b->exec->nodes[n->id].eval = eval_map_shaped;
		}
		break;
	}
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) note_slot(b, n->u.let.slots[i]);
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) note_slot(b, n->u.try_.catches[i].slot);
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: {
		build_ctx inner = {b->exec, false};
		clj_node_children(n, build, &inner);
		return;
	}
	default: break;
	}
	clj_node_children(n, build, b);
}

clj_value clj_exec_new(const clj_node *root) {
	CLJ_ASSERT(root->id == 0, "exec needs the root of a tree");
	clj_exec *e = clj_alloc(&clj_exec_type, sizeof *e + root->nnodes * sizeof *e->nodes);
	clj_retain(clj_from_ptr((void *)root));
	e->root = root;
	build_ctx b = {e, true};
	build(root, &b);
	if (e->nsites) {
		e->sites = calloc(e->nsites, sizeof *e->sites);
		if (!e->sites) clj_fatal("out of memory");
	}
	clj_exec_derive(clj_from_ptr(e));
	return clj_from_ptr(e);
}

static clj_value arity_error(clj_value f, size_t n) {
	clj_value name = clj_fn_of(f)->name;
	clj_value text = clj_is_nil(name) ? clj_string_from_cstr("fn") : clj_pr_str(name);
	if (text == CLJ_THROWN) return CLJ_THROWN;
	clj_value r = clj_throw_msg("Wrong number of args (%zu) passed to: %s", n, clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n) { return clj_closure_invoke_at(f, args, n, NULL); }

clj_value clj_closure_invoke_at(clj_value f, const clj_value *args, size_t n, const clj_node *site) {
	const clj_fn_arity *arity = arity_for(clj_fn_of(f)->u.node, n);
	if (!arity) return arity_error(f, n);
	return closure_run(f, arity, args, n, site);
}

clj_call clj_call_prepare(clj_value f, size_t n) {
	clj_call c = {f, n, NULL, NULL, NULL};
	if (!clj_is_fn(f)) return c;
	const clj_fn *fn = clj_fn_of(f);
	if (fn->kind == CLJ_FN_CLOSURE) {
		c.arity = arity_for(fn->u.node, n);
	} else if (fn->kind == CLJ_FN_NATIVE && n >= fn->min_arity && (fn->max_arity == CLJ_ARITY_ANY || n <= fn->max_arity)) {
		c.native = fn->u.native.fn;
		c.consuming = clj_intrinsic_consuming(f, (uint32_t)n);
	}
	return c;
}

clj_value clj_call_invoke_slow(const clj_call *c, const clj_value *args) {
	if (c->arity) return closure_run(c->f, c->arity, args, c->n, NULL);
	return clj_invoke(c->f, args, c->n);
}

clj_value clj_exec_run(clj_value exec) {
	const clj_exec *e = clj_exec_of(exec);
	uint32_t        nslots = e->nslots;
	clj_value       small[SMALL_SLOTS];
	clj_value      *slots = small;
	if (nslots > SMALL_SLOTS) {
		slots = malloc(nslots * sizeof *slots);
		if (!slots) clj_fatal("out of memory");
	}
	memset(slots, 0, nslots * sizeof *slots);
	clj_frame frame = {slots, NULL, e, 0, NULL};
	execution.exec_depth++;
	clj_value v = eval_child(e->root, &frame);
	CLJ_ASSERT(v != CLJ_RECUR, "recur escaped its target");
	slots_release(&frame, nslots);
	if (slots != small) free(slots);
	if (--execution.exec_depth == 0 && execution.nretired) drain_retired();
	return v;
}

clj_value clj_eval_node(const clj_node *node) {
	clj_value exec = clj_exec_new(node);
	clj_value v = clj_exec_run(exec);
	clj_release(exec);
	return v;
}

// Positions it after the head when form is (do ...); a macro may have produced any seq type.
static bool is_do_form(clj_value form, clj_seq_iter *it) {
	if (!clj_is_seq(form)) return false;
	*it = clj_seq_iter_start(form);
	clj_value head;
	if (!clj_seq_iter_next(it, &head)) return false;
	if (clj_is_symbol(head) && clj_is_nil(clj_symbol_ns(head)) && strcmp(clj_string_bytes(clj_symbol_name(head)), "do") == 0) return true;
	clj_seq_iter_close(it);
	return false;
}

__attribute__((noinline)) bool clj_deadline_tick(void) {
	__asm__ volatile("" ::: "memory");
	clj_shadow_stack *s = clj_shadow_tls;
	return s && clj_deadline_tick_on(s);
}

// A compiled loop captures its execution's ring once, before the first turn (compiled_internal.h).
__attribute__((noinline)) clj_shadow_stack *clj_c_tick_ring(void) {
	__asm__ volatile("" ::: "memory");
	return clj_shadow_stack_init();
}

bool clj_eval_deadline_hit(void *shadow_stack) {
	clj_shadow_stack *s = shadow_stack;
	if (!deadline_reached(s)) return false;
	deadline_throw(s);
	return true;
}

void clj_eval_top_enter(void) { execution.exec_depth++; }

uint32_t clj_eval_exec_depth(void) { return execution.exec_depth; }

void clj_eval_exec_depth_set(uint32_t depth) { execution.exec_depth = depth; }

// The host's synchronous trampoline: a park under it is an error (design §5, host_depth), never a block.
clj_value clj_host_invoke(clj_value f, const clj_value *args, size_t n) {
	// The bracket opens before the point is pushed, so a landing restores the depth to the open bracket.
	clj_coro *c = clj_coro_current();
	c->exec_depth++;
	c->host_depth++;
	clj_recovery r;
	clj_recovery_push(&r);
	clj_value v;
	if (sigsetjmp(r.buf, 0)) v = clj_recovery_throw(&r);
	else v = clj_invoke(f, args, n);
	clj_recovery_pop(&r);
	c->host_depth--;
	if (--c->exec_depth == 0 && c->nretired) drain_retired();
	return v;
}

void clj_eval_top_leave(void) {
	if (--execution.exec_depth == 0 && execution.nretired) drain_retired();
}


static clj_value eval_form(clj_value form, const clj_env *given);

// Every top-level form is a recovery point (guard.h): the first form of a thread makes its shadow stack too.
clj_value clj_eval(clj_value form, const clj_env *given) {
	clj_recovery r;
	clj_recovery_push(&r);
	clj_value v;
	if (sigsetjmp(r.buf, 0)) v = clj_recovery_throw(&r);
	else v = eval_form(form, given);
	clj_recovery_pop(&r);
	return v;
}

// A top-level (do ...) is a sequence of top-level forms: a defmacro in it is visible to the next form.
// Without a position in env the form's own :line/:column stand in, so the expansion's errors keep them.
static clj_value eval_form(clj_value form, const clj_env *given) {
	clj_env local = given ? *given : (clj_env){0};
	if (!local.line) clj_form_position(form, &local.line, &local.col);
	const clj_env       *env = &local;
	clj_load_arm         arm = clj_load_arm_tls;
	const clj_load_hook *hook = clj_load_hook_get();
	bool                 file_owned = false;
	clj_load_arm_tls.armed = false;
	if (!hook) arm.armed = false;
	if (hook && !arm.armed && hook->toplevel && !in_flight()) {
		arm.armed = true;
		arm.form = (clj_load_form){clj_var_deref(clj_load_file_var()), local.line, local.col, clj_load_form_name(form), clj_load_next_serial()};
		file_owned = true;
	}
	clj_load_analysis_failed = true;
	clj_value expanded = clj_macroexpand(form, env);
	if (expanded == CLJ_THROWN) {
		if (file_owned) clj_release(arm.form.file);
		return CLJ_THROWN;
	}
	clj_value    v = CLJ_NIL;
	clj_seq_iter it;
	if (is_do_form(expanded, &it)) {
		clj_value item;
		while (clj_seq_iter_next(&it, &item)) {
			clj_release(v);
			clj_load_arm_tls = arm;
			v = clj_eval(item, env);
			clj_load_arm_tls.armed = false;
			if (v == CLJ_THROWN) break;
		}
		clj_seq_iter_close(&it);
		if (it.thrown) {
			clj_release(v);
			v = CLJ_THROWN;
		}
	} else {
		const clj_node *node = clj_analyze(expanded, env);
		if (node) {
			clj_load_analysis_failed = false;
			bool handled = false;
			if (arm.armed) v = hook->form(&arm.form, node, hook->ctx, &handled);
			if (!handled) v = clj_eval_node(node);
			clj_release(clj_from_ptr((void *)node));
		} else {
			v = CLJ_THROWN;
		}
	}
	if (file_owned) clj_release(arm.form.file);
	clj_release(expanded);
	return v;
}
