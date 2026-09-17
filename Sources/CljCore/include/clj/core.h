#ifndef CLJ_CORE_H
#define CLJ_CORE_H

// Umbrella header: re-exports the public core API.
#include "analyzer.h" // IWYU pragma: export
#include "array.h"    // IWYU pragma: export
#include "atom.h"    // IWYU pragma: export
#include "box.h"     // IWYU pragma: export
#include "coll.h"    // IWYU pragma: export
#include "compare.h" // IWYU pragma: export
#include "cons.h"    // IWYU pragma: export
#include "epoch.h"   // IWYU pragma: export
#include "error.h"   // IWYU pragma: export
#include "eval.h"    // IWYU pragma: export
#include "fn.h"      // IWYU pragma: export
#include "inst.h"    // IWYU pragma: export
#include "intrinsics.h" // IWYU pragma: export
#include "keyword.h" // IWYU pragma: export
#include "lock.h"    // IWYU pragma: export
#include "list.h"    // IWYU pragma: export
#include "map.h"     // IWYU pragma: export
#include "ns.h"      // IWYU pragma: export
#include "number.h"  // IWYU pragma: export
#include "object.h"  // IWYU pragma: export
#include "printer.h" // IWYU pragma: export
#include "profile.h" // IWYU pragma: export
#include "proto.h"   // IWYU pragma: export
#include "reader.h"  // IWYU pragma: export
#include "reduce.h"  // IWYU pragma: export
#include "regex.h"   // IWYU pragma: export
#include "runtime.h" // IWYU pragma: export
#include "seq.h"     // IWYU pragma: export
#include "set.h"     // IWYU pragma: export
#include "shadow.h"  // IWYU pragma: export
#include "sorted.h"  // IWYU pragma: export
#include "string.h"  // IWYU pragma: export
#include "uuid.h"    // IWYU pragma: export
#include "symbol.h"  // IWYU pragma: export
#include "value.h"   // IWYU pragma: export
#include "var.h"     // IWYU pragma: export
#include "vector.h"  // IWYU pragma: export

// Static string; for heap objects the type descriptor's name.
const char *clj_type_name(clj_value v);

// Raw cache slot of a string, symbol, keyword, map or vector: 0 until the first clj_hash. Aborts on other types.
uint32_t clj_debug_cached_hash(clj_value v);

#endif
