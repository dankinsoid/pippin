// @ai-generated(solo)
// Specialization of an exec by the facts of its tree under the dev store: the caller join's first consumer (NOTES.md).
#include <stdlib.h>
#include <string.h>

#include "clj/error.h"
#include "clj/fn.h"
#include "clj/lock.h"
#include "clj/summary.h"
#include "clj/var.h"
#include "specialize_internal.h"

// Re-derivations one trigger (one new exec) may cause: bounds the incremental fixpoint over the call graph.
#define MAX_PER_TRIGGER 64
#define MAX_PER_EXEC 3

// What the index answered for the join, not what the parameters entered at: a self-site's contribution is derived
// from this, so this is what moving invalidates.
typedef struct {
	clj_value           var;
	const clj_node     *fn;
	const clj_fn_arity *arity;
	uint32_t            nparams;
	clj_fact            callers[CLJ_FN_MAX_FIXED + 1];
} join_dep;

struct clj_derivation {
	clj_value   *dep_vars;
	uint32_t    *dep_epochs;
	uint32_t     ndeps;
	join_dep    *joins;
	uint32_t     njoins;
	uint32_t    *spec_ids;
	clj_eval_fn *spec_fns;
	uint32_t     nspec;
	uint32_t     rounds;     // derivations so far
	uint32_t     trigger;    // the trigger of the last one
	uint32_t     in_trigger; // derivations within that trigger, against MAX_PER_EXEC
};

static clj_lock        lock = CLJ_LOCK_INIT;
static clj_summaries  *store;
static bool            enabled = true;
static uint32_t        trigger_serial;
static clj_exec      **work; // execs whose caller join moved, retained until derived
static uint32_t        nwork, cwork;

static void *xalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

void clj_specialize_enable(bool on) { enabled = on; }
bool clj_specialize_enabled(void) { return enabled; }

static clj_summaries *store_locked(void) {
	if (!store) {
		store = clj_summaries_new();
		clj_summaries_use_callers(store, true);
	}
	return store;
}

clj_summaries *clj_specialize_store(void) {
	clj_lock_lock(&lock);
	clj_summaries *s = store_locked();
	clj_lock_unlock(&lock);
	return s;
}

static void free_derivation(clj_derivation *d) {
	if (!d) return;
	free(d->dep_vars);
	free(d->dep_epochs);
	free(d->joins);
	free(d->spec_ids);
	free(d->spec_fns);
	free(d);
}

// ---- the dependents index: var -> the execs whose derivation read its root, what a root rebind pushes to

typedef struct {
	clj_value  var;
	clj_exec **execs;
	uint32_t   n, cap;
} dependents;

static dependents **dtable; // open addressing on the var pointer, grown at 3/4; a var is immortal, so no deletion
static uint32_t     dcap, dcount;

static dependents **dslot(dependents **slots, uint32_t cap, clj_value var) {
	uint64_t h = (uint64_t)var >> 4;
	h *= 0xff51afd7ed558ccdull;
	uint32_t mask = cap - 1;
	for (uint32_t i = (uint32_t)(h >> 32) & mask;; i = (i + 1) & mask) {
		if (!slots[i] || slots[i]->var == var) return &slots[i];
	}
}

static dependents *dependents_find(clj_value var) { return dcap ? *dslot(dtable, dcap, var) : NULL; }

static dependents *dependents_for(clj_value var) {
	if (!dcap) {
		dcap = 64;
		dtable = xalloc(dcap, sizeof *dtable);
	}
	dependents **slot = dslot(dtable, dcap, var);
	if (*slot) return *slot;
	if ((dcount + 1) * 4 > dcap * 3) {
		uint32_t     cap = dcap * 2;
		dependents **fresh = xalloc(cap, sizeof *fresh);
		for (uint32_t i = 0; i < dcap; i++) {
			if (dtable[i]) *dslot(fresh, cap, dtable[i]->var) = dtable[i];
		}
		free(dtable);
		dtable = fresh;
		dcap = cap;
		slot = dslot(dtable, dcap, var);
	}
	dependents *d = xalloc(1, sizeof *d);
	d->var = var;
	*slot = d;
	dcount++;
	return d;
}

static void dependents_add(clj_value var, clj_exec *e) {
	dependents *d = dependents_for(var);
	if (d->n == d->cap) {
		d->cap = d->cap ? d->cap * 2 : 4;
		d->execs = realloc(d->execs, d->cap * sizeof *d->execs);
		if (!d->execs) clj_fatal("out of memory");
	}
	d->execs[d->n++] = e;
}

