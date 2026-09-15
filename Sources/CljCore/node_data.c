// @ai-generated(guided)
// Tree <-> data. Ids are not encoded: from_data renumbers in the analyzer's pre-order, so a round trip
// through pr-str and read yields the same data again. Grammar:
//   node    = [:const value]              value prints and reads back as itself (see serializable)
//           | [:local slot] | [:captured index] | [:outer [depth slot]]   a slot of the frame depth links up
//           | [:var ns/name]              a var reference, resolved or interned on read
//           | [:the-var ns/name]          the var itself, from (var x)
//           | [:if test then else?]
//           | [:do node+] | [:vector node*] | [:map node*]      map alternates key, value
//           | [:let bindings body] | [:loop bindings body]     bindings = [[slot init]*]
//           | [:recur [slot*] [arg*]]
//           | [:fn name-or-nil [arity+] [capture*]]
//           | [:direct-fn name-or-nil [arity+]]      only as a let/loop init; its body links to that frame
//           | [:direct-call [slot depth] arg*]        the direct fn bound at slot of the frame depth links up
//           | [:invoke f arg*]
//           | [:intrinsic ns/name arg*]     a listed core var at the arity of the args; unknown pairs are refused
//           | [:fused [ns/name+] [arg*] fused original]   guard vars of the fusion table; the programs read the
//                                                          args as locals 0..n-1 (optimizer.c)
//           | [:def ns/name init-or-nil meta macro dynamic]
//           | [:try body [catch*] finally-or-nil]
//           | [:throw node]
//   arity   = [nparams variadic self-slot-or-nil nslots body]
//   capture = [:local slot] | [:captured index] | [:outer [depth slot]]  where the closure takes the value from
//   catch   = [:all slot handler] | [:error slot handler]
// A node with a position carries `line column` as two trailing fixnums (line > 0), after everything
// else; no node kind ends in a fixnum otherwise, so the pair is unambiguous and omitted when unknown.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/cons.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/number.h"
#include "clj/printer.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "node.h"

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_const, kw_local, kw_captured, kw_outer, kw_var, kw_the_var, kw_if, kw_do, kw_let, kw_loop, kw_recur, kw_fn,
	kw_direct_fn, kw_direct_call, kw_invoke, kw_intrinsic, kw_fused, kw_def, kw_vector, kw_map, kw_try, kw_throw, kw_all, kw_error;

static void intern_keywords(void) {
	kw_const = clj_keyword_from_cstr("const");
	kw_local = clj_keyword_from_cstr("local");
	kw_captured = clj_keyword_from_cstr("captured");
	kw_outer = clj_keyword_from_cstr("outer");
	kw_direct_fn = clj_keyword_from_cstr("direct-fn");
	kw_direct_call = clj_keyword_from_cstr("direct-call");
	kw_var = clj_keyword_from_cstr("var");
	kw_the_var = clj_keyword_from_cstr("the-var");
	kw_if = clj_keyword_from_cstr("if");
	kw_do = clj_keyword_from_cstr("do");
	kw_let = clj_keyword_from_cstr("let");
	kw_loop = clj_keyword_from_cstr("loop");
	kw_recur = clj_keyword_from_cstr("recur");
	kw_fn = clj_keyword_from_cstr("fn");
	kw_invoke = clj_keyword_from_cstr("invoke");
	kw_intrinsic = clj_keyword_from_cstr("intrinsic");
	kw_fused = clj_keyword_from_cstr("fused");
	kw_def = clj_keyword_from_cstr("def");
	kw_vector = clj_keyword_from_cstr("vector");
	kw_map = clj_keyword_from_cstr("map");
	kw_try = clj_keyword_from_cstr("try");
	kw_throw = clj_keyword_from_cstr("throw");
	kw_all = clj_keyword_from_cstr("all");
	kw_error = clj_keyword_from_cstr("error");
}

static void *zalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

// ---- encoding

// A vector of owned items, released here; CLJ_THROWN when any item is, with the rest released.
static clj_value vec_take(clj_value *items, uint32_t n) {
	bool ok = true;
	for (uint32_t i = 0; i < n; i++) ok = ok && items[i] != CLJ_THROWN;
	clj_value v = ok ? clj_vector_from_array(items, n) : CLJ_THROWN;
	for (uint32_t i = 0; i < n; i++) clj_release(items[i]);
	return v;
}

static clj_value vec2(clj_value a, clj_value b) {
	clj_value items[2] = {a, b};
	return vec_take(items, 2);
}

static clj_value vec3(clj_value a, clj_value b, clj_value c) {
	clj_value items[3] = {a, b, c};
	return vec_take(items, 3);
}

static clj_value vec4(clj_value a, clj_value b, clj_value c, clj_value d) {
	clj_value items[4] = {a, b, c, d};
	return vec_take(items, 4);
}

static clj_value qualified(clj_value var) {
	return clj_symbol_new(clj_symbol_name(clj_var_ns(var)), clj_symbol_name(clj_var_name(var)));
}

static clj_value not_serializable(clj_value v) { return clj_throw_msg("not serializable: %s", clj_type_name(v)); }

static bool serializable(clj_value v);

static bool serializable_item(clj_value item, void *ctx) {
	bool *ok = ctx;
	*ok = serializable(item);
	return *ok;
}

static bool serializable_entry(clj_value key, clj_value val, void *ctx) {
	return serializable_item(key, ctx) && serializable_item(val, ctx);
}

