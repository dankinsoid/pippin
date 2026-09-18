// @ai-generated(solo)
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/eval.h"
#include "clj/facts.h"
#include "clj/summary.h"
#include "cljc/compiler.h"

// ---- string builder

typedef struct {
	char  *s;
	size_t len, cap;
} sb;

static void sb_put(sb *b, const char *s, size_t n) {
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		while (cap < b->len + n + 1) cap *= 2;
		b->s = realloc(b->s, cap);
		if (!b->s) clj_fatal("out of memory");
		b->cap = cap;
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = '\0';
}

static void sb_puts(sb *b, const char *s) { sb_put(b, s, strlen(s)); }

static void sb_printf(sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_printf(sb *b, const char *fmt, ...) {
	char    small[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(small, sizeof small, fmt, ap);
	va_end(ap);
	if (n < 0) clj_fatal("vsnprintf failed");
	if ((size_t)n < sizeof small) {
		sb_put(b, small, (size_t)n);
		return;
	}
	char *big = malloc((size_t)n + 1);
	if (!big) clj_fatal("out of memory");
	va_start(ap, fmt);
	vsnprintf(big, (size_t)n + 1, fmt, ap);
	va_end(ap);
	sb_put(b, big, (size_t)n);
	free(big);
}

static void sb_free(sb *b) {
	free(b->s);
	b->s = NULL;
	b->len = b->cap = 0;
}

static char *xstrdup(const char *s) {
	char *d = strdup(s);
	if (!d) clj_fatal("out of memory");
	return d;
}

// A C string literal of arbitrary bytes: octal escapes are bounded to three digits, so a digit may follow.
static void sb_c_string(sb *b, const char *s, size_t n) {
	sb_puts(b, "\"");
	for (size_t i = 0; i < n; i++) {
		unsigned char ch = (unsigned char)s[i];
		if (ch == '"' || ch == '\\') sb_printf(b, "\\%c", ch);
		else if (ch >= 32 && ch < 127) sb_put(b, (const char *)&ch, 1);
		else sb_printf(b, "\\%03o", ch);
	}
	sb_puts(b, "\"");
}

// ---- name mangling (NOTES.md, "Compiler": the demangling rule)

static void munge_into(sb *b, const char *s) {
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		unsigned char ch = *p;
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) sb_put(b, (const char *)&ch, 1);
		else if (ch == '-' || ch == '.') sb_puts(b, "_");
		else if (ch == '_') sb_puts(b, "_USCORE_");
		else if (ch == '?') sb_puts(b, "_QMARK_");
		else if (ch == '!') sb_puts(b, "_BANG_");
		else if (ch == '*') sb_puts(b, "_STAR_");
		else if (ch == '+') sb_puts(b, "_PLUS_");
		else if (ch == '>') sb_puts(b, "_GT_");
		else if (ch == '<') sb_puts(b, "_LT_");
		else if (ch == '=') sb_puts(b, "_EQ_");
		else if (ch == '/') sb_puts(b, "_SLASH_");
		else if (ch == '\'') sb_puts(b, "_QUOTE_");
		else if (ch == '&') sb_puts(b, "_AMP_");
		else if (ch == '%') sb_puts(b, "_PCT_");
		else if (ch == '#') sb_puts(b, "_HASH_");
		else if (ch == ':') sb_puts(b, "_COLON_");
		else if (ch == '$') sb_puts(b, "_DOLLAR_");
		else sb_printf(b, "_u%02x_", ch);
	}
}

char *cljc_mangle(const char *ns, const char *name) {
	sb b = {0};
	if (ns && *ns) {
		munge_into(&b, ns);
		sb_puts(&b, "_");
	}
	munge_into(&b, name);
	if (b.len == 0 || (b.s[0] >= '0' && b.s[0] <= '9')) {
		sb p = {0};
		sb_puts(&p, "_");
		sb_puts(&p, b.s ? b.s : "");
		sb_free(&b);
		return p.s;
	}
	return b.s;
}

// ---- pools: constants, vars, intrinsic ops, fusion vars, each deduplicated by text

typedef struct {
	char **keys;
	char **extra; // a second string per entry, when the pool needs one
	size_t n, cap;
} pool;

static size_t pool_intern(pool *p, const char *key, const char *extra, bool *fresh) {
	for (size_t i = 0; i < p->n; i++) {
		if (strcmp(p->keys[i], key) == 0 && (!extra || strcmp(p->extra[i], extra) == 0)) {
			*fresh = false;
			return i;
		}
	}
	if (p->n == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 16;
		p->keys = realloc(p->keys, p->cap * sizeof *p->keys);
		p->extra = realloc(p->extra, p->cap * sizeof *p->extra);
		if (!p->keys || !p->extra) clj_fatal("out of memory");
	}
	p->keys[p->n] = xstrdup(key);
	p->extra[p->n] = extra ? xstrdup(extra) : NULL;
	*fresh = true;
	return p->n++;
}

static void pool_free(pool *p) {
	for (size_t i = 0; i < p->n; i++) {
		free(p->keys[i]);
		free(p->extra[i]);
	}
	free(p->keys);
	free(p->extra);
	memset(p, 0, sizeof *p);
}

// ---- units

// A var (def)'d with a fn init in the compiled set: the direct-call target of closed mode.
typedef struct {
	char    *qualified; // ns/name
	char    *base;      // C symbol base of its latest definition
	uint32_t defs;      // definitions seen; only a unique one is called directly
	uint32_t fixed;     // bit n: fixed arity n exists
	int32_t  variadic;  // nparams of the variadic arity, -1 when none
	bool     dynamic;
} direct_entry;

typedef struct {
	char *file;   // the path *file* held, or the core path
	char *cfile;  // munged file name
	sb    protos; // prototypes of every function of the unit
	sb    fns;    // function bodies, callee before caller
	sb    init;   // the init statements in form order
	pool  consts, vars, ops, fusion;
	pool  names;    // C symbol bases handed out, to number redefinitions
	pool  externs;  // direct-call targets referenced: "base_aN"
	pool  defined;  // bases of the vars this unit defines with a fn init, with their qualified names
	pool  impl_regs; // fn bases a closed init registers as protocol impls, with the arity mask a direct arm may call
	uint32_t nstubs, ntops, nforms;
	cljc_slot_stats slots;
	struct proto_site *psites; // the protocol call sites, classified for --stats once every arm's target is resolved
	size_t             npsites, psites_cap;
	uint64_t last_serial; // of the last form whose statements were emitted
	bool     serial_open; // the failure label of the current form is still reachable
	bool     embedded;
	bool     has_file; // K[0] is the file string; a host eval has none
	bool     eval_result;
} unit;

// What the hook saw, kept until cljc_end: emission waits for the whole set, so that every fn is emitted under the
// caller join of every caller the load defined (the compiler's closed-world round over the dev store, NOTES.md).
typedef struct {
	unit          *u;
	clj_load_form  form;    // file and name retained
	const clj_node *node;   // retained; NULL for a failure
	clj_value      ns;      // the current namespace at the hook
	clj_value      message; // of a failure, retained
} pending_form;

// A closure fn node the compiler emitted: how a protocol impl found in the tables as an interpreted closure is named.
// arities: the fixed arities a direct arm may enter without the closure object (no captures, no self slot, <= 64 slots).
typedef struct {
	const clj_node *node;
	char           *base;
	unit           *u;
	uint32_t        arities;
} fn_base;

// A direct arm's target, resolved at write time into CLJC_PIMPL_<id>: an emitted node, or a registered impl name.
typedef struct {
	uint32_t        id;
	unit           *u;    // the requesting unit
	const clj_node *node; // NULL when named
	char           *name;
	uint32_t        nargs;
} pimpl;

struct cljc_compiler {
	cljc_options  opts;
	unit        **units;
	size_t        nunits, units_cap;
	cljc_refusal *refusals;
	size_t        nrefusals, refusals_cap;
	direct_entry *directs;
	size_t        ndirects, directs_cap;
	pending_form *pending;
	size_t        npending, pending_cap;
	fn_base      *fn_bases;
	size_t        nfn_bases, fn_bases_cap;
	pimpl        *pimpls;
	size_t        npimpls, pimpls_cap;
	uint32_t      next_pimpl;
	const clj_node **candidates; // fn nodes given as method impls to deftype*, record* and extend: registered by a closed init
	size_t           ncandidates, candidates_cap;
	const clj_node  *emitting; // root of the form being emitted
	uint32_t         neval_forms;
	bool          installed;
};

// Whether a closure's tree is one of this compile's forms: only then does its fn node get a base to name.
static bool tree_in_set(const cljc_compiler *c, const clj_fn *fn) {
	const clj_node *root = clj_exec_of(fn->code)->root;
	if (root == c->emitting) return true;
	for (size_t i = 0; i < c->npending; i++) {
		if (c->pending[i].node == root) return true;
	}
	return false;
}

static void fn_base_add(cljc_compiler *c, const clj_node *node, const char *base, unit *u, uint32_t arities) {
	for (size_t i = 0; i < c->nfn_bases; i++) {
		if (c->fn_bases[i].node != node) continue;
		// the address of a dead tree's node reused by a live one: the newer mapping is the true one
		free(c->fn_bases[i].base);
		c->fn_bases[i] = (fn_base){node, xstrdup(base), u, arities};
		return;
	}
	if (c->nfn_bases == c->fn_bases_cap) {
		c->fn_bases_cap = c->fn_bases_cap ? c->fn_bases_cap * 2 : 64;
		c->fn_bases = realloc(c->fn_bases, c->fn_bases_cap * sizeof *c->fn_bases);
		if (!c->fn_bases) clj_fatal("out of memory");
	}
	c->fn_bases[c->nfn_bases++] = (fn_base){node, xstrdup(base), u, arities};
}

static const fn_base *fn_base_of(const cljc_compiler *c, const clj_node *node) {
	for (size_t i = 0; i < c->nfn_bases; i++) {
		if (c->fn_bases[i].node == node) return &c->fn_bases[i];
	}
	return NULL;
}

static bool candidate(const cljc_compiler *c, const clj_node *node) {
	for (size_t i = 0; i < c->ncandidates; i++) {
		if (c->candidates[i] == node) return true;
	}
	return false;
}

static void candidate_add(cljc_compiler *c, const clj_node *node) {
	if (candidate(c, node)) return;
	if (c->ncandidates == c->candidates_cap) {
		c->candidates_cap = c->candidates_cap ? c->candidates_cap * 2 : 64;
		c->candidates = realloc(c->candidates, c->candidates_cap * sizeof *c->candidates);
		if (!c->candidates) clj_fatal("out of memory");
	}
	c->candidates[c->ncandidates++] = node;
}

static uint32_t pimpl_add(cljc_compiler *c, unit *u, const clj_node *node, const char *name, uint32_t nargs) {
	if (c->npimpls == c->pimpls_cap) {
		c->pimpls_cap = c->pimpls_cap ? c->pimpls_cap * 2 : 64;
		c->pimpls = realloc(c->pimpls, c->pimpls_cap * sizeof *c->pimpls);
		if (!c->pimpls) clj_fatal("out of memory");
	}
	pimpl *p = &c->pimpls[c->npimpls++];
	*p = (pimpl){c->next_pimpl++, u, node, name ? xstrdup(name) : NULL, nargs};
	return p->id;
}

static void impl_reg_add(unit *u, const char *base, uint32_t arities) {
	char mask[16];
	snprintf(mask, sizeof mask, "%u", arities);
	bool fresh;
	pool_intern(&u->impl_regs, base, mask, &fresh);
}

static unit *unit_new(const char *file) {
	unit *u = calloc(1, sizeof *u);
	if (!u) clj_fatal("out of memory");
	u->file = xstrdup(file);
	const char *base = strrchr(file, '/');
	u->cfile = cljc_mangle(NULL, base ? base + 1 : file);
	u->embedded = strncmp(file, "<embedded>/", 11) == 0 || strcmp(file, CLJ_CORE_CLJ_PATH) == 0;
	u->has_file = strcmp(file, "<host>") != 0;
	if (u->has_file) {
		clj_value str = clj_string_from_cstr(file);
		clj_value text = clj_pr_str(str);
		sb        init = {0};
		sb_puts(&init, "clj_c_const(");
		sb_c_string(&init, clj_string_bytes(text), clj_string_len(text));
		sb_printf(&init, ", %u)", clj_string_len(text));
		bool fresh;
		pool_intern(&u->consts, "file", init.s, &fresh);
		sb_free(&init);
		clj_release(text);
		clj_release(str);
	}
	return u;
}

static unit *unit_for(cljc_compiler *c, const char *file) {
	for (size_t i = 0; i < c->nunits; i++) {
		if (strcmp(c->units[i]->file, file) == 0) return c->units[i];
	}
	unit *u = unit_new(file);
	if (c->nunits == c->units_cap) {
		c->units_cap = c->units_cap ? c->units_cap * 2 : 8;
		c->units = realloc(c->units, c->units_cap * sizeof *c->units);
		if (!c->units) clj_fatal("out of memory");
	}
	c->units[c->nunits++] = u;
	return u;
}

static void unit_free(unit *u) {
	free(u->psites);
	free(u->file);
	free(u->cfile);
	sb_free(&u->protos);
	sb_free(&u->fns);
	sb_free(&u->init);
	pool_free(&u->consts);
	pool_free(&u->vars);
	pool_free(&u->ops);
	pool_free(&u->fusion);
	pool_free(&u->names);
	pool_free(&u->externs);
	pool_free(&u->defined);
	pool_free(&u->impl_regs);
	free(u);
}

static direct_entry *direct_find(cljc_compiler *c, const char *qualified) {
	for (size_t i = 0; i < c->ndirects; i++) {
		if (strcmp(c->directs[i].qualified, qualified) == 0) return &c->directs[i];
	}
	return NULL;
}

static direct_entry *direct_add(cljc_compiler *c, const char *qualified) {
	direct_entry *e = direct_find(c, qualified);
	if (e) return e;
	if (c->ndirects == c->directs_cap) {
		c->directs_cap = c->directs_cap ? c->directs_cap * 2 : 64;
		c->directs = realloc(c->directs, c->directs_cap * sizeof *c->directs);
		if (!c->directs) clj_fatal("out of memory");
	}
	e = &c->directs[c->ndirects++];
	memset(e, 0, sizeof *e);
	e->qualified = xstrdup(qualified);
	e->variadic = -1;
	return e;
}

// ---- the function context

typedef enum { OWN_NO, OWN_YES, OWN_DYN } own_kind;

typedef struct {
	char     name[24];
	own_kind own;
	char     flag[24]; // the bool of an OWN_DYN temp
} temp;

typedef struct {
	int  label;
	int  live_mark;
	bool used;
} handler;

typedef struct fnctx {
	cljc_compiler *c;
	unit          *u;
	sb             out;
	int            ntemp, nlabel, naux; // temps, labels, and the arrays and scratch names beside them
	const char    *frame;  // C name of the clj_cframe
	const clj_facts *facts;    // of the top-level tree being emitted
	uint64_t         promoted; // slots of the running frame that are C variables l<i>, not fr.slots[i]
	uint64_t         borrowed; // the promoted slots that hold a +0 value for the whole call: never released, never rebound
	uint64_t         ints;     // the promoted slots that are int64_t variables: every binding is an unboxed expression
	uint64_t         dbls;     // the same as double variables
	const clj_node  *body;     // the frame's body, what the typed-slot selection walks
	bool             in_split; // inside a branch of an entry-checked loop, where no further split is taken
	const struct fnctx *definer; // the context of the frame a direct fn body links to, for the promotion check of OUTER reads
	temp          *live;
	int            nlive, live_cap;
	handler       *handlers;
	int            nhandlers, handlers_cap;
	int            recur_label; // 0: none
	bool           recur_used;
	bool           recur_loop;  // the target is a loop (deadline tick), else a fn body
	int            recur_live_mark;
	const char    *base;        // C symbol base of the enclosing top-level definition
	uint32_t      *fn_counter;  // anonymous fns numbered within the top-level form
	const char    *ns;          // current namespace name, for anonymous fn names
	uint32_t       last_line;
	const clj_load_form *form;
	bool                 top; // the top-level form's own context, where a def names its fn after itself
} fnctx;

static void fn_line(fnctx *f, const clj_node *n) {
	if (!f->c->opts.line || !n->line || n->line == f->last_line) return;
	f->last_line = n->line;
	sb_printf(&f->out, "#line %u ", n->line);
	sb_c_string(&f->out, f->u->file, strlen(f->u->file));
	sb_puts(&f->out, "\n");
}

static temp new_temp(fnctx *f, own_kind own) {
	temp t;
	memset(&t, 0, sizeof t);
	snprintf(t.name, sizeof t.name, "t%d", f->ntemp);
	if (own == OWN_DYN) snprintf(t.flag, sizeof t.flag, "o%d", f->ntemp);
	f->ntemp++;
	t.own = own;
	return t;
}

static int new_label(fnctx *f) { return ++f->nlabel; }

static void live_push(fnctx *f, temp t) {
	if (t.own == OWN_NO) return;
	if (f->nlive == f->live_cap) {
		f->live_cap = f->live_cap ? f->live_cap * 2 : 16;
		f->live = realloc(f->live, f->live_cap * sizeof *f->live);
		if (!f->live) clj_fatal("out of memory");
	}
	f->live[f->nlive++] = t;
}

// Forgets a temp whose reference moved elsewhere.
static void live_forget(fnctx *f, const temp *t) {
	if (t->own == OWN_NO) return;
	for (int i = f->nlive - 1; i >= 0; i--) {
		if (strcmp(f->live[i].name, t->name) == 0) {
			memmove(&f->live[i], &f->live[i + 1], (size_t)(f->nlive - i - 1) * sizeof *f->live);
			f->nlive--;
			return;
		}
	}
	clj_fatal("compiler: temp not live");
}

static void emit_release_of(sb *out, const temp *t) {
	if (t->own == OWN_YES) sb_printf(out, "\tclj_release(%s);\n", t->name);
	else if (t->own == OWN_DYN) sb_printf(out, "\tif (%s) clj_release(%s);\n", t->flag, t->name);
}

static void release_temp(fnctx *f, const temp *t) {
	if (t->own == OWN_NO) sb_printf(&f->out, "\t(void)%s;\n", t->name);
	emit_release_of(&f->out, t);
	live_forget(f, t);
}

static void push_handler(fnctx *f, int label) {
	if (f->nhandlers == f->handlers_cap) {
		f->handlers_cap = f->handlers_cap ? f->handlers_cap * 2 : 8;
		f->handlers = realloc(f->handlers, f->handlers_cap * sizeof *f->handlers);
		if (!f->handlers) clj_fatal("out of memory");
	}
	f->handlers[f->nhandlers++] = (handler){label, f->nlive, false};
}

