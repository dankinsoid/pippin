// @ai-generated(guided)
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/analyzer.h"
#include "clj/box.h"
#include "clj/coll.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/hosttype.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/printer.h"
#include "clj/runtime.h"
#include "clj/set.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "node.h"

static void visit_node(const clj_node *n, clj_visitor visit, void *ctx) {
	if (n) visit(clj_from_ptr((void *)n), ctx);
}

static void visit_nodes(const clj_node **nodes, uint32_t n, clj_visitor visit, void *ctx) {
	for (uint32_t i = 0; i < n; i++) visit_node(nodes[i], visit, ctx);
}

// Arrays may hold NULLs: a tree released half-built after an analysis error.
static void node_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_node *n = self;
	switch (n->kind) {
	case CLJ_NODE_CONST: visit(n->u.value, ctx); break;
	case CLJ_NODE_LOCAL:
	case CLJ_NODE_CAPTURED: break;
	case CLJ_NODE_VAR: visit(n->u.var, ctx); break;
	case CLJ_NODE_IF:
		visit_node(n->u.if_.test, visit, ctx);
		visit_node(n->u.if_.then, visit, ctx);
		visit_node(n->u.if_.else_, visit, ctx);
		break;
	case CLJ_NODE_DO:
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: visit_nodes(n->u.seq.items, n->u.seq.n, visit, ctx); break;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		visit_nodes(n->u.let.inits, n->u.let.n, visit, ctx);
		visit_node(n->u.let.body, visit, ctx);
		break;
	case CLJ_NODE_RECUR: visit_nodes(n->u.recur.args, n->u.recur.n, visit, ctx); break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
		visit(n->u.fn.name, ctx);
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
			if (n->u.fn.fixed[i]) visit_node(n->u.fn.fixed[i]->body, visit, ctx);
		}
		if (n->u.fn.variadic) visit_node(n->u.fn.variadic->body, visit, ctx);
		break;
	case CLJ_NODE_INVOKE:
		visit_node(n->u.invoke.fn, visit, ctx);
		visit_nodes(n->u.invoke.args, n->u.invoke.n, visit, ctx);
		break;
	case CLJ_NODE_DIRECT_CALL: visit_nodes(n->u.direct.args, n->u.direct.n, visit, ctx); break;
	case CLJ_NODE_OBJC_SEND:
		visit(n->u.objc.selector, ctx);
		visit_node(n->u.objc.target, visit, ctx);
		visit_nodes(n->u.objc.args, n->u.objc.n, visit, ctx);
		break;
	case CLJ_NODE_OUTER: break;
	case CLJ_NODE_DEF:
		visit(n->u.def.var, ctx);
		visit_node(n->u.def.init, visit, ctx);
		visit_node(n->u.def.meta, visit, ctx);
		break;
	case CLJ_NODE_TRY:
		visit_node(n->u.try_.body, visit, ctx);
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) visit_node(n->u.try_.catches[i].handler, visit, ctx);
		visit_node(n->u.try_.finally_, visit, ctx);
		break;
	case CLJ_NODE_THROW: visit_node(n->u.throw_, visit, ctx); break;
	case CLJ_NODE_INTRINSIC:
		visit(n->u.intrinsic.var, ctx);
		visit_nodes(n->u.intrinsic.args, n->u.intrinsic.n, visit, ctx);
		break;
	case CLJ_NODE_FUSED:
		visit_nodes(n->u.fused.args, n->u.fused.nargs, visit, ctx);
		visit_node(n->u.fused.fused, visit, ctx);
		visit_node(n->u.fused.original, visit, ctx);
		break;
	}
}

static void node_finalize(void *self) {
	clj_node *n = self;
	switch (n->kind) {
	case CLJ_NODE_DO:
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: free(n->u.seq.items); break;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		free(n->u.let.slots);
		free(n->u.let.inits);
		break;
	case CLJ_NODE_RECUR:
		free(n->u.recur.args);
		free(n->u.recur.slots);
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) free(n->u.fn.fixed[i]);
		free(n->u.fn.variadic);
		free(n->u.fn.captures);
		break;
	case CLJ_NODE_INVOKE: free(n->u.invoke.args); break;
	case CLJ_NODE_DIRECT_CALL: free(n->u.direct.args); break;
	case CLJ_NODE_INTRINSIC: free(n->u.intrinsic.args); break;
	case CLJ_NODE_FUSED:
		free(n->u.fused.guards);
		free(n->u.fused.args);
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			clj_catch_kind k = n->u.try_.catches[i].kind;
			if (k == CLJ_CATCH_TYPE || k == CLJ_CATCH_HOST) clj_release(n->u.try_.catches[i].selector);
		}
		free(n->u.try_.catches);
		break;
	case CLJ_NODE_OBJC_SEND: free(n->u.objc.args); break;
	default: break;
	}
}

const clj_type clj_node_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "node",
	.each_child = node_each_child,
	.finalize = node_finalize,
};

typedef struct {
	clj_value sym; // borrowed from the form, which outlives the analysis
	uint32_t  slot;
} local;

typedef struct {
	clj_capture *items;
	uint32_t     n, cap;
} capture_list;

typedef struct {
	uint32_t        n;
	const uint32_t *slots;
} recur_target;

// One per fn arity (and one for the top level). Locals form a stack; slots are never reused.
typedef struct scope {
	struct scope       *parent;   // the scope enclosing the fn; NULL at top level
	capture_list       *captures; // the fn's environment, shared by its arities; NULL at top level
	local              *locals;
	uint32_t            nlocals, lcap;
	uint32_t            nslots;
	const recur_target *recur; // innermost loop or the arity itself; NULL where recur is illegal
	uint32_t            try_depth; // try forms between here and the recur target; recur across one is illegal
} scope;

typedef struct {
	clj_env   env;
	uint32_t  line, col; // of the innermost list being analyzed that carries a position; env's at the top
	clj_value keeps;     // vector holding the items of forms whose seq yields them owned (a deftype seq), or nil
} analyzer;

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_line, kw_column, kw_ns, kw_name, kw_doc, kw_arglists, kw_macro, kw_dynamic, kw_file, sym_with_meta;

static void intern_keywords(void) {
	sym_with_meta = clj_symbol_from_cstr("with-meta");
	kw_file = clj_keyword_from_cstr("file");
	kw_line = clj_keyword_from_cstr("line");
	kw_column = clj_keyword_from_cstr("column");
	kw_ns = clj_keyword_from_cstr("ns");
	kw_name = clj_keyword_from_cstr("name");
	kw_doc = clj_keyword_from_cstr("doc");
	kw_arglists = clj_keyword_from_cstr("arglists");
	kw_macro = clj_keyword_from_cstr("macro");
	kw_dynamic = clj_keyword_from_cstr("dynamic");
}

void clj_analyzer_intern_keywords(void) { pthread_once(&keywords_once, intern_keywords); }

static void *zalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

// Consumes data (a map or nil); the position is added when known.
static clj_value with_position(const analyzer *a, clj_value data) {
	if (!a->line) return data;
	if (clj_is_nil(data)) data = clj_map_empty();
	data = clj_map_assoc(data, kw_line, clj_fixnum(a->line));
	return clj_map_assoc(data, kw_column, clj_fixnum(a->col));
}

// @ai-generated(guided)
bool clj_form_position(clj_value form, uint32_t *line, uint32_t *col) {
	pthread_once(&keywords_once, intern_keywords);
	clj_value m = clj_meta(form);
	// Only the reader writes a position, and it writes a hash map.
	if (!clj_is_map(m)) {
		clj_release(m);
		return false;
	}
	clj_value l = clj_map_get(m, kw_line, CLJ_NIL), c = clj_map_get(m, kw_column, CLJ_NIL);
	bool      ok = clj_is_fixnum(l) && clj_is_fixnum(c) && clj_fixnum_val(l) > 0;
	if (ok) {
		*line = (uint32_t)clj_fixnum_val(l);
		*col = (uint32_t)clj_fixnum_val(c);
	}
	clj_release(m);
	return ok;
}