// What the printer writes and the reader hands back as an equal value; a seq comes back as a list.
static bool serializable(clj_value v) {
	if (!clj_is_ptr(v) || clj_is_double(v) || clj_is_string(v) || clj_is_keyword(v) || clj_is_symbol(v)) return true;
	bool ok = true;
	if (clj_is_vector(v)) {
		clj_vector_each(v, serializable_item, &ok);
	} else if (clj_is_map(v)) {
		clj_map_each(v, serializable_entry, &ok);
	} else if (clj_is_seq(v)) {
		clj_seq_iter it = clj_seq_iter_start(v);
		clj_value    item;
		while (ok && clj_seq_iter_next(&it, &item)) ok = serializable(item);
		clj_seq_iter_close(&it);
		if (it.thrown) return false;
	} else {
		not_serializable(v);
		return false;
	}
	return ok;
}

static bool foldable_item(clj_value item, void *ctx) {
	bool *ok = ctx;
	*ok = clj_node_foldable(item);
	return *ok;
}

static bool foldable_entry(clj_value key, clj_value val, void *ctx) {
	return foldable_item(key, ctx) && foldable_item(val, ctx);
}

// Stricter than serializable(): a seq that is not a list comes back as a list, a change of type a fold must not make.
// @ai-generated(guided)
bool clj_node_foldable(clj_value v) {
	if (!clj_is_ptr(v) || clj_is_double(v) || clj_is_string(v) || clj_is_keyword(v) || clj_is_symbol(v)) return true;
	bool ok = true;
	if (clj_is_vector(v)) {
		clj_vector_each(v, foldable_item, &ok);
	} else if (clj_is_map(v)) {
		clj_map_each(v, foldable_entry, &ok);
	} else if (clj_is_list(v)) {
		// By cell, not through the iterator: a cons over a lazy tail must not be realized here.
		for (; ok && !clj_is_empty_list(v); v = clj_cons_of(v)->rest) {
			if (!clj_is_ptr(v) || clj_header_of(v)->type != &clj_cons_type) return clj_is_nil(v);
			ok = clj_node_foldable(clj_cons_of(v)->first);
		}
	} else {
		return false;
	}
	return ok;
}

static clj_value encode(const clj_node *n);

static clj_value encode_all(const clj_node *const *nodes, uint32_t n) {
	clj_value *items = zalloc(n, sizeof *items);
	for (uint32_t i = 0; i < n; i++) items[i] = encode(nodes[i]);
	clj_value v = vec_take(items, n);
	free(items);
	return v;
}

// A vector of [head ...items]: head is immortal (a keyword) and items are owned.
static clj_value tagged(clj_value head, const clj_node *const *nodes, uint32_t n) {
	clj_value *items = zalloc(n + 1, sizeof *items);
	items[0] = head;
	for (uint32_t i = 0; i < n; i++) items[i + 1] = encode(nodes[i]);
	clj_value v = vec_take(items, n + 1);
	free(items);
	return v;
}

static clj_value encode_opt(const clj_node *n) { return n ? encode(n) : CLJ_NIL; }

static clj_value encode_bindings(const clj_node *n) {
	clj_value *items = zalloc(n->u.let.n, sizeof *items);
	for (uint32_t i = 0; i < n->u.let.n; i++) items[i] = vec2(clj_fixnum(n->u.let.slots[i]), encode(n->u.let.inits[i]));
	clj_value v = vec_take(items, n->u.let.n);
	free(items);
	return v;
}

static clj_value encode_slots(const uint32_t *slots, uint32_t n) {
	clj_value *items = zalloc(n, sizeof *items);
	for (uint32_t i = 0; i < n; i++) items[i] = clj_fixnum(slots[i]);
	clj_value v = vec_take(items, n);
	free(items);
	return v;
}

static clj_value encode_arity(const clj_fn_arity *a) {
	clj_value items[5] = {clj_fixnum(a->nparams), clj_bool(a->variadic), a->self_slot < 0 ? CLJ_NIL : clj_fixnum(a->self_slot),
	                      clj_fixnum(a->nslots), encode(a->body)};
	return vec_take(items, 5);
}

static clj_value encode_arities(const clj_node *n) {
	clj_value arities[CLJ_FN_MAX_FIXED + 2];
	uint32_t  na = 0;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (n->u.fn.fixed[i]) arities[na++] = encode_arity(n->u.fn.fixed[i]);
	}
	if (n->u.fn.variadic) arities[na++] = encode_arity(n->u.fn.variadic);
	return vec_take(arities, na);
}

static clj_value encode_capture(const clj_capture *c) {
	switch (c->kind) {
	case CLJ_CAPTURE_LOCAL: return vec2(kw_local, clj_fixnum(c->index));
	case CLJ_CAPTURE_CAPTURED: return vec2(kw_captured, clj_fixnum(c->index));
	case CLJ_CAPTURE_OUTER: return vec2(kw_outer, vec2(clj_fixnum(c->depth), clj_fixnum(c->index)));
	}
	clj_fatal("unknown capture kind");
}

static clj_value encode_fn(const clj_node *n) {
	if (n->kind == CLJ_NODE_DIRECT_FN) return vec3(kw_direct_fn, clj_retain(n->u.fn.name), encode_arities(n));
	clj_value *captures = zalloc(n->u.fn.ncaptures, sizeof *captures);
	for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) captures[i] = encode_capture(&n->u.fn.captures[i]);
	clj_value v = vec4(kw_fn, clj_retain(n->u.fn.name), encode_arities(n), vec_take(captures, n->u.fn.ncaptures));
	free(captures);
	return v;
}

