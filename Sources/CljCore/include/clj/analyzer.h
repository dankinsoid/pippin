// @ai-generated(guided)
#ifndef CLJ_ANALYZER_H
#define CLJ_ANALYZER_H

#include "fusion.h"
#include "intrinsics.h"
#include "object.h"

// Forms become a tree of nodes; every symbol is resolved here, so evaluation never looks a name up.
// A tree is the program only: immutable once analyzed and optimized (optimizer.c rewrites it before it is
// numbered) and free of interpreter state, so an evaluator (eval.h) and an emitter read the same nodes.
// Per-node execution state lives in a side table indexed by node id.
// Nodes are heap objects: a closure retains the tree it was created in, which keeps its bodies alive past the form.

typedef struct clj_node clj_node;

typedef enum {
	CLJ_NODE_CONST,
	CLJ_NODE_LOCAL,    // a slot of the running frame
	CLJ_NODE_CAPTURED, // a slot of the closure's environment
	CLJ_NODE_OUTER,    // a slot of the frame `depth` static links up: a free variable of a direct fn body
	CLJ_NODE_VAR,
	CLJ_NODE_IF,
	CLJ_NODE_DO,
	CLJ_NODE_LET,
	CLJ_NODE_LOOP,
	CLJ_NODE_RECUR,
	CLJ_NODE_FN,
	CLJ_NODE_INVOKE,
	CLJ_NODE_DEF,
	CLJ_NODE_VECTOR, // literal with non-constant elements
	CLJ_NODE_MAP,
	CLJ_NODE_SET,
	CLJ_NODE_TRY,
	CLJ_NODE_THROW,
	CLJ_NODE_INTRINSIC, // a call of a core var the intrinsics table lists, at a listed arity (optimizer.c)
	CLJ_NODE_FUSED,     // a consumer over lazy stages with its transducer form beside the original (optimizer.c)
	CLJ_NODE_DIRECT_FN, // a let/loop-bound fn only ever called: no closure, its slot holds nil (optimizer.c)
	CLJ_NODE_DIRECT_CALL, // a call of a direct fn's arity in a fresh frame linked to the defining one
	CLJ_NODE_OBJC_SEND,   // (.method target args*) with literal keyword labels (design §5, level 1)
	// An inlined call (optimizer.c, design §6 «Инлайнинг без JIT») is (if ROOT_IS (INLINE body) (f args)), its args
	// bound by an enclosing let or substituted: the body runs only while the var holds the closure it was copied from.
	CLJ_NODE_ROOT_IS, // whether a var's root is still the closure an inlined body was copied from
	CLJ_NODE_INLINE,  // a fn's body in the calling frame (clj_inline_how): a frame of that fn in a trace, nothing else
	// A native higher-order fn called with a literal fn that never becomes a closure (optimizer.c try_lower, design
	// §10 step 7a′): the then branch of a guard whose else branch is the call, as an inlined body is.
	CLJ_NODE_LOWERED,
} clj_node_kind;

#define CLJ_NODE_KIND_LAST CLJ_NODE_LOWERED

// What an INLINE stands for; each is a frame of its fn in a trace.
typedef enum {
	CLJ_INLINE_COPY,    // the guard's closure's arity body, copied: the then branch of its guard
	CLJ_INLINE_PATH,    // a known-path specialization of get-in, assoc-in or update-in: the then branch of its guard
	CLJ_INLINE_LEVEL,   // one level of the recursion a known-path specialization unrolled, inside its PATH body
	CLJ_INLINE_LITERAL, // a literal fn argument of a guarded call, its arity body where the callee calls it (beta-reduction)
} clj_inline_how;

// The longest path a known-path specialization unrolls (optimizer.c): its levels, which the verifier bounds.
#define CLJ_INLINE_PATH_MAX 4

// Clojure's limit; more parameters go through the rest argument.
#define CLJ_FN_MAX_FIXED 20

typedef struct {
	uint32_t  nparams;  // fixed parameters, in slots 0..nparams-1; the rest parameter follows in slot nparams
	bool      variadic;
	int32_t   self_slot; // the fn's own name, -1 when anonymous
	uint32_t        nslots;
	const clj_node *body;
} clj_fn_arity;

// A selector naming no specific error excludes a :cancelled ex-type (design.md §4, catch_matches/emit_try).
typedef enum { CLJ_CATCH_ALL, CLJ_CATCH_ERROR, CLJ_CATCH_KEYWORD, CLJ_CATCH_TYPE, CLJ_CATCH_HOST } clj_catch_kind;

typedef struct {
	clj_catch_kind kind;
	uint32_t        slot; // the binding, a frame slot as for let*
	// KEYWORD: the keyword, immortal. TYPE: the var holding our type descriptor, owned. HOST: the name the
	// source spelled the host type with, a string, owned (design.md §4).
	clj_value       selector;
	const clj_node *handler;
} clj_catch;


