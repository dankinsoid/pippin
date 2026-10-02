// @ai-generated(solo)
#include "clj/core.h"
#include "clj/hostmodule.h"

static clj_host_module_loader loader;

void clj_host_module_install(clj_host_module_loader load) { loader = load; }

static clj_value b_require_swift_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_symbol(args[0]) || !clj_is_nil(clj_symbol_ns(args[0]))) {
		return clj_throw_msg("require-swift expects an unqualified module symbol, got: %s", clj_type_name(args[0]));
	}
	if (!loader) {
		return clj_throw_msg("require-swift %s: this host has no Swift bridge, a C-only host cannot load level-2 stubs (design §5)",
		                     clj_string_bytes(clj_symbol_name(args[0])));
	}
	return loader(args[0]);
}

void clj_host_module_builtins_install(void) { clj_builtin_bind_extension("require-swift*", b_require_swift_star, 1, 1); }