static clj_value encode_direct_call(const clj_node *n) {
	clj_value *items = zalloc(n->u.direct.n + 2, sizeof *items);
	items[0] = kw_direct_call;
	items[1] = vec2(clj_fixnum(n->u.direct.slot), clj_fixnum(n->u.direct.depth));
	for (uint32_t i = 0; i < n->u.direct.n; i++) items[i + 2] = encode(n->u.direct.args[i]);
	clj_value v = vec_take(items, n->u.direct.n + 2);
	free(items);
	return v;
}

static clj_value encode_try(const clj_node *n) {
	clj_value *catches = zalloc(n->u.try_.ncatches, sizeof *catches);
	for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
		const clj_catch *c = &n->u.try_.catches[i];
		catches[i] = vec3(c->kind == CLJ_CATCH_ALL ? kw_all : kw_error, clj_fixnum(c->slot), encode(c->handler));
	}
	clj_value v = vec4(kw_try, encode(n->u.try_.body), vec_take(catches, n->u.try_.ncatches), encode_opt(n->u.try_.finally_));
	free(catches);
	return v;
}

static clj_value encode_fused(const clj_node *n) {
	clj_value *names = zalloc(n->u.fused.nguards, sizeof *names);
	for (uint32_t i = 0; i < n->u.fused.nguards; i++) names[i] = clj_symbol_from_cstr(n->u.fused.guards[i]->name);
	clj_value items[5] = {kw_fused, vec_take(names, n->u.fused.nguards), encode_all(n->u.fused.args, n->u.fused.nargs), encode(n->u.fused.fused),
	                      encode(n->u.fused.original)};
	free(names);
	return vec_take(items, 5);
}

static clj_value encode_kind(const clj_node *n);

// @ai-generated(guided)
static clj_value encode(const clj_node *n) {
	clj_value v = encode_kind(n);
	if (v == CLJ_THROWN || !n->line) return v;
	v = clj_vector_conj(v, clj_fixnum(n->line));
	return clj_vector_conj(v, clj_fixnum(n->col));
}

static clj_value encode_kind(const clj_node *n) {
	switch (n->kind) {
	case CLJ_NODE_CONST:
		if (clj_is_var(n->u.value)) return vec2(kw_the_var, qualified(n->u.value));
		if (!serializable(n->u.value)) return CLJ_THROWN;
		return vec2(kw_const, clj_retain(n->u.value));
	case CLJ_NODE_LOCAL: return vec2(kw_local, clj_fixnum(n->u.index));
	case CLJ_NODE_CAPTURED: return vec2(kw_captured, clj_fixnum(n->u.index));
	case CLJ_NODE_OUTER: return vec2(kw_outer, vec2(clj_fixnum(n->u.outer.depth), clj_fixnum(n->u.outer.index)));
	case CLJ_NODE_VAR: return vec2(kw_var, qualified(n->u.var));
	case CLJ_NODE_IF: {
		const clj_node *arms[3] = {n->u.if_.test, n->u.if_.then, n->u.if_.else_};
		return tagged(kw_if, arms, n->u.if_.else_ ? 3 : 2);
	}
	case CLJ_NODE_DO: return tagged(kw_do, n->u.seq.items, n->u.seq.n);
	case CLJ_NODE_VECTOR: return tagged(kw_vector, n->u.seq.items, n->u.seq.n);
	case CLJ_NODE_MAP: return tagged(kw_map, n->u.seq.items, n->u.seq.n);
	case CLJ_NODE_LET: return vec3(kw_let, encode_bindings(n), encode(n->u.let.body));
	case CLJ_NODE_LOOP: return vec3(kw_loop, encode_bindings(n), encode(n->u.let.body));
	case CLJ_NODE_RECUR: return vec3(kw_recur, encode_slots(n->u.recur.slots, n->u.recur.n), encode_all(n->u.recur.args, n->u.recur.n));
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return encode_fn(n);
	case CLJ_NODE_DIRECT_CALL: return encode_direct_call(n);
	case CLJ_NODE_INVOKE: {
		clj_value *items = zalloc(n->u.invoke.n + 2, sizeof *items);
		items[0] = kw_invoke;
		items[1] = encode(n->u.invoke.fn);
		for (uint32_t i = 0; i < n->u.invoke.n; i++) items[i + 2] = encode(n->u.invoke.args[i]);
		clj_value v = vec_take(items, n->u.invoke.n + 2);
		free(items);
		return v;
	}
	case CLJ_NODE_DEF: {
		clj_value items[6] = {kw_def, qualified(n->u.def.var), encode_opt(n->u.def.init), encode(n->u.def.meta), clj_bool(n->u.def.macro),
		                      clj_bool(n->u.def.dynamic)};
		return vec_take(items, 6);
	}
	case CLJ_NODE_TRY: return encode_try(n);
	case CLJ_NODE_THROW: return vec2(kw_throw, encode(n->u.throw_));
	case CLJ_NODE_INTRINSIC: {
		clj_value *items = zalloc(n->u.intrinsic.n + 2, sizeof *items);
		items[0] = kw_intrinsic;
		items[1] = qualified(n->u.intrinsic.var);
		for (uint32_t i = 0; i < n->u.intrinsic.n; i++) items[i + 2] = encode(n->u.intrinsic.args[i]);
		clj_value v = vec_take(items, n->u.intrinsic.n + 2);
		free(items);
		return v;
	}
	case CLJ_NODE_FUSED: return encode_fused(n);
	}
	clj_fatal("unknown node kind");
}

clj_value clj_node_to_data(const clj_node *root) {
	pthread_once(&keywords_once, intern_keywords);
	return encode(root);
}

// ---- decoding

