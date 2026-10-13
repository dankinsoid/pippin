// @ai-generated(guided)
#include "clj/fusion.h"
#include "ice_internal.h"
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
	OTHER("fused-count*", CLJ_FUSION_DRIVER),      OTHER("fused*", CLJ_FUSION_DRIVER),        OTHER("mapv", CLJ_FUSION_CONSUMER),
	OTHER("sequence", CLJ_FUSION_FORM),        OTHER("comp", CLJ_FUSION_FORM),
	OTHER("get-in", CLJ_FUSION_GUARD),         OTHER("assoc-in", CLJ_FUSION_GUARD),       OTHER("update-in", CLJ_FUSION_GUARD),
	OTHER("seq", CLJ_FUSION_GUARD),            OTHER("first", CLJ_FUSION_GUARD),          OTHER("next", CLJ_FUSION_GUARD),
	OTHER("apply", CLJ_FUSION_GUARD),          OTHER("reduced?", CLJ_FUSION_GUARD),       OTHER("unreduced", CLJ_FUSION_GUARD),
	OTHER("volatile!", CLJ_FUSION_GUARD),      OTHER("deref", CLJ_FUSION_GUARD),          OTHER("vreset!", CLJ_FUSION_GUARD),
	OTHER("reduced", CLJ_FUSION_GUARD),        OTHER("ensure-reduced", CLJ_FUSION_GUARD), OTHER("not", CLJ_FUSION_GUARD),
	OTHER("inc", CLJ_FUSION_GUARD),            OTHER("dec", CLJ_FUSION_GUARD),            OTHER("pos?", CLJ_FUSION_GUARD),
	OTHER("nil?", CLJ_FUSION_GUARD),           OTHER("complement", CLJ_FUSION_GUARD),     OTHER("cat", CLJ_FUSION_GUARD),
	OTHER("preserving-reduced", CLJ_FUSION_GUARD), OTHER("=", CLJ_FUSION_GUARD),          OTHER("contains?", CLJ_FUSION_GUARD),
	OTHER("conj", CLJ_FUSION_GUARD),
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

