// @ai-generated(guided)
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/analyzer.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/printer.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"

static void visit_node(clj_node *n, clj_visitor visit, void *ctx) {
	if (n) visit(clj_from_ptr(n), ctx);
}

static void visit_nodes(clj_node **nodes, uint32_t n, clj_visitor visit, void *ctx) {
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
	case CLJ_NODE_MAP: visit_nodes(n->u.seq.items, n->u.seq.n, visit, ctx); break;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		visit_nodes(n->u.let.inits, n->u.let.n, visit, ctx);
		visit_node(n->u.let.body, visit, ctx);
		break;
	case CLJ_NODE_RECUR: visit_nodes(n->u.recur.args, n->u.recur.n, visit, ctx); break;
	case CLJ_NODE_FN:
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
	case CLJ_NODE_DEF:
		visit(n->u.def.var, ctx);
		visit_node(n->u.def.init, visit, ctx);
		break;
	}
}

static void node_finalize(void *self) {
	clj_node *n = self;
	switch (n->kind) {
	case CLJ_NODE_DO:
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP: free(n->u.seq.items); break;
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
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) free(n->u.fn.fixed[i]);
		free(n->u.fn.variadic);
		free(n->u.fn.captures);
		break;
	case CLJ_NODE_INVOKE: free(n->u.invoke.args); break;
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
} scope;

typedef struct {
	clj_env env;
} analyzer;

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_line, kw_column;

static void intern_keywords(void) {
	kw_line = clj_keyword_from_cstr("line");
	kw_column = clj_keyword_from_cstr("column");
}

static void *zalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

