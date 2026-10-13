// @ai-generated(guided)
#ifndef CLJ_FUSION_H
#define CLJ_FUSION_H

#include "object.h"

// Core vars the fusion pass (optimizer.c) names; a FUSED node runs its fused program only while each holds its boot root.

typedef enum {
	CLJ_FUSION_STAGE,    // map, filter, take, ...: a transducer arity one below the lazy one
	CLJ_FUSION_CONSUMER, // reduce, into, vec, count, mapv
	CLJ_FUSION_DRIVER,   // the natives below
	CLJ_FUSION_GUARD,    // a core var a known-path specialization or a fused step takes the boot behavior of
	CLJ_FUSION_FORM,     // sequence, comp: a core fn the original program calls between the consumer and the stages
	CLJ_FUSION_XF_STAGE, // partition-all: a stage at its transducer arity alone, its lazy arity's items being others
} clj_fusion_role;

typedef struct {
	const char     *name;  // qualified, clojure.core/map
	clj_fusion_role role;
	uint32_t        arity; // a stage's lazy arity; 0 otherwise
} clj_fusion_var;

// Keyed by the var: a local or another namespace's var of the same name never matches.
const clj_fusion_var *clj_fusion_find(clj_value var);
const clj_fusion_var *clj_fusion_find_named(clj_value qualified_sym);
const clj_fusion_var *clj_fusion_find_named_cstr(const char *qualified);
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

// What a fused site does with its outputs: `fused*`'s first argument, a fixnum, and a compiled site's mode.
typedef enum {
	CLJ_FUSED_MODE_REDUCE, // lead [f] or [f init]
	CLJ_FUSED_MODE_INTO,   // lead [to]
	CLJ_FUSED_MODE_COUNT,  // lead []
	// over (sequence xf …): an input's outputs reach the consumer after its stage calls, as the lazy seq hands them on
	CLJ_FUSED_MODE_SEQ_REDUCE,
	CLJ_FUSED_MODE_SEQ_INTO,
	CLJ_FUSED_MODE_TRANSDUCE, // lead [f] or [f init]: init (f) when absent, and f's completion over the result
	CLJ_FUSED_MODE_FREQUENCIES,
	CLJ_FUSED_MODE_GROUP_BY, // lead [f]
	CLJ_FUSED_MODES,
} clj_fused_mode;

// (fused-scan* f [init]): reductions as a transducer whose 0-arity, the walk's begin, hands the init on first.
clj_value clj_fused_scan(const clj_value *args, size_t n);
// (fused* mode lead xfs coll & colls): two or three colls walk in step, each seq'd before the shortest ends it, as map's.
clj_value clj_fused_run(const clj_value *args, size_t n);

// A compiled site's stages as one push step (design §6b «Итераторы Rust»); a sink takes an output borrowed.
enum { CLJ_FUSED_THROWN = -1, CLJ_FUSED_GO = 0, CLJ_FUSED_HALT = 2 };
typedef struct clj_fused_sink {
	int (*emit)(struct clj_fused_sink *s, clj_value y);
} clj_fused_sink;
// xs: one item per coll, borrowed; HALT when a stage or the sink ended the walk. Stage state lives in ctx.
typedef int (*clj_fused_step)(void *ctx, const clj_value *xs, clj_fused_sink *sink);
// The stages' completion, what (xf result) does; NULL when no stage completes. The begin is what reaches the sink
// before the source's first item: a reductions' init. HALT from it skips the walk, not the completion.
typedef int (*clj_fused_flush)(void *ctx, clj_fused_sink *sink);
typedef struct {
	clj_fused_step  step;
	clj_fused_flush flush;
	void           *ctx;
	clj_fused_flush begin;
} clj_fused_site;
// The driver over a compiled site; lead and colls as fused*'s.
clj_value clj_fused_run_step(clj_fused_mode mode, const clj_value *lead, size_t nlead, const clj_value *colls, size_t ncolls, const clj_fused_site *site);
// mapcat's cat inside a step: coll's items through cont, or to the sink when NULL, as (reduce rrf result coll) would.
int clj_fused_cat(void *ctx, clj_value coll, clj_fused_step cont, clj_fused_sink *sink);

#endif