static handler pop_handler(fnctx *f) { return f->handlers[--f->nhandlers]; }

// Releases the temps live above the innermost handler's mark and jumps to it.
static void emit_unwind(fnctx *f) {
	handler *h = &f->handlers[f->nhandlers - 1];
	for (int i = f->nlive - 1; i >= h->live_mark; i--) emit_release_of(&f->out, &f->live[i]);
	sb_printf(&f->out, "\tgoto L%d;\n", h->label);
	h->used = true;
}

static void check_thrown(fnctx *f, const char *name) {
	sb_printf(&f->out, "\tif (%s == CLJ_THROWN) {\n", name);
	emit_unwind(f);
	sb_puts(&f->out, "\t}\n");
}

// ---- slot promotion (NOTES.md, "Compiler": promoted slots)

// Slots read through fr.slots by a capture or an OUTER node; the facts pass does not charge OUTER reads to the definer.
typedef struct {
	uint32_t level; // direct-fn frames entered below the scanned frame
	uint64_t pinned;
	uint64_t rebound; // recur targets of the scanned frame's own body: a param among them changes owner
} pin_scan;

static void pin_walk(const clj_node *n, void *ctx);

// A deeper read names an ancestor frame, which pins it in its own scan.
static void pin_index(pin_scan *p, uint32_t depth, uint32_t index) {
	if (depth == p->level && index < 64) p->pinned |= (uint64_t)1 << index;
}

static void pin_walk(const clj_node *n, void *ctx) {
	pin_scan *p = ctx;
	switch (n->kind) {
	case CLJ_NODE_OUTER: pin_index(p, n->u.outer.depth, n->u.outer.index); return;
	case CLJ_NODE_RECUR:
		for (uint32_t i = 0; p->level == 0 && i < n->u.recur.n; i++) {
			if (n->u.recur.slots[i] < 64) p->rebound |= (uint64_t)1 << n->u.recur.slots[i];
		}
		clj_node_children(n, pin_walk, p);
		return;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			const clj_capture *cp = &n->u.fn.captures[i];
			if (cp->kind == CLJ_CAPTURE_LOCAL) pin_index(p, 0, cp->index);
			else if (cp->kind == CLJ_CAPTURE_OUTER) pin_index(p, cp->depth, cp->index);
		}
		return; // its arities are frames without a static link
	case CLJ_NODE_DIRECT_FN:
		p->level++;
		clj_node_children(n, pin_walk, p);
		p->level--;
		return;
	case CLJ_NODE_FUSED:
		// the argument frame links to nothing, so an OUTER inside it is over-counted, never missed
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) pin_walk(n->u.fused.args[i], p);
		p->level++;
		pin_walk(n->u.fused.fused, p);
		pin_walk(n->u.fused.original, p);
		p->level--;
		return;
	default: clj_node_children(n, pin_walk, p);
	}
}

// The facts frame whose body this is; UINT32_MAX when the table has none.
static uint32_t facts_frame(const clj_facts *facts, const clj_node *owner, const clj_node *body) {
	uint32_t id = owner ? owner->id : UINT32_MAX;
	for (uint32_t i = 0; i < clj_facts_nframes(facts); i++) {
		const clj_facts_frame *fr = clj_facts_frame_at(facts, i);
		if (fr->owner == id && fr->first == body->id) return i;
	}
	return UINT32_MAX;
}

// Sets f->promoted and f->borrowed. A promoted slot is owned-or-nil for its whole life, or (a closure's param or
// self slot no recur rebinds) borrowed for its whole life; a direct fn's params are owned by the caller's mask.
static void select_typed_slots(fnctx *f, const clj_node *body);

static void promote_slots(fnctx *f, const clj_node *owner, const clj_fn_arity *a, bool closure, const clj_node *body, uint32_t nslots) {
	cljc_slot_stats *st = &f->u->slots;
	f->promoted = f->borrowed = f->ints = f->dbls = 0;
	f->body = body;
	if (!f->facts || nslots == 0) return;
	uint32_t fi = facts_frame(f->facts, owner, body);
	if (fi == UINT32_MAX) return;
	pin_scan p = {0, 0, 0};
	pin_walk(body, &p);
	for (uint32_t i = 0; i < nslots; i++) {
		clj_escape e = clj_facts_escape(f->facts, fi, i);
		bool       param = a && (i < a->nparams || (int32_t)i == a->self_slot);
		// a frame past 64 slots retains every param and releases every array entry
		bool pinned = nslots > 64 || i >= 64 || ((p.pinned >> i) & 1);
		if (param && (!closure || ((p.rebound >> i) & 1))) {
			if (e == CLJ_ESCAPE_LOCAL) st->local_param++;
			continue;
		}
		if (pinned) {
			if (e == CLJ_ESCAPE_LOCAL) st->local_pinned++;
			continue;
		}
		if (e == CLJ_ESCAPE_CAPTURED) continue;
		f->promoted |= (uint64_t)1 << i;
		if (param) f->borrowed |= (uint64_t)1 << i;
		st->promoted++;
		if (e == CLJ_ESCAPE_LOCAL) st->promoted_local++;
	}
	select_typed_slots(f, body);
}

static bool promoted(const fnctx *f, uint32_t i) { return i < 64 && ((f->promoted >> i) & 1); }
static bool borrowed_slot(const fnctx *f, uint32_t i) { return i < 64 && ((f->borrowed >> i) & 1); }
static bool int_slot(const fnctx *f, uint32_t i) { return i < 64 && ((f->ints >> i) & 1); }
static bool dbl_slot(const fnctx *f, uint32_t i) { return i < 64 && ((f->dbls >> i) & 1); }

// ---- unboxed arithmetic (NOTES.md, "Compiler": unboxed arithmetic, int64 and double slots, entry-checked frames)

typedef enum { UNBOX_NONE, UNBOX_ADD, UNBOX_SUB, UNBOX_MUL, UNBOX_DIV, UNBOX_INC, UNBOX_DEC, UNBOX_LT, UNBOX_LE, UNBOX_GT, UNBOX_GE, UNBOX_EQ, UNBOX_ZERO, UNBOX_POS, UNBOX_NEG } unbox_op;

// The C representation of an unboxed value: int64_t or double.
typedef enum { UK_NONE, UK_INT, UK_DBL } ukind;

static unbox_op unbox_op_of(const clj_intrinsic *op) {
	static const struct {
		const char *name;
		uint32_t    arity;
		unbox_op    op;
	} table[] = {
		{"clojure.core/+", 2, UNBOX_ADD},   {"clojure.core/-", 2, UNBOX_SUB},   {"clojure.core/*", 2, UNBOX_MUL},
		{"clojure.core//", 2, UNBOX_DIV},   {"clojure.core/inc", 1, UNBOX_INC}, {"clojure.core/dec", 1, UNBOX_DEC},
		{"clojure.core/<", 2, UNBOX_LT},    {"clojure.core/<=", 2, UNBOX_LE},   {"clojure.core/>", 2, UNBOX_GT},
		{"clojure.core/>=", 2, UNBOX_GE},   {"clojure.core/=", 2, UNBOX_EQ},    {"clojure.core/zero?", 1, UNBOX_ZERO},
		{"clojure.core/pos?", 1, UNBOX_POS}, {"clojure.core/neg?", 1, UNBOX_NEG},
	};
	for (size_t i = 0; i < sizeof table / sizeof *table; i++) {
		if (table[i].arity == op->arity && strcmp(table[i].name, op->name) == 0) return table[i].op;
	}
	return UNBOX_NONE;
}

static bool unbox_is_arith(unbox_op op) { return op >= UNBOX_ADD && op <= UNBOX_DEC; }

// The kind of an arithmetic result over the kinds of its arguments; an integer quotient may be a ratio, so it stays boxed.
static ukind arith_kind(unbox_op op, ukind a, ukind b) {
	if (a == UK_NONE || b == UK_NONE) return UK_NONE;
	if (a == UK_INT && b == UK_INT) return op == UNBOX_DIV ? UK_NONE : UK_INT;
	return UK_DBL;
}

// = between an integer and a double is false by type, which a C comparison of the converted values would not say.
static bool compare_kinds_ok(unbox_op op, ukind a, ukind b) {
	if (a == UK_NONE || b == UK_NONE) return false;
	return op != UNBOX_EQ || a == b;
}

// The kind the node's fact fixes: a fixnum or a boxed long, the two representations of a long; a double; neither.
static ukind fact_kind(const fnctx *f, const clj_node *n) {
	const clj_fact *a = f->facts ? clj_facts_node(f->facts, n->id) : NULL;
	if (!a || a->types == CLJ_T_BOTTOM) return UK_NONE;
	if ((a->types & ~(uint32_t)(CLJ_T_FIXNUM | CLJ_T_LONG)) == 0) return UK_INT;
	if (a->types == CLJ_T_DOUBLE) return UK_DBL;
	return UK_NONE;
}

static ukind slot_kind(const fnctx *f, uint32_t i) { return int_slot(f, i) ? UK_INT : dbl_slot(f, i) ? UK_DBL : UK_NONE; }
static bool  in_typed(const fnctx *f, uint32_t i) { return slot_kind(f, i) != UK_NONE; }

// Closed only: in dev a rebound operator needs the generic path, whose result is a box.
static ukind unboxable_kind(const fnctx *f, const clj_node *n) {
	if (!f->c->opts.closed) return UK_NONE;
	switch (n->kind) {
	case CLJ_NODE_CONST: return clj_is_fixnum(n->u.value) ? UK_INT : clj_is_double(n->u.value) ? UK_DBL : UK_NONE;
	case CLJ_NODE_LOCAL: return slot_kind(f, n->u.local.index);
	case CLJ_NODE_INTRINSIC: {
		unbox_op op = unbox_op_of(n->u.intrinsic.op);
		if (!unbox_is_arith(op)) return UK_NONE;
		ukind a = unboxable_kind(f, n->u.intrinsic.args[0]);
		ukind b = n->u.intrinsic.n == 2 ? unboxable_kind(f, n->u.intrinsic.args[1]) : UK_INT;
		return arith_kind(op, a, b);
	}
	default: return UK_NONE;
	}
}

// Every binding of the frame's own slots: let and loop inits, recur arguments. Other frames are not entered.
typedef struct {
	uint32_t        *slots;
	const clj_node **inits;
	const clj_node **binders; // the let, loop or recur node of each
	uint32_t         n, cap;
} bind_list;

static void bind_add(bind_list *b, uint32_t slot, const clj_node *init, const clj_node *binder) {
	if (b->n == b->cap) {
		b->cap = b->cap ? b->cap * 2 : 16;
		b->slots = realloc(b->slots, b->cap * sizeof *b->slots);
		b->inits = realloc(b->inits, b->cap * sizeof *b->inits);
		b->binders = realloc(b->binders, b->cap * sizeof *b->binders);
		if (!b->slots || !b->inits || !b->binders) clj_fatal("out of memory");
	}
	b->slots[b->n] = slot;
	b->inits[b->n] = init;
	b->binders[b->n++] = binder;
}

static void bind_free(bind_list *b) {
	free(b->slots);
	free(b->inits);
	free(b->binders);
}

static void bind_walk(const clj_node *n, void *ctx) {
	bind_list *b = ctx;
	switch (n->kind) {
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) bind_add(b, n->u.let.slots[i], n->u.let.inits[i], n);
		break;
	case CLJ_NODE_RECUR:
		for (uint32_t i = 0; i < n->u.recur.n; i++) bind_add(b, n->u.recur.slots[i], n->u.recur.args[i], n);
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) bind_walk(n->u.fused.args[i], ctx);
		return;
	default: break;
	}
	clj_node_children(n, bind_walk, ctx);
}

static bool inside(const clj_node *n, const clj_node *sub) { return sub->id >= n->id && sub->id < n->id + n->nnodes; }

// A fixpoint: a binding may rest on other typed slots. The entry check of `split` converts the exempt inits.
static void typed_masks(fnctx *f, const clj_node *body, const clj_node *split, uint64_t exempt, uint64_t *ints, uint64_t *dbls) {
	uint64_t saved_ints = f->ints, saved_dbls = f->dbls;
	*ints = *dbls = 0;
	if (!f->c->opts.closed || !f->facts) return;
	bind_list b = {NULL, NULL, NULL, 0, 0};
	bind_walk(body, &b);
	uint64_t bound = 0, want_int = 0, want_dbl = 0, bad = 0;
	// a binding outside the branch would write the boxed variable while the shadow is read, or the reverse
	if (split) {
		for (uint32_t i = 0; i < b.n; i++) {
			if (b.slots[i] >= 64 || b.binders[i] == split || inside(split, b.binders[i])) continue;
			uint64_t bit = (uint64_t)1 << b.slots[i];
			if (!in_typed(f, b.slots[i]) && !(exempt & bit)) bad |= bit;
		}
	}
	for (uint32_t i = 0; i < b.n; i++) {
		if (b.slots[i] >= 64) continue;
		uint64_t bit = (uint64_t)1 << b.slots[i];
		ukind    k = b.inits[i]->kind == CLJ_NODE_DIRECT_FN ? UK_NONE : fact_kind(f, b.inits[i]);
		bound |= bit;
		if (k == UK_INT) want_int |= bit;
		else if (k == UK_DBL) want_dbl |= bit;
		else bad |= bit;
	}
	uint64_t ok = 0;
	for (uint32_t i = 0; i < 64; i++) {
		if (promoted(f, i) && !borrowed_slot(f, i)) ok |= (uint64_t)1 << i;
	}
	f->ints = want_int & ~want_dbl & ~bad & ok & bound;
	f->dbls = want_dbl & ~want_int & ~bad & ok & bound;
	for (bool again = true; again;) {
		again = false;
		for (uint32_t i = 0; i < b.n; i++) {
			if (b.slots[i] >= 64) continue;
			uint64_t bit = (uint64_t)1 << b.slots[i];
			ukind    k = slot_kind(f, b.slots[i]);
			if (k == UK_NONE) continue;
			if ((exempt & bit) && b.binders[i]->kind != CLJ_NODE_RECUR) continue;
			if (unboxable_kind(f, b.inits[i]) == k) continue;
			f->ints &= ~bit;
			f->dbls &= ~bit;
			again = true;
		}
	}
	bind_free(&b);
	// the outputs may alias the masks
	uint64_t ri = f->ints, rd = f->dbls;
	f->ints = saved_ints;
	f->dbls = saved_dbls;
	*ints = ri;
	*dbls = rd;
}

static void select_typed_slots(fnctx *f, const clj_node *body) {
	typed_masks(f, body, NULL, 0, &f->ints, &f->dbls);
	f->u->slots.int_slots += (uint64_t)__builtin_popcountll(f->ints);
	f->u->slots.double_slots += (uint64_t)__builtin_popcountll(f->dbls);
}

// The slots an arithmetic node of the body reads directly, fns and fused programs excepted.
static void arith_reads(const clj_node *n, void *ctx) {
	uint64_t *reads = ctx;
	switch (n->kind) {
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) arith_reads(n->u.fused.args[i], ctx);
		return;
	case CLJ_NODE_INTRINSIC:
		if (unbox_op_of(n->u.intrinsic.op) != UNBOX_NONE) {
			for (uint32_t i = 0; i < n->u.intrinsic.n; i++) {
				const clj_node *a = n->u.intrinsic.args[i];
				if (a->kind == CLJ_NODE_LOCAL && a->u.local.index < 64) *reads |= (uint64_t)1 << a->u.local.index;
			}
		}
		break;
	default: break;
	}
	clj_node_children(n, arith_reads, ctx);
}

// Promoted, owned, untyped, and holding a boxed value of a typed fact: what an entry check can convert.
static bool entry_candidate(const fnctx *f, uint32_t s, const clj_node *init) {
	if (s >= 64 || !promoted(f, s) || borrowed_slot(f, s) || in_typed(f, s)) return false;
	if (init->kind == CLJ_NODE_DIRECT_FN) return false;
	return fact_kind(f, init) != UK_NONE && unboxable_kind(f, init) == UK_NONE;
}

// The fast branch's masks (NOTES.md, "Compiler": entry-checked frames); one split per frame, or the body doubles again.
static bool select_split(fnctx *f, const clj_node *n, uint64_t *checked, uint64_t *ints, uint64_t *dbls) {
	if (!f->c->opts.closed || !f->facts || f->in_split || !f->body) return false;
	uint64_t reads = 0;
	arith_reads(n->u.let.body, &reads);
	uint64_t exempt = 0;
	for (uint32_t k = 0; k < n->u.let.n; k++) {
		if (entry_candidate(f, n->u.let.slots[k], n->u.let.inits[k])) exempt |= (uint64_t)1 << n->u.let.slots[k];
	}
	bind_list b = {NULL, NULL, NULL, 0, 0};
	bind_walk(f->body, &b);
	for (uint32_t i = 0; i < b.n; i++) {
		uint32_t s = b.slots[i];
		if (s >= 64 || b.binders[i] == n || inside(n, b.binders[i]) || b.binders[i]->kind == CLJ_NODE_RECUR) continue;
		if (!((reads >> s) & 1) || !entry_candidate(f, s, b.inits[i])) continue;
		bool once = true;
		for (uint32_t j = 0; j < b.n && once; j++) once = j == i || b.slots[j] != s;
		if (once) exempt |= (uint64_t)1 << s;
	}
	bind_free(&b);
	if (!exempt) return false;
	typed_masks(f, f->body, n, exempt, ints, dbls);
	uint64_t fresh = (*ints | *dbls) & ~(f->ints | f->dbls);
	*checked = fresh & exempt;
	return (*checked & reads) != 0;
}

// The slot array keeps the slots that are not promoted, up to the highest one.
static uint32_t array_slots(const fnctx *f, uint32_t nslots) {
	while (nslots > 0 && promoted(f, nslots - 1)) nslots--;
	return nslots;
}

// The owned promoted slots start at nil; a borrowed one is declared where its value arrives.
static void emit_promoted_decls(fnctx *f) {
	for (uint32_t i = 0; i < 64; i++) {
		if (int_slot(f, i)) sb_printf(&f->out, "\tint64_t l%u = 0;\n\t(void)l%u;\n", i, i);
		else if (dbl_slot(f, i)) sb_printf(&f->out, "\tdouble l%u = 0;\n\t(void)l%u;\n", i, i);
		else if (promoted(f, i) && !borrowed_slot(f, i)) sb_printf(&f->out, "\tclj_value l%u = CLJ_NIL;\n", i);
	}
}

