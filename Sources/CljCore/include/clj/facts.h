// @ai-generated(solo)
// Pass 1 of the design's facts lattice: local inference over one optimized tree into a side table (NOTES.md, "Facts").
#ifndef CLJ_FACTS_H
#define CLJ_FACTS_H

#include "analyzer.h"

// The kinds the runtime tells apart; a type fact is a set of them, TOP every bit, BOTTOM none.
typedef enum {
	CLJ_T_NIL     = 1u << 0,
	CLJ_T_BOOL    = 1u << 1,
	CLJ_T_FIXNUM  = 1u << 2,
	CLJ_T_LONG    = 1u << 3, // boxed 64-bit long
	CLJ_T_BIGINT  = 1u << 4,
	CLJ_T_RATIO   = 1u << 5,
	CLJ_T_DECIMAL = 1u << 6,
	CLJ_T_DOUBLE  = 1u << 7,
	CLJ_T_CHAR    = 1u << 8,
	CLJ_T_STRING  = 1u << 9,
	CLJ_T_KEYWORD = 1u << 10,
	CLJ_T_SYMBOL  = 1u << 11,
	CLJ_T_LIST    = 1u << 12, // a list, cons, lazy seq, range or any other seq
	CLJ_T_VECTOR  = 1u << 13,
	CLJ_T_MAP     = 1u << 14,
	CLJ_T_SET     = 1u << 15,
	CLJ_T_SORTED_MAP = 1u << 16,
	CLJ_T_SORTED_SET = 1u << 17,
	CLJ_T_RECORD  = 1u << 18,
	CLJ_T_ARRAY   = 1u << 19,
	CLJ_T_FN      = 1u << 20,
	CLJ_T_VAR     = 1u << 21,
	CLJ_T_ATOM    = 1u << 22,
	CLJ_T_UUID    = 1u << 23,
	CLJ_T_INST    = 1u << 24,
	CLJ_T_REGEX   = 1u << 25,
	CLJ_T_HOST    = 1u << 26, // a deftype instance, a host value, anything else
} clj_type_kind;

#define CLJ_T_BOTTOM 0
// Every kind bit, (1 << 27) - 1, spelled out so Swift imports it.
#define CLJ_T_TOP 0x7ffffff
// Beyond this many kinds a fact widens to TOP (design §3, "полиморфизм ≤4 shape'ов").
#define CLJ_FACT_UNION_MAX 4

// Independent of the type (design §3): join is bitwise or, meet bitwise and.
typedef enum {
	CLJ_NULL_BOTTOM = 0,
	CLJ_NULL_NEVER  = 1,
	CLJ_NULL_ALWAYS = 2,
	CLJ_NULL_MAYBE  = 3,
} clj_null;

// Per local slot: join is the larger value.
typedef enum { CLJ_ESCAPE_LOCAL = 0, CLJ_ESCAPE_CAPTURED = 1, CLJ_ESCAPE_ESCAPES = 2 } clj_escape;

typedef struct {
	uint32_t        types;     // clj_type_kind bitset
	uint8_t         null;      // clj_null
	uint8_t         elem;      // array kind + 1 when types is exactly CLJ_T_ARRAY and it is known, else 0
	uint8_t         unreachable; // the node sits in a branch a refinement proved dead; not part of the lattice
	clj_value       singleton; // the pinned value when types names one kind, else CLJ_UNBOUND
	const clj_type *desc;      // record or host descriptor when known, else NULL
} clj_fact;

// The top level, one fn or direct fn arity, or a fused node's argument frame (NOTES.md, "Facts").
typedef struct {
	uint32_t owner;  // id of the FN, DIRECT_FN or FUSED node; UINT32_MAX for the top-level frame
	uint32_t arity;  // parameter count of the arity, 0 elsewhere
	uint32_t first;  // id of the frame's body
	uint32_t nnodes; // size of that subtree
	uint32_t slot;   // where the frame's slots start in the escape array
	uint32_t nslots;
} clj_facts_frame;

typedef struct {
	uint32_t node;  // id of the LOOP node
	uint32_t first; // where its variables start in the loop-variable array
	uint32_t n;
} clj_facts_loop;

typedef struct clj_facts clj_facts;
typedef struct clj_summaries clj_summaries;

// Design §3 "Строгость": BOTTOM is an error in every mode, TOP into a declaration a warning a namespace may turn off.
typedef enum { CLJ_DIAG_ERROR, CLJ_DIAG_WARNING } clj_diag_severity;
typedef enum {
	CLJ_DIAG_CALL_CONFLICT, // an argument met the callee's requirement down to BOTTOM: the call is proven to throw
	CLJ_DIAG_DECL_CONFLICT, // a :=> declaration met the summary inferred from the body down to BOTTOM
	CLJ_DIAG_TOP_INTO_DECL, // TOP passed where a declaration requires something
	CLJ_DIAG_TOP_RESULT,    // a body answers TOP where its declaration promises something
} clj_diag_kind;

typedef struct {
	clj_diag_kind     kind;
	clj_diag_severity severity;
	uint32_t          line, col;         // the argument at the call site; 0 for a declaration
	uint32_t          use_line, use_col; // the use inside the callee the requirement rests on; 0 for a declaration
	clj_value         callee;            // var, or nil for a direct fn
	uint32_t          arg;               // UINT32_MAX for the result
	clj_fact          passed, required;
	bool              caught; // the site sits in a try body with a handler: the throw it proves is caught, so a warning
} clj_diagnostic;

