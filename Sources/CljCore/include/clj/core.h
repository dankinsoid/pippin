#ifndef CLJ_CORE_H
#define CLJ_CORE_H

// Umbrella header: re-exports the public core API.
#include "cons.h"    // IWYU pragma: export
#include "keyword.h" // IWYU pragma: export
#include "map.h"     // IWYU pragma: export
#include "number.h"  // IWYU pragma: export
#include "object.h"  // IWYU pragma: export
#include "string.h"  // IWYU pragma: export
#include "symbol.h"  // IWYU pragma: export
#include "value.h"   // IWYU pragma: export

// Static string; for heap objects the type descriptor's name.
const char *clj_type_name(clj_value v);

// Raw cache slot of a string, symbol, keyword or map: 0 until the first clj_hash. Aborts on other types.
uint32_t clj_debug_cached_hash(clj_value v);

#endif