static void emit_promoted_releases(fnctx *f) {
	for (uint32_t i = 0; i < 64; i++) {
		if (promoted(f, i) && !borrowed_slot(f, i) && slot_kind(f, i) == UK_NONE) sb_printf(&f->out, "\tclj_release(l%u);\n", i);
	}
}

// A param or self value arriving in a closure arity: into its C variable or its array entry.
static void emit_slot_arrival(fnctx *f, uint32_t slot, const char *value) {
	if (promoted(f, slot)) sb_printf(&f->out, "\tclj_value l%u = %s;\n\t(void)l%u;\n", slot, value, slot);
	else sb_printf(&f->out, "\ts[%u] = %s;\n", slot, value);
}

// The store of an owned value into a slot: slot_set of eval.c, or the rebind of a C variable.
static void emit_set(fnctx *f, uint32_t slot, const char *value) {
	if (borrowed_slot(f, slot)) clj_fatal("compiler: a borrowed slot is rebound");
	if (slot_kind(f, slot) != UK_NONE) clj_fatal("compiler: a typed slot takes a boxed value");
	if (promoted(f, slot)) sb_printf(&f->out, "\tclj_c_rebind(&l%u, %s);\n", slot, value);
	else sb_printf(&f->out, "\tclj_c_set(&%s, %u, %s);\n", f->frame, slot, value);
}

// An unboxed temp: an int64_t or a double.
typedef struct {
	char  name[24];
	ukind kind;
} itemp;

static itemp new_itemp(fnctx *f, ukind kind) {
	itemp t;
	snprintf(t.name, sizeof t.name, "i%d", f->ntemp++);
	t.kind = kind;
	return t;
}

static const char *ctype(ukind k) { return k == UK_DBL ? "double" : "int64_t"; }

// A double as C source: hex keeps every bit, and the non-finite values have no literal.
static void double_literal(char *buf, size_t cap, double d) {
	if (isnan(d)) snprintf(buf, cap, "__builtin_nan(\"\")");
	else if (isinf(d)) snprintf(buf, cap, d > 0 ? "__builtin_inf()" : "(-__builtin_inf())");
	else snprintf(buf, cap, "%a", d);
}

static void emit_unwind(fnctx *f);

// r = a op b with the overflow check of arith2; an overflow throws where the interpreter would.
static void emit_int64_arith(fnctx *f, unbox_op op, const char *a, const char *b, const char *r) {
	const char *builtin = (op == UNBOX_ADD || op == UNBOX_INC) ? "__builtin_add_overflow" : (op == UNBOX_MUL) ? "__builtin_mul_overflow" : "__builtin_sub_overflow";
	sb_printf(&f->out, "\tint64_t %s;\n\tif (__builtin_expect(%s(%s, %s, &%s), 0)) {\n\tclj_throw_msg(\"integer overflow\");\n", r, builtin, a, b, r);
	emit_unwind(f);
	sb_puts(&f->out, "\t}\n");
}

static char arith_char(unbox_op op) {
	switch (op) {
	case UNBOX_ADD:
	case UNBOX_INC: return '+';
	case UNBOX_SUB:
	case UNBOX_DEC: return '-';
	case UNBOX_MUL: return '*';
	default: return '/';
	}
}

// r = a op b as IEEE doubles, either operand converted as arith2 converts a fixnum beside a double.
static void emit_double_arith(fnctx *f, unbox_op op, const char *a, const char *b, const char *r) {
	sb_printf(&f->out, "\tdouble %s = (double)%s %c (double)%s;\n", r, a, arith_char(op), b);
}

// n as an unboxed value: only for an unboxable node.
static itemp emit_raw(fnctx *f, const clj_node *n) {
	fn_line(f, n);
	switch (n->kind) {
	case CLJ_NODE_CONST: {
		if (clj_is_fixnum(n->u.value)) {
			itemp t = new_itemp(f, UK_INT);
			sb_printf(&f->out, "\tint64_t %s = INT64_C(%lld);\n", t.name, (long long)clj_fixnum_val(n->u.value));
			return t;
		}
		itemp t = new_itemp(f, UK_DBL);
		char  lit[64];
		double_literal(lit, sizeof lit, clj_double_val(n->u.value));
		sb_printf(&f->out, "\tdouble %s = %s;\n", t.name, lit);
		return t;
	}
	case CLJ_NODE_LOCAL: {
		itemp t = new_itemp(f, slot_kind(f, n->u.local.index));
		sb_printf(&f->out, "\t%s %s = l%u;\n", ctype(t.kind), t.name, n->u.local.index);
		return t;
	}
	case CLJ_NODE_INTRINSIC: {
		unbox_op op = unbox_op_of(n->u.intrinsic.op);
		itemp    a = emit_raw(f, n->u.intrinsic.args[0]);
		itemp    b = {"INT64_C(1)", UK_INT};
		if (n->u.intrinsic.n == 2) b = emit_raw(f, n->u.intrinsic.args[1]);
		itemp t = new_itemp(f, arith_kind(op, a.kind, b.kind));
		if (t.kind == UK_INT) emit_int64_arith(f, op, a.name, b.name, t.name);
		else emit_double_arith(f, op, a.name, b.name, t.name);
		f->u->slots.unboxed++;
		return t;
	}
	default: clj_fatal("compiler: not an unboxable node");
	}
}

// The C comparison of a predicate or comparison op over untagged values.
static const char *unbox_compare(unbox_op op) {
	switch (op) {
	case UNBOX_LT: return "<";
	case UNBOX_LE: return "<=";
	case UNBOX_GT: return ">";
	case UNBOX_GE: return ">=";
	case UNBOX_EQ: return "==";
	case UNBOX_ZERO: return "==";
	case UNBOX_POS: return ">";
	case UNBOX_NEG: return "<";
	default: clj_fatal("compiler: not a comparison");
	}
}

// The C expression comparing two untagged values, both converted to double when either is one.
static void compare_expr(char *buf, size_t cap, unbox_op op, const char *a, ukind ka, const char *b, ukind kb) {
	if (ka == UK_INT && kb == UK_INT) snprintf(buf, cap, "%s %s %s", a, unbox_compare(op), b);
	else snprintf(buf, cap, "(double)%s %s (double)%s", a, unbox_compare(op), b);
}

// ---- pools from nodes

static size_t var_index(unit *u, clj_value var);
static bool   const_ok(clj_value v);

// A pool entry's init is a C expression: the reader over the printed form, or, for a collection holding vars (the
// codec refuses those, a `[#'a #'b]` literal has them), the constructor over its items' own entries.
static bool const_expr(fnctx *f, clj_value v, sb *out);

typedef struct {
	fnctx *f;
	sb    *out;
	bool   ok;
	size_t n;
} items_ctx;

static bool const_item(clj_value item, void *ctx) {
	items_ctx *c = ctx;
	if (c->n++) sb_puts(c->out, ", ");
	c->ok = const_expr(c->f, item, c->out);
	return c->ok;
}

static bool const_entry(clj_value k, clj_value v, void *ctx) { return const_item(k, ctx) && const_item(v, ctx); }

static bool const_expr(fnctx *f, clj_value v, sb *out) {
	if (clj_is_var(v)) {
		sb_printf(out, "V[%zu]", var_index(f->u, v));
		return true;
	}
	if (const_ok(v)) {
		clj_value text = clj_pr_str(v);
		if (text == CLJ_THROWN) {
			clj_release(clj_take_pending());
			return false;
		}
		sb_puts(out, "clj_c_const(");
		sb_c_string(out, clj_string_bytes(text), clj_string_len(text));
		sb_printf(out, ", %u)", clj_string_len(text));
		clj_release(text);
		return true;
	}
	const char *ctor = clj_is_vector(v) ? "clj_vector_from_array" : clj_is_map(v) ? "clj_c_map_literal" : clj_is_set(v) ? "clj_c_set_literal" : clj_is_list(v) ? "clj_list_from_array" : NULL;
	if (!ctor) return false;
	sb_printf(out, "%s((clj_value[]){", ctor);
	items_ctx c = {f, out, true, 0};
	if (clj_is_vector(v)) clj_vector_each(v, const_item, &c);
	else if (clj_is_map(v)) clj_map_each(v, const_entry, &c);
	else if (clj_is_set(v)) clj_set_each(v, const_item, &c);
	else {
		clj_seq_iter it = clj_seq_iter_start(v);
		clj_value    item;
		while (c.ok && clj_seq_iter_next(&it, &item)) const_item(item, &c);
		clj_seq_iter_close(&it);
		if (it.thrown) {
			clj_release(clj_take_pending());
			c.ok = false;
		}
	}
	if (c.n == 0) sb_puts(out, "CLJ_NIL");
	sb_printf(out, "}, %zu)", c.n);
	return c.ok;
}

static size_t const_index(fnctx *f, clj_value v, bool *ok) {
	if (clj_is_var(v)) clj_fatal("compiler: a var is not a pool constant");
	// Keyed by identity within the form, not by text: two literals the reader made separately stay two objects
	// (a NaN is only = to its own box), while a value a macro copied into two nodes stays one, as it is for the interpreter.
	sb   init = {0};
	*ok = const_expr(f, v, &init);
	if (!*ok) {
		sb_free(&init);
		return 0;
	}
	char key[64];
	if (clj_is_ptr(v)) snprintf(key, sizeof key, "%llu@%p", (unsigned long long)(f->form ? f->form->serial : 0), (void *)v);
	else snprintf(key, sizeof key, "%s", init.s);
	bool   fresh;
	size_t i = pool_intern(&f->u->consts, key, init.s, &fresh);
	sb_free(&init);
	return i;
}

static size_t var_index(unit *u, clj_value var) {
	char key[600];
	snprintf(key, sizeof key, "%s/%s", clj_string_bytes(clj_symbol_name(clj_var_ns(var))), clj_string_bytes(clj_symbol_name(clj_var_name(var))));
	bool fresh;
	return pool_intern(&u->vars, key, NULL, &fresh);
}

static size_t op_index(unit *u, const clj_intrinsic *op) {
	char arity[16];
	snprintf(arity, sizeof arity, "%u", op->arity);
	bool fresh;
	return pool_intern(&u->ops, op->name, arity, &fresh);
}

static size_t fusion_index(unit *u, const clj_fusion_var *fv) {
	bool fresh;
	return pool_intern(&u->fusion, fv->name, NULL, &fresh);
}

// What the printer writes and the reader hands back as an equal value (node_data.c, serializable).
static bool const_ok(clj_value v);

static bool const_ok_item(clj_value item, void *ctx) {
	bool *ok = ctx;
	*ok = const_ok(item);
	return *ok;
}

static bool const_ok_entry(clj_value k, clj_value v, void *ctx) { return const_ok_item(k, ctx) && const_ok_item(v, ctx); }

static bool const_ok(clj_value v) {
	if (!clj_is_ptr(v) || clj_is_number(v) || clj_is_string(v) || clj_is_keyword(v) || clj_is_symbol(v) || clj_is_regex(v) || clj_is_uuid(v) || clj_is_inst(v)) return true;
	bool ok = true;
	if (clj_is_vector(v)) clj_vector_each(v, const_ok_item, &ok);
	else if (clj_is_map(v)) clj_map_each(v, const_ok_entry, &ok);
	else if (clj_is_set(v)) clj_set_each(v, const_ok_item, &ok);
	else if (clj_is_seq(v)) {
		clj_seq_iter it = clj_seq_iter_start(v);
		clj_value    item;
		while (ok && clj_seq_iter_next(&it, &item)) ok = const_ok(item);
		clj_seq_iter_close(&it);
		if (it.thrown) {
			clj_release(clj_take_pending());
			return false;
		}
	} else {
		return false;
	}
	return ok;
}

// ---- refusals

static const char *kind_name(clj_node_kind k) {
	static const char *const names[] = {"const", "local", "captured", "outer", "var", "if", "do", "let", "loop", "recur", "fn", "invoke",
	                                    "def", "vector", "map", "set", "try", "throw", "intrinsic", "fused", "direct-fn", "direct-call"};
	return (size_t)k < sizeof names / sizeof *names ? names[k] : "?";
}

static void refuse(fnctx *f, const clj_node *n, const char *reason) {
	cljc_compiler *c = f->c;
	if (c->nrefusals == c->refusals_cap) {
		c->refusals_cap = c->refusals_cap ? c->refusals_cap * 2 : 8;
		c->refusals = realloc(c->refusals, c->refusals_cap * sizeof *c->refusals);
		if (!c->refusals) clj_fatal("out of memory");
	}
	uint32_t line = n->line ? n->line : f->form->line, col = n->line ? n->col : f->form->col;
	c->refusals[c->nrefusals++] = (cljc_refusal){xstrdup(f->u->file), line, col, kind_name(n->kind), xstrdup(reason)};
}

// ---- expressions

static temp emit(fnctx *f, const clj_node *n);
static temp emit_borrowed(fnctx *f, const clj_node *n);
static void emit_fn_functions(fnctx *f, const clj_node *n, const char *base);
static void emit_direct_fn_functions(fnctx *f, const clj_node *n, const char *base);

static temp emit_refused(fnctx *f, const clj_node *n, const char *reason) {
	refuse(f, n, reason);
	temp t = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = clj_throw_msg(\"compiler refused a %s node at %u:%u: %s\");\n", t.name, kind_name(n->kind), n->line, n->col, reason);
	check_thrown(f, t.name);
	live_push(f, t);
	return t;
}

