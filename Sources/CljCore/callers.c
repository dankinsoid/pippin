// @ai-generated(solo)
// The reverse index of the caller join: what every live tree passes at each call of a var (NOTES.md, "Facts").
#include <stdlib.h>
#include <string.h>

#include "clj/error.h"
#include "clj/lock.h"
#include "clj/summary.h"
#include "clj/var.h"
#include "facts_internal.h"

typedef struct {
	const void *owner;
	uint32_t    nargs; // of the call; args holds the first CLJ_FN_MAX_FIXED + 1 at most
	uint32_t    nkept;
	clj_fact   *args;
} site;

typedef struct {
	clj_value    var;
	site        *sites;
	uint32_t     nsites, csites;
	const void **readers; // owners that read the var as a value, once each
	uint32_t     nreaders, creaders;
} var_entry;

typedef struct {
	const void *owner;
	clj_value  *vars; // every var this owner touched, once each
	uint32_t    n, cap;
} owner_entry;

static clj_lock        lock = CLJ_LOCK_INIT;
static var_entry     **vars;
static uint32_t        vcap, vcount;
static owner_entry   **owners;
static uint32_t        ocap, ocount;
static uint64_t        stats[5];

static void *xalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

static void *xgrow(void *p, size_t n, size_t size) {
	void *q = realloc(p, (n ? n : 1) * size);
	if (!q) clj_fatal("out of memory");
	return q;
}

static uint32_t hash_ptr(const void *p) {
	uint64_t h = (uint64_t)(uintptr_t)p >> 4;
	h *= 0xff51afd7ed558ccdull;
	return (uint32_t)(h >> 32);
}

// ---- the two tables: open addressing on a pointer key, grown at 3/4

static var_entry **var_slot(var_entry **slots, uint32_t cap, const void *key) {
	uint32_t mask = cap - 1;
	for (uint32_t i = hash_ptr(key) & mask;; i = (i + 1) & mask) {
		if (!slots[i] || clj_to_ptr(slots[i]->var) == key) return &slots[i];
	}
}

static var_entry *var_find(clj_value var) {
	if (!vcap) return NULL;
	return *var_slot(vars, vcap, clj_to_ptr(var));
}

static var_entry *var_entry_for(clj_value var) {
	if (!vcap) {
		vcap = 64;
		vars = xalloc(vcap, sizeof *vars);
	}
	var_entry **slot = var_slot(vars, vcap, clj_to_ptr(var));
	if (*slot) return *slot;
	if ((vcount + 1) * 4 > vcap * 3) {
		uint32_t    cap = vcap * 2;
		var_entry **fresh = xalloc(cap, sizeof *fresh);
		for (uint32_t i = 0; i < vcap; i++) {
			if (vars[i]) *var_slot(fresh, cap, clj_to_ptr(vars[i]->var)) = vars[i];
		}
		free(vars);
		vars = fresh;
		vcap = cap;
		slot = var_slot(vars, vcap, clj_to_ptr(var));
	}
	var_entry *e = xalloc(1, sizeof *e);
	e->var = var;
	*slot = e;
	vcount++;
	return e;
}

static owner_entry **owner_slot(owner_entry **slots, uint32_t cap, const void *key) {
	uint32_t mask = cap - 1;
	for (uint32_t i = hash_ptr(key) & mask;; i = (i + 1) & mask) {
		if (!slots[i] || slots[i]->owner == key) return &slots[i];
	}
}

static owner_entry *owner_entry_for(const void *owner) {
	if (!ocap) {
		ocap = 64;
		owners = xalloc(ocap, sizeof *owners);
	}
	owner_entry **slot = owner_slot(owners, ocap, owner);
	if (*slot) return *slot;
	if ((ocount + 1) * 4 > ocap * 3) {
		uint32_t      cap = ocap * 2;
		owner_entry **fresh = xalloc(cap, sizeof *fresh);
		for (uint32_t i = 0; i < ocap; i++) {
			if (owners[i]) *owner_slot(fresh, cap, owners[i]->owner) = owners[i];
		}
		free(owners);
		owners = fresh;
		ocap = cap;
		slot = owner_slot(owners, ocap, owner);
	}
	owner_entry *e = xalloc(1, sizeof *e);
	e->owner = owner;
	*slot = e;
	ocount++;
	return e;
}

static void owner_touch(const void *owner, clj_value var) {
	owner_entry *o = owner_entry_for(owner);
	for (uint32_t i = 0; i < o->n; i++) {
		if (o->vars[i] == var) return;
	}
	if (o->n == o->cap) {
		o->cap = o->cap ? o->cap * 2 : 4;
		o->vars = xgrow(o->vars, o->cap, sizeof(clj_value));
	}
	o->vars[o->n++] = var;
}

static void bump(clj_value var) { atomic_fetch_add_explicit(&clj_var_of(var)->callers_epoch, 1, memory_order_release); }

// What a site keeps of a fact: the kinds and the nullability, never a borrowed constant or a descriptor that may die.
static clj_fact stored(clj_fact f) {
	f.singleton = CLJ_UNBOUND;
	f.desc = NULL;
	f.unreachable = 0;
	return f;
}

static bool same_args(const site *s, uint32_t nargs, uint32_t nkept, const clj_fact *args) {
	if (s->nargs != nargs) return false;
	for (uint32_t i = 0; i < nkept; i++) {
		if (!clj_fact_eq(s->args[i], args[i])) return false;
	}
	return true;
}

