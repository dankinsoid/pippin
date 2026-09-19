// @ai-generated(guided)
#ifndef CLJ_EVAL_H
#define CLJ_EVAL_H

#include "analyzer.h"
#include "fn.h"
#include "intrinsics.h"
#include "summary.h"

typedef struct clj_frame clj_frame;
typedef clj_value (*clj_eval_fn)(const clj_node *node, clj_frame *frame);

// Execution state of one node; inline caches, profile counters and rewrites land here, never in the node.
typedef struct {
	clj_eval_fn eval;
	uint64_t    hits; // counted only while clj_exec_count is on
	void       *ic;   // the keyword-lookup cache of a (:k m)/(get m :k) site, the shape of a keyword-keyed literal; else NULL
} clj_exec_node;

typedef struct clj_call_site clj_call_site;
typedef struct clj_derivation clj_derivation;

// One per tree; a closure retains the exec of the tree it was created in.
typedef struct {
	clj_header      h;
	const clj_node *root;   // retained
	uint32_t        nslots; // frame slots the root needs at top level
	uint32_t        nsites; // invoke nodes in the tree
	clj_call_site  *sites;  // their inline caches, indexed by clj_node.site (eval.c)
	clj_derivation *derived; // the facts the specialized entries rest on (specialize.c); NULL when none
	clj_exec_node   nodes[]; // indexed by node id
} clj_exec;

extern const clj_type clj_exec_type;

static inline clj_exec *clj_exec_of(clj_value v) { return (clj_exec *)clj_to_ptr(v); }

// Slot ownership: a fixed param and the self slot borrow from the caller's argument array, which the +0
// convention keeps alive for the whole call; let/loop/recur/catch store owned values. `owned` has one bit
// per slot for the first 64; a frame with more slots retains every param at entry and treats every slot
// as owned, so a slot index >= 64 is owned by definition.
struct clj_frame {
	clj_value       *slots;
	clj_value       *captured; // borrowed from the running closure; a direct fn's frame shares its definer's
	const clj_exec  *exec;     // of the tree the running node belongs to
	uint64_t         owned;
	const clj_frame *outer;    // the defining frame of a direct fn body, alive for the whole call; else NULL
};

// Builds the table in one walk of the tree. Owned.
clj_value clj_exec_new(const clj_node *root);
// The interpreter's eval for a node kind: what an exec table holds when no rewrite is on.
clj_eval_fn clj_node_eval_fn(clj_node_kind kind);
// Swaps every node's eval for a hit-counting wrapper and back; off costs nothing, not even a branch.
void     clj_exec_count(clj_value exec, bool on);
uint64_t clj_exec_hits(clj_value exec, uint32_t id);

// ---- specialization by facts (specialize.c; NOTES.md, "Analyzer and evaluator": the specialized arithmetic node)
// Building an exec derives its tree's facts under the process-wide dev store (summaries and the caller join) and
// rewrites the exec entry of every arithmetic INTRINSIC whose arguments are known int64 to a fixnum fast path; the
// tree's call sites go into the reverse index, and a callee whose join that changed is re-derived. Off, nothing is
// derived or recorded; on by default.
void clj_specialize_enable(bool on);
bool clj_specialize_enabled(void);
// The process-wide store the derivations use (the compiler's closed-world run reads the same one), made on first use.
clj_summaries *clj_specialize_store(void);
// The epochs the exec's derivation rests on still stand: every var it read and every caller join it took.
bool clj_exec_derivation_valid(clj_value exec);
// How many times the exec was derived (1 after creation; each re-derivation adds one), 0 when never.
uint32_t clj_exec_derivations(clj_value exec);
// Whether the entry of node id is a specialized one, and the count of those.
bool     clj_debug_exec_node_specialized(clj_value exec, uint32_t id);
uint32_t clj_debug_exec_specialized(clj_value exec);
// The id of the first INTRINSIC node of the tree whose op is the named core fn ("inc"), UINT32_MAX when none.
uint32_t clj_debug_exec_intrinsic_id(clj_value exec, const char *name);
// Call-site counters of the invoke node with this id, counted in debug builds only (-1 otherwise): a hit
// entered a closure body directly, called a plain native from the site or called a cached protocol impl, a
// miss went through the generic invoke (a host fn, a variadic or large-frame closure, a keyword or a
// collection at the head, a protocol dispatch through the tables, which refills).
int64_t clj_debug_exec_ic_hits(clj_value exec, uint32_t id);
int64_t clj_debug_exec_ic_misses(clj_value exec, uint32_t id);
// Receiver types the protocol cache of that node holds, at most CLJ_PROTO_IC_ENTRIES.
uint32_t clj_debug_exec_ic_proto_entries(clj_value exec, uint32_t id);
// The keyword-lookup cache of the node with this id (an invoke of a literal keyword or a get with one): the layouts
// it holds, CLJ_KW_IC_MEGA once it gave up; UINT32_MAX when the node has no such cache. Hits and misses of that
// cache are counted in debug builds only (-1 otherwise).
#define CLJ_KW_IC_MEGA 5
uint32_t clj_debug_exec_kw_entries(clj_value exec, uint32_t id);
int64_t  clj_debug_exec_kw_hits(clj_value exec, uint32_t id);
int64_t  clj_debug_exec_kw_misses(clj_value exec, uint32_t id);
// The id of the first node of the tree that is a keyword invoke or a get with a literal keyword, UINT32_MAX when none.
uint32_t clj_debug_exec_kw_site_id(clj_value exec, uint32_t ordinal);
// Whether the map literal with this node id builds through a precomputed shape.
bool clj_debug_exec_map_shaped(clj_value exec, uint32_t id);
// The id of the tree's invoke node number `site` in pre-order (its clj_node.site); aborts past the last.
uint32_t clj_debug_exec_invoke_id(clj_value exec, uint32_t site);
// Direct calls of let/loop-bound fns run on this thread, counted in debug builds only (-1 otherwise).
int64_t clj_debug_direct_calls(void);
#define CLJ_PROTO_IC_ENTRIES 4
// Evaluates the root in a fresh frame. Owned result or CLJ_THROWN.
clj_value clj_exec_run(clj_value exec);
// clj_exec_new, run, release: for a tree executed once.
clj_value clj_eval_node(const clj_node *node);
// Analyze and evaluate one form; env NULL means the current namespace and no position.
clj_value clj_eval(clj_value form, const clj_env *env);