static void throw_at(const analyzer *a, const char *fmt, va_list ap) {
	char buf[512];
	vsnprintf(buf, sizeof buf, fmt, ap);
	clj_value message = clj_string_from_cstr(buf);
	clj_value data = with_position(a, CLJ_NIL);
	clj_throw(clj_ex_info(message, data));
	clj_release(message);
	clj_release(data);
}

static clj_node *fail(const analyzer *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static clj_node *fail(const analyzer *a, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	throw_at(a, fmt, ap);
	va_end(ap);
	return NULL;
}

static clj_value fail_value(const analyzer *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static clj_value fail_value(const analyzer *a, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	throw_at(a, fmt, ap);
	va_end(ap);
	return CLJ_THROWN;
}

// Rethrows the pending exception with the position added to its data and the original as the cause.
// A thrown value that is no error, or an error without a string message, passes unchanged.
static clj_value rethrow_positioned(const analyzer *a) {
	if (!a->line || !clj_is_exception(clj_pending())) return CLJ_THROWN;
	clj_value ex = clj_take_pending();
	clj_value message = clj_ex_message(ex);
	if (!clj_is_string(message)) {
		clj_release(message);
		return clj_throw(ex);
	}
	clj_value data = with_position(a, clj_ex_data(ex));
	clj_value wrapped = clj_ex_info_cause(message, data, ex);
	clj_release(message);
	clj_release(data);
	clj_release(ex);
	return clj_throw(wrapped);
}

// fmt has one %s, filled with the printed form.
static clj_node *fail_form(const analyzer *a, const char *fmt, clj_value form) {
	clj_value text = clj_pr_str(form);
	if (text == CLJ_THROWN) return NULL;
	clj_node *r = fail(a, fmt, clj_string_bytes(text));
	clj_release(text);
	return r;
}

clj_node *clj_node_alloc(clj_node_kind kind) {
	clj_node *n = clj_alloc(&clj_node_type, sizeof *n);
	n->kind = kind;
	return n;
}

static clj_node *node_new(const analyzer *a, clj_node_kind kind) {
	clj_node *n = clj_node_alloc(kind);
	n->line = a->line;
	n->col = a->col;
	return n;
}

static void child(const clj_node *n, clj_node_visitor visit, void *ctx) {
	if (n) visit(n, ctx);
}

static void children(const clj_node *const *nodes, uint32_t n, clj_node_visitor visit, void *ctx) {
	for (uint32_t i = 0; i < n; i++) child(nodes[i], visit, ctx);
}

// Same order as node_each_child minus the values; the codec and the exec table rely on it matching ids.
// @ai-generated(guided)
void clj_node_children(const clj_node *n, clj_node_visitor visit, void *ctx) {
	switch (n->kind) {
	case CLJ_NODE_CONST:
	case CLJ_NODE_LOCAL:
	case CLJ_NODE_CAPTURED:
	case CLJ_NODE_OUTER:
	case CLJ_NODE_VAR: break;
	case CLJ_NODE_IF:
		child(n->u.if_.test, visit, ctx);
		child(n->u.if_.then, visit, ctx);
		child(n->u.if_.else_, visit, ctx);
		break;
	case CLJ_NODE_DO:
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET: children(n->u.seq.items, n->u.seq.n, visit, ctx); break;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		children(n->u.let.inits, n->u.let.n, visit, ctx);
		child(n->u.let.body, visit, ctx);
		break;
	case CLJ_NODE_RECUR: children(n->u.recur.args, n->u.recur.n, visit, ctx); break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
			if (n->u.fn.fixed[i]) child(n->u.fn.fixed[i]->body, visit, ctx);
		}
		if (n->u.fn.variadic) child(n->u.fn.variadic->body, visit, ctx);
		break;
	case CLJ_NODE_INVOKE:
		child(n->u.invoke.fn, visit, ctx);
		children(n->u.invoke.args, n->u.invoke.n, visit, ctx);
		break;
	case CLJ_NODE_DIRECT_CALL: children(n->u.direct.args, n->u.direct.n, visit, ctx); break;
	case CLJ_NODE_OBJC_SEND:
		child(n->u.objc.target, visit, ctx);
		children(n->u.objc.args, n->u.objc.n, visit, ctx);
		break;
	case CLJ_NODE_DEF:
		child(n->u.def.init, visit, ctx);
		child(n->u.def.meta, visit, ctx);
		break;
	case CLJ_NODE_TRY:
		child(n->u.try_.body, visit, ctx);
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) child(n->u.try_.catches[i].handler, visit, ctx);
		child(n->u.try_.finally_, visit, ctx);
		break;
	case CLJ_NODE_THROW: child(n->u.throw_, visit, ctx); break;
	case CLJ_NODE_INTRINSIC: children(n->u.intrinsic.args, n->u.intrinsic.n, visit, ctx); break;
	case CLJ_NODE_FUSED:
		children(n->u.fused.args, n->u.fused.nargs, visit, ctx);
		child(n->u.fused.fused, visit, ctx);
		child(n->u.fused.original, visit, ctx);
		break;
	}
}

typedef struct {
	uint32_t nodes, sites;
} numbering;

// The tree is still the analyzer's own here: the const on the visitor's argument is dropped once.
static void number(const clj_node *n, void *ctx) {
	numbering *c = ctx;
	clj_node  *m = (clj_node *)n;
	m->id = c->nodes++;
	if (m->kind == CLJ_NODE_INVOKE) m->site = c->sites++;
	clj_node_children(m, number, c);
	m->nnodes = c->nodes - m->id;
}

void clj_node_number(clj_node *root) {
	numbering c = {0, 0};
	number(root, &c);
}

static clj_node *node_const(const analyzer *a, clj_value v) {
	clj_node *n = node_new(a, CLJ_NODE_CONST);
	n->u.value = clj_retain(v);
	return n;
}

static void push_local(scope *s, clj_value sym, uint32_t slot) {
	if (s->nlocals == s->lcap) {
		s->lcap = s->lcap ? s->lcap * 2 : 8;
		s->locals = realloc(s->locals, s->lcap * sizeof *s->locals);
		if (!s->locals) clj_fatal("out of memory");
	}
	s->locals[s->nlocals++] = (local){sym, slot};
}

static uint32_t new_slot(scope *s) { return s->nslots++; }

// A local of s, or of an enclosing fn, in which case it joins this fn's environment.
static bool resolve_local(scope *s, clj_value sym, bool *captured, uint32_t *index) {
	for (uint32_t i = s->nlocals; i > 0; i--) {
		if (clj_equals(s->locals[i - 1].sym, sym)) {
			*captured = false;
			*index = s->locals[i - 1].slot;
			return true;
		}
	}
	if (!s->parent) return false;
	bool     outer_captured;
	uint32_t outer_index;
	if (!resolve_local(s->parent, sym, &outer_captured, &outer_index)) return false;
	capture_list *c = s->captures;
	for (uint32_t i = 0; i < c->n; i++) {
		if ((c->items[i].kind == CLJ_CAPTURE_CAPTURED) == outer_captured && c->items[i].index == outer_index) {
			*captured = true;
			*index = i;
			return true;
		}
	}
	if (c->n == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 4;
		c->items = realloc(c->items, c->cap * sizeof *c->items);
		if (!c->items) clj_fatal("out of memory");
	}
	c->items[c->n] = (clj_capture){outer_captured ? CLJ_CAPTURE_CAPTURED : CLJ_CAPTURE_LOCAL, 0, outer_index};
	*captured = true;
	*index = c->n++;
	return true;
}

static bool is_unqualified_symbol(clj_value v) { return clj_is_symbol(v) && clj_is_nil(clj_symbol_ns(v)); }

static bool symbol_is(clj_value sym, const char *name) {
	return is_unqualified_symbol(sym) && strcmp(clj_string_bytes(clj_symbol_name(sym)), name) == 0;
}

