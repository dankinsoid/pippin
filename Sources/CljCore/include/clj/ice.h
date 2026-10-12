// @ai-generated(solo)
#ifndef CLJ_ICE_H
#define CLJ_ICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "value.h"

// Internal errors and the optimization kill switch: design §3 «Корректность реализации», NOTES "Internal errors".

typedef struct clj_node clj_node;

typedef enum { CLJ_BACKEND_INHERIT, CLJ_BACKEND_INTERPRETER, CLJ_BACKEND_DEV, CLJ_BACKEND_CLOSED } clj_backend;

// Linked from the execution's shadow ring, so it follows a coroutine across a park; a zero field inherits.
typedef struct clj_ice_frame {
	struct clj_ice_frame *prev;
	const char           *pass;
	clj_backend           backend;
	clj_value             file;      // borrowed string, or nil
	uint32_t              line, col; // the top-level form
} clj_ice_frame;

void clj_ice_enter(clj_ice_frame *f);
void clj_ice_leave(clj_ice_frame *f);
// Renames the innermost frame's pass: the sub-passes of one entry point.
void clj_ice_pass(const char *pass);
// *file* of the load in progress, borrowed; nil outside one.
clj_value clj_ice_current_file(void);

// kind is stable, the message is not; at may be NULL. Reports to stderr and dies.
void clj_ice(const char *kind, const clj_node *at, const char *fmt, ...) __attribute__((noreturn, format(printf, 3, 4)));
void clj_ice_assert(const char *msg, const char *file, int line) __attribute__((noreturn));
// The report without the trace, always NUL-terminated; the length the whole text needs.
size_t clj_ice_render(char *buf, size_t cap, const char *kind, const clj_node *at, const char *message);
// Async-signal-safe: what a fatal signal adds to its trace (guard.c).
void clj_ice_write_context(int fd);
const char *clj_node_kind_name(int kind);

// Each bit takes away something that may make an answer differ from the interpreter's reading of the tree.
enum {
	CLJ_NO_OPT_REUSE      = 1u << 0, // clj_is_unique and reuse tokens answer "shared"
	CLJ_NO_OPT_PASSES     = 1u << 1, // clj_optimize: direct fns, intrinsics, fusion, folding, last-use marks
	CLJ_NO_OPT_SPECIALIZE = 1u << 2, // specialized arithmetic entries
	CLJ_NO_OPT_LAZY_SEQ   = 1u << 3, // interpreted lazy-seq cells without a closure
	CLJ_NO_OPT_LAYOUTS    = 1u << 4, // tuples, shape maps, flat transients
	CLJ_NO_OPT_COMPILED   = 1u << 5, // the compiled core, registered units, the compiled eval
	CLJ_NO_OPT_EMIT       = 1u << 6, // facts in the C emitter: promoted and typed slots, unboxing, arms, workers, tokens
	CLJ_NO_OPT_INLINE     = 1u << 7, // inlined fn bodies, the emitter's fast arms of an intrinsic and its inlined fused stages
	CLJ_NO_OPT_BETA       = 1u << 8, // inlining a body that calls a parameter, and the fn argument copied where it is called
	CLJ_NO_OPT_PATH       = 1u << 9, // get-in, assoc-in and update-in unrolled over a literal path
	CLJ_NO_OPT_OWNED      = 1u << 10, // owned parameters and the late take of a dying slot: every parameter +0
	CLJ_NO_OPT_LOWER      = 1u << 11, // reduce, reduce-kv, swap! and swap-vals! over a literal fn without its closure
	CLJ_NO_OPT_ALL        = (1u << 12) - 1,
};
// "1"/"all", ""/"0", or a comma list of the names above; UINT32_MAX for an unknown word.
uint32_t clj_no_opt_parse(const char *spec);
// Adds bits; clj_init adds CLJ_NO_OPT's. The compiled core is chosen at clj_init only.
void        clj_no_opt_set(uint32_t mask);
uint32_t    clj_no_opt(void);
const char *clj_no_opt_describe(uint32_t mask, char *buf, size_t cap);

#endif