static temp emit_const(fnctx *f, const clj_node *n, bool borrowed) {
	clj_value v = n->u.value;
	if (clj_is_var(v)) {
		size_t vi = var_index(f->u, v);
		temp   t = new_temp(f, borrowed ? OWN_NO : OWN_YES);
		sb_printf(&f->out, "\tclj_value %s = %sV[%zu]%s;\n", t.name, borrowed ? "" : "clj_retain(", vi, borrowed ? "" : ")");
		live_push(f, t);
		return t;
	}
	bool   ok;
	size_t ki = const_index(f, v, &ok);
	if (!ok) {
		char reason[200];
		snprintf(reason, sizeof reason, "constant of type %s does not print and read back", clj_type_name(v));
		return emit_refused(f, n, reason);
	}
	temp t = new_temp(f, borrowed ? OWN_NO : OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = %sK[%zu]%s;\n", t.name, borrowed ? "" : "clj_retain(", ki, borrowed ? "" : ")");
	live_push(f, t);
	return t;
}

static temp emit_local(fnctx *f, const clj_node *n, bool borrowed) {
	uint32_t i = n->u.local.index;
	if (slot_kind(f, i) != UK_NONE) {
		// a box outside the fixnum range is an allocation, so the read is owned either way
		temp t = new_temp(f, OWN_YES);
		sb_printf(&f->out, "\tclj_value %s = %s(l%u);\n", t.name, int_slot(f, i) ? "clj_long_new" : "clj_double_new", i);
		live_push(f, t);
		return t;
	}
	if (promoted(f, i)) {
		bool taken = n->u.local.last && !borrowed_slot(f, i);
		if (borrowed && !taken) {
			temp t = new_temp(f, OWN_NO);
			sb_printf(&f->out, "\tclj_value %s = l%u;\n", t.name, i);
			return t;
		}
		temp t = new_temp(f, OWN_YES);
		if (taken) sb_printf(&f->out, "\tclj_value %s = l%u;\n\tl%u = CLJ_NIL;\n", t.name, i, i);
		else sb_printf(&f->out, "\tclj_value %s = clj_retain(l%u);\n", t.name, i);
		live_push(f, t);
		return t;
	}
	if (borrowed && !n->u.local.last) {
		temp t = new_temp(f, OWN_NO);
		sb_printf(&f->out, "\tclj_value %s = %s.slots[%u];\n", t.name, f->frame, i);
		return t;
	}
	if (borrowed) {
		temp t = new_temp(f, OWN_DYN);
		sb_printf(&f->out, "\tbool %s;\n\tclj_value %s = clj_c_take(&%s, %u, &%s);\n", t.flag, t.name, f->frame, i, t.flag);
		live_push(f, t);
		return t;
	}
	temp t = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = clj_c_local(&%s, %u, %s);\n", t.name, f->frame, i, n->u.local.last ? "true" : "false");
	live_push(f, t);
	return t;
}

static temp emit_captured(fnctx *f, const clj_node *n, bool borrowed) {
	temp t = new_temp(f, borrowed ? OWN_NO : OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = %s%s.captured[%u]%s;\n", t.name, borrowed ? "" : "clj_retain(", f->frame, n->u.index, borrowed ? "" : ")");
	live_push(f, t);
	return t;
}

// A slot read through the array must not be a C variable: the scan that decides promotion has to have seen this read.
static void check_array_slot(const fnctx *f, uint32_t depth, uint32_t index) {
	for (; depth && f; depth--) f = f->definer;
	if (!f) clj_fatal("compiler: no frame that many links up");
	if (promoted(f, index)) clj_fatal("compiler: a promoted slot is read through the frame");
}

static temp emit_outer(fnctx *f, const clj_node *n) {
	check_array_slot(f, n->u.outer.depth, n->u.outer.index);
	temp t = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = clj_retain(clj_c_outer(&%s, %u)->slots[%u]);\n", t.name, f->frame, n->u.outer.depth, n->u.outer.index);
	live_push(f, t);
	return t;
}

static temp emit_var(fnctx *f, const clj_node *n, bool borrowed) {
	size_t vi = var_index(f->u, n->u.var);
	if (borrowed) {
		temp t = new_temp(f, OWN_DYN);
		sb_printf(&f->out, "\tbool %s;\n\tclj_value %s = clj_c_var_borrow(V[%zu], &%s);\n", t.flag, t.name, vi, t.flag);
		check_thrown(f, t.name);
		live_push(f, t);
		return t;
	}
	temp t = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = clj_var_deref(V[%zu]);\n", t.name, vi);
	check_thrown(f, t.name);
	live_push(f, t);
	return t;
}

static temp emit_if(fnctx *f, const clj_node *n) {
	temp test = emit_borrowed(f, n->u.if_.test);
	temp b = new_temp(f, OWN_NO);
	sb_printf(&f->out, "\tbool %s = clj_truthy(%s);\n", b.name, test.name);
	release_temp(f, &test);
	temp r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s;\n\tif (%s) {\n", r.name, b.name);
	temp then = emit(f, n->u.if_.then);
	sb_printf(&f->out, "\t%s = %s;\n\t} else {\n", r.name, then.name);
	live_forget(f, &then);
	if (n->u.if_.else_) {
		temp e = emit(f, n->u.if_.else_);
		sb_printf(&f->out, "\t%s = %s;\n", r.name, e.name);
		live_forget(f, &e);
	} else {
		sb_printf(&f->out, "\t%s = CLJ_NIL;\n", r.name);
	}
	sb_puts(&f->out, "\t}\n");
	live_push(f, r);
	return r;
}

static temp emit_do(fnctx *f, const clj_node *n) {
	if (n->u.seq.n == 0) {
		temp t = new_temp(f, OWN_NO);
		sb_printf(&f->out, "\tclj_value %s = CLJ_NIL;\n", t.name);
		return t;
	}
	for (uint32_t i = 0; i + 1 < n->u.seq.n; i++) {
		temp t = emit_borrowed(f, n->u.seq.items[i]);
		release_temp(f, &t);
	}
	return emit(f, n->u.seq.items[n->u.seq.n - 1]);
}

static void emit_bindings(fnctx *f, const clj_node *n) {
	for (uint32_t i = 0; i < n->u.let.n; i++) {
		const clj_node *init = n->u.let.inits[i];
		if (init->kind == CLJ_NODE_DIRECT_FN) {
			char base[256];
			snprintf(base, sizeof base, "%s__%u", f->base, (*f->fn_counter)++);
			emit_direct_fn_functions(f, init, base);
			for (uint32_t k = 0; k <= CLJ_FN_MAX_FIXED; k++) {
				if (init->u.fn.fixed[k]) sb_printf(&f->out, "\t(void)%s_a%u;\n", base, k);
			}
			// a promoted slot of a direct fn starts at nil and is never rebound
			if (!promoted(f, n->u.let.slots[i])) emit_set(f, n->u.let.slots[i], "CLJ_NIL");
			continue;
		}
		if (slot_kind(f, n->u.let.slots[i]) != UK_NONE) {
			itemp t = emit_raw(f, init);
			sb_printf(&f->out, "\tl%u = %s;\n", n->u.let.slots[i], t.name);
			continue;
		}
		temp t = emit(f, init);
		emit_set(f, n->u.let.slots[i], t.name);
		live_forget(f, &t);
	}
}

static temp emit_loop_body(fnctx *f, const clj_node *n);

static bool in_mask(uint64_t m, uint32_t i) { return (m >> i) & 1; }

// The two-program form of a loop fed by boxed values (NOTES.md, "Compiler": entry-checked frames): the tag checks at
// entry choose the branch whose typed C variables shadow the boxed ones, or the generic one.
static temp emit_split(fnctx *f, const clj_node *n, uint64_t checked, uint64_t ints, uint64_t dbls) {
	uint64_t fresh_ints = ints & ~f->ints, fresh_dbls = dbls & ~f->dbls;
	temp     r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s;\n\tif (1", r.name);
	for (uint32_t i = 0; i < 64; i++) {
		if (in_mask(fresh_ints & checked, i)) sb_printf(&f->out, " && clj_is_fixnum(l%u)", i);
		else if (in_mask(fresh_dbls & checked, i)) sb_printf(&f->out, " && clj_is_double(l%u)", i);
	}
	sb_puts(&f->out, ") {\n");
	for (uint32_t i = 0; i < 64; i++) {
		if (in_mask(fresh_ints & checked, i)) sb_printf(&f->out, "\tint64_t e%u = clj_fixnum_val(l%u);\n", i, i);
		else if (in_mask(fresh_dbls & checked, i)) sb_printf(&f->out, "\tdouble e%u = clj_double_val(l%u);\n", i, i);
	}
	sb_puts(&f->out, "\t{\n");
	for (uint32_t i = 0; i < 64; i++) {
		if (!in_mask(fresh_ints | fresh_dbls, i)) continue;
		const char *t = ctype(in_mask(fresh_ints, i) ? UK_INT : UK_DBL);
		if (in_mask(checked, i)) sb_printf(&f->out, "\t%s l%u = e%u;\n\t(void)l%u;\n", t, i, i, i);
		else sb_printf(&f->out, "\t%s l%u = 0;\n\t(void)l%u;\n", t, i, i);
	}
	uint64_t saved_ints = f->ints, saved_dbls = f->dbls;
	bool     saved_split = f->in_split;
	f->ints = ints;
	f->dbls = dbls;
	f->in_split = true;
	temp a = emit_loop_body(f, n);
	sb_printf(&f->out, "\t%s = %s;\n\t}\n\t} else {\n", r.name, a.name);
	live_forget(f, &a);
	f->ints = saved_ints;
	f->dbls = saved_dbls;
	temp b = emit_loop_body(f, n);
	sb_printf(&f->out, "\t%s = %s;\n\t}\n", r.name, b.name);
	live_forget(f, &b);
	f->in_split = saved_split;
	live_push(f, r);
	f->u->slots.entry_checked++;
	return r;
}

static temp emit_let(fnctx *f, const clj_node *n) {
	emit_bindings(f, n);
	return emit(f, n->u.let.body);
}

static temp emit_loop(fnctx *f, const clj_node *n) {
	emit_bindings(f, n);
	uint64_t checked, ints, dbls;
	if (select_split(f, n, &checked, &ints, &dbls)) return emit_split(f, n, checked, ints, dbls);
	return emit_loop_body(f, n);
}

static temp emit_loop_body(fnctx *f, const clj_node *n) {
	int  saved_label = f->recur_label, saved_mark = f->recur_live_mark;
	bool saved_used = f->recur_used, saved_loop = f->recur_loop;
	f->recur_label = new_label(f);
	f->recur_used = false;
	f->recur_loop = true;
	f->recur_live_mark = f->nlive;
	// The label goes before the body, which is only known to need it after emission: splice it in.
	size_t at = f->out.len;
	temp   r = emit(f, n->u.let.body);
	if (f->recur_used) {
		char label[32];
		snprintf(label, sizeof label, "L%d: ;\n", f->recur_label);
		sb_put(&f->out, label, strlen(label));
		memmove(f->out.s + at + strlen(label), f->out.s + at, f->out.len - strlen(label) - at);
		memcpy(f->out.s + at, label, strlen(label));
	}
	f->recur_label = saved_label;
	f->recur_used = saved_used;
	f->recur_loop = saved_loop;
	f->recur_live_mark = saved_mark;
	return r;
}

// Every argument is evaluated owned before any slot changes, so (recur b a) swaps.
static temp emit_recur(fnctx *f, const clj_node *n) {
	temp *vals = calloc(n->u.recur.n ? n->u.recur.n : 1, sizeof *vals);
	if (!vals) clj_fatal("out of memory");
	for (uint32_t i = 0; i < n->u.recur.n; i++) {
		if (slot_kind(f, n->u.recur.slots[i]) != UK_NONE) {
			itemp t = emit_raw(f, n->u.recur.args[i]);
			snprintf(vals[i].name, sizeof vals[i].name, "%s", t.name);
			vals[i].own = OWN_NO;
		} else {
			vals[i] = emit(f, n->u.recur.args[i]);
		}
	}
	for (uint32_t i = 0; i < n->u.recur.n; i++) {
		if (slot_kind(f, n->u.recur.slots[i]) != UK_NONE) {
			sb_printf(&f->out, "\tl%u = %s;\n", n->u.recur.slots[i], vals[i].name);
			continue;
		}
		emit_set(f, n->u.recur.slots[i], vals[i].name);
		live_forget(f, &vals[i]);
	}
	free(vals);
	if (!f->recur_label) clj_fatal("compiler: recur outside a loop or fn body");
	if (f->recur_loop) {
		sb_puts(&f->out, "\tif (clj_c_loop_tick()) {\n");
		emit_unwind(f);
		sb_puts(&f->out, "\t}\n");
	}
	for (int i = f->nlive - 1; i >= f->recur_live_mark; i--) emit_release_of(&f->out, &f->live[i]);
	sb_printf(&f->out, "\tgoto L%d;\n", f->recur_label);
	f->recur_used = true;
	temp r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = CLJ_NIL;\n", r.name);
	live_push(f, r);
	return r;
}

static void fn_arity_bounds(const clj_node *n, uint32_t *mask, uint32_t *min, uint32_t *max) {
	*min = UINT32_MAX;
	*max = 0;
	*mask = 0;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (!n->u.fn.fixed[i]) continue;
		*mask |= (uint32_t)1 << i;
		if (i < *min) *min = i;
		if (i > *max) *max = i;
	}
	if (n->u.fn.variadic) {
		if (n->u.fn.variadic->nparams < *min) *min = n->u.fn.variadic->nparams;
		*max = CLJ_ARITY_ANY;
	}
	if (*min == UINT32_MAX) *min = 0;
}

static temp emit_fn_as(fnctx *f, const clj_node *n, const char *base) {
	emit_fn_functions(f, n, base);
	uint32_t nc = n->u.fn.ncaptures;
	char     caps[32] = "NULL";
	if (nc) {
		snprintf(caps, sizeof caps, "c%d", f->naux++);
		sb_printf(&f->out, "\tclj_value %s[%u] = {", caps, nc);
		for (uint32_t i = 0; i < nc; i++) {
			const clj_capture *cp = &n->u.fn.captures[i];
			if (i) sb_puts(&f->out, ", ");
			switch (cp->kind) {
			case CLJ_CAPTURE_LOCAL:
				check_array_slot(f, 0, cp->index);
				sb_printf(&f->out, "%s.slots[%u]", f->frame, cp->index);
				break;
			case CLJ_CAPTURE_CAPTURED: sb_printf(&f->out, "%s.captured[%u]", f->frame, cp->index); break;
			case CLJ_CAPTURE_OUTER:
				check_array_slot(f, cp->depth, cp->index);
				sb_printf(&f->out, "clj_c_outer(&%s, %u)->slots[%u]", f->frame, cp->depth, cp->index);
				break;
			}
		}
		sb_puts(&f->out, "};\n");
	}
	uint32_t mask, min, max;
	fn_arity_bounds(n, &mask, &min, &max);
	char name[64] = "CLJ_NIL", maxs[32] = "CLJ_ARITY_ANY";
	if (!clj_is_nil(n->u.fn.name)) {
		bool ok;
		snprintf(name, sizeof name, "K[%zu]", const_index(f, n->u.fn.name, &ok));
	}
	if (max != CLJ_ARITY_ANY) snprintf(maxs, sizeof maxs, "%u", max);
	temp t = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = clj_c_closure(%s, %s, %s, %u, 0x%x, %u, %s);\n", t.name, name, base, caps, nc, mask, min, maxs);
	live_push(f, t);
	return t;
}

static temp emit_fn(fnctx *f, const clj_node *n) {
	char base[256];
	snprintf(base, sizeof base, "%s__%u", f->base, (*f->fn_counter)++);
	return emit_fn_as(f, n, base);
}

// Evaluates args as borrowed temps into a C array named by the caller; returns the temps (caller frees).
static temp *emit_args(fnctx *f, const clj_node *const *args, uint32_t n, const char *array) {
	temp *ts = calloc(n ? n : 1, sizeof *ts);
	if (!ts) clj_fatal("out of memory");
	for (uint32_t i = 0; i < n; i++) ts[i] = emit_borrowed(f, args[i]);
	if (n) {
		sb_printf(&f->out, "\tclj_value %s[%u] = {", array, n);
		for (uint32_t i = 0; i < n; i++) sb_printf(&f->out, "%s%s", i ? ", " : "", ts[i].name);
		sb_printf(&f->out, "};\n\t(void)%s;\n", array);
	} else {
		sb_printf(&f->out, "\tconst clj_value *%s = NULL;\n", array);
	}
	return ts;
}

static void release_args(fnctx *f, temp *ts, uint32_t n) {
	for (uint32_t i = n; i-- > 0;) release_temp(f, &ts[i]);
	free(ts);
}

static bool var_named(clj_value var, const char *ns, const char *name) {
	return strcmp(clj_string_bytes(clj_symbol_name(clj_var_ns(var))), ns) == 0 && strcmp(clj_string_bytes(clj_symbol_name(clj_var_name(var))), name) == 0;
}

// ---- protocol call sites: CHA arms from the receiver fact, the per-site inline cache (NOTES.md "Compiler")

// The fixed arities a direct protocol arm may enter with no closure object: nothing in them reads self or captured.
static uint32_t entry_arities(const clj_node *n) {
	if (n->u.fn.ncaptures) return 0;
	uint32_t mask = 0;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		const clj_fn_arity *a = n->u.fn.fixed[i];
		if (a && a->self_slot < 0 && a->nslots <= 64) mask |= (uint32_t)1 << i;
	}
	return mask;
}

#define PROTO_ARMS_MAX 4

typedef struct {
	const clj_type *type;
	char            guard[80]; // C expression for the descriptor
	uint32_t        pimpl;     // CLJC_PIMPL id of the direct target, UINT32_MAX when the arm has none
	bool            answer;    // satisfies? for the arm's descriptor
} proto_arm;

typedef struct {
	proto_arm arms[PROTO_ARMS_MAX];
	uint32_t  n;
	bool      partial; // a kind of the fact resolved to no descriptor: the cache stays the fallback for it
} proto_arms;

// The one builtin descriptor a kind bit dispatches through, as a C expression; NULL for a kind of many descriptors.
static const char *kind_type_expr(uint32_t bit, const clj_type **type) {
	switch (bit) {
	case CLJ_T_NIL: *type = &clj_nil_dispatch_type; return "&clj_nil_dispatch_type";
	case CLJ_T_BOOL: *type = &clj_boolean_dispatch_type; return "&clj_boolean_dispatch_type";
	case CLJ_T_FIXNUM:
	case CLJ_T_LONG: *type = &clj_long_type; return "&clj_long_type";
	case CLJ_T_BIGINT: *type = &clj_bigint_type; return "&clj_bigint_type";
	case CLJ_T_RATIO: *type = &clj_ratio_type; return "&clj_ratio_type";
	case CLJ_T_DECIMAL: *type = &clj_decimal_type; return "&clj_decimal_type";
	case CLJ_T_DOUBLE: *type = &clj_double_type; return "&clj_double_type";
	case CLJ_T_CHAR: *type = &clj_char_dispatch_type; return "&clj_char_dispatch_type";
	case CLJ_T_STRING: *type = &clj_string_type; return "&clj_string_type";
	case CLJ_T_KEYWORD: *type = &clj_keyword_type; return "&clj_keyword_type";
	case CLJ_T_SYMBOL: *type = &clj_symbol_type; return "&clj_symbol_type";
	case CLJ_T_VECTOR: *type = &clj_vector_type; return "&clj_vector_type";
	case CLJ_T_MAP: *type = &clj_map_type; return "&clj_map_type";
	case CLJ_T_SET: *type = &clj_set_type; return "&clj_set_type";
	case CLJ_T_SORTED_MAP: *type = &clj_sorted_map_type; return "&clj_sorted_map_type";
	case CLJ_T_SORTED_SET: *type = &clj_sorted_set_type; return "&clj_sorted_set_type";
	case CLJ_T_ARRAY: *type = &clj_array_type; return "&clj_array_type";
	case CLJ_T_FN: *type = &clj_fn_type; return "&clj_fn_type";
	case CLJ_T_VAR: *type = &clj_var_type; return "&clj_var_type";
	case CLJ_T_ATOM: *type = &clj_atom_type; return "&clj_atom_type";
	case CLJ_T_UUID: *type = &clj_uuid_type; return "&clj_uuid_type";
	case CLJ_T_INST: *type = &clj_inst_type; return "&clj_inst_type";
	case CLJ_T_REGEX: *type = &clj_regex_type; return "&clj_regex_type";
	default: return NULL;
	}
}

// The var whose root is the user type, found by the type's ns.Name; nil for a reify or a type no var holds.
static clj_value user_type_var(const clj_type *t) {
	const char *name = t->name;
	const char *dot = strrchr(name, '.');
	if (!dot) return CLJ_NIL;
	clj_value nsname = clj_string_new(name, (size_t)(dot - name));
	clj_value nssym = clj_symbol_new(CLJ_NIL, nsname);
	clj_value ns = clj_ns_find(nssym);
	clj_release(nssym);
	clj_release(nsname);
	if (clj_is_nil(ns)) return CLJ_NIL;
	clj_value sym = clj_symbol_from_cstr(dot + 1);
	clj_value var = clj_ns_resolve(ns, sym);
	clj_release(sym);
	if (clj_is_nil(var) || clj_var_of(var)->dynamic || clj_var_root(var) != clj_from_ptr((void *)t)) return CLJ_NIL;
	return var;
}

typedef struct {
	uint32_t        kind; // CLJ_T_RECORD or CLJ_T_HOST
	const clj_type *found;
	uint32_t        n;
} single_user;

static void visit_single_user(const clj_type *t, void *ctx) {
	single_user *s = ctx;
	bool         record = (t->core_bits & CLJ_CORE_RECORD) != 0;
	if (record != (s->kind == CLJ_T_RECORD)) return;
	s->found = t;
	s->n++;
}

static bool arms_add(proto_arms *a, const clj_type *t, const char *guard) {
	for (uint32_t i = 0; i < a->n; i++) {
		if (a->arms[i].type == t) return true;
	}
	if (a->n == PROTO_ARMS_MAX) return false;
	proto_arm *arm = &a->arms[a->n++];
	arm->type = t;
	snprintf(arm->guard, sizeof arm->guard, "%s", guard);
	arm->pimpl = UINT32_MAX;
	arm->answer = false;
	return true;
}

// The descriptors a receiver fact names: one per kind that a single descriptor dispatches through, a deftype or record
// by its var's root; the protocol's user implementors stand in for a `host`/`record` fact without a descriptor when
// there is exactly one. Empty for TOP, a seq (many descriptors) and anything past the cap.
static proto_arms receiver_arms(fnctx *f, const clj_node *receiver, clj_value proto) {
	proto_arms      a = {{{0}}, 0, false};
	const clj_fact *fact = clj_facts_node(f->facts, receiver->id);
	if (clj_fact_is_top(*fact) || fact->types == CLJ_T_TOP || fact->types == CLJ_T_BOTTOM) return a;
	uint32_t types = fact->types;
	while (types) {
		uint32_t        bit = types & (0u - types);
		const clj_type *t = NULL;
		const char     *expr = kind_type_expr(bit, &t);
		char            guard[80];
		types &= types - 1;
		if (!expr && (bit == CLJ_T_RECORD || bit == CLJ_T_HOST)) {
			t = fact->desc;
			if (!t) {
				single_user s = {bit, NULL, 0};
				clj_proto_each_user(proto, visit_single_user, &s);
				if (s.n == 1) t = s.found;
			}
			clj_value var = t && !(t->h.flags & CLJ_FLAG_IMMORTAL) ? user_type_var(t) : CLJ_NIL;
			if (!clj_is_nil(var)) {
				snprintf(guard, sizeof guard, "clj_c_var_type(V[%zu])", var_index(f->u, var));
				expr = guard;
			}
		}
		if (!expr) {
			a.partial = true;
			continue;
		}
		if (!arms_add(&a, t, expr)) {
			a.n = 0;
			a.partial = true;
			return a;
		}
	}
	return a;
}

// The direct target of an arm: the impl the tables hold for the descriptor, when it is a closure of the compiled set
// (an emitted node, or a compiled closure whose dispatcher a closed unit registered) with a fixed arity a direct
// arm may enter. UINT32_MAX otherwise.
static uint32_t arm_target(fnctx *f, const clj_type *t, clj_value method, uint32_t nargs) {
	clj_value impl = clj_proto_impl_for_type(method, t);
	uint32_t  id = UINT32_MAX;
	if (clj_is_fn(impl)) {
		const clj_fn *fn = clj_fn_of(impl);
		if (fn->kind == CLJ_FN_CLOSURE && (entry_arities(fn->u.node) >> nargs) & 1 && tree_in_set(f->c, fn)) {
			id = pimpl_add(f->c, f->u, fn->u.node, NULL, nargs);
		} else if (fn->kind == CLJ_FN_NATIVE_CTX && fn->u.native_ctx.ctx == fn && fn->nenv == 0 && (fn->arities >> nargs) & 1) {
			const char *name = clj_compiled_impl_name(fn->u.native_ctx.fn);
			if (name) id = pimpl_add(f->c, f->u, NULL, name, nargs);
		}
	}
	clj_release(impl);
	return id;
}

// Method impls a closed init registers, so that a later unit's arm can name them: the fn values of the method maps.
static void mark_impl_candidates(fnctx *f, const clj_node *n) {
	const clj_node *head = n->u.invoke.fn;
	if (head->kind != CLJ_NODE_VAR) return;
	clj_value var = head->u.var;
	if (!var_named(var, "clojure.core", "deftype*") && !var_named(var, "clojure.core", "record*") && !var_named(var, "clojure.core", "extend") && !var_named(var, "clojure.core", "extend*")) return;
	for (uint32_t i = 0; i < n->u.invoke.n; i++) {
		const clj_node *arg = n->u.invoke.args[i];
		if (arg->kind != CLJ_NODE_MAP) continue;
		for (uint32_t k = 1; k < arg->u.seq.n; k += 2) {
			if (arg->u.seq.items[k]->kind == CLJ_NODE_FN) candidate_add(f->c, arg->u.seq.items[k]);
		}
	}
}

static clj_value protocol_method_root(const clj_node *head) {
	if (head->kind != CLJ_NODE_VAR || clj_var_of(head->u.var)->dynamic) return CLJ_NIL;
	clj_value root = clj_var_root(head->u.var);
	return clj_is_protocol_method(root) ? root : CLJ_NIL;
}

// A protocol call site as --stats classifies it: the arms' pimpl ids, whose definition is known only at write time.
typedef struct proto_site {
	uint32_t pimpl[PROTO_ARMS_MAX];
	uint32_t n;
	bool     partial;
} proto_site;

static void proto_site_add(unit *u, const proto_arms *a) {
	if (u->npsites == u->psites_cap) {
		u->psites_cap = u->psites_cap ? u->psites_cap * 2 : 16;
		u->psites = realloc(u->psites, u->psites_cap * sizeof *u->psites);
		if (!u->psites) clj_fatal("out of memory");
	}
	proto_site *s = &u->psites[u->npsites++];
	s->n = a->n;
	s->partial = a->partial;
	for (uint32_t i = 0; i < a->n; i++) s->pimpl[i] = a->arms[i].pimpl;
}

// A protocol method call: the arms the fact allows, under CLJC_PIMPL_<id> (a closed unit's prelude defines it when the
// target resolved), then the per-thread inline cache for every other receiver.
static void emit_proto_call(fnctx *f, const clj_node *n, clj_value method, const temp *fn, const char *array, const temp *r) {
	uint32_t   nargs = n->u.invoke.n;
	proto_arms a = receiver_arms(f, n->u.invoke.args[0], clj_method_ctx_of(method)->proto);
	for (uint32_t i = 0; i < a.n; i++) a.arms[i].pimpl = arm_target(f, a.arms[i].type, method, nargs);
	proto_site_add(f->u, &a);
	int k = f->naux++;
	sb_printf(&f->out, "\tclj_value %s;\n\t{\n\t\tconst clj_type *pt = clj_dispatch_type_inline(%s[0]);\n\t\tuint64_t pe = clj_epoch_load();\n\t\tstatic _Thread_local clj_cproto_ic PC%d;\n", r->name, array, k);
	for (uint32_t i = 0; i < a.n; i++) {
		if (a.arms[i].pimpl != UINT32_MAX) sb_printf(&f->out, "#ifdef CLJC_PIMPL_%u\n\t\tstatic _Atomic uint64_t PA%u;\n#endif\n", a.arms[i].pimpl, a.arms[i].pimpl);
	}
	for (uint32_t i = 0; i < a.n; i++) {
		const proto_arm *arm = &a.arms[i];
		if (arm->pimpl == UINT32_MAX) continue;
		sb_printf(&f->out, "#ifdef CLJC_PIMPL_%u\n\t\tif (pt == %s && (clj_c_arm_hit(&PA%u, pe) || clj_c_arm_fill(&PA%u, %s, %s, %u, CLJC_PIMPL_%u_CODE, CLJC_PIMPL_%u_FN, pe))) %s = CLJC_PIMPL_%u_FN(CLJ_NIL, NULL, %s, %u);\n\t\telse\n#endif\n",
		          arm->pimpl, arm->guard, arm->pimpl, arm->pimpl, fn->name, array, nargs, arm->pimpl, arm->pimpl, r->name, arm->pimpl, array, nargs);
	}
	sb_printf(&f->out, "\t\t%s = clj_c_proto_ic_call(&PC%d, %s, %s, %u, pt, pe);\n\t}\n", r->name, k, fn->name, array, nargs);
}

// (satisfies? P x) with a known receiver under --closed: the answer per descriptor, verified once per epoch.
static bool emit_satisfies(fnctx *f, const clj_node *n, const temp *fn, const temp *args, const char *array, const temp *r) {
	const clj_node *pnode = n->u.invoke.args[0];
	if (pnode->kind != CLJ_NODE_VAR || clj_var_of(pnode->u.var)->dynamic) return false;
	clj_value proto = clj_var_root(pnode->u.var);
	if (!clj_is_protocol(proto)) return false;
	proto_arms a = receiver_arms(f, n->u.invoke.args[1], proto);
	if (a.n == 0) return false;
	for (uint32_t i = 0; i < a.n; i++) {
		const clj_type *t = a.arms[i].type;
		clj_value       type = t == &clj_nil_dispatch_type ? CLJ_NIL : clj_from_ptr((void *)t);
		a.arms[i].answer = clj_truthy(clj_proto_extends(proto, type));
	}
	f->u->slots.proto_folded++;
	sb_printf(&f->out, "\tclj_value %s;\n\t{\n\t\tconst clj_type *pt = clj_dispatch_type_inline(%s);\n\t\tuint64_t pe = clj_epoch_load();\n", r->name, args[1].name);
	int k = f->naux;
	f->naux += (int)a.n;
	for (uint32_t i = 0; i < a.n; i++) sb_printf(&f->out, "\t\tstatic _Atomic uint64_t PS%d;\n", k + (int)i);
	for (uint32_t i = 0; i < a.n; i++) {
		const proto_arm *arm = &a.arms[i];
		const char      *answer = arm->answer ? "CLJ_TRUE" : "CLJ_FALSE";
		sb_printf(&f->out, "\t\tif (pt == %s) %s = (clj_c_arm_hit(&PS%d, pe) || clj_c_satisfies_fill(&PS%d, %s, %s, %s, pe)) ? %s : clj_c_invoke(%s, %s, 2);\n\t\telse\n",
		          arm->guard, r->name, k + (int)i, k + (int)i, args[0].name, args[1].name, answer, answer, fn->name, array);
	}
	sb_printf(&f->out, "\t\t%s = clj_c_invoke(%s, %s, 2);\n\t}\n", r->name, fn->name, array);
	return true;
}

// (extends? P T) with T a var holding a type under --closed: the answer, verified once per epoch (a rebind of T bumps it).
static bool emit_extends(fnctx *f, const clj_node *n, const temp *fn, const temp *args, const char *array, const temp *r) {
	const clj_node *pnode = n->u.invoke.args[0], *tnode = n->u.invoke.args[1];
	if (pnode->kind != CLJ_NODE_VAR || clj_var_of(pnode->u.var)->dynamic || tnode->kind != CLJ_NODE_VAR || clj_var_of(tnode->u.var)->dynamic) return false;
	clj_value proto = clj_var_root(pnode->u.var), type = clj_var_root(tnode->u.var);
	if (!clj_is_protocol(proto) || !(clj_is_nil(type) || clj_is_type(type))) return false;
	const char *answer = clj_truthy(clj_proto_extends(proto, type)) ? "CLJ_TRUE" : "CLJ_FALSE";
	int         k = f->naux++;
	f->u->slots.proto_folded++;
	sb_printf(&f->out, "\tclj_value %s;\n\t{\n\t\tuint64_t pe = clj_epoch_load();\n\t\tstatic _Atomic uint64_t PE%d;\n", r->name, k);
	sb_printf(&f->out, "\t\t%s = (clj_c_arm_hit(&PE%d, pe) || clj_c_extends_fill(&PE%d, %s, %s, %s, pe)) ? %s : clj_c_invoke(%s, %s, 2);\n\t}\n", r->name, k, k, args[0].name, args[1].name, answer, answer, fn->name, array);
	return true;
}

static temp emit_invoke(fnctx *f, const clj_node *n) {
	const clj_node *head = n->u.invoke.fn;
	uint32_t        nargs = n->u.invoke.n;
	if (f->c->opts.closed && !f->u->embedded && head->kind == CLJ_NODE_VAR && (var_named(head->u.var, "clojure.core", "eval") || var_named(head->u.var, "clojure.core", "load-string"))) {
		return emit_refused(f, n, "eval and load-string need the interpreter; refused under --closed");
	}
	mark_impl_candidates(f, n);
	temp fn = emit_borrowed(f, head);
	char array[24];
	snprintf(array, sizeof array, "a%d", f->naux++);
	temp *args = emit_args(f, n->u.invoke.args, nargs, array);
	temp  r = new_temp(f, OWN_YES);
	direct_entry *d = NULL;
	if (head->kind == CLJ_NODE_VAR && nargs <= CLJ_FN_MAX_FIXED) {
		char key[600];
		snprintf(key, sizeof key, "%s/%s", clj_string_bytes(clj_symbol_name(clj_var_ns(head->u.var))), clj_string_bytes(clj_symbol_name(clj_var_name(head->u.var))));
		d = direct_find(f->c, key);
	}
	// a var of the set may hold a protocol method (defprotocol's def) or a builtin (a (def satisfies? ...) shadows it)
	clj_value method = nargs >= 1 && nargs <= CLJ_FN_MAX_FIXED ? protocol_method_root(head) : CLJ_NIL;
	bool      folded = false;
	if (f->c->opts.closed && nargs == 2 && head->kind == CLJ_NODE_VAR && !d && var_named(head->u.var, "clojure.core", "satisfies?")) folded = emit_satisfies(f, n, &fn, args, array, &r);
	else if (f->c->opts.closed && nargs == 2 && head->kind == CLJ_NODE_VAR && !d && var_named(head->u.var, "clojure.core", "extends?")) folded = emit_extends(f, n, &fn, args, array, &r);
	if (!folded && !clj_is_nil(method)) {
		emit_proto_call(f, n, method, &fn, array, &r);
	} else if (!folded && d && !d->dynamic && ((d->fixed >> nargs) & 1)) {
		// The definition may still be superseded: the prelude decides at write time (CLJC_DIRECT_*).
		char target[300];
		snprintf(target, sizeof target, "%s_a%u", d->base, nargs);
		bool fresh;
		pool_intern(&f->u->externs, target, NULL, &fresh);
		sb_printf(&f->out, "\tclj_value %s;\n#ifdef CLJC_DIRECT_%s\n", r.name, target);
		sb_printf(&f->out, "\tif (!CLJC_FN_%s) CLJC_FN_%s = clj_compiled_symbol(\"%s\");\n", target, target, target);
		sb_printf(&f->out, "\t%s = CLJC_FN_%s ? CLJC_FN_%s(clj_var_root_relaxed(V[%zu]), NULL, %s, %u) : clj_c_invoke(%s, %s, %u);\n#else\n", r.name, target, target,
		          var_index(f->u, head->u.var), array, nargs, fn.name, array, nargs);
		sb_printf(&f->out, "\t%s = clj_c_invoke(%s, %s, %u);\n#endif\n", r.name, fn.name, array, nargs);
	} else if (!folded) {
		sb_printf(&f->out, "\tclj_value %s = clj_c_invoke(%s, %s, %u);\n", r.name, fn.name, array, nargs);
	}
	release_args(f, args, nargs);
	release_temp(f, &fn);
	check_thrown(f, r.name);
	live_push(f, r);
	return r;
}

static temp emit_def(fnctx *f, const clj_node *n) {
	size_t vi = var_index(f->u, n->u.def.var);
	if (n->u.def.init) {
		// only the def that is the form itself takes the form's base: two defs under one let each get a nested name
		bool named = n->u.def.init->kind == CLJ_NODE_FN && f->top && n->id == 0 && *f->fn_counter == 0;
		if (named) fn_line(f, n->u.def.init);
		temp init = named ? emit_fn_as(f, n->u.def.init, f->base) : emit(f, n->u.def.init);
		sb_printf(&f->out, "\tclj_var_bind_root(V[%zu], %s);\n", vi, init.name);
		release_temp(f, &init);
	}
	temp meta = emit(f, n->u.def.meta);
	temp r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = clj_c_def(V[%zu], %s, %s, %s);\n", r.name, vi, meta.name, n->u.def.macro ? "true" : "false", n->u.def.dynamic ? "true" : "false");
	release_temp(f, &meta);
	check_thrown(f, r.name);
	live_push(f, r);
	return r;
}

static temp emit_literal(fnctx *f, const clj_node *n, const char *ctor) {
	if (n->u.seq.n == 0) {
		temp t = new_temp(f, OWN_YES);
		sb_printf(&f->out, "\tclj_value %s = %s;\n", t.name, n->kind == CLJ_NODE_VECTOR ? "clj_vector_empty()" : n->kind == CLJ_NODE_MAP ? "clj_map_empty()" : "clj_set_empty()");
		live_push(f, t);
		return t;
	}
	char array[24];
	snprintf(array, sizeof array, "a%d", f->naux++);
	temp *items = emit_args(f, n->u.seq.items, n->u.seq.n, array);
	temp  r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = %s(%s, %u);\n", r.name, ctor, array, n->u.seq.n);
	release_args(f, items, n->u.seq.n);
	check_thrown(f, r.name);
	live_push(f, r);
	return r;
}

static temp emit_throw(fnctx *f, const clj_node *n) {
	temp v = emit(f, n->u.throw_);
	temp r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = clj_throw(%s);\n", r.name, v.name);
	live_forget(f, &v);
	check_thrown(f, r.name);
	live_push(f, r);
	return r;
}

// The interpreter's eval_try step by step: v is the body's result or CLJ_THROWN through the catches and finally.
static temp emit_try(fnctx *f, const clj_node *n) {
	int  k = f->naux++;
	temp v = new_temp(f, OWN_NO);
	sb_printf(&f->out, "\tclj_value %s;\n", v.name);
	int body_fail = new_label(f), body_join = new_label(f);
	push_handler(f, body_fail);
	temp body = emit(f, n->u.try_.body);
	handler h = pop_handler(f);
	sb_printf(&f->out, "\t%s = %s;\n", v.name, body.name);
	live_forget(f, &body);
	if (h.used) sb_printf(&f->out, "\tgoto L%d;\nL%d: ;\n\t%s = CLJ_THROWN;\nL%d: ;\n", body_join, body_fail, v.name, body_join);
	if (n->u.try_.ncatches) {
		sb_printf(&f->out, "\tif (%s == CLJ_THROWN) {\n\tclj_value tr%d = clj_take_pending_trace();\n\tclj_value ex%d = clj_take_pending();\n\tbool h%d = false;\n", v.name, k, k, k);
		int hfail = new_label(f), hdone = new_label(f);
		push_handler(f, hfail);
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			const clj_catch *c = &n->u.try_.catches[i];
			if (c->kind == CLJ_CATCH_ALL) sb_printf(&f->out, "\t%sif (true) {\n", i ? "else " : "");
			else sb_printf(&f->out, "\t%sif (clj_is_exception(ex%d)) {\n", i ? "else " : "", k);
			char ex[16];
			snprintf(ex, sizeof ex, "ex%d", k);
			emit_set(f, c->slot, ex);
			sb_printf(&f->out, "\th%d = true;\n", k);
			temp hv = emit(f, c->handler);
			sb_printf(&f->out, "\t%s = %s;\n\t}\n", v.name, hv.name);
			live_forget(f, &hv);
		}
		handler hh = pop_handler(f);
		sb_printf(&f->out, "\tgoto L%d;\n", hdone);
		if (hh.used) sb_printf(&f->out, "L%d: ;\n\t%s = CLJ_THROWN;\n", hfail, v.name);
		sb_printf(&f->out, "L%d: ;\n\tif (h%d) clj_release(tr%d); else clj_throw_traced(ex%d, tr%d);\n\t}\n", hdone, k, k, k, k);
	}
	if (n->u.try_.finally_) {
		sb_printf(&f->out, "\tclj_value pt%d = %s == CLJ_THROWN ? clj_take_pending_trace() : CLJ_NIL;\n", k, v.name);
		sb_printf(&f->out, "\tclj_value p%d = %s == CLJ_THROWN ? clj_take_pending() : CLJ_NIL;\n", k, v.name);
		int ffail = new_label(f), fdone = new_label(f);
		push_handler(f, ffail);
		temp fv = emit(f, n->u.try_.finally_);
		handler fh = pop_handler(f);
		release_temp(f, &fv);
		sb_printf(&f->out, "\tif (%s == CLJ_THROWN) clj_throw_traced(p%d, pt%d);\n\tgoto L%d;\n", v.name, k, k, fdone);
		if (fh.used) sb_printf(&f->out, "L%d: ;\n\tclj_release(%s == CLJ_THROWN ? p%d : %s);\n\tclj_release(pt%d);\n\t%s = CLJ_THROWN;\n", ffail, v.name, k, v.name, k, v.name);
		sb_printf(&f->out, "L%d: ;\n", fdone);
	}
	temp r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = %s;\n", r.name, v.name);
	check_thrown(f, r.name);
	live_push(f, r);
	return r;
}

static void emit_intrinsic_call(fnctx *f, const clj_intrinsic *op, const char *cname, const temp *args, const char *result) {
	sb_printf(&f->out, "\t%s = %s(", result, cname);
	for (uint32_t i = 0; i < op->arity; i++) sb_printf(&f->out, "%s%s", i ? ", " : "", args[i].name);
	sb_puts(&f->out, ");\n");
}

// Every argument is an unboxable expression: the operation runs on the untagged values and only the result is boxed.
static temp emit_unboxed(fnctx *f, const clj_node *n, unbox_op uop) {
	if (unbox_is_arith(uop)) {
		itemp r = emit_raw(f, n);
		temp  t = new_temp(f, OWN_YES);
		sb_printf(&f->out, "\tclj_value %s = %s(%s);\n", t.name, r.kind == UK_INT ? "clj_long_new" : "clj_double_new", r.name);
		live_push(f, t);
		return t;
	}
	itemp a = emit_raw(f, n->u.intrinsic.args[0]);
	itemp b = {"0", UK_INT};
	if (n->u.intrinsic.n == 2) b = emit_raw(f, n->u.intrinsic.args[1]);
	char cmp[96];
	compare_expr(cmp, sizeof cmp, uop, a.name, a.kind, b.name, b.kind);
	temp t = new_temp(f, OWN_NO);
	sb_printf(&f->out, "\tclj_value %s = clj_bool(%s);\n", t.name, cmp);
	f->u->slots.unboxed++;
	return t;
}

// The tags of the boxed arguments decide at run time: a stale fact costs the check and never the result.
static temp emit_tag_checked(fnctx *f, const clj_node *n, unbox_op uop, const ukind *kinds) {
	const clj_intrinsic *op = n->u.intrinsic.op;
	size_t               oi = op_index(f->u, op), vi = var_index(f->u, n->u.intrinsic.var);
	uint32_t             nargs = n->u.intrinsic.n;
	char                 array[24];
	snprintf(array, sizeof array, "a%d", f->naux++);
	bool  raw[2] = {false, false};
	itemp it[2];
	temp  bt[2];
	for (uint32_t i = 0; i < nargs; i++) {
		raw[i] = unboxable_kind(f, n->u.intrinsic.args[i]) != UK_NONE;
		if (raw[i]) it[i] = emit_raw(f, n->u.intrinsic.args[i]);
		else bt[i] = emit_borrowed(f, n->u.intrinsic.args[i]);
	}
	temp r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s;\n", r.name);
	// the generic paths take the boxed form of every argument
	sb_printf(&f->out, "\tclj_value %s[%u];\n", array, nargs);
	sb_printf(&f->out, "\tif (CLJC_GUARD(V[%zu], B[%zu])) {\n\tif (1", vi, oi);
	for (uint32_t i = 0; i < nargs; i++) {
		if (!raw[i]) sb_printf(&f->out, " && %s(%s)", kinds[i] == UK_INT ? "clj_is_fixnum" : "clj_is_double", bt[i].name);
	}
	sb_puts(&f->out, ") {\n");
	char x[2][24];
	for (uint32_t i = 0; i < nargs; i++) {
		if (raw[i]) snprintf(x[i], sizeof x[i], "%s", it[i].name);
		else {
			itemp v = new_itemp(f, kinds[i]);
			sb_printf(&f->out, "\t%s %s = %s(%s);\n", ctype(kinds[i]), v.name, kinds[i] == UK_INT ? "clj_fixnum_val" : "clj_double_val", bt[i].name);
			snprintf(x[i], sizeof x[i], "%s", v.name);
		}
	}
	ukind k1 = nargs == 2 ? kinds[1] : (unbox_is_arith(uop) ? UK_INT : kinds[0]);
	if (nargs == 1) snprintf(x[1], sizeof x[1], "%s", unbox_is_arith(uop) ? "INT64_C(1)" : "0");
	if (unbox_is_arith(uop)) {
		ukind rk = arith_kind(uop, kinds[0], k1);
		itemp z = new_itemp(f, rk);
		if (rk == UK_INT) {
			const char *builtin = (uop == UNBOX_ADD || uop == UNBOX_INC) ? "__builtin_add_overflow" : (uop == UNBOX_MUL) ? "__builtin_mul_overflow" : "__builtin_sub_overflow";
			sb_printf(&f->out, "\tint64_t %s;\n\tif (__builtin_expect(%s(%s, %s, &%s), 0)) %s = clj_throw_msg(\"integer overflow\");\n\telse %s = clj_long_new(%s);\n", z.name, builtin, x[0], x[1], z.name,
			          r.name, r.name, z.name);
		} else {
			emit_double_arith(f, uop, x[0], x[1], z.name);
			sb_printf(&f->out, "\t%s = clj_double_new(%s);\n", r.name, z.name);
		}
	} else {
		char cmp[96];
		compare_expr(cmp, sizeof cmp, uop, x[0], kinds[0], x[1], k1);
		sb_printf(&f->out, "\t%s = clj_bool(%s);\n", r.name, cmp);
	}
	sb_puts(&f->out, "\t} else {\n");
	for (uint32_t i = 0; i < nargs; i++) sb_printf(&f->out, "\t%s[%u] = %s;\n", array, i, raw[i] ? "CLJ_NIL" : bt[i].name);
	for (uint32_t i = 0; i < nargs; i++) {
		if (raw[i]) sb_printf(&f->out, "\t%s[%u] = %s(%s);\n", array, i, it[i].kind == UK_INT ? "clj_long_new" : "clj_double_new", it[i].name);
	}
	if (nargs == 2) sb_printf(&f->out, "\t%s = %s(%s[0], %s[1]);\n", r.name, op->cname, array, array);
	else sb_printf(&f->out, "\t%s = %s(%s[0]);\n", r.name, op->cname, array);
	for (uint32_t i = 0; i < nargs; i++) {
		if (raw[i]) sb_printf(&f->out, "\tclj_release(%s[%u]);\n", array, i);
	}
	sb_puts(&f->out, "\t}\n\t} else {\n");
	for (uint32_t i = 0; i < nargs; i++) sb_printf(&f->out, "\t%s[%u] = %s;\n", array, i, raw[i] ? "CLJ_NIL" : bt[i].name);
	for (uint32_t i = 0; i < nargs; i++) {
		if (raw[i]) sb_printf(&f->out, "\t%s[%u] = %s(%s);\n", array, i, it[i].kind == UK_INT ? "clj_long_new" : "clj_double_new", it[i].name);
	}
	sb_printf(&f->out, "\t%s = clj_c_intrinsic_fallback(V[%zu], %s, %u);\n", r.name, vi, array, nargs);
	for (uint32_t i = 0; i < nargs; i++) {
		if (raw[i]) sb_printf(&f->out, "\tclj_release(%s[%u]);\n", array, i);
	}
	sb_puts(&f->out, "\t}\n");
	for (uint32_t i = nargs; i-- > 0;) {
		if (!raw[i]) release_temp(f, &bt[i]);
	}
	check_thrown(f, r.name);
	live_push(f, r);
	f->u->slots.tag_checked++;
	return r;
}

static temp emit_intrinsic(fnctx *f, const clj_node *n) {
	const clj_intrinsic *op = n->u.intrinsic.op;
	unbox_op             uop = unbox_op_of(op);
	if (uop != UNBOX_NONE) {
		bool  all_unboxable = true, all_typed = true;
		ukind kinds[2] = {UK_NONE, UK_NONE};
		for (uint32_t i = 0; i < n->u.intrinsic.n; i++) {
			ukind u = unboxable_kind(f, n->u.intrinsic.args[i]);
			kinds[i] = u != UK_NONE ? u : fact_kind(f, n->u.intrinsic.args[i]);
			all_unboxable = all_unboxable && u != UK_NONE;
			all_typed = all_typed && kinds[i] != UK_NONE;
		}
		ukind k1 = n->u.intrinsic.n == 2 ? kinds[1] : (unbox_is_arith(uop) ? UK_INT : kinds[0]);
		bool  ok = unbox_is_arith(uop) ? arith_kind(uop, kinds[0], k1) != UK_NONE : compare_kinds_ok(uop, kinds[0], k1);
		if (all_unboxable && ok) return emit_unboxed(f, n, uop);
		if (all_typed && ok) return emit_tag_checked(f, n, uop, kinds);
	}
	size_t oi = op_index(f->u, op), vi = var_index(f->u, n->u.intrinsic.var);
	char   array[24];
	snprintf(array, sizeof array, "a%d", f->naux++);
	temp *args = emit_args(f, n->u.intrinsic.args, n->u.intrinsic.n, array);
	temp  r = new_temp(f, OWN_YES);
	bool  consuming = clj_intrinsic_consumes(op) && args[0].own != OWN_NO;
	sb_printf(&f->out, "\tclj_value %s;\n", r.name);
	sb_printf(&f->out, "\tif (CLJC_GUARD(V[%zu], B[%zu])) {\n", vi, oi);
	if (consuming && args[0].own == OWN_YES) {
		emit_intrinsic_call(f, op, op->cconsume, args, r.name);
	} else if (consuming) {
		sb_printf(&f->out, "\tif (%s) {\n", args[0].flag);
		emit_intrinsic_call(f, op, op->cconsume, args, r.name);
		sb_printf(&f->out, "\t%s = false;\n\t} else {\n", args[0].flag);
		emit_intrinsic_call(f, op, op->cname, args, r.name);
		sb_puts(&f->out, "\t}\n");
	} else {
		emit_intrinsic_call(f, op, op->cname, args, r.name);
	}
	sb_printf(&f->out, "\t} else {\n\t%s = clj_c_intrinsic_fallback(V[%zu], %s, %u);\n", r.name, vi, array, n->u.intrinsic.n);
	if (consuming && args[0].own == OWN_YES) sb_printf(&f->out, "\tclj_release(%s);\n", args[0].name);
	sb_puts(&f->out, "\t}\n");
	if (consuming && args[0].own == OWN_YES) live_forget(f, &args[0]);
	for (uint32_t i = n->u.intrinsic.n; i-- > 0;) {
		if (i == 0 && consuming && args[0].own == OWN_YES) continue;
		release_temp(f, &args[i]);
	}
	free(args);
	check_thrown(f, r.name);
	live_push(f, r);
	return r;
}

static temp emit_fused(fnctx *f, const clj_node *n) {
	char array[24], frame[24];
	int  k = f->naux++;
	snprintf(array, sizeof array, "a%d", k);
	temp *args = emit_args(f, n->u.fused.args, n->u.fused.nargs, array);
	snprintf(frame, sizeof frame, "fr%d", k);
	sb_printf(&f->out, "\tclj_cframe %s = {(clj_value *)%s, NULL, 0, NULL};\n\t(void)%s;\n", frame, array, frame);
	temp        r = new_temp(f, OWN_YES);
	const char *saved_frame = f->frame;
	uint64_t    saved_promoted = f->promoted, saved_borrowed = f->borrowed, saved_ints = f->ints, saved_dbls = f->dbls;
	const clj_node *saved_body = f->body;
	f->frame = frame;
	f->promoted = f->borrowed = f->ints = f->dbls = 0;
	f->body = NULL;
	if (f->facts) {
		uint32_t fi = facts_frame(f->facts, n, n->u.fused.fused);
		const clj_facts_frame *ff = fi == UINT32_MAX ? NULL : clj_facts_frame_at(f->facts, fi);
		for (uint32_t i = 0; ff && i < ff->nslots; i++) {
			if (clj_facts_escape(f->facts, fi, i) == CLJ_ESCAPE_LOCAL) f->u->slots.local_fused++;
		}
	}
	sb_printf(&f->out, "\tclj_value %s;\n", r.name);
	sb_printf(&f->out, "\t{\n\tconst clj_fusion_var *g%d[%u] = {", k, n->u.fused.nguards);
	for (uint32_t i = 0; i < n->u.fused.nguards; i++) sb_printf(&f->out, "%sF[%zu]", i ? ", " : "", fusion_index(f->u, n->u.fused.guards[i]));
	sb_printf(&f->out, "};\n\t(void)g%d;\n\tif (CLJC_FUSED(g%d, %u)) {\n", k, k, n->u.fused.nguards);
	temp a = emit(f, n->u.fused.fused);
	sb_printf(&f->out, "\t%s = %s;\n", r.name, a.name);
	live_forget(f, &a);
	sb_puts(&f->out, "\t} else {\n");
	temp b = emit(f, n->u.fused.original);
	sb_printf(&f->out, "\t%s = %s;\n", r.name, b.name);
	live_forget(f, &b);
	sb_puts(&f->out, "\t}\n\t}\n");
	f->frame = saved_frame;
	f->promoted = saved_promoted;
	f->borrowed = saved_borrowed;
	f->ints = saved_ints;
	f->dbls = saved_dbls;
	f->body = saved_body;
	release_args(f, args, n->u.fused.nargs);
	live_push(f, r);
	return r;
}

// The name a direct fn's C functions got when its let emitted it: numbered like closures, found again by node,
// and per arity the slot array its promotion left (UINT32_MAX until that arity's body is emitted).
typedef struct {
	const clj_node *fn;
	char            base[256];
	uint32_t        arr[CLJ_FN_MAX_FIXED + 1];
} direct_name;

static direct_name *direct_names;
static size_t       ndirect_names, direct_names_cap;

static void direct_name_add(const clj_node *fn, const char *base) {
	if (ndirect_names == direct_names_cap) {
		direct_names_cap = direct_names_cap ? direct_names_cap * 2 : 16;
		direct_names = realloc(direct_names, direct_names_cap * sizeof *direct_names);
		if (!direct_names) clj_fatal("out of memory");
	}
	direct_names[ndirect_names].fn = fn;
	snprintf(direct_names[ndirect_names].base, sizeof direct_names[ndirect_names].base, "%s", base);
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) direct_names[ndirect_names].arr[i] = UINT32_MAX;
	ndirect_names++;
}