// A def's init runs at the def, or on the first deref (design §4 «Var и ленивые def»): pure by the facts, or ^:lazy.
typedef enum { CLJ_DEF_EAGER, CLJ_DEF_LAZY_INFERRED, CLJ_DEF_LAZY_EXPLICIT } clj_def_lazy;
typedef enum { CLJ_DEF_ASKED_NONE, CLJ_DEF_ASKED_LAZY, CLJ_DEF_ASKED_EAGER } clj_def_asked;

// Where a closure takes a captured value from in the frame that creates it: its slots, its environment, or
// a slot of the frame `depth` static links up when the closure is made inside a direct fn body.
typedef enum { CLJ_CAPTURE_LOCAL, CLJ_CAPTURE_CAPTURED, CLJ_CAPTURE_OUTER } clj_capture_kind;

typedef struct {
	clj_capture_kind kind;
	uint32_t         depth; // OUTER only
	uint32_t         index;
} clj_capture;

struct clj_node {
	clj_header    h;
	clj_node_kind kind;
	uint32_t      id;     // pre-order index within its tree; the root is 0
	uint32_t      nnodes; // size of the subtree: it holds the ids id .. id + nnodes - 1
	uint32_t      line, col; // of the innermost enclosing list the reader positioned; 0 when unknown
	uint32_t      site;      // INVOKE: ordinal among the tree's invoke nodes, the index of its call-site cache in the exec (eval.h)
	union {
		clj_slot  value; // const
		uint32_t  index; // captured
		struct {
			uint32_t index;
			bool     last; // the last use of the slot on its path (optimizer.c): the frame's reference goes to the consumer
		} local;
		clj_slot  var;   // var
		struct {
			uint32_t depth, index; // outer: depth >= 1
		} outer;
		struct {
			const clj_node *test, *then, *else_; // else_ is NULL for a two-armed if
		} if_;
		struct {
			const clj_node **items; // do, vector; map alternates key, value
			uint32_t         n;
		} seq;
		struct {
			uint32_t        *slots; // let, loop
			const clj_node **inits;
			uint32_t         n;
			const clj_node  *body;
		} let;
		struct {
			const clj_node **args;
			uint32_t        *slots; // the target's binding slots, in order
			uint32_t         n;
		} recur;
		struct {
			clj_slot       name; // symbol or nil
			clj_fn_arity  *fixed[CLJ_FN_MAX_FIXED + 1];
			clj_fn_arity  *variadic; // NULL on a direct fn
			union {
				clj_capture   *captures; // none on a direct fn: its body reads the defining frame through OUTER nodes
				// A compiled unit's stub (compiled.c) of a body inlined into another frame fn: the stub of the body it
				// sits in, so a trace names every fn of the chain (trace.c). A stub has no captures.
				const clj_node *inlined_in;
			};
			uint32_t       ncaptures;
		} fn; // fn, direct fn
		// fn is the DIRECT_FN node bound at `slot` of the frame `depth` static links up from the calling one,
		// borrowed from the let/loop that owns it (the same tree); arity is its arity for n args.
		struct {
			const clj_node     *fn;
			const clj_fn_arity *arity;
			const clj_node    **args;
			uint32_t            n;
			uint32_t            slot, depth;
		} direct;
		struct {
			const clj_node  *fn;
			const clj_node **args;
			uint32_t         n;
		} invoke;
		// The guard of an inlined body: expect is the closure the body was copied from (its node is the INLINE's fn),
		// held so that its identity is never reused; a var redefined, rebound or dynamic fails it.
		// guards: a known-path specialization's core vars that must hold their boot roots (fusion.h), the var among them.
		// ops: a scalar replacement's guard (optimizer.c), a let init and nothing else: the intrinsics it answered
		// statically on a replaced value, each var still at its builtin; var and expect are the first one's.
		// proto: a protocol call's guard (optimizer.c try_proto_inline): var's root is a method whose impl for the value
		// in slot recv is expect, the one implementor type's; its else branch's call reads recv as its receiver.
		struct {
			clj_slot               var;
			clj_slot               expect;
			const clj_fusion_var **guards;
			uint32_t               nguards;
			const clj_intrinsic  **ops;
			uint32_t               nops;
			bool                   proto;
			uint32_t               recv;
			clj_slot               type;
		} root_is;
		// fn is borrowed: the guard's closure holds the callee's tree, its else branch a literal argument's FN node.
		struct {
			const clj_node *fn;
			const clj_node *body;
			uint8_t         how; // clj_inline_how
		} inline_;
		// how: the native (clj_lowered_how). fn is borrowed from the else branch's call, where it is argument `at`;
		// args are that call's other arguments, copied, in order. fn's captures are read after them, from this frame.
		// split, owned, or NULL: a reduce's fn over a map entry as (fn [acc k v] (let [e [k v]] body)), its captures
		// fn's, which a map's reduce runs without making the entry (optimizer.c, scalar replacement).
		struct {
			const clj_node  *fn;
			const clj_node **args;
			uint32_t         n;
			uint8_t          how;
			const clj_node  *split;
		} lowered;
		// Labels are syntax, folded into one selector here, so no backend reads them again (design §5).
		struct {
			clj_slot         selector; // string: base plus every label, each colon-terminated
			const clj_node  *target;
			const clj_node **args;
			uint32_t         n;
		} objc;
		struct {
			clj_slot        var;
			const clj_node *init;    // NULL for (def x)
			const clj_node *meta;    // the var's meta: the symbol's meta plus :ns :name :line :column, evaluated at def time
			bool            macro;   // :macro true in the symbol's meta (defmacro puts it there)
			bool            dynamic; // :dynamic in the symbol's meta
			uint8_t         lazy;    // clj_def_lazy: the analyzer's decision, which both backends carry out
			uint8_t         asked;   // clj_def_asked: ^:lazy or ^:eager on the symbol
		} def;
		struct {
			const clj_node *body;
			clj_catch      *catches; // tried in order
			uint32_t        ncatches;
			const clj_node *finally_; // NULL when absent
		} try_;
		const clj_node *throw_; // the value to throw
		struct {
			const clj_intrinsic *op;
			clj_slot             var;  // the core var the head resolved to; its root is checked against op's boot fn on every call
			const clj_node     **args; // n == op->arity
			uint32_t             n;
		} intrinsic;
		// args are evaluated once, in the original order, into a frame of their own; fused and original read them as
		// locals 0..nargs-1 and nothing else. fused runs while every guard var holds its boot root (clj_fusion_guard).
		struct {
			const clj_fusion_var **guards;
			uint32_t               nguards;
			const clj_node       **args;
			uint32_t               nargs;
			const clj_node        *fused, *original;
		} fused;
	} u;
};