static void dependents_remove(clj_value var, clj_exec *e) {
	dependents *d = dependents_find(var);
	for (uint32_t i = 0; d && i < d->n; i++) {
		if (d->execs[i] == e) {
			d->execs[i] = d->execs[--d->n];
			return;
		}
	}
}

static void forget_locked(clj_exec *e) {
	clj_derivation *d = e->derived;
	if (!d) return;
	for (uint32_t i = 0; i < d->ndeps; i++) dependents_remove(d->dep_vars[i], e);
	clj_callers_forget(e);
	free_derivation(d);
	e->derived = NULL;
}

void clj_exec_forget(clj_exec *e) {
	if (!e->derived) return;
	clj_lock_lock(&lock);
	forget_locked(e);
	clj_lock_unlock(&lock);
}

// ---- reading the table into the exec

typedef struct {
	uint32_t        id;
	const clj_node *found;
} find_ctx;

static void find_node(const clj_node *n, void *ctx) {
	find_ctx *c = ctx;
	if (c->found || c->id < n->id || c->id >= n->id + n->nnodes) return;
	if (n->id == c->id) c->found = n;
	else clj_node_children(n, find_node, c);
}

static const clj_node *node_by_id(const clj_node *root, uint32_t id) {
	find_ctx c = {id, NULL};
	find_node(root, &c);
	return c.found;
}

typedef enum { ARG_OTHER, ARG_INT64, ARG_DOUBLE } arg_kind;

// A fixnum or a boxed long, the two representations of a long; a double; anything else.
static arg_kind arg_kind_of(const clj_fact *a) {
	if (!a || a->types == CLJ_T_BOTTOM) return ARG_OTHER;
	if ((a->types & ~(uint32_t)(CLJ_T_FIXNUM | CLJ_T_LONG)) == 0) return ARG_INT64;
	if (a->types == CLJ_T_DOUBLE) return ARG_DOUBLE;
	return ARG_OTHER;
}

// The entry the argument kinds select, NULL when they select none.
static clj_eval_fn entry_for(const clj_node *n, const clj_facts *f) {
	uint32_t nargs = n->u.intrinsic.n;
	arg_kind k[2];
	if (nargs < 1 || nargs > 2) return NULL;
	for (uint32_t i = 0; i < nargs; i++) k[i] = arg_kind_of(clj_facts_node(f, n->u.intrinsic.args[i]->id));
	if (nargs == 1) k[1] = k[0];
	clj_spec_kind kind;
	if (k[0] == ARG_INT64 && k[1] == ARG_INT64) kind = CLJ_SPEC_FIXNUM;
	else if (k[0] == ARG_DOUBLE && k[1] == ARG_DOUBLE) kind = CLJ_SPEC_DOUBLE;
	else if (k[0] == ARG_INT64 && k[1] == ARG_DOUBLE) kind = CLJ_SPEC_FIXNUM_DOUBLE;
	else if (k[0] == ARG_DOUBLE && k[1] == ARG_INT64) kind = CLJ_SPEC_DOUBLE_FIXNUM;
	else return NULL;
	return clj_eval_specialized_entry(n->u.intrinsic.op, kind);
}

typedef struct {
	clj_exec        *e;
	const clj_facts *f;
	clj_derivation  *d;
	uint32_t         cap;
} install_ctx;

// Decided afresh for every arithmetic node: the generic entry is how a re-derivation takes a specialization back.
static void install(const clj_node *n, void *ctx) {
	install_ctx *c = ctx;
	if (n->kind == CLJ_NODE_INTRINSIC) {
		// a rebound operator fails the entry's root guard on every call: the generic entry until the var's next bind
		clj_eval_fn fast = clj_var_root(n->u.intrinsic.var) == clj_intrinsic_builtin(n->u.intrinsic.op) ? entry_for(n, c->f) : NULL;
		if (fast) {
			if (c->d->nspec == c->cap) {
				c->cap = c->cap ? c->cap * 2 : 8;
				c->d->spec_ids = realloc(c->d->spec_ids, c->cap * sizeof *c->d->spec_ids);
				c->d->spec_fns = realloc(c->d->spec_fns, c->cap * sizeof *c->d->spec_fns);
				if (!c->d->spec_ids || !c->d->spec_fns) clj_fatal("out of memory");
			}
			c->d->spec_ids[c->d->nspec] = n->id;
			c->d->spec_fns[c->d->nspec++] = fast;
			c->e->nodes[n->id].eval = fast;
		} else {
			c->e->nodes[n->id].eval = clj_eval_site_entry(c->e, n);
		}
	}
	clj_node_children(n, install, ctx);
}