void clj_callers_add_site(const void *owner, clj_value var, uint32_t nargs, const clj_fact *args) {
	if (!clj_is_var(var)) return;
	clj_fact kept[CLJ_FN_MAX_FIXED + 1];
	uint32_t nkept = nargs > CLJ_FN_MAX_FIXED + 1 ? CLJ_FN_MAX_FIXED + 1 : nargs;
	for (uint32_t i = 0; i < nkept; i++) kept[i] = stored(args[i]);
	clj_lock_lock(&lock);
	var_entry *e = var_entry_for(var);
	bool       seen = false;
	for (uint32_t i = 0; i < e->nsites && !seen; i++) seen = e->sites[i].owner == owner && same_args(&e->sites[i], nargs, nkept, kept);
	if (!seen) {
		if (e->nsites == e->csites) {
			e->csites = e->csites ? e->csites * 2 : 4;
			e->sites = xgrow(e->sites, e->csites, sizeof(site));
		}
		site *s = &e->sites[e->nsites++];
		s->owner = owner;
		s->nargs = nargs;
		s->nkept = nkept;
		s->args = xalloc(nkept, sizeof(clj_fact));
		memcpy(s->args, kept, nkept * sizeof(clj_fact));
		owner_touch(owner, var);
		bump(var);
	}
	clj_lock_unlock(&lock);
}

void clj_callers_add_value_read(const void *owner, clj_value var) {
	if (!clj_is_var(var)) return;
	clj_lock_lock(&lock);
	var_entry *e = var_entry_for(var);
	bool       seen = false;
	for (uint32_t i = 0; i < e->nreaders && !seen; i++) seen = e->readers[i] == owner;
	if (!seen) {
		if (e->nreaders == e->creaders) {
			e->creaders = e->creaders ? e->creaders * 2 : 4;
			e->readers = xgrow(e->readers, e->creaders, sizeof(const void *));
		}
		e->readers[e->nreaders++] = owner;
		owner_touch(owner, var);
		bump(var);
	}
	clj_lock_unlock(&lock);
}

void clj_callers_forget(const void *owner) {
	clj_lock_lock(&lock);
	owner_entry **slot = ocap ? owner_slot(owners, ocap, owner) : NULL;
	owner_entry  *o = slot ? *slot : NULL;
	if (o) {
		for (uint32_t k = 0; k < o->n; k++) {
			var_entry *e = var_find(o->vars[k]);
			if (!e) continue;
			bool changed = false;
			for (uint32_t i = 0; i < e->nsites;) {
				if (e->sites[i].owner != owner) {
					i++;
					continue;
				}
				free(e->sites[i].args);
				e->sites[i] = e->sites[--e->nsites];
				changed = true;
			}
			for (uint32_t i = 0; i < e->nreaders;) {
				if (e->readers[i] != owner) {
					i++;
					continue;
				}
				e->readers[i] = e->readers[--e->nreaders];
				changed = true;
			}
			if (changed) bump(o->vars[k]);
		}
		free(o->vars);
		free(o);
		// Deletion from open addressing: re-insert the probe run that follows the hole.
		*slot = NULL;
		ocount--;
		uint32_t mask = ocap - 1;
		for (uint32_t i = (uint32_t)((slot - owners) + 1) & mask; owners[i]; i = (i + 1) & mask) {
			owner_entry *moved = owners[i];
			owners[i] = NULL;
			*owner_slot(owners, ocap, moved->owner) = moved;
		}
	}
	clj_lock_unlock(&lock);
}

clj_join_reason clj_callers_join(clj_value var, const clj_node *fn, const clj_fn_arity *arity, clj_fact *out) {
	uint32_t np = arity->nparams;
	for (uint32_t i = 0; i < np; i++) out[i] = clj_fact_top();
	clj_join_reason reason = CLJ_JOIN_OK;
	clj_lock_lock(&lock);
	var_entry *e = clj_is_var(var) ? var_find(var) : NULL;
	if (clj_is_var(var) && clj_var_is_dynamic(var)) reason = CLJ_JOIN_DYNAMIC;
	else if (e && e->nreaders) reason = CLJ_JOIN_FIRST_CLASS;
	else {
		bool any = false;
		for (uint32_t i = 0; i < np; i++) out[i] = clj_fact_bottom();
		for (uint32_t k = 0; e && k < e->nsites; k++) {
			const site *s = &e->sites[k];
			if (clj_facts_arity_for(fn, s->nargs) != arity) continue;
			any = true;
			for (uint32_t i = 0; i < np && i < s->nkept; i++) out[i] = clj_fact_join(out[i], s->args[i]);
		}
		if (!any) reason = CLJ_JOIN_NO_SITES;
		else {
			for (uint32_t i = 0; i < np; i++) {
				if (clj_fact_is_top(out[i])) reason = CLJ_JOIN_TOP_ARG;
			}
		}
	}
	if (reason != CLJ_JOIN_OK && reason != CLJ_JOIN_TOP_ARG) {
		for (uint32_t i = 0; i < np; i++) out[i] = clj_fact_top();
	}
	stats[reason]++;
	clj_lock_unlock(&lock);
	return reason;
}

uint32_t clj_callers_nsites(clj_value var) {
	clj_lock_lock(&lock);
	var_entry *e = clj_is_var(var) ? var_find(var) : NULL;
	uint32_t   n = e ? e->nsites : 0;
	clj_lock_unlock(&lock);
	return n;
}

uint32_t clj_callers_nvalue_reads(clj_value var) {
	clj_lock_lock(&lock);
	var_entry *e = clj_is_var(var) ? var_find(var) : NULL;
	uint32_t   n = e ? e->nreaders : 0;
	clj_lock_unlock(&lock);
	return n;
}

void clj_callers_stats(uint64_t counts[5]) {
	clj_lock_lock(&lock);
	memcpy(counts, stats, sizeof stats);
	clj_lock_unlock(&lock);
}