static clj_node *fail_data(clj_value data, const char *what) {
	clj_value text = clj_pr_str(data);
	if (text == CLJ_THROWN) return NULL;
	clj_throw_msg("malformed node data, %s: %s", what, clj_string_bytes(text));
	clj_release(text);
	return NULL;
}

static bool is_vector_of(clj_value v, uint32_t min) { return clj_is_vector(v) && clj_vector_count(v) >= min; }

static bool as_u32(clj_value v, uint32_t *out) {
	if (!clj_is_fixnum(v) || clj_fixnum_val(v) < 0 || clj_fixnum_val(v) > UINT32_MAX) return false;
	*out = (uint32_t)clj_fixnum_val(v);
	return true;
}

// Borrowed: the var of a qualified symbol, interned in its namespace when new; nil for anything else.
static clj_value var_named(clj_value sym) {
	if (!clj_is_symbol(sym) || clj_is_nil(clj_symbol_ns(sym))) return CLJ_NIL;
	clj_value var = clj_ns_resolve(clj_ns_current(), sym);
	if (!clj_is_nil(var)) return var;
	clj_value ns_name = clj_symbol_new(CLJ_NIL, clj_symbol_ns(sym)), name = clj_symbol_new(CLJ_NIL, clj_symbol_name(sym));
	var = clj_ns_intern(clj_ns_find_or_create(ns_name), name);
	clj_release(ns_name);
	clj_release(name);
	return var;
}

// The direct fns bound so far in one frame, and the frame's static link: a direct call names its fn by the
// slot of the frame `depth` links up. A closure body starts a chain of its own (nothing crosses it).
typedef struct dframe {
	const struct dframe *outer;
	const clj_node     **fns;   // indexed by slot; NULL where no direct fn is bound
	uint32_t             nfns;
} dframe;

static void dframe_bind(dframe *fr, uint32_t slot, const clj_node *fn) {
	if (slot >= fr->nfns) {
		uint32_t n = slot + 1 > 2 * fr->nfns ? slot + 1 : 2 * fr->nfns;
		fr->fns = realloc(fr->fns, n * sizeof *fr->fns);
		if (!fr->fns) clj_fatal("out of memory");
		memset(fr->fns + fr->nfns, 0, (n - fr->nfns) * sizeof *fr->fns);
		fr->nfns = n;
	}
	fr->fns[slot] = fn;
}

static const clj_node *dframe_find(const dframe *fr, uint32_t depth, uint32_t slot) {
	while (depth-- && fr) fr = fr->outer;
	return fr && slot < fr->nfns ? fr->fns[slot] : NULL;
}

static clj_node *decode(clj_value data, dframe *fr);
static clj_node *decode_bound(clj_value data, dframe *fr, uint32_t slot);

static bool decode_into(const clj_node **out, clj_value vec, uint32_t from, uint32_t n, dframe *fr) {
	for (uint32_t i = 0; i < n; i++) {
		out[i] = decode(clj_vector_nth(vec, from + i), fr);
		if (!out[i]) return false;
	}
	return true;
}

static clj_node *drop(clj_node *n) {
	clj_release(clj_from_ptr(n));
	return NULL;
}

static clj_node *decode_seq(clj_node_kind kind, clj_value data, uint32_t min, dframe *fr) {
	uint32_t count = clj_vector_count(data);
	if (count < min || (kind == CLJ_NODE_MAP && (count - 1) % 2)) return fail_data(data, "wrong item count");
	clj_node *n = clj_node_alloc(kind);
	n->u.seq.n = count - 1;
	n->u.seq.items = zalloc(n->u.seq.n, sizeof *n->u.seq.items);
	return decode_into(n->u.seq.items, data, 1, n->u.seq.n, fr) ? n : drop(n);
}

static bool decode_arities(clj_node *fn, clj_value arities, dframe *fr);

// Bound to its slot before its arities are read: a self-call inside them names that slot one link up.
static clj_node *decode_direct_fn(clj_value data, dframe *fr, uint32_t slot) {
	if (clj_vector_count(data) != 3 || !(clj_is_nil(clj_vector_nth(data, 1)) || clj_is_symbol(clj_vector_nth(data, 1))) ||
	    !is_vector_of(clj_vector_nth(data, 2), 1))
		return fail_data(data, "expected [name arities]");
	clj_node *n = clj_node_alloc(CLJ_NODE_DIRECT_FN);
	n->u.fn.name = clj_retain(clj_vector_nth(data, 1));
	dframe_bind(fr, slot, n);
	if (!decode_arities(n, clj_vector_nth(data, 2), fr)) return drop(n);
	if (!n->u.fn.variadic) return n;
	fail_data(data, "a direct fn cannot be variadic");
	return drop(n);
}

static clj_node *decode_let(clj_node_kind kind, clj_value data, dframe *fr) {
	if (clj_vector_count(data) != 3 || !clj_is_vector(clj_vector_nth(data, 1))) return fail_data(data, "expected [bindings body]");
	clj_value bindings = clj_vector_nth(data, 1);
	clj_node *n = clj_node_alloc(kind);
	n->u.let.n = clj_vector_count(bindings);
	n->u.let.slots = zalloc(n->u.let.n, sizeof *n->u.let.slots);
	n->u.let.inits = zalloc(n->u.let.n, sizeof *n->u.let.inits);
	for (uint32_t i = 0; i < n->u.let.n; i++) {
		clj_value b = clj_vector_nth(bindings, i);
		if (!is_vector_of(b, 2) || clj_vector_count(b) != 2 || !as_u32(clj_vector_nth(b, 0), &n->u.let.slots[i])) {
			fail_data(b, "expected [slot init]");
			return drop(n);
		}
		if (!(n->u.let.inits[i] = decode_bound(clj_vector_nth(b, 1), fr, n->u.let.slots[i]))) return drop(n);
	}
	return (n->u.let.body = decode(clj_vector_nth(data, 2), fr)) ? n : drop(n);
}

