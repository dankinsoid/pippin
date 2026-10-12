// @ai-generated(guided)
#include "clj/fusion.h"
#include "shadow_internal.h"
#include "stats_internal.h"

#include <stdlib.h>
#include <string.h>

#include "clj/box.h"
#include "clj/coll.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/ice.h"
#include "clj/intrinsics.h"
#include "clj/ns.h"
#include "clj/number.h"
#include "clj/reduce.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"

#define STAGE(n, a) {"clojure.core/" n, CLJ_FUSION_STAGE, a}
#define OTHER(n, r) {"clojure.core/" n, r, 0}

static const clj_fusion_var table[] = {
	STAGE("map", 2),         STAGE("keep", 2),          STAGE("filter", 2),       STAGE("remove", 2),
	STAGE("take", 2),        STAGE("drop", 2),          STAGE("take-while", 2),   STAGE("drop-while", 2),
	STAGE("mapcat", 2),      STAGE("map-indexed", 2),   STAGE("keep-indexed", 2), STAGE("interpose", 2),
	STAGE("dedupe", 1),      STAGE("distinct", 1),
	OTHER("reduce", CLJ_FUSION_CONSUMER),      OTHER("into", CLJ_FUSION_CONSUMER),       OTHER("vec", CLJ_FUSION_CONSUMER),
	OTHER("count", CLJ_FUSION_CONSUMER),       OTHER("fused-reduce*", CLJ_FUSION_DRIVER), OTHER("fused-into*", CLJ_FUSION_DRIVER),
	OTHER("fused-count*", CLJ_FUSION_DRIVER),
	OTHER("get-in", CLJ_FUSION_GUARD),         OTHER("assoc-in", CLJ_FUSION_GUARD),       OTHER("update-in", CLJ_FUSION_GUARD),
	OTHER("seq", CLJ_FUSION_GUARD),            OTHER("first", CLJ_FUSION_GUARD),          OTHER("next", CLJ_FUSION_GUARD),
	OTHER("apply", CLJ_FUSION_GUARD),          OTHER("reduced?", CLJ_FUSION_GUARD),       OTHER("unreduced", CLJ_FUSION_GUARD),
	OTHER("volatile!", CLJ_FUSION_GUARD),      OTHER("deref", CLJ_FUSION_GUARD),          OTHER("vreset!", CLJ_FUSION_GUARD),
	OTHER("reduced", CLJ_FUSION_GUARD),        OTHER("ensure-reduced", CLJ_FUSION_GUARD), OTHER("not", CLJ_FUSION_GUARD),
};

enum { N = sizeof table / sizeof *table };

// Resolved by clj_fusion_install, parallel to the table; zero before it, so nothing matches during boot.
static clj_value vars[N];
static clj_value roots[N];
static bool      enabled = true;

const clj_fusion_var *clj_fusion_find(clj_value var) {
	for (size_t i = 0; i < N; i++) {
		if (vars[i] == var) return &table[i];
	}
	return NULL;
}

const clj_fusion_var *clj_fusion_find_named(clj_value qualified_sym) {
	if (!clj_is_symbol(qualified_sym) || clj_is_nil(clj_symbol_ns(qualified_sym))) return NULL;
	const char *ns = CLJ_STR(clj_symbol_ns(qualified_sym)).bytes, *name = CLJ_STR(clj_symbol_name(qualified_sym)).bytes;
	size_t      ns_len = strlen(ns);
	for (size_t i = 0; i < N; i++) {
		const char *full = table[i].name;
		if (strncmp(full, ns, ns_len) == 0 && full[ns_len] == '/' && strcmp(full + ns_len + 1, name) == 0) return vars[i] ? &table[i] : NULL;
	}
	return NULL;
}

clj_value clj_fusion_var_of(const clj_fusion_var *fv) { return vars[fv - table]; }

clj_value clj_fusion_boot_root(const clj_fusion_var *fv) { return roots[fv - table]; }

// Relaxed loads suffice: a match runs code that reads nothing a rebind published.
bool clj_fusion_guard(const clj_fusion_var *const *fvs, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) {
		size_t k = fvs[i] - table;
		if (clj_var_root_relaxed(vars[k]) != roots[k]) return false;
	}
	return true;
}

void clj_fusion_install(void) {
	clj_value core = clj_ns_core();
	for (size_t i = 0; i < N; i++) {
		clj_value sym = clj_symbol_from_cstr(strchr(table[i].name, '/') + 1);
		clj_value var = clj_ns_resolve(core, sym);
		clj_release(sym);
		if (clj_is_nil(var)) clj_ice("boot/fusion-table", NULL, "%s names no core var", table[i].name);
		clj_value root = clj_var_root(var);
		if (root == CLJ_UNBOUND) clj_ice("boot/fusion-table", NULL, "%s is unbound", table[i].name);
		roots[i] = clj_retain(root);
		vars[i] = var;
	}
}

bool clj_fusion_set_enabled(bool on) {
	bool was = enabled;
	enabled = on;
	return was;
}

