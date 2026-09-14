// @ai-generated(guided)
#ifndef CLJ_EPOCH_H
#define CLJ_EPOCH_H

#include <stdint.h>

// The definition epoch: one process-wide counter every inline cache keys on. A cache entry made at epoch e
// is valid while clj_epoch() still reads e; one bump invalidates every cache in the process, and they rewarm
// in microseconds, so there is no per-var or per-protocol epoch. Bumped by:
//   - clj_var_bind_root: def, defmacro, the boot bindings, a host bind;
//   - clj_proto_extend: extend, extend-type, extend-protocol;
//   - clj_user_type_new: deftype, the first evaluation of a reify site.
// Meta changes (alter-meta!, reset-meta!) do not bump it: no cache reads meta.
uint64_t clj_epoch(void);
void     clj_epoch_bump(void);

#endif
