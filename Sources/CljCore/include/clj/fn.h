// @ai-generated(guided)
#ifndef CLJ_FN_H
#define CLJ_FN_H

#include "object.h"

typedef struct clj_node clj_node;

// args are borrowed; the result is owned or CLJ_THROWN.
typedef clj_value (*clj_native_fn)(const clj_value *args, size_t n);
// The same with a context: how a host closure (a Swift block) becomes a fn. ctx is borrowed from the fn.
typedef clj_value (*clj_native_ctx_fn)(void *ctx, const clj_value *args, size_t n);

typedef enum { CLJ_FN_CLOSURE, CLJ_FN_NATIVE, CLJ_FN_NATIVE_CTX } clj_fn_kind;

// max_arity of a variadic native.
static const uint32_t CLJ_ARITY_ANY = UINT32_MAX;

typedef struct {
	clj_header  h;
	clj_value   name; // symbol or nil; only for messages
	clj_fn_kind kind;
	uint32_t    min_arity, max_arity; // natives; a closure dispatches on its code's arity table
	union {
		struct {
			clj_native_fn fn;
			const void   *consuming; // the consuming intrinsics entry whose boot builtin this is, else NULL (intrinsics.c)
		} native;
		struct {
			clj_native_ctx_fn fn;
			void             *ctx;
			void (*release)(void *ctx); // NULL when ctx needs no cleanup
		} native_ctx;
		const clj_node *node; // closure: its fn node, borrowed from the exec in code
	} u;
	clj_value code; // exec (eval.h) of a closure's tree; a native parks a value its ctx borrows here (a protocol method: its protocol, a with-meta copy: the original)
	clj_value meta; // map or nil
	uint32_t  nenv;
	clj_value env[]; // captured values, owned
} clj_fn;

extern const clj_type clj_fn_type;

clj_value clj_fn_native(clj_value name, clj_native_fn fn, uint32_t min_arity, uint32_t max_arity);
// The fn owns ctx: release runs once, when the fn dies, on whatever thread drops the last reference.
clj_value clj_fn_native_ctx(clj_value name, clj_native_ctx_fn fn, void *ctx, void (*release)(void *ctx), uint32_t min_arity, uint32_t max_arity);
// A compiled closure: ctx is the fn object itself, its captured values retained in env.
clj_value clj_fn_native_env(clj_value name, clj_native_ctx_fn fn, const clj_value *env, uint32_t nenv, uint32_t min_arity, uint32_t max_arity);
// node is a fn node of exec's tree; env items are borrowed and retained.
clj_value clj_fn_closure(clj_value exec, const clj_node *node, clj_value name, const clj_value *env, uint32_t nenv);

static inline bool    clj_is_fn(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_fn_type; }
static inline clj_fn *clj_fn_of(clj_value v) { return (clj_fn *)clj_to_ptr(v); }

// The single IFn dispatch through the type's invoke slot: fn, keyword (lookup in args[0]), map (get), vector (nth).
// args are borrowed; the result is owned or CLJ_THROWN.
clj_value clj_invoke(clj_value f, const clj_value *args, size_t n);
// (apply f a b ... seq): the last argument is any seqable and is spread.
clj_value clj_apply(clj_value f, const clj_value *args, size_t n);
// "Wrong number of args (n) passed to: f" for any invokable; invoke slots share it.
clj_value clj_arity_error(clj_value f, size_t n);
// Whether a fn takes n arguments; true for any other invokable, which checks on the call.
bool clj_fn_accepts(clj_value f, size_t n);

#endif