bool clj_fusion_enabled(void) { return enabled; }

// ---- the drivers

typedef enum { BOTTOM_REDUCE, BOTTOM_INTO, BOTTOM_COUNT } bottom_kind;

// Owned by its fn, on the heap: a transducer that kept the fn past the reduce meets `done`, not a dead frame.
typedef struct {
	bottom_kind kind;
	clj_call    call;
	clj_value   acc; // owned; CLJ_UNBOUND while a seedless reduce awaits its first value
	int64_t     count;
	bool        done;
} bottom;

// The step of a reduce whose rf consumes: the driver's own reference is what the rf takes, and the result is
// the driver's new one. Nothing a consuming core returns is a `reduced` box.
static clj_value step_consuming(bottom *b, clj_value x) {
	clj_value in[2] = {b->acc, x};
	clj_value r = clj_intrinsic_call_consuming(b->call.consuming, in);
	b->acc = r == CLJ_THROWN ? CLJ_NIL : r;
	return r == CLJ_THROWN ? CLJ_THROWN : CLJ_NIL;
}

// The completion arity answers the accumulator, as transduce's rf does: halt-when's retf reads it there.
static clj_value bottom_fn(void *ctx, const clj_value *args, size_t n) {
	bottom *b = ctx;
	if (n == 1) return b->kind == BOTTOM_COUNT ? clj_fixnum((intptr_t)b->count) : b->acc == CLJ_UNBOUND ? CLJ_NIL : clj_retain(b->acc);
	if (b->done) return clj_throw_msg("reducing fn called after its reduce finished");
	clj_value x = args[1];
	switch (b->kind) {
	case BOTTOM_COUNT:
		b->count++;
		return CLJ_NIL;
	case BOTTOM_INTO: {
		clj_value acc = clj_conj(b->acc, x);
		b->acc = acc == CLJ_THROWN ? CLJ_NIL : acc;
		return acc == CLJ_THROWN ? CLJ_THROWN : CLJ_NIL;
	}
	case BOTTOM_REDUCE: {
		if (b->acc == CLJ_UNBOUND) {
			b->acc = clj_retain(x);
			return CLJ_NIL;
		}
		if (b->call.consuming) return step_consuming(b, x);
		clj_value in[2] = {b->acc, x};
		clj_value r = clj_call_invoke(&b->call, in);
		if (r == CLJ_THROWN) return CLJ_THROWN;
		clj_release(b->acc);
		if (clj_is_reduced(r)) {
			b->acc = clj_retain(clj_reduced_value(r));
			clj_release(r);
			return clj_reduced_new(CLJ_NIL);
		}
		b->acc = r;
		return CLJ_NIL;
	}
	}
	clj_ice("fusion/bottom-kind", NULL, "a driver bottom of kind %u", (unsigned)b->kind);
}

// A compiled site's stages as one step over the bottom (clj_fused_step): what the transducer stack would do, for
// stages that only map and filter, so they see nil as the result too and pass it through.
typedef struct {
	clj_value      bot; // owned
	clj_fused_step step;
	void          *ctx;
} mono;

static clj_value mono_rf(void *ctx, const clj_value *args, size_t n) {
	mono *m = ctx;
	if (n == 1) return clj_invoke(m->bot, args, 1);
	// the step's ctx is the site's frame: a source that kept the fn past its reduce meets this, not a dead frame
	if (!m->step) return clj_throw_msg("reducing fn called after its reduce finished");
	clj_value y, k = m->step(m->ctx, args[1], &y);
	if (k == CLJ_THROWN) return CLJ_THROWN;
	if (k == CLJ_FALSE) return clj_retain(args[0]);
	clj_value in[2] = {args[0], y};
	clj_value r = clj_invoke(m->bot, in, 2);
	clj_release(y);
	return r;
}

static void mono_free(void *ctx) {
	mono *m = ctx;
	clj_release(m->bot);
	free(m);
}

// The caller keeps bot alive across the walk to read its state after; false with the exception pending. The
// transducers see nil as the result, so a non-nil one is theirs (halt-when's reduced map) and completes as
// on the JVM: *done is the completion's answer, owned, which replaces the accumulator. With m, its step stands for xfs.
static bool run(clj_value bot, clj_value coll, clj_value xfs, const mono *m, clj_value *done) {
	*done = CLJ_NIL;
	clj_value rf;
	mono     *own = NULL;
	if (m) {
		own = malloc(sizeof *own);
		if (!own) clj_fatal("out of memory");
		*own = (mono){clj_retain(bot), m->step, m->ctx};
		rf = clj_fn_native_ctx(CLJ_NIL, mono_rf, own, mono_free, 1, 2);
	} else {
		if (!clj_is_vector(xfs)) {
			clj_throw_msg("fused driver expects a vector of transducers, got: %s", clj_type_name(xfs));
			return false;
		}
		rf = clj_retain(bot);
		for (uint32_t i = 0, n = clj_vector_count(xfs); i < n; i++) {
			clj_value next = clj_invoke(clj_vector_nth(xfs, i), &rf, 1);
			clj_release(rf);
			rf = next;
			if (rf == CLJ_THROWN) return false;
		}
	}
	clj_value r = clj_reduce(rf, CLJ_NIL, coll);
	bool      ok = r != CLJ_THROWN;
	if (ok) {
		*done = clj_invoke(rf, &r, 1);
		clj_release(r);
		ok = *done != CLJ_THROWN;
		if (!ok) *done = CLJ_NIL;
	}
	if (own) own->step = NULL;
	clj_release(rf);
	return ok;
}