typedef enum {
	SP_NONE, SP_QUOTE, SP_IF, SP_DO, SP_LET, SP_LOOP, SP_FN, SP_DEF, SP_DEFMACRO, SP_RECUR, SP_VAR, SP_TRY, SP_THROW,
	SP_SET, SP_CATCH, SP_FINALLY, SP_RESERVED
} special;

static const struct {
	const char *name;
	special     kind;
} specials[] = {
	// let/loop/fn are core.clj macros over the starred forms (destructuring).
	{"quote", SP_QUOTE}, {"if", SP_IF},     {"do", SP_DO},         {"let*", SP_LET},    {"loop*", SP_LOOP},
	{"fn*", SP_FN},      {"def", SP_DEF},   {"defmacro", SP_DEFMACRO}, {"recur", SP_RECUR}, {"var", SP_VAR},
	{"try", SP_TRY},     {"throw", SP_THROW},  {"set!", SP_SET},
	// Clause heads and `&` in params: not forms of their own, but syntax-quote must keep them unqualified.
	{"catch", SP_CATCH}, {"finally", SP_FINALLY}, {"&", SP_RESERVED},
};

static special special_of(clj_value sym) {
	if (!is_unqualified_symbol(sym)) return SP_NONE;
	const char *name = clj_string_bytes(clj_symbol_name(sym));
	for (size_t i = 0; i < sizeof specials / sizeof *specials; i++) {
		if (strcmp(specials[i].name, name) == 0) return specials[i].kind;
	}
	return SP_NONE;
}

bool clj_is_special_symbol(clj_value sym) { return special_of(sym) != SP_NONE; }

static bool is_map(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_map_type; }

// Borrowed items of a seq or vector form in a malloc'd array; the form keeps them alive, or the analyzer
// does until it is done when the form's seq hands them out owned (clj_seq_items then keeps a vector).
// NULL with the exception pending when realizing a lazy form throws.
static clj_value *seq_items(analyzer *a, clj_value form, uint32_t *n) {
	size_t     count;
	clj_value  keep;
	clj_value *items = clj_seq_items(form, &count, &keep);
	if (!items) return NULL;
	if (clj_is_vector(keep)) a->keeps = clj_vector_conj(clj_is_nil(a->keeps) ? clj_vector_empty() : a->keeps, keep);
	clj_release(keep);
	*n = (uint32_t)count;
	return items;
}

static clj_node *analyze(analyzer *a, scope *s, clj_value form, bool tail);

// A qualified reference to a private var of another namespace; (var ns/x) still reaches it.
static bool private_elsewhere(const analyzer *a, clj_value sym, clj_value var) {
	return !clj_is_nil(clj_symbol_ns(sym)) && !clj_equals(clj_var_ns(var), clj_ns_name(a->env.ns)) && clj_var_is_private(var);
}

static clj_value qualified_text(clj_value sym);

// A qualified symbol no var answers is a host type, as a classname is on the JVM (design §4): the value is
// made at runtime, because a compiled unit carries the name and meets the resolver only when it runs.
static clj_node *host_type_symbol(analyzer *a, scope *s, clj_value sym) {
	if (clj_is_nil(clj_symbol_ns(sym))) return NULL;
	clj_value text = qualified_text(sym);
	bool      known = !clj_is_nil(clj_host_type_named(clj_string_bytes(text), clj_string_len(text)));
	clj_node *node = NULL;
	if (known) {
		clj_value items[2] = {clj_symbol_from_cstr("clojure.core/host-type"), text};
		clj_value form = clj_list_from_array(items, 2);
		clj_release(items[0]);
		node = analyze(a, s, form, false);
		clj_release(form);
	}
	clj_release(text);
	return node;
}

static clj_node *analyze_symbol(analyzer *a, scope *s, clj_value sym) {
	if (clj_is_nil(clj_symbol_ns(sym))) {
		bool     captured;
		uint32_t index;
		if (resolve_local(s, sym, &captured, &index)) {
			clj_node *n = node_new(a, captured ? CLJ_NODE_CAPTURED : CLJ_NODE_LOCAL);
			if (captured) n->u.index = index;
			else n->u.local.index = index;
			return n;
		}
	}
	clj_value var = clj_ns_resolve(a->env.ns, sym);
	if (clj_is_nil(var)) {
		clj_node *host = host_type_symbol(a, s, sym);
		if (host) return host;
		return fail_form(a, "Unable to resolve symbol: %s in this context", sym);
	}
	if (private_elsewhere(a, sym, var)) return fail_form(a, "var: %s is not public", sym);
	if (clj_var_is_macro(var)) return fail_form(a, "Can't take value of a macro: %s", var);
	clj_node *n = node_new(a, CLJ_NODE_VAR);
	n->u.var = clj_retain(var);
	return n;
}

// ---- macro expansion

enum { MAX_EXPANSION_STEPS = 1000 };

// The macro var a seq form's head names, or nil: a local of s (NULL at top level) shadows it.
static clj_value macro_var(const analyzer *a, scope *s, clj_value form) {
	if (!clj_is_seq(form)) return CLJ_NIL;
	clj_seq_iter it = clj_seq_iter_start(form);
	clj_value    head;
	if (!clj_seq_iter_next(&it, &head)) return CLJ_NIL;
	clj_seq_iter_close(&it);
	if (!clj_is_symbol(head) || clj_is_special_symbol(head)) return CLJ_NIL;
	if (s && clj_is_nil(clj_symbol_ns(head))) {
		bool     captured;
		uint32_t index;
		if (resolve_local(s, head, &captured, &index)) return CLJ_NIL;
	}
	clj_value var = clj_ns_resolve(a->env.ns, head);
	// A private macro of another namespace is left for analyze_symbol to refuse.
	return !clj_is_nil(var) && clj_var_is_macro(var) && !private_elsewhere(a, head, var) ? var : CLJ_NIL;
}

// (macro &form &env args...) with &env nil: there is no local environment value yet.
static clj_value expand_once(analyzer *a, clj_value var, clj_value form) {
	clj_value fn = clj_var_deref(var);
	if (fn == CLJ_THROWN) return rethrow_positioned(a);
	uint32_t   n;
	clj_value *items = seq_items(a, form, &n);
	if (!items) {
		clj_release(fn);
		return rethrow_positioned(a);
	}
	clj_value *args = zalloc(n + 1, sizeof *args);
	args[0] = form;
	args[1] = CLJ_NIL;
	memcpy(args + 2, items + 1, (n - 1) * sizeof *args);
	clj_value r = clj_invoke(fn, args, n + 1);
	free(args);
	free(items);
	clj_release(fn);
	return r == CLJ_THROWN ? rethrow_positioned(a) : r;
}

// A macro returning its own form ends the loop, as in Clojure; the form is then analyzed as a call.
static clj_value expand_all(analyzer *a, scope *s, clj_value form) {
	clj_value cur = clj_retain(form);
	for (uint32_t step = 0;; step++) {
		clj_value var = macro_var(a, s, cur);
		if (clj_is_nil(var)) return cur;
		if (step == MAX_EXPANSION_STEPS) {
			clj_value text = clj_pr_str(cur);
			clj_release(cur);
			if (text == CLJ_THROWN) return CLJ_THROWN;
			clj_value r = fail_value(a, "Macro expansion exceeded %d steps at: %s", MAX_EXPANSION_STEPS, clj_string_bytes(text));
			clj_release(text);
			return r;
		}
		clj_value next = expand_once(a, var, cur);
		if (next == CLJ_THROWN) {
			clj_release(cur);
			return CLJ_THROWN;
		}
		if (next == cur) {
			clj_release(next);
			return cur;
		}
		clj_release(cur);
		cur = next;
	}
}

static analyzer analyzer_for(const clj_env *env) {
	pthread_once(&keywords_once, intern_keywords);
	analyzer a = {.env = env ? *env : (clj_env){0}};
	if (clj_is_nil(a.env.ns)) a.env.ns = clj_ns_current();
	a.line = a.env.line;
	a.col = a.env.col;
	return a;
}

