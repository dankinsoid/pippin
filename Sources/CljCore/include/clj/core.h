#ifndef CLJ_CORE_H
#define CLJ_CORE_H

// Umbrella header: re-exports the public core API.
#include "cons.h"   // IWYU pragma: export
#include "object.h" // IWYU pragma: export
#include "value.h"  // IWYU pragma: export

// Static string; for heap objects the type descriptor's name.
const char *clj_type_name(clj_value v);

#endif
