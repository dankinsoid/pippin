// @ai-generated(solo)
#ifndef CLJ_HOSTMODULE_H
#define CLJ_HOSTMODULE_H

#include "object.h"

// Binds a Swift module's stubs in the namespace of its name (design §5 «Объявленная граница»); nil or CLJ_THROWN.
typedef clj_value (*clj_host_module_loader)(clj_value module);
// From clj_host_boot; a C-only host installs none and `require-swift` refuses there.
void clj_host_module_install(clj_host_module_loader load);
// require-swift*; clj_builtins_install calls it.
void clj_host_module_builtins_install(void);

// A module whose stubs are linked into the binary (design §5, the production path): its `@_cdecl` register entry.
typedef void (*clj_host_module_entry)(void);
// A closed build's libs.c calls it at boot with a literal: the name is kept, not copied. The loader calls the entry
// in place of a generator.
void clj_host_module_link(const char *module, clj_host_module_entry entry);
// NULL when nothing linked the module in.
clj_host_module_entry clj_host_module_linked(const char *module);

#endif