static clj_node *decode_recur(clj_value data, dframe *fr) {
	if (clj_vector_count(data) != 3 || !clj_is_vector(clj_vector_nth(data, 1)) || !clj_is_vector(clj_vector_nth(data, 2)))
		return fail_data(data, "expected [slots args]");
	clj_value slots = clj_vector_nth(data, 1), args = clj_vector_nth(data, 2);
	if (clj_vector_count(slots) != clj_vector_count(args)) return fail_data(data, "slots and args differ in count");
	clj_node *n = clj_node_alloc(CLJ_NODE_RECUR);
	n->u.recur.n = clj_vector_count(slots);
	n->u.recur.slots = zalloc(n->u.recur.n, sizeof *n->u.recur.slots);
	n->u.recur.args = zalloc(n->u.recur.n, sizeof *n->u.recur.args);
	for (uint32_t i = 0; i < n->u.recur.n; i++) {
		if (!as_u32(clj_vector_nth(slots, i), &n->u.recur.slots[i])) {
			fail_data(slots, "expected slot numbers");
			return drop(n);
		}
	}
	return decode_into(n->u.recur.args, args, 0, n->u.recur.n, fr) ? n : drop(n);
}

// A closure's body reads its own environment and slots: a fresh chain. A direct fn's links to its definer.
static bool decode_arity(clj_node *fn, clj_value data, dframe *fr) {
	uint32_t nparams, nslots, self_slot = 0;
	bool     ok = is_vector_of(data, 5) && clj_vector_count(data) == 5 && as_u32(clj_vector_nth(data, 0), &nparams) &&
	          clj_is_bool(clj_vector_nth(data, 1)) && (clj_is_nil(clj_vector_nth(data, 2)) || as_u32(clj_vector_nth(data, 2), &self_slot)) &&
	          as_u32(clj_vector_nth(data, 3), &nslots) && nparams <= CLJ_FN_MAX_FIXED;
	if (!ok) return fail_data(data, "expected [nparams variadic self-slot nslots body]") != NULL;
	bool           variadic = clj_vector_nth(data, 1) == CLJ_TRUE;
	clj_fn_arity **at = variadic ? &fn->u.fn.variadic : &fn->u.fn.fixed[nparams];
	if (*at) return fail_data(data, "duplicate arity") != NULL;
	clj_fn_arity *a = zalloc(1, sizeof *a);
	a->nparams = nparams;
	a->variadic = variadic;
	a->self_slot = clj_is_nil(clj_vector_nth(data, 2)) ? -1 : (int32_t)self_slot;
	a->nslots = nslots;
	*at = a;
	dframe inner = {fn->kind == CLJ_NODE_DIRECT_FN ? fr : NULL, NULL, 0};
	a->body = decode(clj_vector_nth(data, 4), &inner);
	free(inner.fns);
	return a->body != NULL;
}

static bool decode_arities(clj_node *fn, clj_value arities, dframe *fr) {
	for (uint32_t i = 0; i < clj_vector_count(arities); i++) {
		if (!decode_arity(fn, clj_vector_nth(arities, i), fr)) return false;
	}
	return true;
}

// [depth slot] with depth >= 1.
static bool as_outer(clj_value pair, uint32_t *depth, uint32_t *slot) {
	return is_vector_of(pair, 2) && clj_vector_count(pair) == 2 && as_u32(clj_vector_nth(pair, 0), depth) && *depth &&
	       as_u32(clj_vector_nth(pair, 1), slot);
}

static bool decode_capture(clj_capture *c, clj_value data) {
	const char *expected = "expected [:local slot], [:captured index] or [:outer [depth slot]]";
	if (!is_vector_of(data, 2) || clj_vector_count(data) != 2) return fail_data(data, expected) != NULL;
	clj_value head = clj_vector_nth(data, 0);
	if (head == kw_outer) {
		if (!as_outer(clj_vector_nth(data, 1), &c->depth, &c->index)) return fail_data(data, expected) != NULL;
		c->kind = CLJ_CAPTURE_OUTER;
		return true;
	}
	if ((head != kw_local && head != kw_captured) || !as_u32(clj_vector_nth(data, 1), &c->index)) return fail_data(data, expected) != NULL;
	c->kind = head == kw_captured ? CLJ_CAPTURE_CAPTURED : CLJ_CAPTURE_LOCAL;
	c->depth = 0;
	return true;
}

static clj_node *decode_fn(clj_value data, dframe *fr) {
	if (clj_vector_count(data) != 4 || !(clj_is_nil(clj_vector_nth(data, 1)) || clj_is_symbol(clj_vector_nth(data, 1))) ||
	    !is_vector_of(clj_vector_nth(data, 2), 1) || !clj_is_vector(clj_vector_nth(data, 3)))
		return fail_data(data, "expected [name arities captures]");
	clj_value arities = clj_vector_nth(data, 2), captures = clj_vector_nth(data, 3);
	clj_node *n = clj_node_alloc(CLJ_NODE_FN);
	n->u.fn.name = clj_retain(clj_vector_nth(data, 1));
	n->u.fn.ncaptures = clj_vector_count(captures);
	n->u.fn.captures = zalloc(n->u.fn.ncaptures, sizeof *n->u.fn.captures);
	for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
		if (!decode_capture(&n->u.fn.captures[i], clj_vector_nth(captures, i))) return drop(n);
	}
	return decode_arities(n, arities, fr) ? n : drop(n);
}

