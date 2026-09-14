// @ai-generated(guided)
// Tree <-> data. Ids are not encoded: from_data renumbers in the analyzer's pre-order, so a round trip
// through pr-str and read yields the same data again. Grammar:
//   node    = [:const value]              value prints and reads back as itself (see serializable)
//           | [:local slot] | [:captured index]
//           | [:var ns/name]              a var reference, resolved or interned on read
//           | [:the-var ns/name]          the var itself, from (var x)
//           | [:if test then else?]
//           | [:do node+] | [:vector node*] | [:map node*]      map alternates key, value
//           | [:let bindings body] | [:loop bindings body]     bindings = [[slot init]*]
//           | [:recur [slot*] [arg*]]
//           | [:fn name-or-nil [arity+] [capture*]]
//           | [:invoke f arg*]
//           | [:def ns/name init-or-nil meta macro dynamic]
//           | [:try body [catch*] finally-or-nil]
//           | [:throw node]
//   arity   = [nparams variadic self-slot-or-nil nslots body]
//   capture = [:local slot] | [:captured index]                 where the closure takes the value from
//   catch   = [:all slot handler] | [:error slot handler]
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "clj/coll.h"
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
static clj_value      kw_const, kw_local, kw_captured, kw_var, kw_the_var, kw_if, kw_do, kw_let, kw_loop, kw_recur, kw_fn,
	kw_invoke, kw_def, kw_vector, kw_map, kw_try, kw_throw, kw_all, kw_error;

static void intern_keywords(void) {
	kw_const = clj_keyword_from_cstr("const");
	kw_local = clj_keyword_from_cstr("local");
	kw_captured = clj_keyword_from_cstr("captured");
	kw_var = clj_keyword_from_cstr("var");
	kw_the_var = clj_keyword_from_cstr("the-var");
	kw_if = clj_keyword_from_cstr("if");
	kw_do = clj_keyword_from_cstr("do");
	kw_let = clj_keyword_from_cstr("let");
	kw_loop = clj_keyword_from_cstr("loop");
	kw_recur = clj_keyword_from_cstr("recur");
	kw_fn = clj_keyword_from_cstr("fn");
	kw_invoke = clj_keyword_from_cstr("invoke");
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

static clj_value encode_fn(const clj_node *n) {
	clj_value arities[CLJ_FN_MAX_FIXED + 2];
	uint32_t  na = 0;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (n->u.fn.fixed[i]) arities[na++] = encode_arity(n->u.fn.fixed[i]);
	}
	if (n->u.fn.variadic) arities[na++] = encode_arity(n->u.fn.variadic);
	clj_value *captures = zalloc(n->u.fn.ncaptures, sizeof *captures);
	for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
		const clj_capture *c = &n->u.fn.captures[i];
		captures[i] = vec2(c->from_captured ? kw_captured : kw_local, clj_fixnum(c->index));
	}
	clj_value v = vec4(kw_fn, clj_retain(n->u.fn.name), vec_take(arities, na), vec_take(captures, n->u.fn.ncaptures));
	free(captures);
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

static clj_value encode(const clj_node *n) {
	switch (n->kind) {
	case CLJ_NODE_CONST:
		if (clj_is_var(n->u.value)) return vec2(kw_the_var, qualified(n->u.value));
		if (!serializable(n->u.value)) return CLJ_THROWN;
		return vec2(kw_const, clj_retain(n->u.value));
	case CLJ_NODE_LOCAL: return vec2(kw_local, clj_fixnum(n->u.index));
	case CLJ_NODE_CAPTURED: return vec2(kw_captured, clj_fixnum(n->u.index));
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
	case CLJ_NODE_FN: return encode_fn(n);
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

static clj_node *decode(clj_value data);

static bool decode_into(const clj_node **out, clj_value vec, uint32_t from, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) {
		out[i] = decode(clj_vector_nth(vec, from + i));
		if (!out[i]) return false;
	}
	return true;
}

static clj_node *drop(clj_node *n) {
	clj_release(clj_from_ptr(n));
	return NULL;
}

static clj_node *decode_seq(clj_node_kind kind, clj_value data, uint32_t min) {
	uint32_t count = clj_vector_count(data);
	if (count < min || (kind == CLJ_NODE_MAP && (count - 1) % 2)) return fail_data(data, "wrong item count");
	clj_node *n = clj_node_alloc(kind);
	n->u.seq.n = count - 1;
	n->u.seq.items = zalloc(n->u.seq.n, sizeof *n->u.seq.items);
	return decode_into(n->u.seq.items, data, 1, n->u.seq.n) ? n : drop(n);
}

static clj_node *decode_let(clj_node_kind kind, clj_value data) {
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
		if (!(n->u.let.inits[i] = decode(clj_vector_nth(b, 1)))) return drop(n);
	}
	return (n->u.let.body = decode(clj_vector_nth(data, 2))) ? n : drop(n);
}

