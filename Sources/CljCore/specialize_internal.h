// @ai-generated(solo)
// What eval.c and specialize.c share: the derivation of an exec and the specialized entries it installs.
#ifndef CLJ_SPECIALIZE_INTERNAL_H
#define CLJ_SPECIALIZE_INTERNAL_H

#include "clj/eval.h"

// Derives, installs, records the sites, re-derives the callees whose join moved; clj_exec_new calls it last.
void clj_exec_derive(clj_value exec);
// Drops the exec's sites from the reverse index and frees its derivation: the exec's finalizer.
void clj_exec_forget(clj_exec *e);
// Puts the specialized entries back after clj_exec_count restored the generic ones.
void clj_exec_reapply(clj_value exec);
// What the facts said every argument of a specialized node is; the mixed kinds name the two positions in order.
typedef enum { CLJ_SPEC_FIXNUM, CLJ_SPEC_DOUBLE, CLJ_SPEC_FIXNUM_DOUBLE, CLJ_SPEC_DOUBLE_FIXNUM } clj_spec_kind;
// The fast path of an arithmetic entry of the intrinsics table for that kind, NULL when the entry has none.
clj_eval_fn clj_eval_specialized_entry(const clj_intrinsic *op, clj_spec_kind kind);

#endif