static direct_name *direct_name_entry(const clj_node *fn) {
	for (size_t i = ndirect_names; i-- > 0;) {
		if (direct_names[i].fn == fn) return &direct_names[i];
	}
	clj_fatal("compiler: direct call before its fn");
}

static const char *direct_name_of(const clj_node *fn) { return direct_name_entry(fn)->base; }

// The slots a caller must give the arity: what its promotion left in the array (its params are always there), or
// the whole frame while the arity is not emitted yet (a call from an earlier arity of the same fn).
static uint32_t direct_array_slots(const clj_node *fn, const clj_fn_arity *a) {
	uint32_t arr = direct_name_entry(fn)->arr[a->nparams];
	return arr == UINT32_MAX ? a->nslots : arr;
}

static temp emit_direct_call(fnctx *f, const clj_node *n) {
	const clj_fn_arity *arity = n->u.direct.arity;
	uint32_t            nargs = n->u.direct.n;
	int                 k = f->naux++;
	temp               *args = calloc(nargs ? nargs : 1, sizeof *args);
	if (!args) clj_fatal("out of memory");
	for (uint32_t i = 0; i < nargs; i++) args[i] = emit_borrowed(f, n->u.direct.args[i]);
	uint32_t nslots = direct_array_slots(n->u.direct.fn, arity);
	f->u->slots.direct_slots += arity->nslots;
	f->u->slots.direct_array += nslots;
	if (nslots < nargs) clj_fatal("compiler: a direct fn's params outside its array");
	if (nslots == 0) nslots = 1;
	sb_printf(&f->out, "\tclj_value ds%d[%u] = {", k, nslots);
	for (uint32_t i = 0; i < nargs; i++) sb_printf(&f->out, "%s%s", i ? ", " : "", args[i].name);
	if (!nargs) sb_puts(&f->out, "CLJ_NIL");
	sb_printf(&f->out, "};\n\tuint64_t dm%d = 0", k);
	for (uint32_t i = 0; i < nargs; i++) {
		if (args[i].own == OWN_YES) sb_printf(&f->out, " | ((uint64_t)1 << %u)", i);
		else if (args[i].own == OWN_DYN) sb_printf(&f->out, " | (%s ? (uint64_t)1 << %u : 0)", args[i].flag, i);
	}
	sb_puts(&f->out, ";\n");
	temp r = new_temp(f, OWN_YES);
	sb_printf(&f->out, "\tclj_value %s = %s_a%u(clj_c_outer(&%s, %u), %s.captured, ds%d, dm%d);\n", r.name, direct_name_of(n->u.direct.fn), arity->nparams, f->frame,
	          n->u.direct.depth, f->frame, k, k);
	for (uint32_t i = nargs; i-- > 0;) live_forget(f, &args[i]);
	free(args);
	check_thrown(f, r.name);
	live_push(f, r);
	return r;
}

