#ifndef CLJ_CORE_H
#define CLJ_CORE_H

// Umbrella header: re-exports the public core API.
#include "cons.h"    // IWYU pragma: export
#include "error.h"   // IWYU pragma: export
#include "keyword.h" // IWYU pragma: export
#include "list.h"    // IWYU pragma: export
#include "map.h"     // IWYU pragma: export
#include "ns.h"      // IWYU pragma: export
#include "number.h"  // IWYU pragma: export
#include "object.h"  // IWYU pragma: export
#include "printer.h" // IWYU pragma: export
#include "reader.h"  // IWYU pragma: export
#include "string.h"  // IWYU pragma: export
#include "symbol.h"  // IWYU pragma: export
#include "value.h"   // IWYU pragma: export
#include "var.h"     // IWYU pragma: export
#include "vector.h"  // IWYU pragma: export

// Static string; for heap objects the type descriptor's name.
const char *clj_type_name(clj_value v);

// Raw cache slot of a string, symbol, keyword, map or vector: 0 until the first clj_hash. Aborts on other types.
uint32_t clj_debug_cached_hash(clj_value v);

#endif