clj_value clj_macroexpand_1(clj_value form, const clj_env *env) {
	analyzer a = analyzer_for(env);
	clj_form_position(form, &a.line, &a.col);
	clj_value var = macro_var(&a, NULL, form);
	clj_value r = clj_is_nil(var) ? clj_retain(form) : expand_once(&a, var, form);
	clj_release(a.keeps);
	return r;
}

clj_value clj_macroexpand(clj_value form, const clj_env *env) {
	analyzer a = analyzer_for(env);
	clj_form_position(form, &a.line, &a.col);
	clj_value r = expand_all(&a, NULL, form);
	clj_release(a.keeps);
	return r;
}

static bool all_const(const clj_node *const *nodes, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) {
		if (nodes[i]->kind != CLJ_NODE_CONST) return false;
	}
	return true;
}

// A literal whose elements all analyzed to constants folds into one constant.
static clj_node *fold_or_keep(const analyzer *a, clj_node *node, clj_value (*build)(const clj_node *const *, uint32_t)) {
	if (!all_const(node->u.seq.items, node->u.seq.n)) return node;
	clj_value v = build(node->u.seq.items, node->u.seq.n);
	clj_node *c = node_const(a, v);
	clj_release(v);
	clj_release(clj_from_ptr(node));
	return c;
}

static clj_value build_vector(const clj_node *const *items, uint32_t n) {
	clj_value *vals = zalloc(n, sizeof *vals);
	for (uint32_t i = 0; i < n; i++) vals[i] = items[i]->u.value;
	clj_value v = clj_vector_from_array(vals, n);
	free(vals);
	return v;
}

static clj_value build_map(const clj_node *const *items, uint32_t n) {
	clj_value *vals = malloc((n ? n : 1) * sizeof *vals);
	if (!vals) clj_fatal("out of memory");
	for (uint32_t i = 0; i < n; i++) vals[i] = items[i]->u.value;
	clj_value m = clj_map_from_items(vals, n, NULL);
	free(vals);
	return m;
}

static clj_value build_set(const clj_node *const *items, uint32_t n) {
	clj_value s = clj_set_empty();
	for (uint32_t i = 0; i < n; i++) s = clj_set_conj(s, items[i]->u.value);
	return s;
}

// Clojure's MetaExpr: a literal's reader metadata is a with-meta call, not part of the constant.
static clj_node *literal_with_meta(analyzer *a, scope *s, clj_value form, clj_node *node) {
	if (!node) return NULL;
	clj_value m = clj_meta(form);
	if (clj_is_nil(m)) return node;
	clj_node *meta_node = analyze(a, s, m, false);
	clj_release(m);
	if (!meta_node) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	clj_node *fn = node_new(a, CLJ_NODE_VAR);
	fn->u.var = clj_retain(clj_ns_intern(clj_ns_core(), sym_with_meta));
	clj_node *call = node_new(a, CLJ_NODE_INVOKE);
	call->u.invoke.fn = fn;
	call->u.invoke.args = zalloc(2, sizeof *call->u.invoke.args);
	call->u.invoke.n = 2;
	call->u.invoke.args[0] = node;
	call->u.invoke.args[1] = meta_node;
	return call;
}

static bool analyze_into(analyzer *a, scope *s, const clj_node **out, const clj_value *forms, uint32_t n, bool tail_last) {
	for (uint32_t i = 0; i < n; i++) {
		out[i] = analyze(a, s, forms[i], tail_last && i + 1 == n);
		if (!out[i]) return false;
	}
	return true;
}

static clj_node *analyze_vector(analyzer *a, scope *s, clj_value form) {
	uint32_t   n;
	clj_value *items = seq_items(a, form, &n);
	if (!items) return NULL;
	clj_node  *node = node_new(a, CLJ_NODE_VECTOR);
	node->u.seq.items = zalloc(n, sizeof *node->u.seq.items);
	node->u.seq.n = n;
	bool ok = analyze_into(a, s, node->u.seq.items, items, n, false);
	free(items);
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return literal_with_meta(a, s, form, fold_or_keep(a, node, build_vector));
}

typedef struct {
	clj_value *entries;
	size_t     n;
} collect_ctx;

static bool collect_entry(clj_value key, clj_value val, void *ctx) {
	collect_ctx *c = ctx;
	c->entries[c->n++] = key;
	c->entries[c->n++] = val;
	return true;
}

static clj_node *analyze_map(analyzer *a, scope *s, clj_value form) {
	uint32_t    n = 2 * clj_map_count(form);
	clj_value  *entries = zalloc(n, sizeof *entries);
	collect_ctx c = {entries, 0};
	clj_map_each(form, collect_entry, &c);
	clj_node *node = node_new(a, CLJ_NODE_MAP);
	node->u.seq.items = zalloc(n, sizeof *node->u.seq.items);
	node->u.seq.n = n;
	bool ok = analyze_into(a, s, node->u.seq.items, entries, n, false);
	free(entries);
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return literal_with_meta(a, s, form, fold_or_keep(a, node, build_map));
}

static bool collect_item(clj_value item, void *ctx) {
	collect_ctx *c = ctx;
	c->entries[c->n++] = item;
	return true;
}

static clj_node *analyze_set(analyzer *a, scope *s, clj_value form) {
	uint32_t    n = clj_set_count(form);
	clj_value  *items = zalloc(n, sizeof *items);
	collect_ctx c = {items, 0};
	clj_set_each(form, collect_item, &c);
	clj_node *node = node_new(a, CLJ_NODE_SET);
	node->u.seq.items = zalloc(n, sizeof *node->u.seq.items);
	node->u.seq.n = n;
	bool ok = analyze_into(a, s, node->u.seq.items, items, n, false);
	free(items);
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return literal_with_meta(a, s, form, fold_or_keep(a, node, build_set));
}