static temp emit(fnctx *f, const clj_node *n) {
	fn_line(f, n);
	switch (n->kind) {
	case CLJ_NODE_CONST: return emit_const(f, n, false);
	case CLJ_NODE_LOCAL: return emit_local(f, n, false);
	case CLJ_NODE_CAPTURED: return emit_captured(f, n, false);
	case CLJ_NODE_OUTER: return emit_outer(f, n);
	case CLJ_NODE_VAR: return emit_var(f, n, false);
	case CLJ_NODE_IF: return emit_if(f, n);
	case CLJ_NODE_DO: return emit_do(f, n);
	case CLJ_NODE_LET: return emit_let(f, n);
	case CLJ_NODE_LOOP: return emit_loop(f, n);
	case CLJ_NODE_RECUR: return emit_recur(f, n);
	case CLJ_NODE_FN: return emit_fn(f, n);
	case CLJ_NODE_INVOKE: return emit_invoke(f, n);
	case CLJ_NODE_DEF: return emit_def(f, n);
	case CLJ_NODE_VECTOR: return emit_literal(f, n, "clj_vector_from_array");
	case CLJ_NODE_MAP: return emit_literal(f, n, "clj_c_map_literal");
	case CLJ_NODE_SET: return emit_literal(f, n, "clj_c_set_literal");
	case CLJ_NODE_TRY: return emit_try(f, n);
	case CLJ_NODE_THROW: return emit_throw(f, n);
	case CLJ_NODE_INTRINSIC: return emit_intrinsic(f, n);
	case CLJ_NODE_FUSED: return emit_fused(f, n);
	case CLJ_NODE_DIRECT_FN: {
		// Only a let/loop init holds one, and emit_bindings handles it there.
		temp t = new_temp(f, OWN_NO);
		sb_printf(&f->out, "\tclj_value %s = CLJ_NIL;\n", t.name);
		return t;
	}
	case CLJ_NODE_DIRECT_CALL: return emit_direct_call(f, n);
	}
	clj_fatal("compiler: unknown node kind");
}