// Owned; the root is retained, so a singleton stays valid for the table's life. Pure: same tree, same table.
clj_facts *clj_facts_of(const clj_node *root);
// The same with summaries consulted at call sites (pass 2): the table then depends on var roots, see clj_facts_valid.
clj_facts *clj_facts_of_with(const clj_node *root, clj_summaries *sums);
void       clj_facts_free(clj_facts *f);
size_t     clj_facts_bytes(const clj_facts *f);

uint32_t        clj_facts_nnodes(const clj_facts *f);
const clj_fact *clj_facts_node(const clj_facts *f, uint32_t id);

uint32_t               clj_facts_nframes(const clj_facts *f);
const clj_facts_frame *clj_facts_frame_at(const clj_facts *f, uint32_t i);
// The innermost frame whose body subtree holds the node; UINT32_MAX when the id is out of range.
uint32_t   clj_facts_frame_of(const clj_facts *f, uint32_t id);
clj_escape clj_facts_escape(const clj_facts *f, uint32_t frame, uint32_t slot);

uint32_t              clj_facts_nloops(const clj_facts *f);
const clj_facts_loop *clj_facts_loop_at(const clj_facts *f, uint32_t i);
// The widened fact of loop variable i: the join of the init and every recur argument.
const clj_fact *clj_facts_loop_var(const clj_facts *f, uint32_t loop, uint32_t i);

// Refinements that met a fact down to BOTTOM: a statically dead branch, or a wrong signature in the pass.
uint32_t clj_facts_conflicts(const clj_facts *f);
// The id of the test node whose refinement first hit ⊥, UINT32_MAX when none.
uint32_t clj_facts_conflict_node(const clj_facts *f);
// Loop variables the widening rule sent to TOP before the fixpoint reached it.
uint32_t clj_facts_widenings(const clj_facts *f);

// The site diagnostics of pass 2; nothing warns or halts on its own, a consumer reads them (design §3, NOTES.md).
uint32_t              clj_facts_ndiagnostics(const clj_facts *f);
const clj_diagnostic *clj_facts_diagnostic(const clj_facts *f, uint32_t i);
uint32_t              clj_facts_nerrors(const clj_facts *f);
// "f uses argument 0 as map at 12:3, a vector is passed at 40:7" into buf; returns buf.
const char *clj_diagnostic_message(const clj_diagnostic *d, char *buf, size_t n);
// The :facts/warnings switch of a namespace's meta: false turns the TOP warnings off there.
bool clj_facts_warnings_enabled(clj_value ns);
// Call sites that took a result or a requirement from a summary, and arguments a requirement narrowed.
uint32_t clj_facts_summary_hits(const clj_facts *f);
uint32_t clj_facts_narrowed_args(const clj_facts *f);
// Vars whose root or summary the table rests on; false once any was rebound since (its epoch moved).
uint32_t clj_facts_ndeps(const clj_facts *f);
bool     clj_facts_valid(const clj_facts *f);

// ---- the lattice

clj_fact clj_fact_top(void);
clj_fact clj_fact_bottom(void);
clj_fact clj_fact_join(clj_fact a, clj_fact b);
// Refinement; conflicts is bumped when the result is BOTTOM and neither input was.
clj_fact clj_fact_meet(clj_fact a, clj_fact b, uint32_t *conflicts);
// The meet without the union cap: for a requirement, which is never stored on a node (summary.h).
clj_fact clj_fact_meet_wide(clj_fact a, clj_fact b);
// Widens a set past CLJ_FACT_UNION_MAX members to TOP, the numeric kinds counting as one (facts.c).
clj_fact clj_fact_cap(clj_fact f);
bool     clj_fact_eq(clj_fact a, clj_fact b);

static inline bool clj_fact_is_top(clj_fact f) { return f.types == CLJ_T_TOP && f.null == CLJ_NULL_MAYBE; }
static inline bool clj_fact_is_bottom(clj_fact f) { return f.types == CLJ_T_BOTTOM; }
uint32_t           clj_fact_union_size(clj_fact f);
// Exactly one kind: what a consumer can specialize on without a guard per member.
static inline bool clj_fact_known(clj_fact f) { return clj_fact_union_size(f) == 1; }

// The signature table's entries, for the round trip against the schema vocabulary (summary.h): a transfer entry
// computes its result from the arguments (arithmetic, conj, assoc, into) and has no fixed result to project.
uint32_t clj_facts_nsignatures(void);
bool     clj_facts_signature(uint32_t i, const char **name, uint32_t *arity, clj_fact *result, bool *transfer);
// The fact of a constant: a singleton of its kind.
clj_fact clj_fact_of_value(clj_value v);
// The kind bit a value has.
uint32_t clj_fact_kind_of_value(clj_value v);
// Whether a node of this kind evaluates to a value the type fact describes; RECUR, THROW and DIRECT_FN do not.
bool clj_facts_value_node(clj_node_kind kind);
// Static name of a kind bit, "?" when the set is not exactly one kind: for reports and test failures.
const char *clj_fact_kind_name(uint32_t types);

#endif