static clj_node *decode_direct_call(clj_value data, dframe *fr) {
	uint32_t  slot, depth;
	clj_value pair = clj_vector_count(data) >= 2 ? clj_vector_nth(data, 1) : CLJ_NIL;
	if (!is_vector_of(pair, 2) || clj_vector_count(pair) != 2 || !as_u32(clj_vector_nth(pair, 0), &slot) || !as_u32(clj_vector_nth(pair, 1), &depth))
		return fail_data(data, "expected [[slot depth] args*]");
	const clj_node *fn = dframe_find(fr, depth, slot);
	if (!fn) return fail_data(data, "no direct fn bound at that slot");
	uint32_t nargs = clj_vector_count(data) - 2;
	if (nargs > CLJ_FN_MAX_FIXED || !fn->u.fn.fixed[nargs]) return fail_data(data, "the direct fn has no such arity");
	clj_node *n = clj_node_alloc(CLJ_NODE_DIRECT_CALL);
	n->u.direct.fn = fn;
	n->u.direct.arity = fn->u.fn.fixed[nargs];
	n->u.direct.slot = slot;
	n->u.direct.depth = depth;
	n->u.direct.n = nargs;
	n->u.direct.args = zalloc(nargs, sizeof *n->u.direct.args);
	return decode_into(n->u.direct.args, data, 2, nargs, fr) ? n : drop(n);
}

static clj_node *decode_def(clj_value data, dframe *fr) {
	clj_value var = clj_vector_count(data) == 6 ? var_named(clj_vector_nth(data, 1)) : CLJ_NIL;
	if (clj_is_nil(var) || !clj_is_bool(clj_vector_nth(data, 4)) || !clj_is_bool(clj_vector_nth(data, 5)))
		return fail_data(data, "expected [ns/name init meta macro dynamic]");
	clj_node *n = clj_node_alloc(CLJ_NODE_DEF);
	n->u.def.var = clj_retain(var);
	n->u.def.macro = clj_vector_nth(data, 4) == CLJ_TRUE;
	n->u.def.dynamic = clj_vector_nth(data, 5) == CLJ_TRUE;
	clj_value init = clj_vector_nth(data, 2);
	if (!clj_is_nil(init) && !(n->u.def.init = decode(init, fr))) return drop(n);
	return (n->u.def.meta = decode(clj_vector_nth(data, 3), fr)) ? n : drop(n);
}

static bool decode_catch(clj_catch *c, clj_value data, dframe *fr) {
	if (!is_vector_of(data, 3) || clj_vector_count(data) != 3 || !as_u32(clj_vector_nth(data, 1), &c->slot) ||
	    (clj_vector_nth(data, 0) != kw_all && clj_vector_nth(data, 0) != kw_error))
		return fail_data(data, "expected [:all|:error slot handler]") != NULL;
	c->kind = clj_vector_nth(data, 0) == kw_all ? CLJ_CATCH_ALL : CLJ_CATCH_ERROR;
	return (c->handler = decode(clj_vector_nth(data, 2), fr)) != NULL;
}

static clj_node *decode_try(clj_value data, dframe *fr) {
	if (clj_vector_count(data) != 4 || !clj_is_vector(clj_vector_nth(data, 2))) return fail_data(data, "expected [body catches finally]");
	clj_value catches = clj_vector_nth(data, 2), finally_ = clj_vector_nth(data, 3);
	clj_node *n = clj_node_alloc(CLJ_NODE_TRY);
	n->u.try_.catches = zalloc(clj_vector_count(catches), sizeof *n->u.try_.catches);
	if (!(n->u.try_.body = decode(clj_vector_nth(data, 1), fr))) return drop(n);
	for (uint32_t i = 0; i < clj_vector_count(catches); i++) {
		n->u.try_.ncatches = i + 1;
		if (!decode_catch(&n->u.try_.catches[i], clj_vector_nth(catches, i), fr)) return drop(n);
	}
	if (!clj_is_nil(finally_) && !(n->u.try_.finally_ = decode(finally_, fr))) return drop(n);
	return n;
}

static clj_node *decode_leaf(clj_node_kind kind, clj_value data) {
	uint32_t index;
	if (clj_vector_count(data) != 2 || !as_u32(clj_vector_nth(data, 1), &index)) return fail_data(data, "expected a slot number");
	clj_node *n = clj_node_alloc(kind);
	n->u.index = index;
	return n;
}

static clj_node *decode_var(clj_node_kind kind, clj_value data) {
	clj_value var = clj_vector_count(data) == 2 ? var_named(clj_vector_nth(data, 1)) : CLJ_NIL;
	if (clj_is_nil(var)) return fail_data(data, "expected a qualified symbol");
	clj_node *n = clj_node_alloc(kind);
	if (kind == CLJ_NODE_VAR) n->u.var = clj_retain(var);
	else n->u.value = clj_retain(var);
	return n;
}

static clj_node *decode_outer(clj_value data) {
	uint32_t depth, index;
	if (clj_vector_count(data) != 2 || !as_outer(clj_vector_nth(data, 1), &depth, &index)) return fail_data(data, "expected [[depth slot]]");
	clj_node *n = clj_node_alloc(CLJ_NODE_OUTER);
	n->u.outer.depth = depth;
	n->u.outer.index = index;
	return n;
}