static temp emit_borrowed(fnctx *f, const clj_node *n) {
	switch (n->kind) {
	case CLJ_NODE_CONST: fn_line(f, n); return emit_const(f, n, true);
	case CLJ_NODE_LOCAL: fn_line(f, n); return emit_local(f, n, true);
	case CLJ_NODE_CAPTURED: fn_line(f, n); return emit_captured(f, n, true);
	case CLJ_NODE_VAR: fn_line(f, n); return emit_var(f, n, true);
	default: return emit(f, n);
	}
}

// ---- functions

static uint32_t stub_new(unit *u, fnctx *f, clj_value name, uint32_t line, uint32_t col) {
	uint32_t id = u->nstubs++;
	char     sym[64] = "CLJ_NIL";
	if (!clj_is_nil(name)) {
		bool ok;
		snprintf(sym, sizeof sym, "K[%zu]", const_index(f, name, &ok));
	}
	sb_printf(&u->init, "\tclj_c_stub_init(&S[%u], %s, %u, %u);\n", id, sym, line, col);
	return id;
}

static fnctx fnctx_child(fnctx *parent, const char *base) {
	fnctx f;
	memset(&f, 0, sizeof f);
	f.c = parent->c;
	f.u = parent->u;
	f.frame = "fr";
	f.facts = parent->facts;
	f.base = base;
	f.fn_counter = parent->fn_counter;
	f.ns = parent->ns;
	f.form = parent->form;
	return f;
}

static void fnctx_free(fnctx *f) {
	sb_free(&f->out);
	free(f->live);
	free(f->handlers);
}

// The releases at a frame's exit: the promoted slots, then the owned entries of the array.
static void emit_frame_teardown(fnctx *f, uint32_t arr) {
	emit_promoted_releases(f);
	if (arr) sb_printf(&f->out, "\tclj_c_release_slots(&fr, %u);\n", arr);
}

// One arity of a closure: the frame, the guard and shadow frame, the body under a recur label, teardown.
static void emit_closure_arity(fnctx *parent, const clj_node *n, const clj_fn_arity *a, const char *base, uint32_t stub, bool exported) {
	fnctx f = fnctx_child(parent, base);
	char  name[300];
	snprintf(name, sizeof name, "%s_%c%u", base, a->variadic ? 'v' : 'a', a->nparams);
	sb_printf(&f.u->protos, "%sclj_value %s(clj_value self, const clj_value *captured, const clj_value *args, size_t nargs);\n", exported ? "" : "static ", name);
	sb_printf(&f.out, "%sclj_value %s(clj_value self, const clj_value *captured, const clj_value *args, size_t nargs) {\n", exported ? "" : "static ", name);
	sb_puts(&f.out, "\t(void)self; (void)captured; (void)args; (void)nargs;\n");
	promote_slots(&f, n, a, true, a->body, a->nslots);
	uint32_t arr = array_slots(&f, a->nslots);
	if (arr) sb_printf(&f.out, "\tclj_value s[%u];\n\tclj_cframe fr = {s, captured, 0, NULL};\n", arr);
	else sb_puts(&f.out, "\tclj_cframe fr = {NULL, captured, 0, NULL};\n");
	sb_puts(&f.out, "\t(void)fr;\n");
	for (uint32_t i = 0; i < a->nparams; i++) {
		char arg[24];
		snprintf(arg, sizeof arg, "args[%u]", i);
		emit_slot_arrival(&f, i, arg);
	}
	if (a->nparams < arr) sb_printf(&f.out, "\tfor (uint32_t i = %u; i < %u; i++) s[i] = CLJ_NIL;\n", a->nparams, arr);
	emit_promoted_decls(&f);
	if (a->variadic) {
		uint32_t r = a->nparams;
		if (promoted(&f, r)) sb_printf(&f.out, "\tl%u = nargs > %u ? clj_list_from_array(args + %u, nargs - %u) : CLJ_NIL;\n", r, r, r, r);
		else sb_printf(&f.out, "\ts[%u] = nargs > %u ? clj_list_from_array(args + %u, nargs - %u) : CLJ_NIL;\n\tfr.owned |= (uint64_t)1 << %u;\n", r, r, r, r, r);
	}
	if (a->self_slot >= 0) emit_slot_arrival(&f, (uint32_t)a->self_slot, "self");
	if (a->nslots > 64) {
		sb_printf(&f.out, "\tclj_c_retain_params(&fr, %u);\n", a->nparams);
		if (a->self_slot >= 0) sb_puts(&f.out, "\tclj_retain(self);\n");
	}
	int noenter = new_label(&f), fail = new_label(&f);
	sb_printf(&f.out, "\tclj_ccall cc;\n\tif (!clj_c_enter(&S[%u], &cc)) goto L%d;\n", stub, noenter);
	push_handler(&f, fail);
	f.recur_label = new_label(&f);
	f.recur_loop = false;
	f.recur_live_mark = 0;
	size_t at = f.out.len;
	temp   r = emit(&f, a->body);
	if (f.recur_used) {
		char label[32];
		snprintf(label, sizeof label, "L%d: ;\n", f.recur_label);
		sb_put(&f.out, label, strlen(label));
		memmove(f.out.s + at + strlen(label), f.out.s + at, f.out.len - strlen(label) - at);
		memcpy(f.out.s + at, label, strlen(label));
	}
	handler h = pop_handler(&f);
	sb_printf(&f.out, "\tclj_c_leave(&S[%u], &cc);\n", stub);
	emit_frame_teardown(&f, arr);
	sb_printf(&f.out, "\treturn %s;\n", r.name);
	if (h.used) sb_printf(&f.out, "L%d: ;\n\tclj_c_leave(&S[%u], &cc);\n", fail, stub);
	sb_printf(&f.out, "L%d: ;\n", noenter);
	emit_frame_teardown(&f, arr);
	sb_puts(&f.out, "\treturn CLJ_THROWN;\n}\n\n");
	sb_put(&f.u->fns, f.out.s, f.out.len);
	fnctx_free(&f);
}

static void emit_fn_functions(fnctx *parent, const clj_node *n, const char *base) {
	unit    *u = parent->u;
	uint32_t stub = stub_new(u, parent, n->u.fn.name, n->line, n->col);
	// A top-level (def name (fn ...)) is a direct-call target of closed units; its base is the def's own symbol.
	bool exported = strcmp(base, parent->base) == 0 && parent->top;
	fn_base_add(parent->c, n, base, u, entry_arities(n));
	if (candidate(parent->c, n) && entry_arities(n)) impl_reg_add(u, base, entry_arities(n));
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (n->u.fn.fixed[i]) emit_closure_arity(parent, n, n->u.fn.fixed[i], base, stub, exported);
	}
	if (n->u.fn.variadic) emit_closure_arity(parent, n, n->u.fn.variadic, base, stub, exported);
	sb   d = {0};
	sb_printf(&u->protos, "static clj_value %s(void *ctx, const clj_value *args, size_t n);\n", base);
	sb_printf(&d, "static clj_value %s(void *ctx, const clj_value *args, size_t n) {\n\tconst clj_fn *f = ctx;\n\tswitch (n) {\n", base);
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (n->u.fn.fixed[i]) sb_printf(&d, "\tcase %u: return %s_a%u(clj_from_ptr((void *)f), f->env, args, n);\n", i, base, i);
	}
	sb_puts(&d, "\tdefault: break;\n\t}\n");
	if (n->u.fn.variadic) sb_printf(&d, "\tif (n >= %u) return %s_v%u(clj_from_ptr((void *)f), f->env, args, n);\n", n->u.fn.variadic->nparams, base, n->u.fn.variadic->nparams);
	sb_puts(&d, "\treturn clj_c_arity_error(clj_from_ptr((void *)f), n);\n}\n\n");
	sb_put(&u->fns, d.s, d.len);
	sb_free(&d);
}

// One arity of a direct fn: the caller filled the slots and hands their owned mask over with the defining frame.
static void emit_direct_arity(fnctx *parent, const clj_node *n, const clj_fn_arity *a, const char *base, uint32_t stub) {
	fnctx f = fnctx_child(parent, base);
	char  name[300];
	snprintf(name, sizeof name, "%s_a%u", base, a->nparams);
	sb_printf(&f.u->protos, "static clj_value %s(const clj_cframe *outer, const clj_value *captured, clj_value *slots, uint64_t owned);\n", name);
	sb_printf(&f.out, "static clj_value %s(const clj_cframe *outer, const clj_value *captured, clj_value *slots, uint64_t owned) {\n", name);
	sb_puts(&f.out, "\tclj_cframe fr = {slots, captured, owned, outer};\n\t(void)fr;\n");
	f.definer = parent;
	// the array is the caller's, sized by what promotion leaves in it: a promoted entry is simply never touched
	promote_slots(&f, n, a, false, a->body, a->nslots);
	uint32_t arr = array_slots(&f, a->nslots);
	direct_name_entry(n)->arr[a->nparams] = arr;
	emit_promoted_decls(&f);
	if (a->nslots > 64) sb_printf(&f.out, "\tclj_c_retain_params(&fr, %u);\n", a->nparams);
	int noenter = new_label(&f), fail = new_label(&f);
	sb_printf(&f.out, "\tclj_ccall cc;\n\tif (!clj_c_enter(&S[%u], &cc)) goto L%d;\n", stub, noenter);
	push_handler(&f, fail);
	f.recur_label = new_label(&f);
	f.recur_loop = false;
	size_t at = f.out.len;
	temp   r = emit(&f, a->body);
	if (f.recur_used) {
		char label[32];
		snprintf(label, sizeof label, "L%d: ;\n", f.recur_label);
		sb_put(&f.out, label, strlen(label));
		memmove(f.out.s + at + strlen(label), f.out.s + at, f.out.len - strlen(label) - at);
		memcpy(f.out.s + at, label, strlen(label));
	}
	handler h = pop_handler(&f);
	sb_printf(&f.out, "\tclj_c_leave(&S[%u], &cc);\n", stub);
	emit_frame_teardown(&f, arr);
	sb_printf(&f.out, "\treturn %s;\n", r.name);
	if (h.used) sb_printf(&f.out, "L%d: ;\n\tclj_c_leave(&S[%u], &cc);\n", fail, stub);
	sb_printf(&f.out, "L%d: ;\n", noenter);
	emit_frame_teardown(&f, arr);
	sb_puts(&f.out, "\treturn CLJ_THROWN;\n}\n\n");
	sb_put(&f.u->fns, f.out.s, f.out.len);
	fnctx_free(&f);
}

static void emit_direct_fn_functions(fnctx *parent, const clj_node *n, const char *base) {
	uint32_t stub = stub_new(parent->u, parent, n->u.fn.name, n->line, n->col);
	direct_name_add(n, base);
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (n->u.fn.fixed[i]) emit_direct_arity(parent, n, n->u.fn.fixed[i], base, stub);
	}
}

// ---- top-level forms

typedef struct {
	uint32_t nslots;
} slot_count;

static void count_slots(const clj_node *n, void *ctx) {
	slot_count *s = ctx;
	switch (n->kind) {
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) {
			if (n->u.let.slots[i] >= s->nslots) s->nslots = n->u.let.slots[i] + 1;
		}
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
			if (n->u.try_.catches[i].slot >= s->nslots) s->nslots = n->u.try_.catches[i].slot + 1;
		}
		break;
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN: return;
	default: break;
	}
	clj_node_children(n, count_slots, s);
}

// The symbol base of a top-level form: the def's name, else the namespace plus a form ordinal.
static char *form_base(cljc_compiler *c, unit *u, const clj_node *n) {
	const char *ns = clj_string_bytes(clj_symbol_name(clj_ns_name(clj_ns_current())));
	char       *base;
	if (n->kind == CLJ_NODE_DEF) {
		base = cljc_mangle(clj_string_bytes(clj_symbol_name(clj_var_ns(n->u.def.var))), clj_string_bytes(clj_symbol_name(clj_var_name(n->u.def.var))));
	} else {
		char ordinal[32];
		// compiled-eval units are one form each: number them per compiler, or every unit's impls share a registry name
		snprintf(ordinal, sizeof ordinal, "form%u", u->eval_result ? c->neval_forms++ : u->ntops);
		base = cljc_mangle(ns, ordinal);
	}
	bool   fresh;
	size_t i = pool_intern(&u->names, base, NULL, &fresh);
	(void)i;
	if (!fresh) {
		// A second definition of the same name in one unit: number it so the C symbols stay distinct.
		uint32_t k = 2;
		for (;;) {
			char numbered[600];
			snprintf(numbered, sizeof numbered, "%s__r%u", base, k);
			pool_intern(&u->names, numbered, NULL, &fresh);
			if (fresh) {
				free(base);
				base = xstrdup(numbered);
				break;
			}
			k++;
		}
	}
	return base;
}

static void close_form(unit *u) {
	if (!u->serial_open) return;
	if (!u->eval_result) sb_printf(&u->init, "N%u: ;\n", u->nforms);
	u->serial_open = false;
}

static void open_form(cljc_compiler *c, unit *u, fnctx *scratch, const clj_load_form *form) {
	(void)c;
	if (u->serial_open && u->last_serial == form->serial) return;
	close_form(u);
	u->nforms++;
	u->last_serial = form->serial;
	u->serial_open = true;
	char name[64] = "CLJ_NIL";
	if (!clj_is_nil(form->name)) {
		bool ok;
		snprintf(name, sizeof name, "K[%zu]", const_index(scratch, form->name, &ok));
	}
	if (c->opts.eval_result) {
		sb_printf(&u->init, "\t// form %u at %u:%u\n", u->nforms, form->line, form->col);
		return;
	}
	sb_printf(&u->init, "\t// form %u at %u:%u\n\tif (0) {\nF%u: ;\n\tif (!clj_load_form_failed(FILE_STR, %u, %u, %s)) goto fail;\n\tgoto N%u;\n\t}\n", u->nforms, form->line, form->col, u->nforms,
	          form->line, form->col, name, u->nforms);
}

static void record_direct(cljc_compiler *c, unit *u, const clj_node *n, const char *base) {
	if (n->kind != CLJ_NODE_DEF || !n->u.def.init) return;
	char key[600];
	snprintf(key, sizeof key, "%s/%s", clj_string_bytes(clj_symbol_name(clj_var_ns(n->u.def.var))), clj_string_bytes(clj_symbol_name(clj_var_name(n->u.def.var))));
	direct_entry *d = direct_add(c, key);
	bool          fresh;
	if (n->u.def.init->kind == CLJ_NODE_FN && !n->u.def.init->u.fn.ncaptures) pool_intern(&u->defined, base, key, &fresh);
	d->defs++;
	d->dynamic = d->dynamic || n->u.def.dynamic;
	free(d->base);
	d->base = xstrdup(base);
	d->fixed = 0;
	d->variadic = -1;
	const clj_node *init = n->u.def.init;
	if (init->kind != CLJ_NODE_FN || init->u.fn.ncaptures) return;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) {
		if (init->u.fn.fixed[i]) d->fixed |= (uint32_t)1 << i;
	}
	if (init->u.fn.variadic) d->variadic = (int32_t)init->u.fn.variadic->nparams;
}

// The population docs/facts-coverage.md counts: every slot of every frame of the tree.
static void count_facts_slots(unit *u, const clj_facts *facts) {
	for (uint32_t i = 0; i < clj_facts_nframes(facts); i++) {
		const clj_facts_frame *fr = clj_facts_frame_at(facts, i);
		u->slots.slots += fr->nslots;
		for (uint32_t k = 0; k < fr->nslots; k++) {
			if (clj_facts_escape(facts, i, k) == CLJ_ESCAPE_LOCAL) u->slots.local++;
		}
	}
}

// The facts a form is emitted under: the dev store's summaries and caller join. A form compiled on its own
// (compiled eval) records its fn bodies' sites first, so a callee defined in the same form sees its callers;
// a file's forms were evaluated by the interpreter, whose execs recorded every live fn body already.
static clj_facts *form_facts(cljc_compiler *c, const clj_node *n) {
	clj_summaries *store = clj_specialize_store();
	if (!c->opts.eval_result) return clj_facts_of_with(n, store);
	clj_facts *first = clj_facts_of_with(n, store);
	const void *owner = n;
	for (uint32_t i = 0; i < clj_facts_nsites(first); i++) {
		uint32_t        node, nargs;
		clj_value       var;
		const clj_fact *args;
		bool            in_fn;
		clj_facts_site(first, i, &node, &var, &nargs, &args, &in_fn);
		if (in_fn) clj_callers_add_site(owner, var, nargs, args);
	}
	for (uint32_t i = 0; i < clj_facts_nvalue_reads(first); i++) {
		bool      in_fn;
		clj_value var = clj_facts_value_read(first, i, &in_fn);
		if (in_fn) clj_callers_add_value_read(owner, var);
	}
	clj_facts_free(first);
	clj_facts *facts = clj_facts_of_with(n, store);
	clj_callers_forget(owner);
	return facts;
}