static clj_node *decode_recur(clj_value data) {
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
	return decode_into(n->u.recur.args, args, 0, n->u.recur.n) ? n : drop(n);
}

static bool decode_arity(clj_node *fn, clj_value data) {
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
	return (a->body = decode(clj_vector_nth(data, 4))) != NULL;
}

static bool decode_capture(clj_capture *c, clj_value data) {
	if (!is_vector_of(data, 2) || clj_vector_count(data) != 2 || !as_u32(clj_vector_nth(data, 1), &c->index))
		return fail_data(data, "expected [:local slot] or [:captured index]") != NULL;
	clj_value head = clj_vector_nth(data, 0);
	if (head != kw_local && head != kw_captured) return fail_data(data, "expected [:local slot] or [:captured index]") != NULL;
	c->from_captured = head == kw_captured;
	return true;
}

static clj_node *decode_fn(clj_value data) {
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
	for (uint32_t i = 0; i < clj_vector_count(arities); i++) {
		if (!decode_arity(n, clj_vector_nth(arities, i))) return drop(n);
	}
	return n;
}

static clj_node *decode_def(clj_value data) {
	clj_value var = clj_vector_count(data) == 6 ? var_named(clj_vector_nth(data, 1)) : CLJ_NIL;
	if (clj_is_nil(var) || !clj_is_bool(clj_vector_nth(data, 4)) || !clj_is_bool(clj_vector_nth(data, 5)))
		return fail_data(data, "expected [ns/name init meta macro dynamic]");
	clj_node *n = clj_node_alloc(CLJ_NODE_DEF);
	n->u.def.var = clj_retain(var);
	n->u.def.macro = clj_vector_nth(data, 4) == CLJ_TRUE;
	n->u.def.dynamic = clj_vector_nth(data, 5) == CLJ_TRUE;
	clj_value init = clj_vector_nth(data, 2);
	if (!clj_is_nil(init) && !(n->u.def.init = decode(init))) return drop(n);
	return (n->u.def.meta = decode(clj_vector_nth(data, 3))) ? n : drop(n);
}

static bool decode_catch(clj_catch *c, clj_value data) {
	if (!is_vector_of(data, 3) || clj_vector_count(data) != 3 || !as_u32(clj_vector_nth(data, 1), &c->slot) ||
	    (clj_vector_nth(data, 0) != kw_all && clj_vector_nth(data, 0) != kw_error))
		return fail_data(data, "expected [:all|:error slot handler]") != NULL;
	c->kind = clj_vector_nth(data, 0) == kw_all ? CLJ_CATCH_ALL : CLJ_CATCH_ERROR;
	return (c->handler = decode(clj_vector_nth(data, 2))) != NULL;
}