static bottom *bottom_new(bottom_kind kind) {
	bottom *b = calloc(1, sizeof *b);
	if (!b) clj_fatal("out of memory");
	b->kind = kind;
	b->acc = CLJ_NIL;
	return b;
}

// Runs the walk and hands back the accumulator (owned) or CLJ_THROWN; the fn owns b from here on.
static clj_value drive(bottom *b, clj_value coll, clj_value xfs, const mono *m) {
	if (clj_deadline_tick()) {
		if (b->acc != CLJ_UNBOUND) clj_release(b->acc);
		free(b);
		return CLJ_THROWN;
	}
	clj_value bot = clj_fn_native_ctx(CLJ_NIL, bottom_fn, b, free, 1, 2);
	clj_value done;
	bool      ok = run(bot, coll, xfs, m, &done);
	b->done = true;
	clj_value acc = b->acc;
	b->acc = CLJ_NIL;
	clj_release(bot);
	if (!ok) {
		if (acc != CLJ_UNBOUND) clj_release(acc);
		return CLJ_THROWN;
	}
	if (acc == CLJ_UNBOUND && clj_is_nil(done)) return acc;
	if (acc != CLJ_UNBOUND) clj_release(acc);
	return done;
}

static clj_value fused_reduce(const clj_value *args, size_t n, clj_value coll, clj_value xfs, const mono *m) {
	bottom *b = bottom_new(BOTTOM_REDUCE);
	b->call = clj_call_prepare(args[0], 2);
	b->acc = n == 2 ? clj_retain(args[1]) : CLJ_UNBOUND;
	clj_value acc = drive(b, coll, xfs, m);
	return acc == CLJ_UNBOUND ? clj_invoke(args[0], NULL, 0) : acc;
}

clj_value clj_fused_reduce(const clj_value *args, size_t n) { return fused_reduce(args, n - 2, args[n - 2], args[n - 1], NULL); }

CLJ_UNIT_ENTRY clj_value clj_fused_reduce_step(const clj_value *args, size_t n, clj_value coll, clj_fused_step step, void *ctx) {
	mono m = {CLJ_NIL, step, ctx};
	return fused_reduce(args, n, coll, CLJ_NIL, &m);
}

clj_value clj_fused_into(const clj_value *args, size_t n) {
	(void)n;
	bottom *b = bottom_new(BOTTOM_INTO);
	b->acc = clj_retain(args[0]);
#if CLJ_STATS
	clj_census_builder(1);
	clj_value r = drive(b, args[1], args[2], NULL);
	clj_census_builder(-1);
	return r;
#else
	return drive(b, args[1], args[2], NULL);
#endif
}

CLJ_UNIT_ENTRY clj_value clj_fused_into_step(clj_value to, clj_value coll, clj_fused_step step, void *ctx) {
	bottom *b = bottom_new(BOTTOM_INTO);
	b->acc = clj_retain(to);
	mono m = {CLJ_NIL, step, ctx};
#if CLJ_STATS
	clj_census_builder(1);
	clj_value r = drive(b, coll, CLJ_NIL, &m);
	clj_census_builder(-1);
	return r;
#else
	return drive(b, coll, CLJ_NIL, &m);
#endif
}

clj_value clj_into_xform(clj_value to, clj_value xform, clj_value coll) {
	bottom   *b = bottom_new(BOTTOM_INTO);
	clj_value xfs = clj_vector_from_array(&xform, 1);
	b->acc = clj_retain(to);
	clj_value r = drive(b, coll, xfs, NULL);
	clj_release(xfs);
	return r;
}

static clj_value fused_count(clj_value coll, clj_value xfs, const mono *m) {
	bottom   *b = bottom_new(BOTTOM_COUNT);
	clj_value bot = clj_fn_native_ctx(CLJ_NIL, bottom_fn, b, free, 1, 2);
	clj_value done;
	bool      ok = run(bot, coll, xfs, m, &done);
	b->done = true;
	int64_t count = b->count;
	clj_release(bot);
	clj_release(done);
	return ok ? clj_fixnum(count) : CLJ_THROWN;
}

clj_value clj_fused_count(const clj_value *args, size_t n) {
	(void)n;
	return fused_count(args[0], args[1], NULL);
}

CLJ_UNIT_ENTRY clj_value clj_fused_count_step(clj_value coll, clj_fused_step step, void *ctx) {
	mono m = {CLJ_NIL, step, ctx};
	return fused_count(coll, CLJ_NIL, &m);
}
