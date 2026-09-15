// @ai-generated(guided)
#ifndef CLJ_FUSION_H
#define CLJ_FUSION_H

#include "object.h"

// Core vars the fusion pass (optimizer.c) names; a FUSED node runs its fused program only while each holds its boot root.

typedef enum {
	CLJ_FUSION_STAGE,    // map, filter, take, ...: a transducer arity one below the lazy one
	CLJ_FUSION_CONSUMER, // reduce, into, vec, count
	CLJ_FUSION_DRIVER,   // the natives below
} clj_fusion_role;

typedef struct {
	const char     *name;  // qualified, clojure.core/map
	clj_fusion_role role;
	uint32_t        arity; // a stage's lazy arity; 0 otherwise
} clj_fusion_var;

// Keyed by the var: a local or another namespace's var of the same name never matches.
const clj_fusion_var *clj_fusion_find(clj_value var);
const clj_fusion_var *clj_fusion_find_named(clj_value qualified_sym);
// Borrowed; the boot root is immortal.
clj_value clj_fusion_var_of(const clj_fusion_var *fv);
clj_value clj_fusion_boot_root(const clj_fusion_var *fv);
bool      clj_fusion_guard(const clj_fusion_var *const *fvs, uint32_t n);

// Resolves the boot roots; clj_init calls it after core.clj. Nothing fuses before.
void clj_fusion_install(void);

// Optimizer switch, on by default; returns the previous setting.
bool clj_fusion_set_enabled(bool on);
bool clj_fusion_enabled(void);

// (fused-reduce* f [init] coll xfs): the transducers of xfs, applied first to last, see nil as the accumulator.
clj_value clj_fused_reduce(const clj_value *args, size_t n);
// (fused-into* to coll xfs)
clj_value clj_fused_into(const clj_value *args, size_t n);
// (into to xform coll): the same driver under one transducer, so the accumulator is the driver's own.
clj_value clj_into_xform(clj_value to, clj_value xform, clj_value coll);
// (fused-count* coll xfs)
clj_value clj_fused_count(const clj_value *args, size_t n);

#endif