// Zero forms is nil, one is itself, more is a do.
static clj_node *analyze_body(analyzer *a, scope *s, const clj_value *forms, uint32_t n, bool tail) {
	if (n == 0) return node_const(a, CLJ_NIL);
	if (n == 1) return analyze(a, s, forms[0], tail);
	clj_node *node = node_new(a, CLJ_NODE_DO);
	node->u.seq.items = zalloc(n, sizeof *node->u.seq.items);
	node->u.seq.n = n;
	if (!analyze_into(a, s, node->u.seq.items, forms, n, tail)) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

static clj_node *analyze_if(analyzer *a, scope *s, const clj_value *items, uint32_t n, bool tail) {
	if (n < 3) return fail(a, "Too few arguments to if");
	if (n > 4) return fail(a, "Too many arguments to if");
	clj_node *node = node_new(a, CLJ_NODE_IF);
	if (!(node->u.if_.test = analyze(a, s, items[1], false)) ||
	    !(node->u.if_.then = analyze(a, s, items[2], tail)) ||
	    (n == 4 && !(node->u.if_.else_ = analyze(a, s, items[3], tail)))) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

static clj_node *analyze_let(analyzer *a, scope *s, const clj_value *items, uint32_t n, bool tail, bool loop) {
	const char *what = loop ? "loop" : "let";
	if (n < 2 || !clj_is_vector(items[1])) return fail(a, "%s requires a vector for its binding", what);
	uint32_t   nforms;
	clj_value *forms = seq_items(a, items[1], &nforms);
	if (!forms) return NULL;
	if (nforms % 2) {
		free(forms);
		return fail(a, "%s requires an even number of forms in binding vector", what);
	}
	uint32_t  nb = nforms / 2;
	clj_node *node = node_new(a, loop ? CLJ_NODE_LOOP : CLJ_NODE_LET);
	node->u.let.slots = zalloc(nb, sizeof *node->u.let.slots);
	node->u.let.inits = zalloc(nb, sizeof *node->u.let.inits);
	node->u.let.n = nb;
	uint32_t saved_nlocals = s->nlocals;
	bool     ok = true;
	for (uint32_t i = 0; i < nb && ok; i++) {
		clj_value sym = forms[2 * i];
		if (!clj_is_symbol(sym)) ok = fail_form(a, "Unsupported binding form: %s", sym) != NULL;
		else if (!clj_is_nil(clj_symbol_ns(sym))) ok = fail_form(a, "Can't let qualified name: %s", sym) != NULL;
		else if (!(node->u.let.inits[i] = analyze(a, s, forms[2 * i + 1], false))) ok = false;
		if (!ok) break;
		uint32_t slot = new_slot(s);
		node->u.let.slots[i] = slot;
		push_local(s, sym, slot);
	}
	free(forms);
	if (ok) {
		if (loop) {
			recur_target        target = {nb, node->u.let.slots};
			const recur_target *saved = s->recur;
			uint32_t            saved_try_depth = s->try_depth;
			s->recur = &target;
			s->try_depth = 0;
			node->u.let.body = analyze_body(a, s, items + 2, n - 2, true);
			s->recur = saved;
			s->try_depth = saved_try_depth;
		} else {
			node->u.let.body = analyze_body(a, s, items + 2, n - 2, tail);
		}
		ok = node->u.let.body != NULL;
	}
	s->nlocals = saved_nlocals;
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

static clj_node *analyze_recur(analyzer *a, scope *s, const clj_value *items, uint32_t n, bool tail) {
	if (s->recur && s->try_depth) return fail(a, "Cannot recur across try");
	if (!tail || !s->recur) return fail(a, "Can only recur from tail position");
	uint32_t nargs = n - 1;
	if (nargs != s->recur->n) return fail(a, "Mismatched argument count to recur, expected: %u args, got: %u", s->recur->n, nargs);
	clj_node *node = node_new(a, CLJ_NODE_RECUR);
	node->u.recur.args = zalloc(nargs, sizeof *node->u.recur.args);
	node->u.recur.slots = zalloc(nargs, sizeof *node->u.recur.slots);
	node->u.recur.n = nargs;
	memcpy(node->u.recur.slots, s->recur->slots, nargs * sizeof *node->u.recur.slots);
	if (!analyze_into(a, s, node->u.recur.args, items + 1, nargs, false)) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

static bool analyze_arity(analyzer *a, clj_node *fn, capture_list *captures, scope *parent, clj_value params, const clj_value *body, uint32_t nbody) {
	scope    s = {.parent = parent, .captures = captures};
	uint32_t nparams;
	clj_value *syms = seq_items(a, params, &nparams);
	if (!syms) return false;
	clj_fn_arity *arity = zalloc(1, sizeof *arity);
	arity->self_slot = -1;
	bool ok = true;
	for (uint32_t i = 0; i < nparams && ok; i++) {
		clj_value sym = syms[i];
		if (!clj_is_symbol(sym)) {
			ok = fail_form(a, "fn params must be symbols, got: %s", sym) != NULL;
		} else if (!clj_is_nil(clj_symbol_ns(sym))) {
			ok = fail_form(a, "Can't use qualified name as parameter: %s", sym) != NULL;
		} else if (symbol_is(sym, "&")) {
			if (i + 2 != nparams || !is_unqualified_symbol(syms[i + 1])) ok = fail(a, "Invalid parameter list: a rest parameter must be exactly one symbol after &") != NULL;
			else if (arity->nparams > CLJ_FN_MAX_FIXED) ok = fail(a, "Can't specify more than %d params", CLJ_FN_MAX_FIXED) != NULL;
			else {
				arity->variadic = true;
				push_local(&s, syms[i + 1], new_slot(&s));
				i++;
			}
		} else {
			arity->nparams++;
			push_local(&s, sym, new_slot(&s));
		}
	}
	free(syms);
	if (ok && !arity->variadic && arity->nparams > CLJ_FN_MAX_FIXED) ok = fail(a, "Can't specify more than %d params", CLJ_FN_MAX_FIXED) != NULL;
	if (ok) {
		if (arity->variadic) {
			if (fn->u.fn.variadic) ok = fail(a, "Can't have more than 1 variadic overload") != NULL;
			for (uint32_t i = arity->nparams + 1; ok && i <= CLJ_FN_MAX_FIXED; i++) {
				if (fn->u.fn.fixed[i]) ok = fail(a, "Can't have fixed arity function with more params than variadic function") != NULL;
			}
		} else if (fn->u.fn.fixed[arity->nparams]) {
			ok = fail(a, "Can't have 2 overloads with same arity") != NULL;
		} else if (fn->u.fn.variadic && arity->nparams > fn->u.fn.variadic->nparams) {
			ok = fail(a, "Can't have fixed arity function with more params than variadic function") != NULL;
		}
	}
	if (!ok) {
		free(arity);
		free(s.locals);
		return false;
	}
	if (arity->variadic) fn->u.fn.variadic = arity;
	else fn->u.fn.fixed[arity->nparams] = arity;
	if (!clj_is_nil(fn->u.fn.name)) {
		arity->self_slot = (int32_t)new_slot(&s);
		push_local(&s, fn->u.fn.name, (uint32_t)arity->self_slot);
	}
	uint32_t  nrecur = arity->nparams + (arity->variadic ? 1 : 0);
	uint32_t *slots = zalloc(nrecur, sizeof *slots);
	for (uint32_t i = 0; i < nrecur; i++) slots[i] = i;
	recur_target target = {nrecur, slots};
	s.recur = &target;
	arity->body = analyze_body(a, &s, body, nbody, true);
	arity->nslots = s.nslots;
	free(slots);
	free(s.locals);
	return arity->body != NULL;
}

static clj_node *analyze_fn(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	uint32_t  i = 1;
	clj_node *node = node_new(a, CLJ_NODE_FN);
	if (i < n && clj_is_symbol(items[i])) {
		if (!clj_is_nil(clj_symbol_ns(items[i]))) {
			clj_release(clj_from_ptr(node));
			return fail_form(a, "Can't use qualified name as fn name: %s", items[i]);
		}
		node->u.fn.name = clj_retain(items[i]);
		i++;
	}
	capture_list captures = {0};
	bool         ok;
	if (i >= n) {
		ok = fail(a, "Parameter declaration missing") != NULL;
	} else if (clj_is_vector(items[i])) {
		ok = analyze_arity(a, node, &captures, s, items[i], items + i + 1, n - i - 1);
	} else {
		ok = true;
		for (; i < n && ok; i++) {
			uint32_t   nsig = 0;
			clj_value *sig = clj_is_seq(items[i]) ? seq_items(a, items[i], &nsig) : NULL;
			if (!sig && clj_is_seq(items[i])) {
				ok = false;
				break;
			}
			if (!sig || nsig == 0) {
				free(sig);
				ok = fail_form(a, "Parameter declaration %s should be a vector", items[i]) != NULL;
				break;
			}
			if (!clj_is_vector(sig[0])) ok = fail_form(a, "Parameter declaration %s should be a vector", sig[0]) != NULL;
			else ok = analyze_arity(a, node, &captures, s, sig[0], sig + 1, nsig - 1);
			free(sig);
		}
	}
	node->u.fn.captures = captures.items;
	node->u.fn.ncaptures = captures.n;
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

// (quote v)
static clj_value quoted(clj_value v) {
	clj_value items[2] = {clj_symbol_from_cstr("quote"), v};
	clj_value list = clj_list_from_array(items, 2);
	clj_release(items[0]);
	return list;
}

// The var's meta as a form: the symbol's meta, whose values are expressions as in Clojure
// ((def ^{:doc (str ..)} x)), then :line/:column of the def form, then :ns and :name, which always win.
// :name is the var's own symbol, so a redefinition allocates nothing new for it.
// @ai-generated(guided)
static clj_value def_meta_form(const analyzer *a, clj_value sym, clj_value var, clj_value doc) {
	clj_value m = clj_meta(sym);
	if (clj_is_nil(m)) m = clj_map_empty();
	if (!clj_is_nil(doc)) m = clj_map_assoc(m, kw_doc, doc);
	if (a->line) {
		m = clj_map_assoc(m, kw_line, clj_fixnum(a->line));
		m = clj_map_assoc(m, kw_column, clj_fixnum(a->col));
	}
	clj_value file = clj_var_thread_binding(clj_load_file_var());
	if (!clj_is_nil(file) && clj_is_string(clj_volatile_value(file))) m = clj_map_assoc(m, kw_file, clj_volatile_value(file));
	clj_value ns = quoted(clj_var_ns(var)), nm = quoted(clj_var_name(var));
	m = clj_map_assoc(m, kw_ns, ns);
	m = clj_map_assoc(m, kw_name, nm);
	clj_release(ns);
	clj_release(nm);
	return m;
}

// (def sym), (def sym init), (def sym "doc" init).
static clj_node *analyze_def(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	if (n < 2) return fail(a, "Too few arguments to def");
	clj_value doc = CLJ_NIL;
	uint32_t  init_at = 2;
	if (n == 4 && clj_is_string(items[2])) {
		doc = items[2];
		init_at = 3;
	} else if (n > 3) {
		return fail(a, "Too many arguments to def");
	}
	clj_value sym = items[1];
	if (!clj_is_symbol(sym)) return fail(a, "First argument to def must be a Symbol");
	clj_value ns_name = clj_symbol_name(clj_ns_name(a->env.ns));
	clj_value sym_meta = clj_meta(sym);
	clj_value name = sym;
	if (!clj_is_nil(clj_symbol_ns(sym)) && !clj_equals(clj_symbol_ns(sym), ns_name)) {
		clj_release(sym_meta);
		return fail(a, "Can't create defs outside of current ns");
	}
	// The var's name is a bare symbol: the meta stays on the var, not on the key that reaches it.
	if (!clj_is_nil(clj_symbol_ns(sym)) || !clj_is_nil(sym_meta)) name = clj_symbol_new(CLJ_NIL, clj_symbol_name(sym));
	clj_node *node = node_new(a, CLJ_NODE_DEF);
	node->u.def.var = clj_retain(clj_ns_intern(a->env.ns, name));
	node->u.def.dynamic = clj_is_map(sym_meta) && clj_truthy(clj_map_get(sym_meta, kw_dynamic, CLJ_NIL));
	clj_release(sym_meta);
	if (name != sym) clj_release(name);
	clj_value meta_form = def_meta_form(a, sym, node->u.def.var, doc);
	node->u.def.meta = analyze(a, s, meta_form, false);
	clj_release(meta_form);
	if (!node->u.def.meta) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	if (n > init_at) {
		clj_node *init = analyze(a, s, items[init_at], false);
		node->u.def.init = init;
		if (!init) {
			clj_release(clj_from_ptr(node));
			return NULL;
		}
		// (def f (fn ...)) names the fn after the var, as Clojure does, so arity errors can say who.
		if (init->kind == CLJ_NODE_FN && clj_is_nil(init->u.fn.name)) init->u.fn.name = clj_symbol_new(ns_name, clj_symbol_name(sym));
	}
	return node;
}

static clj_node *analyze_invoke(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	clj_node *node = node_new(a, CLJ_NODE_INVOKE);
	node->u.invoke.args = zalloc(n - 1, sizeof *node->u.invoke.args);
	node->u.invoke.n = n - 1;
	if (!(node->u.invoke.fn = analyze(a, s, items[0], false)) || !analyze_into(a, s, node->u.invoke.args, items + 1, n - 1, false)) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

// A head symbol of the (.method target args*) form: a lone . is reserved, and a namespace is not a method.
static bool is_method_head(clj_value v) {
	if (!clj_is_symbol(v) || !clj_is_nil(clj_symbol_ns(v))) return false;
	clj_value name = clj_symbol_name(v);
	return clj_string_len(name) > 1 && clj_string_bytes(name)[0] == '.';
}

static bool spell_append(char *buf, size_t cap, size_t *used, const char *bytes, size_t len) {
	if (*used + len >= cap) return false;
	memcpy(buf + *used, bytes, len);
	*used += len;
	buf[*used] = '\0';
	return true;
}

// (.add-target btn self :action sel :for-control-events e) -> "add-target:action:for-control-events:".
// The labels are literal keywords in selector order: another order names another method (design §5).
static clj_value method_selector(analyzer *a, const clj_value *items, uint32_t n, uint32_t *nargs) {
	clj_value base = clj_symbol_name(items[0]);
	char      buf[512];
	size_t    used = 0;
	buf[0] = '\0';
	if (!spell_append(buf, sizeof buf, &used, clj_string_bytes(base) + 1, clj_string_len(base) - 1)) return fail_value(a, "Method name too long: %s", clj_string_bytes(base));
	*nargs = n > 2 ? 1 : 0;
	if (n > 2 && !spell_append(buf, sizeof buf, &used, ":", 1)) return fail_value(a, "Selector too long");
	for (uint32_t i = 3; i < n; i += 2) {
		if (!clj_is_keyword(items[i]) || !clj_is_nil(clj_keyword_ns(items[i]))) {
			fail_form(a, "A method call takes literal unqualified keyword labels in selector order, got: %s", items[i]);
			return CLJ_THROWN;
		}
		if (i + 1 >= n) {
			fail_form(a, "Label %s has no argument", items[i]);
			return CLJ_THROWN;
		}
		clj_value label = clj_keyword_name(items[i]);
		if (!spell_append(buf, sizeof buf, &used, clj_string_bytes(label), clj_string_len(label)) || !spell_append(buf, sizeof buf, &used, ":", 1))
			return fail_value(a, "Selector too long");
		(*nargs)++;
	}
	return clj_string_new(buf, used);
}

static clj_node *analyze_objc_send(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	if (n < 2) return fail_form(a, "A method call needs a target: (%s target args*)", items[0]);
	uint32_t  nargs;
	clj_value selector = method_selector(a, items, n, &nargs);
	if (selector == CLJ_THROWN) return NULL;
	clj_node *node = node_new(a, CLJ_NODE_OBJC_SEND);
	node->u.objc.selector = selector;
	node->u.objc.args = zalloc(nargs, sizeof *node->u.objc.args);
	node->u.objc.n = nargs;
	bool ok = (node->u.objc.target = analyze(a, s, items[1], false)) != NULL;
	// Arguments stay positional and left to right; the labels between them are the odd indices.
	for (uint32_t i = 2, k = 0; i < n && ok; i += 2) ok = (node->u.objc.args[k++] = analyze(a, s, items[i], false)) != NULL;
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

// [&form &env params...]
static clj_value macro_params(analyzer *a, clj_value params) {
	uint32_t   n;
	clj_value *syms = seq_items(a, params, &n);
	if (!syms) return CLJ_THROWN;
	clj_value *all = zalloc(n + 2, sizeof *all);
	all[0] = clj_symbol_from_cstr("&form");
	all[1] = clj_symbol_from_cstr("&env");
	memcpy(all + 2, syms, n * sizeof *all);
	clj_value v = clj_vector_from_array(all, n + 2);
	clj_release(all[0]);
	clj_release(all[1]);
	free(all);
	free(syms);
	return v;
}

// @ai-generated(guided)
// clojure.core/fn once core.clj has defined the macro (params then destructure); fn* while booting before it.
static clj_value macro_fn_symbol(void) {
	clj_value fn = clj_symbol_from_cstr("fn");
	clj_value var = clj_ns_resolve(clj_ns_core(), fn);
	if (clj_is_nil(var) || !clj_var_is_macro(var)) {
		clj_release(fn);
		return clj_symbol_from_cstr("fn*");
	}
	clj_value qualified = clj_symbol_new(clj_symbol_name(clj_ns_name(clj_ns_core())), clj_symbol_name(fn));
	clj_release(fn);
	return qualified;
}

typedef struct {
	clj_value *entries;
	size_t     n;
} merge_ctx;

static bool merge_entry(clj_value key, clj_value val, void *ctx) {
	merge_ctx *c = ctx;
	c->entries[c->n++] = key;
	c->entries[c->n++] = val;
	return true;
}

// Consumes m: m with every entry of other.
static clj_value map_merge(clj_value m, clj_value other) {
	size_t     n = 2 * (size_t)clj_map_count(other);
	merge_ctx  c = {zalloc(n, sizeof(clj_value)), 0};
	clj_map_each(other, merge_entry, &c);
	for (size_t i = 0; i < n; i += 2) m = clj_map_assoc(m, c.entries[i], c.entries[i + 1]);
	free(c.entries);
	return m;
}

// (defmacro name docstring? attr-map? [params] body...) or with ([params] body...)+ arities:
// a def of the fn with &form and &env prepended to every arity, the var's meta carrying :macro true,
// :doc, the attr-map's entries and :arglists of the params as written; the var is flagged when the def runs.
// @ai-generated(guided)
static clj_node *analyze_defmacro(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	if (n < 2 || !clj_is_symbol(items[1])) return fail(a, "First argument to defmacro must be a Symbol");
	clj_value meta = clj_meta(items[1]);
	if (clj_is_nil(meta)) meta = clj_map_empty();
	uint32_t i = 2;
	if (i < n && clj_is_string(items[i])) meta = clj_map_assoc(meta, kw_doc, items[i++]);
	if (i < n && is_map(items[i])) meta = map_merge(meta, items[i++]);
	if (i >= n) {
		clj_release(meta);
		return fail(a, "Parameter declaration missing");
	}
	clj_value *fn_items = zalloc(n - i + 1, sizeof *fn_items);
	clj_value *arglists = zalloc(n - i, sizeof *arglists);
	uint32_t   nfn = 0, nargs = 0;
	fn_items[nfn++] = macro_fn_symbol();
	bool ok = true;
	if (clj_is_vector(items[i])) {
		fn_items[nfn++] = macro_params(a, items[i]);
		ok = fn_items[nfn - 1] != CLJ_THROWN;
		arglists[nargs++] = items[i];
		for (uint32_t j = i + 1; j < n; j++) fn_items[nfn++] = clj_retain(items[j]);
	} else {
		for (uint32_t j = i; j < n && ok; j++) {
			clj_value arity = items[j];
			clj_value head = clj_is_seq(arity) ? clj_first(arity) : CLJ_NIL;
			if (head == CLJ_THROWN) {
				ok = false;
				break;
			}
			if (clj_is_vector(head)) {
				clj_value params = macro_params(a, head);
				clj_value body = clj_rest(arity);
				if (params == CLJ_THROWN || body == CLJ_THROWN) {
					ok = false;
				} else {
					arity = clj_cons_new(params, body);
					arglists[nargs++] = head;
				}
				clj_release(params);
				clj_release(body);
			} else {
				clj_retain(arity); // analyze_fn reports the malformed arity
			}
			clj_release(head);
			if (ok) fn_items[nfn++] = arity;
		}
	}
	if (!ok) {
		for (uint32_t j = 0; j < nfn; j++) clj_release(fn_items[j]);
		free(fn_items);
		free(arglists);
		clj_release(meta);
		return NULL;
	}
	clj_value lists = clj_list_from_array(arglists, nargs), quoted_lists = quoted(lists);
	meta = clj_map_assoc(meta, kw_arglists, quoted_lists);
	meta = clj_map_assoc(meta, kw_macro, CLJ_TRUE);
	clj_release(lists);
	clj_release(quoted_lists);
	free(arglists);
	clj_value fn_form = clj_list_from_array(fn_items, nfn);
	for (uint32_t j = 0; j < nfn; j++) clj_release(fn_items[j]);
	free(fn_items);
	clj_value sym = clj_with_meta(clj_retain(items[1]), meta);
	clj_release(meta);
	clj_value def_items[3] = {items[0], sym, fn_form};
	clj_node *node = analyze_def(a, s, def_items, 3);
	clj_release(sym);
	clj_release(fn_form);
	if (node) node->u.def.macro = true;
	return node;
}

// @ai-generated(guided)
static clj_node *analyze_throw(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	if (n < 2) return fail(a, "Too few arguments to throw, throw expects a single Throwable instance");
	if (n > 2) return fail(a, "Too many arguments to throw, throw expects a single Throwable instance");
	clj_node *node = node_new(a, CLJ_NODE_THROW);
	if (!(node->u.throw_ = analyze(a, s, items[1], false))) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

typedef enum { TRY_BODY, TRY_CATCH, TRY_FINALLY, TRY_THROWN } try_clause;

// What a try item is by its head; TRY_THROWN when reading the head throws.
static try_clause try_clause_of(clj_value item) {
	if (!clj_is_seq(item)) return TRY_BODY;
	clj_value head = clj_first(item);
	if (head == CLJ_THROWN) return TRY_THROWN;
	special sp = special_of(head);
	clj_release(head);
	return sp == SP_CATCH ? TRY_CATCH : sp == SP_FINALLY ? TRY_FINALLY : TRY_BODY;
}

// The one host type that is a runtime concept of ours: it arrives as our own cancellation, so a cast
// against the Swift type would be false always (design §4).
static bool is_cancellation_type(clj_value cls) {
	clj_value ns = clj_symbol_ns(cls);
	return !clj_is_nil(ns) && strcmp(clj_string_bytes(ns), "Swift") == 0 &&
	       strcmp(clj_string_bytes(clj_symbol_name(cls)), "CancellationError") == 0;
}

// "Module/Name" as one string: what the host resolver takes and what the clause prints as.
static clj_value qualified_text(clj_value sym) {
	clj_value ns = clj_symbol_ns(sym), name = clj_symbol_name(sym);
	size_t    nlen = clj_string_len(ns), mlen = clj_string_len(name);
	char     *text = zalloc(nlen + mlen + 2, 1);
	memcpy(text, clj_string_bytes(ns), nlen);
	text[nlen] = '/';
	memcpy(text + nlen + 1, clj_string_bytes(name), mlen);
	clj_value v = clj_string_new(text, nlen + mlen + 1);
	free(text);
	return v;
}

// @ai-generated(guided)
static bool catch_kind_of(analyzer *a, clj_value cls, clj_catch *c) {
	if (clj_is_keyword(cls)) {
		if (clj_is_nil(clj_keyword_ns(cls)) && strcmp(clj_string_bytes(clj_keyword_name(cls)), "default") == 0) {
			c->kind = CLJ_CATCH_ALL;
		} else {
			c->kind = CLJ_CATCH_KEYWORD;
			c->selector = cls; // immortal: no retain to pair with the release the field never gets
		}
		return true;
	}
	if (symbol_is(cls, "Throwable") || symbol_is(cls, "Exception") || symbol_is(cls, "Object")) {
		c->kind = CLJ_CATCH_ALL;
		return true;
	}
	if (symbol_is(cls, "ExceptionInfo")) {
		c->kind = CLJ_CATCH_ERROR;
		return true;
	}
	if (!clj_is_symbol(cls)) return fail_form(a, "Unable to resolve classname: %s", cls) != NULL;
	// Qualified is a host type, matched by a dynamic cast the host makes; unqualified is ours, a var holding
	// a type descriptor, matched by instance? (design §4).
	if (!clj_is_nil(clj_symbol_ns(cls))) {
		if (is_cancellation_type(cls)) {
			c->kind = CLJ_CATCH_KEYWORD;
			c->selector = clj_cancelled_keyword();
			return true;
		}
		c->kind = CLJ_CATCH_HOST;
		c->selector = qualified_text(cls);
		return true;
	}
	clj_value var = clj_ns_resolve(a->env.ns, cls);
	if (clj_is_nil(var)) return fail_form(a, "Unable to resolve classname: %s", cls) != NULL;
	c->kind = CLJ_CATCH_TYPE;
	c->selector = clj_retain(var);
	return true;
}

// (catch Class name body*): the binding is a local of the handler only.
// @ai-generated(guided)
static bool analyze_catch(analyzer *a, scope *s, clj_catch *c, clj_value clause) {
	uint32_t   n;
	clj_value *items = seq_items(a, clause, &n);
	if (!items) return false;
	bool ok;
	if (n < 3) ok = fail(a, "catch clause requires a classname and a binding: (catch Class name body*)") != NULL;
	else if (!catch_kind_of(a, items[1], c)) ok = false;
	else if (!is_unqualified_symbol(items[2])) ok = fail_form(a, "Bad binding form, expected symbol, got: %s", items[2]) != NULL;
	else {
		uint32_t saved_nlocals = s->nlocals;
		c->slot = new_slot(s);
		push_local(s, items[2], c->slot);
		c->handler = analyze_body(a, s, items + 3, n - 3, false);
		s->nlocals = saved_nlocals;
		ok = c->handler != NULL;
	}
	free(items);
	return ok;
}

// @ai-generated(guided)
static bool analyze_finally(analyzer *a, scope *s, const clj_node **out, clj_value clause) {
	uint32_t   n;
	clj_value *items = seq_items(a, clause, &n);
	if (!items) return false;
	*out = analyze_body(a, s, items + 1, n - 1, false);
	free(items);
	return *out != NULL;
}

// (try body* catch* finally?). Nothing inside is in tail position: recur must not leave the try.
// @ai-generated(guided)
static clj_node *analyze_try(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	uint32_t   nbody = 1;
	try_clause clause = TRY_BODY;
	while (nbody < n && (clause = try_clause_of(items[nbody])) == TRY_BODY) nbody++;
	if (clause == TRY_THROWN) return NULL;
	clj_node *node = node_new(a, CLJ_NODE_TRY);
	node->u.try_.catches = zalloc(n - nbody, sizeof *node->u.try_.catches);
	s->try_depth++;
	node->u.try_.body = analyze_body(a, s, items + 1, nbody - 1, false);
	bool ok = node->u.try_.body != NULL;
	for (uint32_t i = nbody; i < n && ok; i++) {
		clause = try_clause_of(items[i]);
		if (clause == TRY_THROWN) ok = false;
		else if (node->u.try_.finally_) ok = fail(a, "finally clause must be last in try expression") != NULL;
		else if (clause == TRY_BODY) ok = fail(a, "Only catch or finally clause can follow catch in try expression") != NULL;
		else if (clause == TRY_CATCH) ok = analyze_catch(a, s, &node->u.try_.catches[node->u.try_.ncatches++], items[i]);
		else ok = analyze_finally(a, s, &node->u.try_.finally_, items[i]);
	}
	s->try_depth--;
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

static clj_node *analyze_var(analyzer *a, const clj_value *items, uint32_t n) {
	if (n != 2) return fail(a, "Wrong number of args (%u) passed to var", n - 1);
	if (!clj_is_symbol(items[1])) return fail_form(a, "var requires a symbol, got: %s", items[1]);
	clj_value var = clj_ns_resolve(a->env.ns, items[1]);
	if (clj_is_nil(var)) return fail_form(a, "Unable to resolve var: %s in this context", items[1]);
	return node_const(a, var);
}

// (set! sym expr) on a var: (clojure.core/var-set (var sym) expr). Locals and fields are not assignable.
// @ai-generated(guided)
static clj_node *analyze_set_bang(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	if (n != 3) return fail(a, "Malformed assignment, expecting (set! target val)");
	if (!clj_is_symbol(items[1])) return fail_form(a, "Invalid assignment target: %s", items[1]);
	if (clj_is_nil(clj_symbol_ns(items[1]))) {
		bool     captured;
		uint32_t index;
		if (resolve_local(s, items[1], &captured, &index)) return fail_form(a, "Cannot assign to non-mutable: %s", items[1]);
	}
	clj_value var_items[2] = {clj_symbol_from_cstr("var"), clj_retain(items[1])};
	clj_value var_form = clj_list_from_array(var_items, 2);
	clj_release(var_items[0]);
	clj_release(var_items[1]);
	clj_value call_items[3] = {clj_symbol_from_cstr("clojure.core/var-set"), var_form, clj_retain(items[2])};
	clj_value call = clj_list_from_array(call_items, 3);
	for (int i = 0; i < 3; i++) clj_release(call_items[i]);
	clj_node *node = analyze(a, s, call, false);
	clj_release(call);
	return node;
}

static clj_node *analyze_list_at(analyzer *a, scope *s, clj_value form, bool tail);

// Errors inside the list report its own position when the reader gave it one.
static clj_node *analyze_list(analyzer *a, scope *s, clj_value form, bool tail) {
	uint32_t saved_line = a->line, saved_col = a->col;
	clj_form_position(form, &a->line, &a->col);
	clj_node *node = analyze_list_at(a, s, form, tail);
	a->line = saved_line;
	a->col = saved_col;
	return node;
}

static clj_node *analyze_list_at(analyzer *a, scope *s, clj_value form, bool tail) {
	clj_value expanded = expand_all(a, s, form);
	if (expanded == CLJ_THROWN) return NULL;
	if (expanded != form) {
		clj_node *node = analyze(a, s, expanded, tail);
		clj_release(expanded);
		return node;
	}
	clj_release(expanded);
	uint32_t   n;
	clj_value *items = seq_items(a, form, &n);
	if (!items) return NULL;
	clj_node *node;
	switch (special_of(items[0])) {
	case SP_QUOTE: node = n == 2 ? node_const(a, items[1]) : fail(a, "Wrong number of args (%u) passed to quote", n - 1); break;
	case SP_IF: node = analyze_if(a, s, items, n, tail); break;
	case SP_DO: node = analyze_body(a, s, items + 1, n - 1, tail); break;
	case SP_LET: node = analyze_let(a, s, items, n, tail, false); break;
	case SP_LOOP: node = analyze_let(a, s, items, n, tail, true); break;
	case SP_FN: node = analyze_fn(a, s, items, n); break;
	case SP_DEF: node = analyze_def(a, s, items, n); break;
	case SP_DEFMACRO: node = analyze_defmacro(a, s, items, n); break;
	case SP_RECUR: node = analyze_recur(a, s, items, n, tail); break;
	case SP_VAR: node = analyze_var(a, items, n); break;
	case SP_TRY: node = analyze_try(a, s, items, n); break;
	case SP_THROW: node = analyze_throw(a, s, items, n); break;
	case SP_SET: node = analyze_set_bang(a, s, items, n); break;
	case SP_CATCH: node = fail(a, "catch outside try"); break;
	case SP_FINALLY: node = fail(a, "finally outside try"); break;
	case SP_NONE:
	case SP_RESERVED: node = is_method_head(items[0]) ? analyze_objc_send(a, s, items, n) : analyze_invoke(a, s, items, n); break;
	}
	free(items);
	return node;
}

// Any seq is a list form: macros hand back lazy seqs, and () analyzes as itself.
static clj_node *analyze(analyzer *a, scope *s, clj_value form, bool tail) {
	if (clj_is_symbol(form)) return analyze_symbol(a, s, form);
	if (clj_is_seq(form)) {
		clj_value seq = clj_seq(form);
		if (seq == CLJ_THROWN) return NULL;
		if (clj_is_nil(seq)) return literal_with_meta(a, s, form, node_const(a, clj_list_empty()));
		clj_node *node = analyze_list(a, s, seq, tail);
		clj_release(seq);
		return node;
	}
	if (clj_is_vector(form)) return analyze_vector(a, s, form);
	if (is_map(form)) return analyze_map(a, s, form);
	if (clj_is_set(form)) return analyze_set(a, s, form);
	return node_const(a, form);
}

clj_node *clj_analyze(clj_value form, const clj_env *env) {
	analyzer  a = analyzer_for(env);
	scope     top = {0};
	clj_node *node = analyze(&a, &top, form, false);
	free(top.locals);
	clj_release(a.keeps);
	if (node) {
		clj_optimize(node);
		clj_node_number(node);
	}
	return node;
}