static void emit_top(cljc_compiler *c, unit *u, const clj_load_form *form, const clj_node *n) {
	uint32_t counter = 0;
	ndirect_names = 0;
	fnctx    f;
	memset(&f, 0, sizeof f);
	f.c = c;
	f.u = u;
	f.frame = "fr";
	f.fn_counter = &counter;
	f.ns = clj_string_bytes(clj_symbol_name(clj_ns_name(clj_ns_current())));
	f.form = form;
	f.top = true;
	c->emitting = n;
	open_form(c, u, &f, form);
	char *base = form_base(c, u, n);
	f.base = base;
	record_direct(c, u, n, base);
	slot_count sc = {0};
	count_slots(n, &sc);
	clj_facts *facts = form_facts(c, n);
	f.facts = facts;
	count_facts_slots(u, facts);
	promote_slots(&f, NULL, NULL, false, n, sc.nslots);
	uint32_t arr = array_slots(&f, sc.nslots);
	uint32_t top = u->ntops++;
	sb_printf(&u->protos, "static clj_value top_%u(void);\n", top);
	sb_printf(&f.out, "static clj_value top_%u(void) {\n", top);
	if (arr) sb_printf(&f.out, "\tclj_value s[%u];\n\tclj_cframe fr = {s, NULL, 0, NULL};\n\tfor (uint32_t i = 0; i < %u; i++) s[i] = CLJ_NIL;\n", arr, arr);
	else sb_puts(&f.out, "\tclj_cframe fr = {NULL, NULL, 0, NULL};\n");
	sb_puts(&f.out, "\t(void)fr;\n");
	emit_promoted_decls(&f);
	sb_puts(&f.out, "\tclj_eval_top_enter();\n");
	int fail = new_label(&f);
	push_handler(&f, fail);
	temp    r = emit(&f, n);
	handler h = pop_handler(&f);
	emit_frame_teardown(&f, arr);
	sb_printf(&f.out, "\tclj_eval_top_leave();\n\treturn %s;\n", r.name);
	if (h.used) {
		sb_printf(&f.out, "L%d: ;\n", fail);
		emit_frame_teardown(&f, arr);
		sb_puts(&f.out, "\tclj_eval_top_leave();\n\treturn CLJ_THROWN;\n");
	}
	sb_puts(&f.out, "}\n\n");
	sb_put(&u->fns, f.out.s, f.out.len);
	clj_facts_free(facts);
	if (c->opts.eval_result) sb_printf(&u->init, "\tr = top_%u();\n\tif (r == CLJ_THROWN) goto fail;\n", top);
	else sb_printf(&u->init, "\tr = top_%u();\n\tif (r == CLJ_THROWN) goto F%u;\n\tclj_release(r);\n", top, u->nforms);
	free(base);
	fnctx_free(&f);
}

// ---- the hook

static char *unit_text(cljc_compiler *c, unit *u, const char *init_name);
static void  emit_failed(cljc_compiler *c, unit *u, const clj_load_form *form, clj_value message);
static void  mark_cross_unit_impls(cljc_compiler *c);

// core.clj and the embedded libs are units only for the boot build; a file compile leaves them to the interpreter.
static bool skipped(const cljc_compiler *c, const char *file) {
	if (!c->opts.skip_embedded) return false;
	return strncmp(file, "<embedded>/", 11) == 0 || strcmp(file, CLJ_CORE_CLJ_PATH) == 0;
}

static pending_form *pend(cljc_compiler *c, unit *u, const clj_load_form *form) {
	if (c->npending == c->pending_cap) {
		c->pending_cap = c->pending_cap ? c->pending_cap * 2 : 64;
		c->pending = realloc(c->pending, c->pending_cap * sizeof *c->pending);
		if (!c->pending) clj_fatal("out of memory");
	}
	pending_form *p = &c->pending[c->npending++];
	memset(p, 0, sizeof *p);
	p->u = u;
	p->form = *form;
	clj_retain(form->file);
	clj_retain(form->name);
	p->ns = clj_ns_current();
	return p;
}

static clj_value on_form(const clj_load_form *form, const clj_node *node, void *ctx, bool *handled) {
	cljc_compiler *c = ctx;
	*handled = false;
	const char *file = clj_is_string(form->file) ? clj_string_bytes(form->file) : "<host>";
	if (skipped(c, file)) return CLJ_NIL;
	unit         *u = unit_for(c, file);
	pending_form *p = pend(c, u, form);
	p->node = node;
	clj_retain(clj_from_ptr((void *)node));
	return CLJ_NIL;
}

// Emits every pending form in load order, each in the namespace the hook saw it in.
static void flush_pending(cljc_compiler *c) {
	clj_value previous = clj_ns_current();
	for (size_t i = 0; i < c->npending; i++) {
		pending_form *p = &c->pending[i];
		clj_ns_set_current(p->ns);
		if (p->node) emit_top(c, p->u, &p->form, p->node);
		else emit_failed(c, p->u, &p->form, p->message);
		clj_release(clj_from_ptr((void *)p->node));
		clj_release(p->message);
		clj_release(p->form.file);
		clj_release(p->form.name);
	}
	clj_ns_set_current(previous);
	c->npending = 0;
}

// A unit's arm requests go with it: a later unit at the same address must not inherit them.
static void pimpls_forget(cljc_compiler *c, const unit *u) {
	size_t kept = 0;
	for (size_t i = 0; i < c->npimpls; i++) {
		if (c->pimpls[i].u == u) free(c->pimpls[i].name);
		else c->pimpls[kept++] = c->pimpls[i];
	}
	c->npimpls = kept;
}

char *cljc_form_text(cljc_compiler *c, const clj_load_form *form, const clj_node *node) {
	unit *u = unit_new(clj_is_string(form->file) ? clj_string_bytes(form->file) : "<host>");
	u->eval_result = c->opts.eval_result;
	emit_top(c, u, form, node);
	char *text = unit_text(c, u, NULL);
	pimpls_forget(c, u);
	unit_free(u);
	return text;
}

static void on_failed(const clj_load_form *form, clj_value message, void *ctx) {
	cljc_compiler *c = ctx;
	const char    *file = clj_is_string(form->file) ? clj_string_bytes(form->file) : "<host>";
	if (skipped(c, file)) return;
	pending_form *p = pend(c, unit_for(c, file), form);
	p->message = clj_retain(message);
}

static void emit_failed(cljc_compiler *c, unit *u, const clj_load_form *form, clj_value message) {
	fnctx scratch;
	memset(&scratch, 0, sizeof scratch);
	scratch.c = c;
	scratch.u = u;
	open_form(c, u, &scratch, form);
	sb_puts(&u->init, "\tr = clj_throw_msg(\"%s\", ");
	sb_c_string(&u->init, clj_string_bytes(message), clj_string_len(message));
	sb_printf(&u->init, ");\n\tgoto F%u;\n", u->nforms);
	close_form(u);
	fnctx_free(&scratch);
}

cljc_compiler *cljc_new(const cljc_options *opts) {
	cljc_compiler *c = calloc(1, sizeof *c);
	if (!c) clj_fatal("out of memory");
	c->opts = *opts;
	return c;
}

void cljc_free(cljc_compiler *c) {
	if (c->installed) cljc_end(c);
	free(c->pending);
	for (size_t i = 0; i < c->nunits; i++) unit_free(c->units[i]);
	free(c->units);
	for (size_t i = 0; i < c->nrefusals; i++) {
		free((char *)c->refusals[i].file);
		free((char *)c->refusals[i].reason);
	}
	free(c->refusals);
	for (size_t i = 0; i < c->ndirects; i++) {
		free(c->directs[i].qualified);
		free(c->directs[i].base);
	}
	free(c->directs);
	for (size_t i = 0; i < c->nfn_bases; i++) free(c->fn_bases[i].base);
	free(c->fn_bases);
	for (size_t i = 0; i < c->npimpls; i++) free(c->pimpls[i].name);
	free(c->pimpls);
	free(c->candidates);
	free(c);
}

void cljc_begin(cljc_compiler *c) {
	clj_load_hook h = {on_form, on_failed, c, c->opts.toplevel};
	clj_load_set_hook(&h);
	c->installed = true;
}

void cljc_end(cljc_compiler *c) {
	clj_load_set_hook(NULL);
	c->installed = false;
	flush_pending(c);
	mark_cross_unit_impls(c);
}

size_t      cljc_unit_count(const cljc_compiler *c) { return c->nunits; }
const char *cljc_unit_file(const cljc_compiler *c, size_t i) { return c->units[i]->file; }
char *cljc_unit_cname(const cljc_compiler *c, size_t i) {
	char name[600];
	snprintf(name, sizeof name, "u%zu_%s", i, c->units[i]->cfile);
	return xstrdup(name);
}


size_t              cljc_refusal_count(const cljc_compiler *c) { return c->nrefusals; }
const cljc_refusal *cljc_refusal_at(const cljc_compiler *c, size_t i) { return &c->refusals[i]; }

// The direct-call targets a unit binds to: every var defined exactly once in the compiled set, per arity. A target
// of this unit is a pointer to the function itself; another unit's is looked up in the symbol registry at first use.
static void emit_direct_prelude(cljc_compiler *c, unit *u, sb *out) {
	for (size_t i = 0; i < u->externs.n; i++) {
		const char *target = u->externs.keys[i];
		const char *us = strrchr(target, '_');
		if (!us || us[1] != 'a') continue;
		char base[300];
		snprintf(base, sizeof base, "%.*s", (int)(us - target), target);
		uint32_t arity = (uint32_t)strtoul(us + 2, NULL, 10);
		for (size_t j = 0; j < c->ndirects; j++) {
			const direct_entry *d = &c->directs[j];
			if (d->defs != 1 || d->dynamic || strcmp(d->base, base) != 0 || !((d->fixed >> arity) & 1)) continue;
			bool local = false;
			for (size_t k = 0; k < u->defined.n && !local; k++) local = strcmp(u->defined.keys[k], base) == 0;
			sb_printf(out, "#define CLJC_DIRECT_%s 1\nstatic clj_compiled_fn CLJC_FN_%s%s%s;\n", target, target, local ? " = " : "", local ? target : "");
		}
	}
}

// Whether CLJC_PIMPL_<id> gets defined: a named impl always, a node's once some unit emitted that arity of it.
static bool pimpl_defined(const cljc_compiler *c, const pimpl *p) {
	if (!p->node) return true;
	const fn_base *fb = fn_base_of(c, p->node);
	return fb && ((fb->arities >> p->nargs) & 1);
}

static const pimpl *pimpl_by_id(const cljc_compiler *c, uint32_t id) {
	for (size_t i = 0; i < c->npimpls; i++) {
		if (c->pimpls[i].id == id) return &c->pimpls[i];
	}
	return NULL;
}

// The --stats classification of a unit's protocol sites, by the arms that are actually written.
static void count_proto_sites(const cljc_compiler *c, const unit *u, cljc_slot_stats *st) {
	st->proto_direct = st->proto_switch = st->proto_cache = 0;
	for (size_t k = 0; k < u->npsites; k++) {
		const proto_site *s = &u->psites[k];
		uint32_t          direct = 0;
		for (uint32_t i = 0; i < s->n; i++) {
			const pimpl *p = s->pimpl[i] == UINT32_MAX ? NULL : pimpl_by_id(c, s->pimpl[i]);
			if (p && pimpl_defined(c, p)) direct++;
		}
		if (direct == 0) st->proto_cache++;
		else if (direct == 1 && s->n == 1 && !s->partial) st->proto_direct++;
		else st->proto_switch++;
	}
}

void cljc_unit_slots(const cljc_compiler *c, size_t i, cljc_slot_stats *out) {
	*out = c->units[i]->slots;
	count_proto_sites(c, c->units[i], out);
}

// The direct arms of a unit's protocol sites: CLJC_PIMPL_<id> names the impl's dispatcher (what a fill verifies the
// tables against) and its arity function (what a hit calls); a static of this unit by symbol, another unit's through
// the registry at the first fill. An unresolved arm stays undefined and its site keeps the cache alone.
static void emit_pimpl_prelude(cljc_compiler *c, unit *u, sb *out) {
	for (size_t i = 0; i < c->npimpls; i++) {
		const pimpl *p = &c->pimpls[i];
		if (p->u != u || !pimpl_defined(c, p)) continue;
		const char *base = p->name;
		bool        local = false;
		if (p->node) {
			const fn_base *fb = fn_base_of(c, p->node);
			base = fb->base;
			local = fb->u == u;
		}
		sb_printf(out, "#define CLJC_PIMPL_%u 1\n", p->id);
		if (local) {
			sb_printf(out, "#define CLJC_PIMPL_%u_CODE %s\n#define CLJC_PIMPL_%u_FN %s_a%u\n", p->id, base, p->id, base, p->nargs);
		} else {
			sb_printf(out, "static clj_cproto_impl PI%u = {\"%s\", \"%s_a%u\", NULL, NULL};\n", p->id, base, base, p->nargs);
			sb_printf(out, "#define CLJC_PIMPL_%u_CODE clj_c_impl_code(&PI%u)\n#define CLJC_PIMPL_%u_FN clj_c_impl_fn(&PI%u)\n", p->id, p->id, p->id, p->id);
		}
	}
}

// Every impl another unit of the set names by node registers here: known once every unit is emitted.
static void mark_cross_unit_impls(cljc_compiler *c) {
	for (size_t i = 0; i < c->npimpls; i++) {
		const pimpl *p = &c->pimpls[i];
		if (!p->node) continue;
		const fn_base *fb = fn_base_of(c, p->node);
		if (fb && fb->u != p->u && fb->arities) impl_reg_add(fb->u, fb->base, fb->arities);
	}
}

static void emit_impl_registrations(unit *u, sb *out) {
	for (size_t k = 0; k < u->impl_regs.n; k++) {
		const char *base = u->impl_regs.keys[k];
		uint32_t    arities = (uint32_t)strtoul(u->impl_regs.extra[k], NULL, 10);
		sb_printf(out, "\tclj_compiled_register_impl(\"%s\", %s);\n", base, base);
		for (uint32_t a = 0; a <= CLJ_FN_MAX_FIXED; a++) {
			if ((arities >> a) & 1) sb_printf(out, "\tclj_compiled_register_symbol(\"%s_a%u\", %s_a%u);\n", base, a, base, a);
		}
	}
}

static void emit_direct_registrations(cljc_compiler *c, unit *u, sb *out) {
	for (size_t k = 0; k < u->defined.n; k++) {
		const direct_entry *d = direct_find(c, u->defined.extra[k]);
		if (!d || d->defs != 1 || d->dynamic || strcmp(d->base, u->defined.keys[k]) != 0) continue;
		for (uint32_t a = 0; a <= CLJ_FN_MAX_FIXED; a++) {
			if ((d->fixed >> a) & 1) sb_printf(out, "\tclj_compiled_register_symbol(\"%s_a%u\", %s_a%u);\n", d->base, a, d->base, a);
		}
	}
}

static char *unit_text(cljc_compiler *c, unit *u, const char *init_name) {
	close_form(u);
	sb out = {0};
	sb_printf(&out, "// Generated by clj-compile from %s; do not edit.\n", u->file);
	if (c->opts.closed && !c->opts.guard_macro) sb_puts(&out, "#define CLJ_CLOSED 1\n");
	sb_puts(&out, "#include \"compiled_internal.h\"\n\n");
	sb_printf(&out, "#define FILE_STR %s\n", u->has_file ? "(K[0])" : "CLJ_NIL");
	if (c->opts.guard_macro) sb_printf(&out, "#ifdef %s\n", c->opts.guard_macro);
	sb_printf(&out, "static clj_value K[%zu];\nstatic clj_value V[%zu];\nstatic clj_value B[%zu];\nstatic const clj_intrinsic *OP[%zu];\nstatic const clj_fusion_var *F[%zu];\nstatic clj_node S[%u];\n",
	          u->consts.n ? u->consts.n : 1, u->vars.n ? u->vars.n : 1, u->ops.n ? u->ops.n : 1, u->ops.n ? u->ops.n : 1, u->fusion.n ? u->fusion.n : 1, u->nstubs ? u->nstubs : 1);
	sb_puts(&out, "static void unit_pools(void);\n");
	sb_puts(&out, "\n");
	sb_put(&out, u->protos.s ? u->protos.s : "", u->protos.len);
	sb_puts(&out, "\n#ifdef CLJ_CLOSED\n");
	emit_direct_prelude(c, u, &out);
	emit_pimpl_prelude(c, u, &out);
	sb_puts(&out, "#endif\n\n");
	sb_put(&out, u->fns.s ? u->fns.s : "", u->fns.len);
	// The pools are filled before any form runs: reading a constant or interning a var has no effect on the program.
	sb_puts(&out, "static bool pools_filled;\n\nstatic void unit_pools(void) {\n\t(void)K; (void)V; (void)B; (void)OP; (void)F; (void)S;\n\tpools_filled = true;\n");
	for (size_t k = 0; k < u->vars.n; k++) {
		const char *slash = strchr(u->vars.keys[k], '/');
		if (slash == u->vars.keys[k] || !slash) slash = strrchr(u->vars.keys[k], '/');
		sb_printf(&out, "\tV[%zu] = clj_c_var(", k);
		sb_c_string(&out, u->vars.keys[k], (size_t)(slash - u->vars.keys[k]));
		sb_puts(&out, ", ");
		sb_c_string(&out, slash + 1, strlen(slash + 1));
		sb_puts(&out, ");\n");
	}
	for (size_t k = 0; k < u->consts.n; k++) sb_printf(&out, "\tK[%zu] = %s;\n", k, u->consts.extra[k]);
	for (size_t k = 0; k < u->ops.n; k++) {
		sb_printf(&out, "\tOP[%zu] = clj_c_intrinsic(", k);
		sb_c_string(&out, u->ops.keys[k], strlen(u->ops.keys[k]));
		sb_printf(&out, ", %s);\n\tB[%zu] = clj_intrinsic_builtin(OP[%zu]);\n", u->ops.extra[k], k, k);
	}
	for (size_t k = 0; k < u->fusion.n; k++) {
		sb_printf(&out, "\tF[%zu] = clj_c_fusion_var(", k);
		sb_c_string(&out, u->fusion.keys[k], strlen(u->fusion.keys[k]));
		sb_puts(&out, ");\n");
	}
	sb_puts(&out, "}\n\n");
	const char *iname = init_name ? init_name : "unit_init";
	sb_printf(&out, "%sclj_value %s(void) {\n\tclj_value r;\n\t(void)r;\n\tif (!pools_filled) unit_pools();\n#ifdef CLJ_CLOSED\n", init_name ? "" : "static ", iname);
	emit_direct_registrations(c, u, &out);
	emit_impl_registrations(u, &out);
	sb_puts(&out, "#endif\n");
	sb_put(&out, u->init.s ? u->init.s : "", u->init.len);
	if (c->opts.eval_result && u->ntops) sb_puts(&out, "\treturn r;\n");
	else sb_puts(&out, "\treturn CLJ_NIL;\n");
	if (u->nforms) sb_puts(&out, "fail:\n\treturn CLJ_THROWN;\n");
	sb_puts(&out, "}\n");
	if (!init_name) {
		sb_puts(&out, "\nconst clj_compiled_unit clj_compiled_unit_desc = {");
		sb_c_string(&out, u->file, strlen(u->file));
		sb_puts(&out, ", unit_init, unit_pools};\n");
	}
	if (c->opts.guard_macro) sb_puts(&out, "#else\ntypedef int cljc_unit_disabled;\n#endif\n");
	return out.s;
}

char *cljc_unit_text(cljc_compiler *c, size_t i, const char *init_name) { return unit_text(c, c->units[i], init_name); }
