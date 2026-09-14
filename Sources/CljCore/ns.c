// @ai-generated(guided)
#include <pthread.h>

#include "clj/map.h"
#include "clj/ns.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static clj_value       registry; // symbol → ns; nil until the first namespace
static clj_value       core_ns, user_ns;

static _Thread_local clj_value current_ns;

static void ns_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_ns *n = self;
	visit(n->name, ctx);
	visit(n->mappings, ctx);
	visit(n->refers, ctx);
}

const clj_type clj_ns_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "namespace",
	.each_child = ns_each_child,
};

// Replaces a map slot of a shared owner: the new map must be shared before it becomes reachable.
static void store_map(clj_value *slot, clj_value map) {
	clj_share(map);
	*slot = map;
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
	ns = clj_from_ptr(n);
	if (clj_is_nil(registry)) registry = clj_map_empty();
	store_map(&registry, clj_map_assoc(registry, name, ns));
	return ns;
}

clj_value clj_ns_find(clj_value name) {
	CLJ_ASSERT(clj_is_symbol(name), "namespace name must be a symbol");
	pthread_mutex_lock(&lock);
	clj_value ns = find_locked(name);
	pthread_mutex_unlock(&lock);
	return ns;
}

clj_value clj_ns_find_or_create(clj_value name) {
	CLJ_ASSERT(clj_is_symbol(name) && clj_is_nil(clj_symbol_ns(name)), "namespace name must be an unqualified symbol");
	pthread_mutex_lock(&lock);
	clj_value ns = find_or_create_locked(name);
	pthread_mutex_unlock(&lock);
	return ns;
}

clj_value clj_ns_intern(clj_value ns, clj_value sym) {
	CLJ_ASSERT(clj_is_symbol(sym) && clj_is_nil(clj_symbol_ns(sym)), "interned symbol must be unqualified");
	clj_ns *n = clj_ns_of(ns);
	pthread_mutex_lock(&lock);
	clj_value var = clj_map_get(n->mappings, sym, CLJ_NIL);
	if (clj_is_nil(var)) {
		var = clj_var_new(n->name, sym);
		store_map(&n->mappings, clj_map_assoc(n->mappings, sym, var));
	}
	pthread_mutex_unlock(&lock);
	return var;
}

void clj_ns_refer(clj_value ns, clj_value sym, clj_value var) {
	clj_ns *n = clj_ns_of(ns);
	pthread_mutex_lock(&lock);
	store_map(&n->refers, clj_map_assoc(n->refers, sym, var));
	pthread_mutex_unlock(&lock);
}

clj_value clj_ns_resolve(clj_value ns, clj_value sym) {
	clj_value var = CLJ_NIL;
	pthread_mutex_lock(&lock);
	if (clj_is_nil(clj_symbol_ns(sym))) {
		clj_ns *n = clj_ns_of(ns);
		var = clj_map_get(n->mappings, sym, CLJ_NIL);
		if (clj_is_nil(var)) var = clj_map_get(n->refers, sym, CLJ_NIL);
		if (clj_is_nil(var) && !clj_is_nil(core_ns) && core_ns != ns) {
			var = clj_map_get(clj_ns_of(core_ns)->mappings, sym, CLJ_NIL);
			if (!clj_is_nil(var) && clj_var_is_private(var)) var = CLJ_NIL;
		}
	} else {
		clj_value ns_name = clj_symbol_new(CLJ_NIL, clj_symbol_ns(sym));
		clj_value target = find_locked(ns_name);
		clj_release(ns_name);
		if (!clj_is_nil(target)) {
			clj_value name = clj_symbol_new(CLJ_NIL, clj_symbol_name(sym));
			var = clj_map_get(clj_ns_of(target)->mappings, name, CLJ_NIL);
			clj_release(name);
		}
	}
	pthread_mutex_unlock(&lock);
	return var;
}

static clj_value named(clj_value *slot, const char *name) {
	pthread_mutex_lock(&lock);
	if (clj_is_nil(*slot)) {
		clj_value sym = clj_symbol_from_cstr(name);
		*slot = find_or_create_locked(sym);
		clj_release(sym);
	}
	clj_value ns = *slot;
	pthread_mutex_unlock(&lock);
	return ns;
}

clj_value clj_ns_core(void) { return named(&core_ns, "clojure.core"); }
clj_value clj_ns_user(void) { return named(&user_ns, "user"); }

clj_value clj_ns_current(void) { return clj_is_nil(current_ns) ? clj_ns_user() : current_ns; }

void clj_ns_set_current(clj_value ns) {
	CLJ_ASSERT(clj_is_ns(ns), "current namespace must be a namespace");
	current_ns = ns;
}