extern const clj_type clj_node_type;

// The child nodes of n in id order, each once: the walk a numbering, an exec table or an emitter follows.
typedef void (*clj_node_visitor)(const clj_node *child, void *ctx);
void clj_node_children(const clj_node *n, clj_node_visitor visit, void *ctx);

typedef struct {
	clj_value ns;        // namespace to resolve in; nil means the current one
	uint32_t  line, col; // position of the top-level form for error data, 0 when unknown
} clj_env;

// Owned tree, or NULL with the exception pending. Top-level let/loop/catch slots are numbered from 0, so the
// frame a tree needs is one past its highest slot (clj_exec_new counts them).
// An error reports the :line/:column of the innermost enclosing list that carries them (the reader puts
// them on every list), falling back to env's position.
clj_node *clj_analyze(clj_value form, const clj_env *env);

// (clojure.core/lazy-seq* (fn* [] body)) whose fn has that one arity and no self slot: a site both backends build
// the cell of without a fn object, the thunk's captures in the cell (seq.h), while the var holds the builtin.
bool clj_lazy_seq_site(const clj_node *n);

// The inlining pass of the optimizer (optimizer.c), on by default; returns the previous setting. For tests that
// read the facts of a call: CLJ_NO_OPT=inline is the switch.
bool clj_inline_set_enabled(bool on);
// Whether a call of a closure of this fn node may be inlined at some arity: what a unit offers other units' copies.
bool clj_inline_callee_shaped(const clj_node *fn);
// Scalar replacement so far (optimizer.c): values kept in slots, of them under an intrinsic guard, materializations
// left in their extents, values an if of two literals made, lowered reduces given an entry twin. Process totals.
void clj_debug_scalar_counts(uint64_t out[5]);

// EDN-shaped encoding of a tree (the grammar heads node_data.c): owned data, or CLJ_THROWN "not serializable:
// <type>" for a constant that would not read back (a protocol, a deftype descriptor, a host value, a fn).
clj_value clj_node_to_data(const clj_node *root);
// A tree's identity across processes: the encoding names vars and prints constants. False for one it refuses.
bool clj_node_tree_digest(const clj_node *root, uint64_t out[2]);
// Owned tree from that data with fresh ids, or NULL with the exception pending on a malformed shape. A var
// resolves by its qualified symbol and is interned when missing, so the tree never dangles.
clj_node *clj_node_from_data(clj_value data);

// The :line/:column a form's meta carries (the reader puts them on lists); false without both.
bool clj_form_position(clj_value form, uint32_t *line, uint32_t *col);
// The same as {line, column, end-line, end-column}; the last two stay 0 for a form read without a span
// (anything a macro built). A diagnostic underlines the range (design §3 «Диагностика»).
bool clj_form_span(clj_value form, uint32_t span[4]);

// Interns the keywords this module otherwise makes on first use; clj_init calls it (runtime.c).
void clj_analyzer_intern_keywords(void);
void clj_node_data_intern_keywords(void);

// Special-form names (plus & and the clause heads catch/finally): syntax-quote leaves them unqualified.
bool clj_is_special_symbol(clj_value sym);

// Owned expansion, or form retained when its head is no macro; a macro's exception gets env's position.
clj_value clj_macroexpand_1(clj_value form, const clj_env *env);
// Every step; throws when a macro keeps expanding past 1000 steps.
clj_value clj_macroexpand(clj_value form, const clj_env *env);

#endif
