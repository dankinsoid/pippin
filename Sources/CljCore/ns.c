// @ai-generated(guided)
#include <pthread.h>

#include "clj/box.h"
#include "clj/lock.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/set.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"

static clj_lock  lock = CLJ_LOCK_INIT;
static clj_value registry; // symbol → ns; nil until the first namespace
static clj_value all;      // vector of namespaces in creation order
static clj_value core_ns, user_ns;

static void ns_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_ns *n = self;
	visit(n->name, ctx);
	visit(n->mappings, ctx);
	visit(n->refers, ctx);
	visit(n->aliases, ctx);
	visit(n->excludes, ctx);
	visit(n->meta, ctx);
}

static clj_value ns_meta(clj_value self) { return clj_retain(clj_ns_of(self)->meta); }

// IMeta without IObj, like a var: the meta changes through alter-meta!/reset-meta!.
const clj_type clj_ns_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "namespace",
	.core_bits = CLJ_CORE_META,
	.each_child = ns_each_child,
	.meta = ns_meta,
};

// Replaces a slot of a shared owner: the new value must be shared before it becomes reachable.
static void store(clj_value *slot, clj_value v) {
	clj_share(v);
	*slot = v;
}

static clj_value find_locked(clj_value name) {
	return clj_is_nil(registry) ? CLJ_NIL : clj_map_get(registry, name, CLJ_NIL);
}

static clj_value find_or_create_locked(clj_value name) {
	clj_value ns = find_locked(name);
	if (!clj_is_nil(ns)) return ns;
	clj_ns *n = clj_alloc(&clj_ns_type, sizeof *n);
	n->h.flags |= CLJ_FLAG_IMMORTAL | CLJ_FLAG_SHARED;
	clj_share(name);
	n->name = clj_retain(name);
	n->mappings = clj_map_empty();
	n->refers = clj_map_empty();
	n->aliases = clj_map_empty();
	n->meta = CLJ_NIL;
	ns = clj_from_ptr(n);
	if (clj_is_nil(registry)) registry = clj_map_empty();
	store(&registry, clj_map_assoc(registry, name, ns));
	if (clj_is_nil(all)) all = clj_vector_empty();
	store(&all, clj_vector_conj(all, ns));
	return ns;
}

clj_value clj_ns_find(clj_value name) {
	CLJ_ASSERT(clj_is_symbol(name), "namespace name must be a symbol");
	clj_lock_lock(&lock);
	clj_value ns = find_locked(name);
	clj_lock_unlock(&lock);
	return ns;
}

clj_value clj_ns_find_or_create(clj_value name) {
	CLJ_ASSERT(clj_is_symbol(name) && clj_is_nil(clj_symbol_ns(name)), "namespace name must be an unqualified symbol");
	clj_lock_lock(&lock);
	clj_value ns = find_or_create_locked(name);
	clj_lock_unlock(&lock);
	return ns;
}

clj_value clj_ns_all(void) {
	clj_lock_lock(&lock);
	clj_value v = clj_retain(clj_is_nil(all) ? clj_vector_empty() : all);
	clj_lock_unlock(&lock);
	return v;
}

clj_value clj_ns_intern(clj_value ns, clj_value sym) {
	CLJ_ASSERT(clj_is_symbol(sym) && clj_is_nil(clj_symbol_ns(sym)), "interned symbol must be unqualified");
	clj_ns *n = clj_ns_of(ns);
	clj_lock_lock(&lock);
	clj_value var = clj_map_get(n->mappings, sym, CLJ_NIL);
	if (clj_is_nil(var)) {
		var = clj_var_new(n->name, sym);
		store(&n->mappings, clj_map_assoc(n->mappings, sym, var));
	}
	clj_lock_unlock(&lock);
	return var;
}

void clj_ns_refer(clj_value ns, clj_value sym, clj_value var) {
	clj_ns *n = clj_ns_of(ns);
	clj_lock_lock(&lock);
	store(&n->refers, clj_map_assoc(n->refers, sym, var));
	clj_lock_unlock(&lock);
}

void clj_ns_unmap(clj_value ns, clj_value sym) {
	clj_ns *n = clj_ns_of(ns);
	clj_lock_lock(&lock);
	store(&n->mappings, clj_map_dissoc(n->mappings, sym));
	store(&n->refers, clj_map_dissoc(n->refers, sym));
	clj_lock_unlock(&lock);
}

void clj_ns_alias(clj_value ns, clj_value alias, clj_value target) {
	CLJ_ASSERT(clj_is_ns(target), "alias target must be a namespace");
	clj_ns *n = clj_ns_of(ns);
	clj_lock_lock(&lock);
	store(&n->aliases, clj_map_assoc(n->aliases, alias, target));
	clj_lock_unlock(&lock);
}

