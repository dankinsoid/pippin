// @ai-generated(solo)
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/hostmodule.h"

static clj_host_module_loader loader;

typedef struct linked {
	const char           *module;
	clj_host_module_entry entry;
	struct linked        *next;
} linked;

// Prepended, never removed: a reader walks whatever head it loaded.
static _Atomic(linked *) links;

void clj_host_module_install(clj_host_module_loader load) { loader = load; }

CLJ_UNIT_ENTRY void clj_host_module_link(const char *module, clj_host_module_entry entry) {
	linked *l = malloc(sizeof *l);
	if (!l) clj_fatal("out of memory");
	*l = (linked){module, entry, atomic_load_explicit(&links, memory_order_relaxed)};
	while (!atomic_compare_exchange_weak_explicit(&links, &l->next, l, memory_order_release, memory_order_relaxed)) {}
}

clj_host_module_entry clj_host_module_linked(const char *module) {
	for (linked *l = atomic_load_explicit(&links, memory_order_acquire); l; l = l->next) {
		if (strcmp(l->module, module) == 0) return l->entry;
	}
	return NULL;
}

static clj_value b_require_swift_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_symbol(args[0]) || !clj_is_nil(clj_symbol_ns(args[0]))) {
		return clj_throw_msg("require-swift expects an unqualified module symbol, got: %s", clj_type_name(args[0]));
	}
	if (!loader) {
		return clj_throw_msg("require-swift %s: this host has no Swift bridge, a C-only host cannot load level-2 stubs (design §5)",
		                     CLJ_STR(clj_symbol_name(args[0])).bytes);
	}
	return loader(args[0]);
}

void clj_host_module_builtins_install(void) { clj_builtin_bind_extension("require-swift*", b_require_swift_star, 1, 1); }
