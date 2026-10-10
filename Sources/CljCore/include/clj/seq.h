// @ai-generated(guided)
#ifndef CLJ_SEQ_H
#define CLJ_SEQ_H

#include <stdatomic.h>

#include "fn.h"
#include "object.h"

// Seq types on the descriptor slots: views over a vector and a string, a fixnum range and the lazy seq.
// All carry the ASeq trait (coll.h): sequential equality and hash, conj as cons.

// Metadata in the trailing CLJ_FLAG_META word, as a cons: a view without it keeps its size. `size` is the
// type's own sizeof; its each_child must pass clj_meta_trailing on.
clj_value clj_view_meta(clj_value self, size_t size);
// Consumes self (+1 in): a unique with-meta'd view is rewritten in place, anything else copied.
clj_value clj_view_with_meta(clj_value self, clj_value m, size_t size);

// (seq v) as an O(1) view; next is a new view one index on.
typedef struct {
	clj_header h;
	uint32_t   i;
	clj_slot   vec;
} clj_vector_seq;

extern const clj_type clj_vector_seq_type;

// i < count. vec is retained.
clj_value clj_vector_seq_new(clj_value vec, uint32_t i);

// Code points of a string from byte offset pos.
typedef struct {
	clj_header h;
	uint32_t   pos;
	clj_slot   str;
} clj_string_seq;

extern const clj_type clj_string_seq_type;

// pos < len, at a code point boundary. str is retained.
clj_value clj_string_seq_new(clj_value str, uint32_t pos);

// [start, end) by step over the whole int64, so Long/MAX_VALUE is a bound like any other. Never empty and
// step never 0: the constructor returns () instead.
typedef struct {
	clj_header h;
	int64_t    start, end, step;
} clj_range;

extern const clj_type clj_range_type;

// () when the range is empty. Aborts on step 0; the caller maps that to Clojure's infinite repeat.
clj_value clj_range_new(int64_t start, int64_t end, int64_t step);

// at + step, false when it leaves the int64: the range is then over, as every bound lies inside it.
static inline bool clj_range_step(int64_t at, int64_t step, int64_t *out) { return !__builtin_add_overflow(at, step, out); }

// A compiled closure arity's entry (`<base>_a0`): self and the captures as its frame reads them, then the arguments.
typedef clj_value (*clj_lazy_code)(clj_value self, const clj_value *captured, const clj_value *args, size_t nargs);

// A thunk forced at most once; the realized seq is cached for the object's life, so a walk may borrow
// it. Nested lazy seqs are unwrapped iteratively (a thunk returning a lazy seq does not recurse).
// state: 0 unforced, 1 forcing, 2 forced. Forcing a shared object claims it with a CAS and other
// threads spin until the value is published; a thunk that forces its own object throws.
// The thunk is one of three, told apart by `owner`: a fn (the closure path), the exec of an interpreted thunk's
// tree with `node` its fn node, or nil with `code` a compiled arity. The last two keep the captures in the cell
// itself; owner and captures are cleared once forced.
typedef struct {
	clj_header       h;
	_Atomic uint32_t state;
	uint32_t         ncaptured; // captured[] of a code or node thunk; 0 with a fn thunk and under CLJ_FLAG_META
	clj_slot         owner;     // the fn thunk, or the exec a node thunk runs in; nil once forced and with a code thunk
	clj_slot         value;     // realized seq or nil; meaningful once forced
	union {
		clj_lazy_code          code; // owner nil
		const struct clj_node *node; // owner an exec: a fn node of its tree with the one arity [] and no self slot
	};
	clj_slot captured[];
} clj_lazy_seq;

extern const clj_type clj_lazy_seq_type;

// fn is retained and called with no arguments; its result is seq'd.
clj_value clj_lazy_seq_new(clj_value fn);
// The thunk code(nil, captured, NULL, 0) of a closure arity that reads no self; captured is borrowed and retained.
clj_value clj_lazy_seq_code(clj_lazy_code code, const clj_value *captured, uint32_t ncaptured);
// The interpreter's thunk: node's arity run over exec with captured as the frame's environment (clj_lazy_thunk_run,
// eval.c). exec and captured are borrowed and retained.
clj_value clj_lazy_seq_node(clj_value exec, const struct clj_node *node, const clj_value *captured, uint32_t ncaptured);
// The lazy-seq* builtin: (lazy-seq* f) with f a fn. A site builds its cell without the fn while the var holds it.
clj_value clj_lazy_seq_star(const clj_value *args, size_t n);
// The var's root is still the builtin, a with-meta copy of it included: what both backends' inline cells check.
static inline bool clj_lazy_seq_star_is(clj_value root) {
	return clj_is_fn(root) && clj_fn_of(root)->kind == CLJ_FN_NATIVE && clj_fn_of(root)->u.native.fn == clj_lazy_seq_star;
}
// Borrowed realized seq (nil when empty), valid while ls is. CLJ_THROWN when the thunk throws; the
// object stays unforced and a later force runs the thunk again.
clj_value clj_lazy_seq_force(clj_value ls);
bool clj_lazy_seq_realized(clj_value ls);

static inline bool clj_is_lazy_seq(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_lazy_seq_type; }

static inline clj_vector_seq *clj_vector_seq_of(clj_value v) { return (clj_vector_seq *)clj_to_ptr(v); }
static inline clj_string_seq *clj_string_seq_of(clj_value v) { return (clj_string_seq *)clj_to_ptr(v); }
static inline clj_range      *clj_range_of(clj_value v) { return (clj_range *)clj_to_ptr(v); }
static inline clj_lazy_seq   *clj_lazy_seq_of(clj_value v) { return (clj_lazy_seq *)clj_to_ptr(v); }

#endif