static void push_work(clj_exec *e) {
	for (uint32_t i = 0; i < nwork; i++) {
		if (work[i] == e) return;
	}
	if (nwork == cwork) {
		cwork = cwork ? cwork * 2 : 8;
		work = realloc(work, cwork * sizeof *work);
		if (!work) clj_fatal("out of memory");
	}
	clj_retain(clj_from_ptr(e));
	work[nwork++] = e;
}

static void enqueue_if_stale_in(clj_exec *ce, clj_value var) {
	if (!ce->derived) return;
	for (uint32_t i = 0; i < ce->derived->njoins; i++) {
		const join_dep *j = &ce->derived->joins[i];
		if (j->var != var) continue;
		clj_fact now[CLJ_FN_MAX_FIXED + 1];
		clj_callers_join(var, j->fn, j->arity, now);
		for (uint32_t k = 0; k < j->nparams; k++) {
			if (!clj_fact_eq(now[k], j->callers[k])) {
				push_work(ce);
				return;
			}
		}
	}
}

// Queues the exec whose derivation took a join of the var that no longer holds: the var's root closure's, and
// the recording exec's own when it defines the var itself (the def has not run yet, or calls it recursively).
static void enqueue_if_stale(clj_value var, clj_exec *self) {
	enqueue_if_stale_in(self, var);
	clj_value root = clj_var_root(var);
	if (root == CLJ_UNBOUND || !clj_is_fn(root) || clj_fn_of(root)->kind != CLJ_FN_CLOSURE) return;
	clj_exec *ce = clj_exec_of(clj_fn_of(root)->code);
	if (ce != self) enqueue_if_stale_in(ce, var);
}

static void derive(clj_exec *e, uint32_t trigger) {
	clj_derivation *old = e->derived;
	uint32_t        rounds = old ? old->rounds : 0, in_trigger = old && old->trigger == trigger ? old->in_trigger : 0;
	if (in_trigger >= MAX_PER_EXEC) return;
	clj_facts      *f = clj_facts_of_with(e->root, store_locked());
	clj_derivation *d = xalloc(1, sizeof *d);
	d->rounds = rounds + 1;
	d->trigger = trigger;
	d->in_trigger = in_trigger + 1;
	d->ndeps = clj_facts_ndeps(f);
	d->dep_vars = xalloc(d->ndeps, sizeof(clj_value));
	d->dep_epochs = xalloc(d->ndeps, sizeof(uint32_t));
	for (uint32_t i = 0; i < d->ndeps; i++) d->dep_vars[i] = clj_facts_dep(f, i, &d->dep_epochs[i]);
	d->njoins = clj_facts_njoins(f);
	d->joins = xalloc(d->njoins, sizeof(join_dep));
	for (uint32_t i = 0; i < d->njoins; i++) {
		const clj_facts_join *j = clj_facts_join_at(f, i);
		join_dep             *dep = &d->joins[i];
		dep->var = j->var;
		dep->fn = node_by_id(e->root, j->fn);
		dep->arity = j->arity > CLJ_FN_MAX_FIXED ? dep->fn->u.fn.variadic : dep->fn->u.fn.fixed[j->arity];
		dep->nparams = j->nparams;
		memcpy(dep->callers, j->callers, sizeof dep->callers);
	}
	install_ctx ic = {e, f, d, 0};
	install(e->root, &ic);
	// the old sites go before the new ones so that a site the re-derivation narrowed is replaced, not joined
	forget_locked(e);
	e->derived = d;
	// a dynamic var's root is no fact to rest on (a thread binding is invisible to the epoch): *ns* moves per form
	for (uint32_t i = 0; i < d->ndeps; i++) {
		if (!clj_var_is_dynamic(d->dep_vars[i])) dependents_add(d->dep_vars[i], e);
	}
	clj_value touched[64];
	uint32_t  ntouched = 0;
	// only what a fn body does is a fact about the program: the rest of a form runs once, as a host call does
	for (uint32_t i = 0; i < clj_facts_nsites(f); i++) {
		uint32_t        node, nargs;
		clj_value       var;
		const clj_fact *args;
		bool            in_fn;
		clj_facts_site(f, i, &node, &var, &nargs, &args, &in_fn);
		if (!in_fn) continue;
		clj_callers_add_site(e, var, nargs, args);
		bool seen = false;
		for (uint32_t k = 0; k < ntouched && !seen; k++) seen = touched[k] == var;
		if (!seen && ntouched < 64) touched[ntouched++] = var;
		else if (!seen) enqueue_if_stale(var, e);
	}
	for (uint32_t i = 0; i < clj_facts_nvalue_reads(f); i++) {
		bool      in_fn;
		clj_value var = clj_facts_value_read(f, i, &in_fn);
		if (!in_fn) continue;
		clj_callers_add_value_read(e, var);
		bool seen = false;
		for (uint32_t k = 0; k < ntouched && !seen; k++) seen = touched[k] == var;
		if (!seen && ntouched < 64) touched[ntouched++] = var;
		else if (!seen) enqueue_if_stale(var, e);
	}
	clj_facts_free(f);
	for (uint32_t k = 0; k < ntouched; k++) enqueue_if_stale(touched[k], e);
}

