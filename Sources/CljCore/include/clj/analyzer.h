// @ai-generated(guided)
#ifndef CLJ_ANALYZER_H
#define CLJ_ANALYZER_H

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
	CLJ_NODE_TRY,
	CLJ_NODE_THROW,
	CLJ_NODE_INTRINSIC, // a call of a core var the intrinsics table lists, at a listed arity (optimizer.c)
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

// Which thrown values a catch clause takes. There is no class hierarchy: :default, Throwable, Exception and
// Object take every value; ExceptionInfo takes values whose type has CLJ_CORE_ERROR.
typedef enum { CLJ_CATCH_ALL, CLJ_CATCH_ERROR } clj_catch_kind;

typedef struct {
	clj_catch_kind kind;
	uint32_t        slot; // the binding, a frame slot as for let*
	const clj_node *handler;
} clj_catch;

// Where a closure takes a captured value from in the frame that creates it.
typedef struct {
	bool     from_captured;
	uint32_t index;
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
		uint32_t  index; // local, captured
		clj_value var;   // var
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
			uint64_t       serial; // unique for the process: the identity a call-site cache keys on without holding the node
			clj_value      name;   // symbol or nil
			clj_fn_arity  *fixed[CLJ_FN_MAX_FIXED + 1];
			clj_fn_arity  *variadic;
			clj_capture   *captures;
			uint32_t       ncaptures;
		} fn;
		struct {
			const clj_node  *fn;
			const clj_node **args;
			uint32_t         n;
		} invoke;
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

// Special-form names (plus & and the clause heads catch/finally): syntax-quote leaves them unqualified.
bool clj_is_special_symbol(clj_value sym);

// Owned expansion, or form retained when its head is no macro; a macro's exception gets env's position.
clj_value clj_macroexpand_1(clj_value form, const clj_env *env);
// Every step; throws when a macro keeps expanding past 1000 steps.
clj_value clj_macroexpand(clj_value form, const clj_env *env);

#endif
