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
} clj_node_kind;

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
typedef enum { CLJ_CATCH_ALL, CLJ_CATCH_ERROR, CLJ_CATCH_KEYWORD } clj_catch_kind;

typedef struct {
	clj_catch_kind kind;
	uint32_t        slot; // the binding, a frame slot as for let*
	clj_value       keyword; // the selector, only when kind == CLJ_CATCH_KEYWORD; immortal, no retain/release
	const clj_node *handler;
} clj_catch;

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
		clj_value value; // const
		uint32_t  index; // captured
		struct {
			uint32_t index;
			bool     last; // the last use of the slot on its path (optimizer.c): the frame's reference goes to the consumer
		} local;
		clj_value var;   // var
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
			clj_value      name; // symbol or nil
			clj_fn_arity  *fixed[CLJ_FN_MAX_FIXED + 1];
			clj_fn_arity  *variadic; // NULL on a direct fn
			clj_capture   *captures; // none on a direct fn: its body reads the defining frame through OUTER nodes
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
		// Labels are syntax, folded into one selector here, so no backend reads them again (design §5).
		struct {
			clj_value        selector; // string: base plus every label, each colon-terminated
			const clj_node  *target;
			const clj_node **args;
			uint32_t         n;
		} objc;
		struct {
			clj_value       var;
			const clj_node *init;    // NULL for (def x)
			const clj_node *meta;    // the var's meta: the symbol's meta plus :ns :name :line :column, evaluated at def time
			bool            macro;   // defmacro
			bool            dynamic; // :dynamic in the symbol's meta
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
			clj_value            var;  // the core var the head resolved to; its root is checked against op's boot fn on every call
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

// EDN-shaped encoding of a tree (the grammar heads node_data.c): owned data, or CLJ_THROWN "not serializable:
// <type>" for a constant that would not read back (a protocol, a deftype descriptor, a host value, a fn).
clj_value clj_node_to_data(const clj_node *root);
// Owned tree from that data with fresh ids, or NULL with the exception pending on a malformed shape. A var
// resolves by its qualified symbol and is interned when missing, so the tree never dangles.
clj_node *clj_node_from_data(clj_value data);

// The :line/:column a form's meta carries (the reader puts them on lists); false without both.
bool clj_form_position(clj_value form, uint32_t *line, uint32_t *col);

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
