// @ai-generated(guided)
#ifndef CLJ_ANALYZER_H
#define CLJ_ANALYZER_H

#include "object.h"

// Forms become a tree of nodes; every symbol is resolved here, so evaluation never looks a name up.
// Nodes are heap objects: a closure retains its fn node, which keeps the bodies alive past the form.

typedef struct clj_node  clj_node;
typedef struct clj_frame clj_frame;
typedef clj_value (*clj_eval_fn)(const clj_node *node, clj_frame *frame);

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
} clj_node_kind;

// Clojure's limit; more parameters go through the rest argument.
#define CLJ_FN_MAX_FIXED 20

typedef struct {
	uint32_t  nparams;  // fixed parameters, in slots 0..nparams-1; the rest parameter follows in slot nparams
	bool      variadic;
	int32_t   self_slot; // the fn's own name, -1 when anonymous
	uint32_t  nslots;
	clj_node *body;
} clj_fn_arity;

// Which thrown values a catch clause takes. There is no class hierarchy: :default, Throwable, Exception and
// Object take every value; ExceptionInfo takes values whose type has CLJ_CORE_ERROR.
typedef enum { CLJ_CATCH_ALL, CLJ_CATCH_ERROR } clj_catch_kind;

typedef struct {
	clj_catch_kind kind;
	uint32_t       slot; // the binding, a frame slot as for let*
	clj_node      *handler;
} clj_catch;

// Where a closure takes a captured value from in the frame that creates it.
typedef struct {
	bool     from_captured;
	uint32_t index;
} clj_capture;

struct clj_node {
	clj_header    h;
	clj_node_kind kind;
	clj_eval_fn   eval;
	union {
		clj_value value; // const
		uint32_t  index; // local, captured
		clj_value var;   // var
		struct {
			clj_node *test, *then, *else_; // else_ is NULL for a two-armed if
		} if_;
		struct {
			clj_node **items; // do, vector; map alternates key, value
			uint32_t   n;
		} seq;
		struct {
			uint32_t  *slots; // let, loop
			clj_node **inits;
			uint32_t   n;
			clj_node  *body;
		} let;
		struct {
			clj_node **args;
			uint32_t  *slots; // the target's binding slots, in order
			uint32_t   n;
		} recur;
		struct {
			clj_value      name; // symbol or nil
			clj_fn_arity  *fixed[CLJ_FN_MAX_FIXED + 1];
			clj_fn_arity  *variadic;
			clj_capture   *captures;
			uint32_t       ncaptures;
		} fn;
		struct {
			clj_node  *fn;
			clj_node **args;
			uint32_t   n;
		} invoke;
		struct {
			clj_value var;
			clj_node *init;    // NULL for (def x)
			clj_node *meta;    // the var's meta: the symbol's meta plus :ns :name :line :column, evaluated at def time
			bool      macro;   // defmacro
			bool      dynamic; // :dynamic in the symbol's meta
		} def;
		struct {
			clj_node  *body;
			clj_catch *catches; // tried in order
			uint32_t   ncatches;
			clj_node  *finally_; // NULL when absent
		} try_;
		clj_node *throw_; // the value to throw
	} u;
};

extern const clj_type clj_node_type;

static inline clj_node *clj_node_of(clj_value v) { return (clj_node *)clj_to_ptr(v); }

typedef struct {
	clj_value ns;        // namespace to resolve in; nil means the current one
	uint32_t  line, col; // position of the top-level form for error data, 0 when unknown
} clj_env;

// Owned node, or NULL with the exception pending. *nslots is the number of frame slots the tree needs.
// An error reports the :line/:column of the innermost enclosing list that carries them (the reader puts
// them on every list), falling back to env's position.
clj_node *clj_analyze(clj_value form, const clj_env *env, uint32_t *nslots);

// The :line/:column a form's meta carries (the reader puts them on lists); false without both.
bool clj_form_position(clj_value form, uint32_t *line, uint32_t *col);

// Special-form names (plus & and the clause heads catch/finally): syntax-quote leaves them unqualified.
bool clj_is_special_symbol(clj_value sym);

// Owned expansion, or form retained when its head is no macro; a macro's exception gets env's position.
clj_value clj_macroexpand_1(clj_value form, const clj_env *env);
// Every step; throws when a macro keeps expanding past 1000 steps.
clj_value clj_macroexpand(clj_value form, const clj_env *env);

#endif