// Consumes data (a map or nil); the position is added when known.
static clj_value with_position(const analyzer *a, clj_value data) {
	if (!a->env.line) return data;
	pthread_once(&keywords_once, intern_keywords);
	if (clj_is_nil(data)) data = clj_map_empty();
	data = clj_map_assoc(data, kw_line, clj_fixnum(a->env.line));
	return clj_map_assoc(data, kw_column, clj_fixnum(a->env.col));
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
static clj_value rethrow_positioned(const analyzer *a) {
	if (!a->env.line) return CLJ_THROWN;
	clj_value ex = clj_take_pending();
	clj_value data = clj_exception_data(ex);
	data = with_position(a, clj_is_nil(data) ? CLJ_NIL : clj_retain(data));
	clj_value wrapped = clj_ex_info_cause(clj_exception_message(ex), data, ex);
	clj_release(data);
	clj_release(ex);
	return clj_throw(wrapped);
}

// fmt has one %s, filled with the printed form.
static clj_node *fail_form(const analyzer *a, const char *fmt, clj_value form) {
	clj_value text = clj_pr_str(form);
	clj_node *r = fail(a, fmt, clj_string_bytes(text));
	clj_release(text);
	return r;
}

static clj_node *node_new(clj_node_kind kind) {
	clj_node *n = clj_alloc(&clj_node_type, sizeof *n);
	n->kind = kind;
	n->eval = clj_node_eval_fn(kind);
	return n;
}

static clj_node *node_const(clj_value v) {
	clj_node *n = node_new(CLJ_NODE_CONST);
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
		if (c->items[i].from_captured == outer_captured && c->items[i].index == outer_index) {
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
	c->items[c->n] = (clj_capture){outer_captured, outer_index};
	*captured = true;
	*index = c->n++;
	return true;
}

static bool is_unqualified_symbol(clj_value v) { return clj_is_symbol(v) && clj_is_nil(clj_symbol_ns(v)); }

static bool symbol_is(clj_value sym, const char *name) {
	return is_unqualified_symbol(sym) && strcmp(clj_string_bytes(clj_symbol_name(sym)), name) == 0;
}

typedef enum { SP_NONE, SP_QUOTE, SP_IF, SP_DO, SP_LET, SP_LOOP, SP_FN, SP_DEF, SP_DEFMACRO, SP_RECUR, SP_VAR, SP_RESERVED } special;

static const struct {
	const char *name;
	special     kind;
} specials[] = {
	{"quote", SP_QUOTE}, {"if", SP_IF},     {"do", SP_DO},         {"let", SP_LET},     {"let*", SP_LET},
	{"loop", SP_LOOP},   {"loop*", SP_LOOP}, {"fn", SP_FN},         {"fn*", SP_FN},      {"def", SP_DEF},
	{"defmacro", SP_DEFMACRO}, {"recur", SP_RECUR}, {"var", SP_VAR},
	// Kept unqualified by syntax-quote: `&` in params; throw/try/catch/finally before they become special forms.
	{"&", SP_RESERVED}, {"throw", SP_RESERVED}, {"try", SP_RESERVED}, {"catch", SP_RESERVED}, {"finally", SP_RESERVED},
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

// Borrowed items of a list or vector in a malloc'd array.
static clj_value *seq_items(clj_value seq, uint32_t *n) {
	size_t     count = clj_list_count(seq);
	clj_value *items = zalloc(count, sizeof *items);
	clj_seq_iter it = clj_seq_iter_start(seq);
	size_t       i = 0;
	while (clj_seq_iter_next(&it, &items[i])) i++;
	*n = (uint32_t)count;
	return items;
}

static clj_node *analyze(analyzer *a, scope *s, clj_value form, bool tail);

static clj_node *analyze_symbol(analyzer *a, scope *s, clj_value sym) {
	if (clj_is_nil(clj_symbol_ns(sym))) {
		bool     captured;
		uint32_t index;
		if (resolve_local(s, sym, &captured, &index)) {
			clj_node *n = node_new(captured ? CLJ_NODE_CAPTURED : CLJ_NODE_LOCAL);
			n->u.index = index;
			return n;
		}
	}
	clj_value var = clj_ns_resolve(a->env.ns, sym);
	if (clj_is_nil(var)) return fail_form(a, "Unable to resolve symbol: %s in this context", sym);
	if (clj_var_is_macro(var)) return fail_form(a, "Can't take value of a macro: %s", var);
	clj_node *n = node_new(CLJ_NODE_VAR);
	n->u.var = clj_retain(var);
	return n;
}

// ---- macro expansion

enum { MAX_EXPANSION_STEPS = 1000 };

// The macro var a list form's head names, or nil: a local of s (NULL at top level) shadows it.
static clj_value macro_var(const analyzer *a, scope *s, clj_value form) {
	if (!clj_is_list(form) || clj_is_empty_list(form)) return CLJ_NIL;
	clj_value head = clj_cons_of(form)->first;
	if (!clj_is_symbol(head) || clj_is_special_symbol(head)) return CLJ_NIL;
	if (s && clj_is_nil(clj_symbol_ns(head))) {
		bool     captured;
		uint32_t index;
		if (resolve_local(s, head, &captured, &index)) return CLJ_NIL;
	}
	clj_value var = clj_ns_resolve(a->env.ns, head);
	return !clj_is_nil(var) && clj_var_is_macro(var) ? var : CLJ_NIL;
}

// (macro &form &env args...) with &env nil: there is no local environment value yet.
static clj_value expand_once(const analyzer *a, clj_value var, clj_value form) {
	clj_value fn = clj_var_deref(var);
	if (fn == CLJ_THROWN) return rethrow_positioned(a);
	uint32_t   n;
	clj_value *items = seq_items(form, &n);
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
static clj_value expand_all(const analyzer *a, scope *s, clj_value form) {
	clj_value cur = clj_retain(form);
	for (uint32_t step = 0;; step++) {
		clj_value var = macro_var(a, s, cur);
		if (clj_is_nil(var)) return cur;
		if (step == MAX_EXPANSION_STEPS) {
			clj_value text = clj_pr_str(cur);
			clj_release(cur);
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
	analyzer a = {.env = env ? *env : (clj_env){0}};
	if (clj_is_nil(a.env.ns)) a.env.ns = clj_ns_current();
	return a;
}

clj_value clj_macroexpand_1(clj_value form, const clj_env *env) {
	analyzer  a = analyzer_for(env);
	clj_value var = macro_var(&a, NULL, form);
	return clj_is_nil(var) ? clj_retain(form) : expand_once(&a, var, form);
}

clj_value clj_macroexpand(clj_value form, const clj_env *env) {
	analyzer a = analyzer_for(env);
	return expand_all(&a, NULL, form);
}

static bool all_const(clj_node *const *nodes, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) {
		if (nodes[i]->kind != CLJ_NODE_CONST) return false;
	}
	return true;
}

// A literal whose elements all analyzed to constants folds into one constant.
static clj_node *fold_or_keep(clj_node *node, clj_value (*build)(clj_node *const *, uint32_t)) {
	if (!all_const(node->u.seq.items, node->u.seq.n)) return node;
	clj_value v = build(node->u.seq.items, node->u.seq.n);
	clj_node *c = node_const(v);
	clj_release(v);
	clj_release(clj_from_ptr(node));
	return c;
}

static clj_value build_vector(clj_node *const *items, uint32_t n) {
	clj_value *vals = zalloc(n, sizeof *vals);
	for (uint32_t i = 0; i < n; i++) vals[i] = items[i]->u.value;
	clj_value v = clj_vector_from_array(vals, n);
	free(vals);
	return v;
}

static clj_value build_map(clj_node *const *items, uint32_t n) {
	clj_value m = clj_map_empty();
	for (uint32_t i = 0; i < n; i += 2) m = clj_map_assoc(m, items[i]->u.value, items[i + 1]->u.value);
	return m;
}

static bool analyze_into(analyzer *a, scope *s, clj_node **out, const clj_value *forms, uint32_t n, bool tail_last) {
	for (uint32_t i = 0; i < n; i++) {
		out[i] = analyze(a, s, forms[i], tail_last && i + 1 == n);
		if (!out[i]) return false;
	}
	return true;
}

static clj_node *analyze_vector(analyzer *a, scope *s, clj_value form) {
	uint32_t   n;
	clj_value *items = seq_items(form, &n);
	clj_node  *node = node_new(CLJ_NODE_VECTOR);
	node->u.seq.items = zalloc(n, sizeof *node->u.seq.items);
	node->u.seq.n = n;
	bool ok = analyze_into(a, s, node->u.seq.items, items, n, false);
	free(items);
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return fold_or_keep(node, build_vector);
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
	clj_node *node = node_new(CLJ_NODE_MAP);
	node->u.seq.items = zalloc(n, sizeof *node->u.seq.items);
	node->u.seq.n = n;
	bool ok = analyze_into(a, s, node->u.seq.items, entries, n, false);
	free(entries);
	if (!ok) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return fold_or_keep(node, build_map);
}

// Zero forms is nil, one is itself, more is a do.
static clj_node *analyze_body(analyzer *a, scope *s, const clj_value *forms, uint32_t n, bool tail) {
	if (n == 0) return node_const(CLJ_NIL);
	if (n == 1) return analyze(a, s, forms[0], tail);
	clj_node *node = node_new(CLJ_NODE_DO);
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
	clj_node *node = node_new(CLJ_NODE_IF);
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
	clj_value *forms = seq_items(items[1], &nforms);
	if (nforms % 2) {
		free(forms);
		return fail(a, "%s requires an even number of forms in binding vector", what);
	}
	uint32_t  nb = nforms / 2;
	clj_node *node = node_new(loop ? CLJ_NODE_LOOP : CLJ_NODE_LET);
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
			s->recur = &target;
			node->u.let.body = analyze_body(a, s, items + 2, n - 2, true);
			s->recur = saved;
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
	if (!tail || !s->recur) return fail(a, "Can only recur from tail position");
	uint32_t nargs = n - 1;
	if (nargs != s->recur->n) return fail(a, "Mismatched argument count to recur, expected: %u args, got: %u", s->recur->n, nargs);
	clj_node *node = node_new(CLJ_NODE_RECUR);
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
	clj_value *syms = seq_items(params, &nparams);
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
	clj_node *node = node_new(CLJ_NODE_FN);
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
			if (!clj_is_list(items[i]) || clj_is_empty_list(items[i])) {
				ok = fail_form(a, "Parameter declaration %s should be a vector", items[i]) != NULL;
				break;
			}
			uint32_t   nsig;
			clj_value *sig = seq_items(items[i], &nsig);
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

static clj_node *analyze_def(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	if (n < 2) return fail(a, "Too few arguments to def");
	if (n > 3) return fail(a, "Too many arguments to def");
	clj_value sym = items[1];
	if (!clj_is_symbol(sym)) return fail(a, "First argument to def must be a Symbol");
	clj_value ns_name = clj_symbol_name(clj_ns_name(a->env.ns));
	clj_value name = sym;
	if (!clj_is_nil(clj_symbol_ns(sym))) {
		if (!clj_equals(clj_symbol_ns(sym), ns_name)) return fail(a, "Can't create defs outside of current ns");
		name = clj_symbol_new(CLJ_NIL, clj_symbol_name(sym));
	}
	clj_node *node = node_new(CLJ_NODE_DEF);
	node->u.def.var = clj_retain(clj_ns_intern(a->env.ns, name));
	if (name != sym) clj_release(name);
	if (n == 3) {
		node->u.def.init = analyze(a, s, items[2], false);
		if (!node->u.def.init) {
			clj_release(clj_from_ptr(node));
			return NULL;
		}
		// (def f (fn ...)) names the fn after the var, as Clojure does, so arity errors can say who.
		clj_node *init = node->u.def.init;
		if (init->kind == CLJ_NODE_FN && clj_is_nil(init->u.fn.name)) init->u.fn.name = clj_symbol_new(ns_name, clj_symbol_name(sym));
	}
	return node;
}

static clj_node *analyze_invoke(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	clj_node *node = node_new(CLJ_NODE_INVOKE);
	node->u.invoke.args = zalloc(n - 1, sizeof *node->u.invoke.args);
	node->u.invoke.n = n - 1;
	if (!(node->u.invoke.fn = analyze(a, s, items[0], false)) || !analyze_into(a, s, node->u.invoke.args, items + 1, n - 1, false)) {
		clj_release(clj_from_ptr(node));
		return NULL;
	}
	return node;
}

// [&form &env params...]
static clj_value macro_params(clj_value params) {
	uint32_t   n;
	clj_value *syms = seq_items(params, &n);
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

// A def of the fn with &form and &env prepended to every arity; the var is flagged when the def runs.
static clj_node *analyze_defmacro(analyzer *a, scope *s, const clj_value *items, uint32_t n) {
	if (n < 2 || !clj_is_symbol(items[1])) return fail(a, "First argument to defmacro must be a Symbol");
	uint32_t i = 2;
	if (i < n && clj_is_string(items[i])) i++; // docstring; no metadata to keep it in yet
	if (i >= n) return fail(a, "Parameter declaration missing");
	clj_value *fn_items = zalloc(n - i + 1, sizeof *fn_items);
	uint32_t   nfn = 0;
	fn_items[nfn++] = clj_symbol_from_cstr("fn");
	if (clj_is_vector(items[i])) {
		fn_items[nfn++] = macro_params(items[i]);
		for (uint32_t j = i + 1; j < n; j++) fn_items[nfn++] = clj_retain(items[j]);
	} else {
		for (uint32_t j = i; j < n; j++) {
			clj_value arity = items[j];
			if (clj_is_list(arity) && !clj_is_empty_list(arity) && clj_is_vector(clj_cons_of(arity)->first)) {
				clj_value params = macro_params(clj_cons_of(arity)->first);
				arity = clj_cons_new(params, clj_cons_of(arity)->rest);
				clj_release(params);
			} else {
				clj_retain(arity); // analyze_fn reports the malformed arity
			}
			fn_items[nfn++] = arity;
		}
	}
	clj_value fn_form = clj_list_from_array(fn_items, nfn);
	for (uint32_t j = 0; j < nfn; j++) clj_release(fn_items[j]);
	free(fn_items);
	clj_value def_items[3] = {items[0], items[1], fn_form};
	clj_node *node = analyze_def(a, s, def_items, 3);
	clj_release(fn_form);
	if (node) node->u.def.macro = true;
	return node;
}

static clj_node *analyze_var(analyzer *a, const clj_value *items, uint32_t n) {
	if (n != 2) return fail(a, "Wrong number of args (%u) passed to var", n - 1);
	if (!clj_is_symbol(items[1])) return fail_form(a, "var requires a symbol, got: %s", items[1]);
	clj_value var = clj_ns_resolve(a->env.ns, items[1]);
	if (clj_is_nil(var)) return fail_form(a, "Unable to resolve var: %s in this context", items[1]);
	return node_const(var);
}

static clj_node *analyze_list(analyzer *a, scope *s, clj_value form, bool tail) {
	clj_value expanded = expand_all(a, s, form);
	if (expanded == CLJ_THROWN) return NULL;
	if (expanded != form) {
		clj_node *node = analyze(a, s, expanded, tail);
		clj_release(expanded);
		return node;
	}
	clj_release(expanded);
	uint32_t   n;
	clj_value *items = seq_items(form, &n);
	clj_node  *node;
	switch (special_of(items[0])) {
	case SP_QUOTE: node = n == 2 ? node_const(items[1]) : fail(a, "Wrong number of args (%u) passed to quote", n - 1); break;
	case SP_IF: node = analyze_if(a, s, items, n, tail); break;
	case SP_DO: node = analyze_body(a, s, items + 1, n - 1, tail); break;
	case SP_LET: node = analyze_let(a, s, items, n, tail, false); break;
	case SP_LOOP: node = analyze_let(a, s, items, n, tail, true); break;
	case SP_FN: node = analyze_fn(a, s, items, n); break;
	case SP_DEF: node = analyze_def(a, s, items, n); break;
	case SP_DEFMACRO: node = analyze_defmacro(a, s, items, n); break;
	case SP_RECUR: node = analyze_recur(a, s, items, n, tail); break;
	case SP_VAR: node = analyze_var(a, items, n); break;
	case SP_NONE:
	case SP_RESERVED: node = analyze_invoke(a, s, items, n); break;
	}
	free(items);
	return node;
}

static clj_node *analyze(analyzer *a, scope *s, clj_value form, bool tail) {
	if (clj_is_symbol(form)) return analyze_symbol(a, s, form);
	if (clj_is_list(form)) return clj_is_empty_list(form) ? node_const(form) : analyze_list(a, s, form, tail);
	if (clj_is_vector(form)) return analyze_vector(a, s, form);
	if (is_map(form)) return analyze_map(a, s, form);
	return node_const(form);
}

clj_node *clj_analyze(clj_value form, const clj_env *env, uint32_t *nslots) {
	analyzer  a = analyzer_for(env);
	scope     top = {0};
	clj_node *node = analyze(&a, &top, form, false);
	free(top.locals);
	*nslots = top.nslots;
	return node;
}
