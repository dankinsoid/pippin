// @ai-generated(guided)
#ifndef CLJ_FN_H
#define CLJ_FN_H

#include "object.h"

// args are borrowed; the result is owned or CLJ_THROWN.
typedef clj_value (*clj_native_fn)(const clj_value *args, size_t n);

typedef enum { CLJ_FN_CLOSURE, CLJ_FN_NATIVE } clj_fn_kind;

// max_arity of a variadic native.
static const uint32_t CLJ_ARITY_ANY = UINT32_MAX;

typedef struct {
	clj_header    h;
	clj_value     name; // symbol or nil; only for messages
	clj_fn_kind   kind;
	uint32_t      min_arity, max_arity; // natives; a closure dispatches on its code's arity table
	clj_native_fn native;
	clj_value     code; // fn node (analyzer.h) of a closure, nil for a native
	uint32_t      nenv;
	clj_value     env[]; // captured values, owned
} clj_fn;

extern const clj_type clj_fn_type;

clj_value clj_fn_native(clj_value name, clj_native_fn fn, uint32_t min_arity, uint32_t max_arity);
// env items are borrowed and retained.
clj_value clj_fn_closure(clj_value code, clj_value name, const clj_value *env, uint32_t nenv);

static inline bool    clj_is_fn(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_fn_type; }
static inline clj_fn *clj_fn_of(clj_value v) { return (clj_fn *)clj_to_ptr(v); }

// The single IFn dispatch through the type's invoke slot: fn, keyword (lookup in args[0]), map (get), vector (nth).
// args are borrowed; the result is owned or CLJ_THROWN.
clj_value clj_invoke(clj_value f, const clj_value *args, size_t n);
// (apply f a b ... seq): the last argument is any seqable and is spread.
clj_value clj_apply(clj_value f, const clj_value *args, size_t n);
// "Wrong number of args (n) passed to: f" for any invokable; invoke slots share it.
clj_value clj_arity_error(clj_value f, size_t n);

#endif