// The queue's references are dropped after the unlock: an exec dying there forgets itself under the lock.
static void drain_and_unlock(uint32_t trigger, uint32_t done) {
	clj_exec **spent = NULL;
	uint32_t   nspent = 0, cspent = 0;
	while (nwork) {
		clj_exec *e = work[--nwork];
		if (done < MAX_PER_TRIGGER) {
			derive(e, trigger);
			done++;
		}
		if (nspent == cspent) {
			cspent = cspent ? cspent * 2 : 8;
			spent = realloc(spent, cspent * sizeof *spent);
			if (!spent) clj_fatal("out of memory");
		}
		spent[nspent++] = e;
	}
	clj_lock_unlock(&lock);
	for (uint32_t i = 0; i < nspent; i++) clj_release(clj_from_ptr(spent[i]));
	free(spent);
}

void clj_exec_derive(clj_value exec) {
	if (!enabled) return;
	clj_lock_lock(&lock);
	uint32_t trigger = ++trigger_serial;
	derive(clj_exec_of(exec), trigger);
	drain_and_unlock(trigger, 1);
}

// A rebound operator's entries would fail their guard on every call; a redefined callee has a new summary.
void clj_exec_root_rebound(clj_value var) {
	if (!enabled) return;
	clj_lock_lock(&lock);
	dependents *d = dependents_find(var);
	if (!d || !d->n) {
		clj_lock_unlock(&lock);
		return;
	}
	for (uint32_t i = 0; i < d->n; i++) push_work(d->execs[i]);
	drain_and_unlock(++trigger_serial, 0);
}

void clj_exec_reapply(clj_value exec) {
	clj_exec       *e = clj_exec_of(exec);
	clj_derivation *d = e->derived;
	if (!d) return;
	for (uint32_t i = 0; i < d->nspec; i++) e->nodes[d->spec_ids[i]].eval = d->spec_fns[i];
}

bool clj_exec_derivation_valid(clj_value exec) {
	const clj_derivation *d = clj_exec_of(exec)->derived;
	if (!d) return false;
	for (uint32_t i = 0; i < d->ndeps; i++) {
		if (clj_var_epoch(d->dep_vars[i]) != d->dep_epochs[i]) return false;
	}
	for (uint32_t i = 0; i < d->njoins; i++) {
		clj_fact now[CLJ_FN_MAX_FIXED + 1];
		clj_callers_join(d->joins[i].var, d->joins[i].fn, d->joins[i].arity, now);
		for (uint32_t k = 0; k < d->joins[i].nparams; k++) {
			if (!clj_fact_eq(now[k], d->joins[i].callers[k])) return false;
		}
	}
	return true;
}

uint32_t clj_exec_derivations(clj_value exec) {
	const clj_derivation *d = clj_exec_of(exec)->derived;
	return d ? d->rounds : 0;
}

bool clj_debug_exec_node_specialized(clj_value exec, uint32_t id) {
	const clj_derivation *d = clj_exec_of(exec)->derived;
	for (uint32_t i = 0; d && i < d->nspec; i++) {
		if (d->spec_ids[i] == id) return true;
	}
	return false;
}

uint32_t clj_debug_exec_specialized(clj_value exec) {
	const clj_derivation *d = clj_exec_of(exec)->derived;
	return d ? d->nspec : 0;
}

typedef struct {
	const char     *name;
	const clj_node *found;
} find_op_ctx;

static void find_op(const clj_node *n, void *ctx) {
	find_op_ctx *c = ctx;
	if (c->found) return;
	if (n->kind == CLJ_NODE_INTRINSIC && strcmp(n->u.intrinsic.op->name + strlen("clojure.core/"), c->name) == 0) c->found = n;
	else clj_node_children(n, find_op, c);
}

uint32_t clj_debug_exec_intrinsic_id(clj_value exec, const char *name) {
	find_op_ctx c = {name, NULL};
	find_op(clj_exec_of(exec)->root, &c);
	return c.found ? c.found->id : UINT32_MAX;
}
