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

#endif
