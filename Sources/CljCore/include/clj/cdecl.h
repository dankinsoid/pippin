// @ai-generated(solo)
#ifndef CLJ_CDECL_H
#define CLJ_CDECL_H

#include "object.h"

// c-global* reads an exported C global by its kind, c-fn* binds an exported C function by its encodings
// (design §5 «C — уровень 0»).
// clj_builtins_install calls it.
void clj_cdecl_builtins_install(void);

#endif