clj_value clj_ns_mappings(clj_value ns) { return clj_ns_of(ns)->mappings; }
clj_value clj_ns_refers(clj_value ns) { return clj_ns_of(ns)->refers; }
clj_value clj_ns_aliases(clj_value ns) { return clj_ns_of(ns)->aliases; }

void clj_ns_set_excludes(clj_value ns, clj_value excludes) {
	CLJ_ASSERT(clj_is_nil(excludes) || clj_is_set(excludes), "excludes must be a set or nil");
	clj_ns *n = clj_ns_of(ns);
	clj_lock_lock(&lock);
	clj_value old = n->excludes;
	store(&n->excludes, clj_retain(excludes));
	clj_release(old);
	clj_lock_unlock(&lock);
}

static clj_value resolve_ns_locked(clj_ns *n, clj_value name) {
	clj_value target = clj_map_get(n->aliases, name, CLJ_NIL);
	return clj_is_nil(target) ? find_locked(name) : target;
}

clj_value clj_ns_resolve(clj_value ns, clj_value sym) {
	clj_value var = CLJ_NIL;
	clj_ns   *n = clj_ns_of(ns);
	clj_lock_lock(&lock);
	if (clj_is_nil(clj_symbol_ns(sym))) {
		var = clj_map_get(n->mappings, sym, CLJ_NIL);
		if (clj_is_nil(var)) var = clj_map_get(n->refers, sym, CLJ_NIL);
		if (clj_is_nil(var) && !clj_is_nil(core_ns) && core_ns != ns && !(!clj_is_nil(n->excludes) && clj_set_contains(n->excludes, sym))) {
			var = clj_map_get(clj_ns_of(core_ns)->mappings, sym, CLJ_NIL);
			if (!clj_is_nil(var) && clj_var_is_private(var)) var = CLJ_NIL;
		}
	} else {
		clj_value ns_name = clj_symbol_new(CLJ_NIL, clj_symbol_ns(sym));
		clj_value target = resolve_ns_locked(n, ns_name);
		clj_release(ns_name);
		if (!clj_is_nil(target)) {
			clj_value name = clj_symbol_new(CLJ_NIL, clj_symbol_name(sym));
			var = clj_map_get(clj_ns_of(target)->mappings, name, CLJ_NIL);
			clj_release(name);
		}
	}
	clj_lock_unlock(&lock);
	return var;
}

clj_value clj_ns_resolve_ns(clj_value ns, clj_value name) {
	clj_lock_lock(&lock);
	clj_value target = resolve_ns_locked(clj_ns_of(ns), name);
	clj_lock_unlock(&lock);
	return target;
}

static clj_value named(clj_value *slot, const char *name) {
	clj_lock_lock(&lock);
	if (clj_is_nil(*slot)) {
		clj_value sym = clj_symbol_from_cstr(name);
		*slot = find_or_create_locked(sym);
		clj_release(sym);
	}
	clj_value ns = *slot;
	clj_lock_unlock(&lock);
	return ns;
}

clj_value clj_ns_core(void) { return named(&core_ns, "clojure.core"); }
clj_value clj_ns_user(void) { return named(&user_ns, "user"); }

static pthread_once_t ns_var_once = PTHREAD_ONCE_INIT;
static clj_value      ns_var;

static void make_ns_var(void) {
	clj_value sym = clj_symbol_from_cstr("*ns*");
	ns_var = clj_ns_intern(clj_ns_core(), sym);
	clj_release(sym);
	clj_var_set_dynamic(ns_var, true);
	clj_var_bind_root(ns_var, clj_ns_user());
}

clj_value clj_ns_var(void) {
	pthread_once(&ns_var_once, make_ns_var);
	return ns_var;
}

clj_value clj_ns_current(void) {
	clj_value var = clj_ns_var();
	clj_value box = clj_var_thread_binding(var);
	clj_value ns = clj_is_nil(box) ? clj_var_root(var) : clj_volatile_value(box);
	return clj_is_ns(ns) ? ns : clj_ns_user();
}

void clj_ns_set_current(clj_value ns) {
	CLJ_ASSERT(clj_is_ns(ns), "current namespace must be a namespace");
	clj_value var = clj_ns_var();
	clj_value box = clj_var_thread_binding(var);
	if (clj_is_nil(box)) clj_var_bind_root(var, ns);
	else clj_release(clj_volatile_reset(box, ns));
}

clj_value clj_ns_meta(clj_value ns) { return clj_ns_of(ns)->meta; }

// Retained like a var's meta, and the old one released: a borrowed clj_ns_meta has the var's caveat against a writer.
void clj_ns_set_meta(clj_value ns, clj_value m) {
	clj_share(m);
	clj_lock_lock(&lock);
	clj_value old = clj_ns_of(ns)->meta;
	clj_ns_of(ns)->meta = clj_retain(m);
	clj_lock_unlock(&lock);
	clj_release(old);
}