static clj_node *decode_try(clj_value data) {
	if (clj_vector_count(data) != 4 || !clj_is_vector(clj_vector_nth(data, 2))) return fail_data(data, "expected [body catches finally]");
	clj_value catches = clj_vector_nth(data, 2), finally_ = clj_vector_nth(data, 3);
	clj_node *n = clj_node_alloc(CLJ_NODE_TRY);
	n->u.try_.catches = zalloc(clj_vector_count(catches), sizeof *n->u.try_.catches);
	if (!(n->u.try_.body = decode(clj_vector_nth(data, 1)))) return drop(n);
	for (uint32_t i = 0; i < clj_vector_count(catches); i++) {
		n->u.try_.ncatches = i + 1;
		if (!decode_catch(&n->u.try_.catches[i], clj_vector_nth(catches, i))) return drop(n);
	}
	if (!clj_is_nil(finally_) && !(n->u.try_.finally_ = decode(finally_))) return drop(n);
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

static clj_node *decode_single(clj_node_kind kind, clj_value data) {
	if (clj_vector_count(data) != 2) return fail_data(data, "expected one item");
	clj_node *n = clj_node_alloc(kind);
	if (kind == CLJ_NODE_CONST) {
		n->u.value = clj_retain(clj_vector_nth(data, 1));
		return n;
	}
	return (n->u.throw_ = decode(clj_vector_nth(data, 1))) ? n : drop(n);
}

static clj_node *decode_if(clj_value data) {
	uint32_t count = clj_vector_count(data);
	if (count != 3 && count != 4) return fail_data(data, "expected [test then else?]");
	clj_node *n = clj_node_alloc(CLJ_NODE_IF);
	if (!(n->u.if_.test = decode(clj_vector_nth(data, 1))) || !(n->u.if_.then = decode(clj_vector_nth(data, 2))) ||
	    (count == 4 && !(n->u.if_.else_ = decode(clj_vector_nth(data, 3)))))
		return drop(n);
	return n;
}

static clj_node *decode_invoke(clj_value data) {
	if (clj_vector_count(data) < 2) return fail_data(data, "expected [f args*]");
	clj_node *n = clj_node_alloc(CLJ_NODE_INVOKE);
	n->u.invoke.n = clj_vector_count(data) - 2;
	n->u.invoke.args = zalloc(n->u.invoke.n, sizeof *n->u.invoke.args);
	if (!(n->u.invoke.fn = decode(clj_vector_nth(data, 1)))) return drop(n);
	return decode_into(n->u.invoke.args, data, 2, n->u.invoke.n) ? n : drop(n);
}

static clj_node *decode(clj_value data) {
	if (!is_vector_of(data, 1) || !clj_is_keyword(clj_vector_nth(data, 0))) return fail_data(data, "expected [:kind ...]");
	clj_value head = clj_vector_nth(data, 0);
	if (head == kw_const) return decode_single(CLJ_NODE_CONST, data);
	if (head == kw_local) return decode_leaf(CLJ_NODE_LOCAL, data);
	if (head == kw_captured) return decode_leaf(CLJ_NODE_CAPTURED, data);
	if (head == kw_var) return decode_var(CLJ_NODE_VAR, data);
	if (head == kw_the_var) return decode_var(CLJ_NODE_CONST, data);
	if (head == kw_if) return decode_if(data);
	if (head == kw_do) return decode_seq(CLJ_NODE_DO, data, 2);
	if (head == kw_vector) return decode_seq(CLJ_NODE_VECTOR, data, 1);
	if (head == kw_map) return decode_seq(CLJ_NODE_MAP, data, 1);
	if (head == kw_let) return decode_let(CLJ_NODE_LET, data);
	if (head == kw_loop) return decode_let(CLJ_NODE_LOOP, data);
	if (head == kw_recur) return decode_recur(data);
	if (head == kw_fn) return decode_fn(data);
	if (head == kw_invoke) return decode_invoke(data);
	if (head == kw_def) return decode_def(data);
	if (head == kw_try) return decode_try(data);
	if (head == kw_throw) return decode_single(CLJ_NODE_THROW, data);
	return fail_data(data, "unknown node kind");
}

typedef struct {
	uint32_t nslots, ncaptures; // of the enclosing arity, or the top level
	bool     ok;
} bounds;

static void check_bounds(const clj_node *n, void *ctx);

static bool in_range(bounds *b, uint32_t slot) {
	if (slot < b->nslots) return true;
	b->ok = false;
	clj_throw_msg("malformed node data, slot %u outside a frame of %u", slot, b->nslots);
	return false;
}

// Every slot an arity's body touches must fit its frame: the frame is sized from nslots, not from the body.
static void check_bounds(const clj_node *n, void *ctx) {
	bounds *b = ctx;
	if (!b->ok) return;
	switch (n->kind) {
	case CLJ_NODE_LOCAL: in_range(b, n->u.index); break;
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
	case CLJ_NODE_FN: {
		for (uint32_t i = 0; i < n->u.fn.ncaptures && b->ok; i++) {
			const clj_capture *c = &n->u.fn.captures[i];
			if (c->from_captured ? c->index < b->ncaptures : c->index < b->nslots) continue;
			b->ok = false;
			clj_throw_msg("malformed node data, capture source %u outside the creating frame", c->index);
		}
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED + 1 && b->ok; i++) {
			const clj_fn_arity *a = i <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[i] : n->u.fn.variadic;
			if (!a) continue;
			bounds   inner = {a->nslots, n->u.fn.ncaptures, true};
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
	default: break;
	}
	if (b->ok) clj_node_children(n, check_bounds, b);
}

clj_node *clj_node_from_data(clj_value data) {
	pthread_once(&keywords_once, intern_keywords);
	clj_node *root = decode(data);
	if (!root) return NULL;
	bounds top = {UINT32_MAX, 0, true};
	check_bounds(root, &top);
	if (!top.ok) return drop(root);
	clj_node_number(root);
	return root;
}