static clj_node *decode_single(clj_node_kind kind, clj_value data, dframe *fr) {
	if (clj_vector_count(data) != 2) return fail_data(data, "expected one item");
	clj_node *n = clj_node_alloc(kind);
	if (kind == CLJ_NODE_CONST) {
		n->u.value = clj_retain(clj_vector_nth(data, 1));
		return n;
	}
	return (n->u.throw_ = decode(clj_vector_nth(data, 1), fr)) ? n : drop(n);
}

static clj_node *decode_if(clj_value data, dframe *fr) {
	uint32_t count = clj_vector_count(data);
	if (count != 3 && count != 4) return fail_data(data, "expected [test then else?]");
	clj_node *n = clj_node_alloc(CLJ_NODE_IF);
	if (!(n->u.if_.test = decode(clj_vector_nth(data, 1), fr)) || !(n->u.if_.then = decode(clj_vector_nth(data, 2), fr)) ||
	    (count == 4 && !(n->u.if_.else_ = decode(clj_vector_nth(data, 3), fr))))
		return drop(n);
	return n;
}

static clj_node *decode_invoke(clj_value data, dframe *fr) {
	if (clj_vector_count(data) < 2) return fail_data(data, "expected [f args*]");
	clj_node *n = clj_node_alloc(CLJ_NODE_INVOKE);
	n->u.invoke.n = clj_vector_count(data) - 2;
	n->u.invoke.args = zalloc(n->u.invoke.n, sizeof *n->u.invoke.args);
	if (!(n->u.invoke.fn = decode(clj_vector_nth(data, 1), fr))) return drop(n);
	return decode_into(n->u.invoke.args, data, 2, n->u.invoke.n, fr) ? n : drop(n);
}

static clj_node *decode_intrinsic(clj_value data, dframe *fr) {
	if (clj_vector_count(data) < 3) return fail_data(data, "expected [ns/name args*]");
	uint32_t             nargs = clj_vector_count(data) - 2;
	const clj_intrinsic *op = clj_intrinsic_find_named(clj_vector_nth(data, 1), nargs);
	if (!op) return fail_data(data, "unknown intrinsic");
	clj_node *n = clj_node_alloc(CLJ_NODE_INTRINSIC);
	n->u.intrinsic.op = op;
	n->u.intrinsic.var = clj_retain(clj_intrinsic_var(op));
	n->u.intrinsic.n = nargs;
	n->u.intrinsic.args = zalloc(nargs, sizeof *n->u.intrinsic.args);
	return decode_into(n->u.intrinsic.args, data, 2, nargs, fr) ? n : drop(n);
}

static clj_node *decode_fused(clj_value data, dframe *fr) {
	if (clj_vector_count(data) != 5 || !is_vector_of(clj_vector_nth(data, 1), 1) || !clj_is_vector(clj_vector_nth(data, 2)))
		return fail_data(data, "expected [guards args fused original]");
	clj_value guards = clj_vector_nth(data, 1), args = clj_vector_nth(data, 2);
	clj_node *n = clj_node_alloc(CLJ_NODE_FUSED);
	n->u.fused.nguards = clj_vector_count(guards);
	n->u.fused.guards = zalloc(n->u.fused.nguards, sizeof *n->u.fused.guards);
	for (uint32_t i = 0; i < n->u.fused.nguards; i++) {
		if (!(n->u.fused.guards[i] = clj_fusion_find_named(clj_vector_nth(guards, i)))) {
			fail_data(guards, "unknown fusion var");
			return drop(n);
		}
	}
	n->u.fused.nargs = clj_vector_count(args);
	n->u.fused.args = zalloc(n->u.fused.nargs, sizeof *n->u.fused.args);
	dframe inner = {NULL, NULL, 0};
	if (!decode_into(n->u.fused.args, args, 0, n->u.fused.nargs, fr)) return drop(n);
	if (!(n->u.fused.fused = decode(clj_vector_nth(data, 3), &inner))) return drop(n);
	return (n->u.fused.original = decode(clj_vector_nth(data, 4), &inner)) ? n : drop(n);
}

static clj_node *decode_kind(clj_value data, dframe *fr, uint32_t slot);

// A let/loop init passes its slot down for a direct fn; anywhere else the slot is unused.
// @ai-generated(guided)
static clj_node *decode_bound(clj_value data, dframe *fr, uint32_t slot) {
	if (!is_vector_of(data, 1) || !clj_is_keyword(clj_vector_nth(data, 0))) return fail_data(data, "expected [:kind ...]");
	uint32_t count = clj_vector_count(data), line = 0, col = 0;
	if (count >= 3 && as_u32(clj_vector_nth(data, count - 2), &line) && line > 0 && as_u32(clj_vector_nth(data, count - 1), &col)) {
		clj_value body = clj_vector_pop(clj_vector_pop(clj_retain(data)));
		clj_node *n = decode_kind(body, fr, slot);
		clj_release(body);
		if (n) {
			n->line = line;
			n->col = col;
		}
		return n;
	}
	return decode_kind(data, fr, slot);
}

static clj_node *decode(clj_value data, dframe *fr) { return decode_bound(data, fr, UINT32_MAX); }