// ---- cooperative deadline
// The message a timed-out call throws; a host tells a timeout from any other error by it.
#define CLJ_DEADLINE_MESSAGE "Execution timed out"
// Bounds the wall time of what this thread runs next: the first interpreted call, loop turn, lazy-seq cell or
// reduce past the deadline throws CLJ_DEADLINE_MESSAGE, and so does every one after it until the deadline is cleared, each after an
// unwind budget of calls that lets a handler run; past a fixed number of those budgets every check throws, so
// code that catches the timeout inside a loop of its own still stops. ms == 0 clears it, which is what a
// handler that must run unbounded does first.
// Cooperative: a native that loops for ever without calling back into Clojure is not interrupted, and the
// clock is read once per 1024 calls, so the overshoot is one such batch.
void     clj_deadline_set_ms(uint64_t ms);
// The absolute deadline (monotonic ns, 0 when none) and its restore: a host callback that must not be timed
// holds it for its own length.
uint64_t clj_deadline_get(void);
void     clj_deadline_restore(uint64_t deadline);
bool     clj_deadline_expired(void);

// Takes an old fn root a rebind replaced while this thread evaluates (a closure frame or clj_exec_run is up)
// and releases it once the thread is idle; false when the caller releases it itself. Only fn roots are read
// at +0 by the evaluator, so only they are parked.
bool clj_eval_retire_root(clj_value old);
// Parked roots on this thread, for tests.
size_t clj_debug_retired_roots(void);
// What clj_exec_run brackets a top-level form with: a compiled unit's form does the same so a def inside parks fn roots.
void clj_eval_top_enter(void);
void clj_eval_top_leave(void);
// clj_invoke from the host: a recovery point for a stack overflow in compiled code (guard.h) and a top-level bracket.
clj_value clj_host_invoke(clj_value f, const clj_value *args, size_t n);

// A call of f with a fixed argument count prepared once, for a native that calls f per element: a closure has its
// arity resolved and enters its body directly, a plain native skips its arity check. f is borrowed and must
// outlive the calls; a fn that takes no n arguments still goes through clj_invoke, which reports it.
typedef struct {
	clj_value            f;
	size_t               n;
	const clj_fn_arity  *arity;     // of a closure for n, else NULL
	clj_native_fn        native;    // of a plain native accepting n, else NULL
	const clj_intrinsic *consuming; // the consuming entry when f is its boot builtin (conj, assoc, ...), else NULL: a
	                                // driver that owns its accumulator hands it over through clj_intrinsic_call_consuming
} clj_call;

clj_call  clj_call_prepare(clj_value f, size_t n);
clj_value clj_call_invoke_slow(const clj_call *c, const clj_value *args);
// args: n of them, borrowed. Owned result or CLJ_THROWN.
static inline clj_value clj_call_invoke(const clj_call *c, const clj_value *args) {
	if (c->native) return c->native(args, c->n);
	return clj_call_invoke_slow(c, args);
}

// Arity dispatch and body evaluation of a closure; clj_invoke calls it.
clj_value clj_closure_invoke(clj_value f, const clj_value *args, size_t n);
// The same with the invoke node for the shadow frame (shadow.h); NULL when there is none.
clj_value clj_closure_invoke_at(clj_value f, const clj_value *args, size_t n, const clj_node *site);

#endif