const clj_fusion_var *clj_fusion_find_named_cstr(const char *qualified) {
	for (size_t i = 0; i < N; i++) {
		if (strcmp(table[i].name, qualified) == 0) return vars[i] ? &table[i] : NULL;
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

// ---- the transducer drivers

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

// The rf is the transducers of xfs over bot. The caller keeps bot alive across the walk to read its state after;
// false with the exception pending. The transducers see nil as the result, so a non-nil one is theirs (halt-when's
// reduced map) and completes as on the JVM: *done is the completion's answer, owned, which replaces the accumulator.
static bool run(clj_value bot, clj_value coll, clj_value xfs, clj_value *done) {
	*done = CLJ_NIL;
	if (!clj_is_vector(xfs)) {
		clj_throw_msg("fused driver expects a vector of transducers, got: %s", clj_type_name(xfs));
		return false;
	}
	clj_value rf = clj_retain(bot);
	for (uint32_t i = 0, n = clj_vector_count(xfs); i < n; i++) {
		clj_value next = clj_invoke(clj_vector_nth(xfs, i), &rf, 1);
		clj_release(rf);
		rf = next;
		if (rf == CLJ_THROWN) return false;
	}
	clj_value r = clj_reduce(rf, CLJ_NIL, coll);
	bool      ok = r != CLJ_THROWN;
	if (ok) {
		*done = clj_invoke(rf, &r, 1);
		clj_release(r);
		ok = *done != CLJ_THROWN;
		if (!ok) *done = CLJ_NIL;
	}
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
static clj_value drive(bottom *b, clj_value coll, clj_value xfs) {
	if (clj_deadline_tick()) {
		if (b->acc != CLJ_UNBOUND) clj_release(b->acc);
		free(b);
		return CLJ_THROWN;
	}
	clj_value done = CLJ_NIL;
	clj_value bot = clj_fn_native_ctx(CLJ_NIL, bottom_fn, b, free, 1, 2);
	bool      ok = run(bot, coll, xfs, &done);
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

clj_value clj_fused_reduce(const clj_value *args, size_t n) {
	bottom *b = bottom_new(BOTTOM_REDUCE);
	b->call = clj_call_prepare(args[0], 2);
	b->acc = n == 4 ? clj_retain(args[1]) : CLJ_UNBOUND;
	clj_value acc = drive(b, args[n - 2], args[n - 1]);
	return acc == CLJ_UNBOUND ? clj_invoke(args[0], NULL, 0) : acc;
}

clj_value clj_fused_into(const clj_value *args, size_t n) {
	(void)n;
	bottom *b = bottom_new(BOTTOM_INTO);
	b->acc = clj_retain(args[0]);
#if CLJ_STATS
	clj_census_builder(1);
	clj_value r = drive(b, args[1], args[2]);
	clj_census_builder(-1);
	return r;
#else
	return drive(b, args[1], args[2]);
#endif
}

clj_value clj_into_xform(clj_value to, clj_value xform, clj_value coll) {
	bottom   *b = bottom_new(BOTTOM_INTO);
	clj_value xfs = clj_vector_from_array(&xform, 1);
	b->acc = clj_retain(to);
	clj_value r = drive(b, coll, xfs);
	clj_release(xfs);
	return r;
}

clj_value clj_fused_count(const clj_value *args, size_t n) {
	(void)n;
	bottom   *b = bottom_new(BOTTOM_COUNT);
	clj_value bot = clj_fn_native_ctx(CLJ_NIL, bottom_fn, b, free, 1, 2);
	clj_value done;
	bool      ok = run(bot, args[0], args[1], &done);
	b->done = true;
	int64_t count = b->count;
	clj_release(bot);
	clj_release(done);
	return ok ? clj_fixnum(count) : CLJ_THROWN;
}

// ---- the push walker: fused* and the compiled sites, one step per input into the consumer's sink

typedef struct cat_rf cat_rf;

// The driver's sink; a step's sink pointer is this, so clj_fused_cat finds the walk's state behind it.
typedef struct {
	clj_fused_sink sink;
	clj_fused_mode mode;
	clj_call       call; // reduce's f
	clj_value      f;    // borrowed
	clj_value      acc;  // owned; CLJ_UNBOUND while a seedless reduce awaits its first value
	int64_t        count;
	bool           halted; // the consumer is done
	clj_value     *buf;    // the seq modes: the current input's outputs, owned
	size_t         nbuf, cap;
	cat_rf        *cat; // the rf a mapcat hands a reduce over a coll that is no vector, kept for the walk
	clj_value      catfn;
} out;

static bool seq_mode(clj_fused_mode m) { return m == CLJ_FUSED_MODE_SEQ_REDUCE || m == CLJ_FUSED_MODE_SEQ_INTO; }

static int consume(out *o, clj_value y) {
	switch (o->mode) {
	case CLJ_FUSED_MODE_COUNT: o->count++; return CLJ_FUSED_GO;
	case CLJ_FUSED_MODE_INTO:
	case CLJ_FUSED_MODE_SEQ_INTO: {
		clj_value acc = clj_conj(o->acc, y);
		o->acc = acc == CLJ_THROWN ? CLJ_NIL : acc;
		return acc == CLJ_THROWN ? CLJ_FUSED_THROWN : CLJ_FUSED_GO;
	}
	case CLJ_FUSED_MODE_REDUCE:
	case CLJ_FUSED_MODE_SEQ_REDUCE: {
		if (o->acc == CLJ_UNBOUND) {
			o->acc = clj_retain(y);
			return CLJ_FUSED_GO;
		}
		clj_value in[2] = {o->acc, y};
		if (o->call.consuming) {
			clj_value r = clj_intrinsic_call_consuming(o->call.consuming, in);
			o->acc = r == CLJ_THROWN ? CLJ_NIL : r;
			return r == CLJ_THROWN ? CLJ_FUSED_THROWN : CLJ_FUSED_GO;
		}
		clj_value r = clj_call_invoke(&o->call, in);
		if (r == CLJ_THROWN) return CLJ_FUSED_THROWN;
		clj_release(o->acc);
		if (clj_is_reduced(r)) {
			o->acc = clj_retain(clj_reduced_value(r));
			clj_release(r);
			o->halted = true;
			return CLJ_FUSED_HALT;
		}
		o->acc = r;
		return CLJ_FUSED_GO;
	}
	case CLJ_FUSED_MODES: break;
	}
	clj_ice("fusion/mode", NULL, "a fused walk of mode %u", (unsigned)o->mode);
}

static int emit_direct(clj_fused_sink *s, clj_value y) { return consume((out *)s, y); }

static int emit_buffered(clj_fused_sink *s, clj_value y) {
	out *o = (out *)s;
	if (o->nbuf == o->cap) {
		o->cap = o->cap ? o->cap * 2 : 8;
		o->buf = realloc(o->buf, o->cap * sizeof *o->buf);
		if (!o->buf) clj_fatal("out of memory");
	}
	o->buf[o->nbuf++] = clj_retain(y);
	return CLJ_FUSED_GO;
}

// The current input's outputs to the consumer, in order, until it is done.
static int drain(out *o) {
	int    r = CLJ_FUSED_GO;
	size_t i = 0;
	for (; i < o->nbuf && r == CLJ_FUSED_GO; i++) {
		r = consume(o, o->buf[i]);
		clj_release(o->buf[i]);
	}
	for (; i < o->nbuf; i++) clj_release(o->buf[i]);
	o->nbuf = 0;
	return r;
}

// A step's HALT keeps the buffer: the lazy seq runs the completion before it hands that input's outputs on.
static int input(out *o, const clj_fused_site *s, const clj_value *xs) {
	int r = s->step(s->ctx, xs, &o->sink);
	if (r != CLJ_FUSED_GO || !seq_mode(o->mode)) return r;
	return drain(o);
}

// Owned by its fn: a user's reduce slot that kept the fn meets `alive`, not a dead walk.
typedef struct {
	out                  *o;
	const clj_fused_site *site;
	int                   status;
	bool                  alive;
} source_rf;

static clj_value source_fn(void *ctx, const clj_value *args, size_t n) {
	(void)n;
	source_rf *s = ctx;
	if (!s->alive) return clj_throw_msg("reducing fn called after its reduce finished");
	int r = input(s->o, s->site, &args[1]);
	if (r == CLJ_FUSED_THROWN) return CLJ_THROWN;
	if (r == CLJ_FUSED_HALT) {
		s->status = CLJ_FUSED_HALT;
		return clj_reduced_new(CLJ_NIL);
	}
	return CLJ_NIL;
}

static int walk_one(out *o, const clj_fused_site *site, clj_value coll) {
	if (clj_is_vector(coll) && !clj_opt_off(CLJ_NO_OPT_INLINE)) {
		// what the vector's reduce slot does, leaf by leaf, without the rf's call per item
		for (uint32_t i = 0, n = clj_vector_count(coll), start, end; i < n;) {
			const clj_slot *run = clj_vector_run(coll, i, &start, &end);
			for (; i < end; i++) {
				int r = input(o, site, &run[i - start].v);
				if (r != CLJ_FUSED_GO) return r;
			}
		}
		return CLJ_FUSED_GO;
	}
	source_rf *s = malloc(sizeof *s);
	if (!s) clj_fatal("out of memory");
	*s = (source_rf){o, site, CLJ_FUSED_GO, true};
	clj_value fn = clj_fn_native_ctx(CLJ_NIL, source_fn, s, free, 2, 2);
	clj_value r = clj_reduce(fn, CLJ_NIL, coll);
	int       status = r == CLJ_THROWN ? CLJ_FUSED_THROWN : s->status;
	s->alive = false;
	clj_release(r);
	clj_release(fn);
	return status;
}

enum { SOURCES_MAX = 3 };

// As map's 2- and 3-coll arities: each coll seq'd per step before the check, so a longer one realizes one item more.
static int walk_many(out *o, const clj_fused_site *site, const clj_value *colls, size_t n) {
	clj_value    seqs[SOURCES_MAX];
	clj_seq_iter it[SOURCES_MAX];
	size_t       k = 0;
	int          r = CLJ_FUSED_GO;
	for (; k < n; k++) {
		seqs[k] = clj_seq(colls[k]);
		if (seqs[k] == CLJ_THROWN) {
			r = CLJ_FUSED_THROWN;
			break;
		}
		it[k] = clj_seq_iter_start(seqs[k]);
	}
	for (bool more = r == CLJ_FUSED_GO; more;) {
		clj_value xs[SOURCES_MAX];
		for (size_t i = 0; i < n; i++) {
			if (clj_seq_iter_next(&it[i], &xs[i])) continue;
			more = false;
			if (it[i].thrown) {
				r = CLJ_FUSED_THROWN;
				break;
			}
		}
		if (more) {
			r = input(o, site, xs);
			more = r == CLJ_FUSED_GO;
		}
	}
	for (size_t i = 0; i < k; i++) {
		clj_seq_iter_close(&it[i]);
		clj_release(seqs[i]);
	}
	return r;
}

struct cat_rf {
	clj_fused_step  cont;
	void           *ctx;
	clj_fused_sink *sink;
	int             status;
	bool            alive;
};

// preserving-reduced's rf: a halt below ends the reduce over this coll and, through the status, the walk.
static clj_value cat_fn(void *ctx, const clj_value *args, size_t n) {
	(void)n;
	cat_rf *c = ctx;
	if (!c->alive) return clj_throw_msg("reducing fn called after its reduce finished");
	int r = c->cont ? c->cont(c->ctx, &args[1], c->sink) : c->sink->emit(c->sink, args[1]);
	if (r == CLJ_FUSED_THROWN) return CLJ_THROWN;
	if (r == CLJ_FUSED_HALT) {
		c->status = CLJ_FUSED_HALT;
		return clj_reduced_new(CLJ_NIL);
	}
	return CLJ_NIL;
}

static void cat_drop(out *o) {
	if (!o->cat) return;
	o->cat->alive = false;
	o->cat = NULL;
	clj_release(o->catfn);
	o->catfn = CLJ_NIL;
}

CLJ_UNIT_ENTRY int clj_fused_cat(void *ctx, clj_value coll, clj_fused_step cont, clj_fused_sink *sink) {
	if (clj_is_nil(coll)) return CLJ_FUSED_GO;
	if (clj_is_vector(coll) && !clj_opt_off(CLJ_NO_OPT_INLINE)) {
		for (uint32_t i = 0, n = clj_vector_count(coll), start, end; i < n;) {
			const clj_slot *run = clj_vector_run(coll, i, &start, &end);
			for (; i < end; i++) {
				int r = cont ? cont(ctx, &run[i - start].v, sink) : sink->emit(sink, run[i - start].v);
				if (r != CLJ_FUSED_GO) return r;
			}
		}
		return CLJ_FUSED_GO;
	}
	out *o = (out *)sink;
	if (!o->cat || o->cat->cont != cont || o->cat->ctx != ctx) {
		cat_drop(o);
		cat_rf *c = malloc(sizeof *c);
		if (!c) clj_fatal("out of memory");
		*c = (cat_rf){cont, ctx, sink, CLJ_FUSED_GO, true};
		o->cat = c;
		o->catfn = clj_fn_native_ctx(CLJ_NIL, cat_fn, c, free, 2, 2);
	}
	cat_rf   *c = o->cat;
	clj_value fn = clj_retain(o->catfn);
	c->status = CLJ_FUSED_GO;
	clj_value r = clj_reduce(fn, CLJ_NIL, coll);
	int       status = r == CLJ_THROWN ? CLJ_FUSED_THROWN : c->status;
	clj_release(r);
	clj_release(fn);
	return status;
}

CLJ_UNIT_ENTRY clj_value clj_fused_run_step(clj_fused_mode mode, const clj_value *lead, size_t nlead, const clj_value *colls, size_t ncolls,
                                            const clj_fused_site *site) {
	if (clj_deadline_tick()) return CLJ_THROWN;
	bool seq = seq_mode(mode);
	out  o = {.sink = {seq ? emit_buffered : emit_direct}, .mode = mode, .f = CLJ_NIL, .acc = CLJ_NIL, .catfn = CLJ_NIL};
	switch (mode) {
	case CLJ_FUSED_MODE_REDUCE:
	case CLJ_FUSED_MODE_SEQ_REDUCE:
		CLJ_ASSERT(nlead == 1 || nlead == 2, "a fused reduce takes f [init]");
		o.f = lead[0];
		o.call = clj_call_prepare(lead[0], 2);
		o.acc = nlead == 2 ? clj_retain(lead[1]) : CLJ_UNBOUND;
		break;
	case CLJ_FUSED_MODE_INTO:
	case CLJ_FUSED_MODE_SEQ_INTO:
		CLJ_ASSERT(nlead == 1, "a fused into takes to");
		o.acc = clj_retain(lead[0]);
		break;
	case CLJ_FUSED_MODE_COUNT: CLJ_ASSERT(nlead == 0, "a fused count takes no lead"); break;
	case CLJ_FUSED_MODES: clj_ice("fusion/mode", NULL, "a fused walk of mode %u", (unsigned)mode);
	}
	CLJ_ASSERT(ncolls >= 1 && ncolls <= SOURCES_MAX, "a fused walk over 1 to 3 colls");
#if CLJ_STATS
	if (mode == CLJ_FUSED_MODE_INTO || mode == CLJ_FUSED_MODE_SEQ_INTO) clj_census_builder(1);
#endif
	int r = ncolls == 1 ? walk_one(&o, site, colls[0]) : walk_many(&o, site, colls, ncolls);
	// once the consumer is done the lazy seq is pulled no further, so its completion never runs
	if (r != CLJ_FUSED_THROWN && site->flush && !(seq && o.halted)) r = site->flush(site->ctx, &o.sink) == CLJ_FUSED_THROWN ? CLJ_FUSED_THROWN : r;
	if (r != CLJ_FUSED_THROWN && seq && !o.halted) r = drain(&o);
#if CLJ_STATS
	if (mode == CLJ_FUSED_MODE_INTO || mode == CLJ_FUSED_MODE_SEQ_INTO) clj_census_builder(-1);
#endif
	for (size_t i = 0; i < o.nbuf; i++) clj_release(o.buf[i]);
	free(o.buf);
	cat_drop(&o);
	if (r == CLJ_FUSED_THROWN) {
		if (o.acc != CLJ_UNBOUND) clj_release(o.acc);
		return CLJ_THROWN;
	}
	if (mode == CLJ_FUSED_MODE_COUNT) return clj_fixnum((intptr_t)o.count);
	return o.acc == CLJ_UNBOUND ? clj_invoke(o.f, NULL, 0) : o.acc;
}

// ---- fused* over the transducer stack: the stack is the step, its bottom the walk's sink

// Owned by the bottom fn: a transducer that kept its rf past the walk meets `alive`, not a dead sink.
typedef struct {
	clj_fused_sink *sink;
	bool            alive;
} xf_bottom;

static clj_value xf_bottom_fn(void *ctx, const clj_value *args, size_t n) {
	xf_bottom *b = ctx;
	if (n < 2) return CLJ_NIL;
	if (!b->alive) return clj_throw_msg("reducing fn called after its reduce finished");
	int r = b->sink->emit(b->sink, args[1]);
	return r == CLJ_FUSED_THROWN ? CLJ_THROWN : r == CLJ_FUSED_HALT ? clj_reduced_new(CLJ_NIL) : CLJ_NIL;
}

typedef struct {
	clj_value rf;
	uint32_t  nin;
} xf_site;

static int xf_step(void *ctx, const clj_value *xs, clj_fused_sink *sink) {
	(void)sink;
	const xf_site *x = ctx;
	clj_value      in[1 + SOURCES_MAX] = {CLJ_NIL};
	for (uint32_t i = 0; i < x->nin; i++) in[1 + i] = xs[i];
	clj_value r = clj_invoke(x->rf, in, 1 + x->nin);
	if (r == CLJ_THROWN) return CLJ_FUSED_THROWN;
	bool halt = clj_is_reduced(r);
	clj_release(r);
	return halt ? CLJ_FUSED_HALT : CLJ_FUSED_GO;
}

static int xf_flush(void *ctx, clj_fused_sink *sink) {
	(void)sink;
	const xf_site *x = ctx;
	clj_value      nil = CLJ_NIL;
	clj_value      r = clj_invoke(x->rf, &nil, 1);
	if (r == CLJ_THROWN) return CLJ_FUSED_THROWN;
	clj_release(r);
	return CLJ_FUSED_GO;
}

// The walk's sink lives in clj_fused_run_step's frame, so each step binds the bottom to it.
typedef struct {
	xf_site    x;
	xf_bottom *bottom;
} xf_walk;

static int xf_walk_step(void *ctx, const clj_value *xs, clj_fused_sink *sink) {
	xf_walk *w = ctx;
	w->bottom->sink = sink;
	return xf_step(&w->x, xs, sink);
}

static int xf_walk_flush(void *ctx, clj_fused_sink *sink) {
	xf_walk *w = ctx;
	w->bottom->sink = sink;
	return xf_flush(&w->x, sink);
}

clj_value clj_fused_run(const clj_value *args, size_t n) {
	CLJ_ASSERT(n >= 4, "fused* takes mode lead xfs coll & colls");
	clj_value mode = args[0], lead = args[1], xfs = args[2];
	size_t    ncolls = n - 3;
	if (!clj_is_fixnum(mode) || clj_fixnum_val(mode) < 0 || clj_fixnum_val(mode) >= CLJ_FUSED_MODES)
		return clj_throw_msg("fused* expects a mode below %d, got: %s", (int)CLJ_FUSED_MODES, clj_type_name(mode));
	if (!clj_is_vector(lead) || !clj_is_vector(xfs)) return clj_throw_msg("fused* expects vectors of the lead args and the transducers");
	if (ncolls > SOURCES_MAX) return clj_throw_msg("fused* walks at most %d colls", SOURCES_MAX);
	clj_fused_mode m = (clj_fused_mode)clj_fixnum_val(mode);
	uint32_t       nlead = clj_vector_count(lead);
	bool           reduce = m == CLJ_FUSED_MODE_REDUCE || m == CLJ_FUSED_MODE_SEQ_REDUCE;
	if (reduce ? nlead != 1 && nlead != 2 : nlead != (m == CLJ_FUSED_MODE_COUNT ? 0u : 1u)) return clj_throw_msg("fused* got %u lead args for mode %d", nlead, (int)m);
	xf_bottom *b = malloc(sizeof *b);
	if (!b) clj_fatal("out of memory");
	*b = (xf_bottom){NULL, true};
	// bot is held to the end: b dies with it, and a transducer need not keep its rf
	clj_value bot = clj_fn_native_ctx(CLJ_NIL, xf_bottom_fn, b, free, 0, 2);
	clj_value rf = clj_retain(bot);
	for (uint32_t i = 0, nx = clj_vector_count(xfs); i < nx; i++) {
		clj_value next = clj_invoke(clj_vector_nth(xfs, i), &rf, 1);
		clj_release(rf);
		rf = next;
		if (rf == CLJ_THROWN) {
			b->alive = false;
			clj_release(bot);
			return CLJ_THROWN;
		}
	}
	clj_value ld[2];
	for (uint32_t i = 0; i < nlead; i++) ld[i] = clj_vector_nth(lead, i);
	xf_walk        w = {{rf, (uint32_t)ncolls}, b};
	clj_fused_site site = {xf_walk_step, xf_walk_flush, &w};
	clj_value      r = clj_fused_run_step(m, ld, nlead, args + 3, ncolls, &site);
	b->alive = false;
	clj_release(rf);
	clj_release(bot);
	return r;
}