static clj_node *decode_kind(clj_value data, dframe *fr, uint32_t slot) {
	clj_value head = clj_vector_nth(data, 0);
	if (head == kw_const) return decode_single(CLJ_NODE_CONST, data, fr);
	if (head == kw_local) return decode_leaf(CLJ_NODE_LOCAL, data);
	if (head == kw_captured) return decode_leaf(CLJ_NODE_CAPTURED, data);
	if (head == kw_outer) return decode_outer(data);
	if (head == kw_var) return decode_var(CLJ_NODE_VAR, data);
	if (head == kw_the_var) return decode_var(CLJ_NODE_CONST, data);
	if (head == kw_if) return decode_if(data, fr);
	if (head == kw_do) return decode_seq(CLJ_NODE_DO, data, 2, fr);
	if (head == kw_vector) return decode_seq(CLJ_NODE_VECTOR, data, 1, fr);
	if (head == kw_map) return decode_seq(CLJ_NODE_MAP, data, 1, fr);
	if (head == kw_let) return decode_let(CLJ_NODE_LET, data, fr);
	if (head == kw_loop) return decode_let(CLJ_NODE_LOOP, data, fr);
	if (head == kw_recur) return decode_recur(data, fr);
	if (head == kw_fn) return decode_fn(data, fr);
	if (head == kw_direct_fn) return slot == UINT32_MAX ? fail_data(data, "direct fn outside a let/loop binding") : decode_direct_fn(data, fr, slot);
	if (head == kw_direct_call) return decode_direct_call(data, fr);
	if (head == kw_invoke) return decode_invoke(data, fr);
	if (head == kw_intrinsic) return decode_intrinsic(data, fr);
	if (head == kw_fused) return decode_fused(data, fr);
	if (head == kw_def) return decode_def(data, fr);
	if (head == kw_try) return decode_try(data, fr);
	if (head == kw_throw) return decode_single(CLJ_NODE_THROW, data, fr);
	return fail_data(data, "unknown node kind");
}

typedef struct bounds {
	uint32_t             nslots, ncaptures; // of the enclosing arity, or the top level
	bool                 ok;
	const struct bounds *outer; // the defining frame of a direct fn body; NULL for a closure or the top level
} bounds;

static void check_bounds(const clj_node *n, void *ctx);

static bool in_range(bounds *b, uint32_t slot) {
	if (slot < b->nslots) return true;
	b->ok = false;
	clj_throw_msg("malformed node data, slot %u outside a frame of %u", slot, b->nslots);
	return false;
}

static bool outer_in_range(bounds *b, uint32_t depth, uint32_t slot) {
	const bounds *at = b;
	for (uint32_t i = 0; i < depth && at; i++) at = at->outer;
	if (at && slot < at->nslots) return true;
	b->ok = false;
	if (at) clj_throw_msg("malformed node data, slot %u outside a frame of %u", slot, at->nslots);
	else clj_throw_msg("malformed node data, no frame %u links up", depth);
	return false;
}

// Every slot an arity's body touches must fit its frame: the frame is sized from nslots, not from the body.
static void check_bounds(const clj_node *n, void *ctx) {
	bounds *b = ctx;
	if (!b->ok) return;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: in_range(b, n->u.index); break;
	case CLJ_NODE_OUTER: outer_in_range(b, n->u.outer.depth, n->u.outer.index); break;
	case CLJ_NODE_CAPTURED:
		if (n->u.index >= b->ncaptures) {
			b->ok = false;
			clj_throw_msg("malformed node data, capture %u outside an environment of %u", n->u.index, b->ncaptures);
		}
		break;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n && in_range(b, n->u.let.slots[i]); i++) {}
		break;
	case CLJ_NODE_RECUR:
		for (uint32_t i = 0; i < n->u.recur.n && in_range(b, n->u.recur.slots[i]); i++) {}
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches && in_range(b, n->u.try_.catches[i].slot); i++) {}
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: {
		for (uint32_t i = 0; i < n->u.fn.ncaptures && b->ok; i++) {
			const clj_capture *c = &n->u.fn.captures[i];
			if (c->kind == CLJ_CAPTURE_OUTER) {
				outer_in_range(b, c->depth, c->index);
				continue;
			}
			if (c->kind == CLJ_CAPTURE_CAPTURED ? c->index < b->ncaptures : c->index < b->nslots) continue;
			b->ok = false;
			clj_throw_msg("malformed node data, capture source %u outside the creating frame", c->index);
		}
		bool direct = n->kind == CLJ_NODE_DIRECT_FN;
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED + 1 && b->ok; i++) {
			const clj_fn_arity *a = i <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[i] : n->u.fn.variadic;
			if (!a) continue;
			bounds   inner = {a->nslots, direct ? b->ncaptures : n->u.fn.ncaptures, true, direct ? b : NULL};
			uint32_t params = a->nparams + (a->variadic ? 1 : 0);
			if (!(params <= a->nslots && (a->self_slot < 0 || in_range(&inner, (uint32_t)a->self_slot)))) {
				b->ok = false;
				if (params > a->nslots) clj_throw_msg("malformed node data, %u params outside a frame of %u", params, a->nslots);
				break;
			}
			check_bounds(a->body, &inner);
			b->ok = inner.ok;
		}
		return;
	}
	case CLJ_NODE_FUSED: {
		for (uint32_t i = 0; i < n->u.fused.nargs && b->ok; i++) check_bounds(n->u.fused.args[i], b);
		bounds inner = {n->u.fused.nargs, 0, b->ok, NULL};
		if (inner.ok) check_bounds(n->u.fused.fused, &inner);
		if (inner.ok) check_bounds(n->u.fused.original, &inner);
		b->ok = inner.ok;
		return;
	}
	default: break;
	}
	if (b->ok) clj_node_children(n, check_bounds, b);
}

clj_node *clj_node_from_data(clj_value data) {
	pthread_once(&keywords_once, intern_keywords);
	dframe    fr = {NULL, NULL, 0};
	clj_node *root = decode(data, &fr);
	free(fr.fns);
	if (!root) return NULL;
	bounds top = {UINT32_MAX, 0, true, NULL};
	check_bounds(root, &top);
	if (!top.ok) return drop(root);
	clj_node_number(root);
	return root;
}
