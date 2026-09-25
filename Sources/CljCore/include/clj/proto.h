// @ai-generated(guided)
#ifndef CLJ_PROTO_H
#define CLJ_PROTO_H

#include "fn.h"
#include "object.h"

// Protocol tables are immutable snapshots: heap descriptors own theirs in user_protos, immortal ones sit in a side table (NOTES.md).
typedef struct {
	clj_header h;
	uint32_t   id;
	uint64_t   core_bits; // nonzero: a core interface with no methods; satisfies? reads the bits, extend refuses it
	clj_value  name;      // qualified symbol
	clj_value  methods;   // vector of unqualified symbols
	clj_value  sigs;      // vector, per method, of param vectors
	_Atomic uint32_t user_types, user_records; // extends by deftypes and by records ever, since those types have no registry (facts)
} clj_protocol;

extern const clj_type clj_protocol_type;

static inline bool          clj_is_protocol(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_protocol_type; }
static inline clj_protocol *clj_protocol_of(clj_value v) { return (clj_protocol *)clj_to_ptr(v); }

// A bare name is qualified with the current namespace; sigs is a vector of [method-symbol [params...]].
clj_value clj_protocol_new(clj_value name, clj_value sigs);
// The dispatching fn of method idx; its min arity is at least 1.
clj_value clj_protocol_method(clj_value proto, uint32_t idx);

// The context of a method fn (a CLJ_FN_NATIVE_CTX native around clj_protocol_method_invoke).
typedef struct {
	clj_value proto;  // borrowed: the fn's code slot keeps the protocol alive
	uint32_t  idx;    // method index in the protocol
	uint64_t  serial; // unique for the process: the identity a call-site cache keys on without holding the fn
} clj_method_ctx;

clj_value clj_protocol_method_invoke(void *ctx, const clj_value *args, size_t n);

static inline bool clj_is_protocol_method(clj_value f) {
	return clj_is_fn(f) && clj_fn_of(f)->kind == CLJ_FN_NATIVE_CTX && clj_fn_of(f)->u.native_ctx.fn == clj_protocol_method_invoke;
}
static inline const clj_method_ctx *clj_method_ctx_of(clj_value f) { return clj_fn_of(f)->u.native_ctx.ctx; }

// Owned impl of the method for v, or nil when v's type does not satisfy the protocol; the lookup opens a
// reader window (NOTES.md). The impl is an element of the type's table, which only a definition-epoch bump
// retires: a cache may hold it borrowed and, inside a window that saw the epoch unchanged, retain it.
clj_value clj_protocol_method_impl(clj_value method, clj_value v);

// Throws "No implementation of method: ..." for v.
clj_value clj_protocol_no_impl(clj_value method, clj_value v);

// Pseudo-descriptors stand in for nil, fixnums, booleans and chars.
const clj_type *clj_dispatch_type(clj_value v);
extern const clj_type clj_nil_dispatch_type, clj_boolean_dispatch_type, clj_char_dispatch_type;
// Every immortal type (builtins, pseudo-descriptors, core interfaces, Object) with an entry for proto, in no order.
void clj_proto_each_immortal(clj_value proto, void (*visit)(const clj_type *t, void *ctx), void *ctx);
// Every live user type (deftype, record, reify) with its own entry for proto; visit runs under the registry's lock
// with the descriptor borrowed, so it must neither release values nor make or free a type.
void clj_proto_each_user(clj_value proto, void (*visit)(const clj_type *t, void *ctx), void *ctx);
// Owned impl of method for a receiver of type t (nil, boolean and char through their pseudo-descriptors), or nil.
clj_value clj_proto_impl_for_type(clj_value method, const clj_type *t);
// One of the core-interface pseudo-descriptors an extend-type on an interface name designates.
bool clj_proto_is_interface_type(const clj_type *t);
// The fallback every type reaches: (extend-type Object ...).
const clj_type *clj_object_type(void);

// type is a descriptor, nil (the nil type) or a core interface; a core interface as proto is refused. nil or CLJ_THROWN.
clj_value clj_proto_extend(clj_value type, clj_value proto, clj_value method_map);
clj_value clj_proto_satisfies(clj_value proto, clj_value v);
clj_value clj_proto_extends(clj_value proto, clj_value type);

// ---- deftype

// The methods a deftype/reify form may give for the core interfaces, one per slot (table in NOTES.md).
typedef enum {
	CLJ_CM_SEQ,
	CLJ_CM_FIRST,
	CLJ_CM_NEXT,
	CLJ_CM_REST,
	CLJ_CM_COUNT,
	CLJ_CM_LOOKUP,
	CLJ_CM_CONJ,
	CLJ_CM_INVOKE,
	CLJ_CM_EX_MESSAGE,
	CLJ_CM_EX_DATA,
	CLJ_CM_EX_CAUSE,
	CLJ_CM_HASH,
	CLJ_CM_EQUALS,
	CLJ_CM_META,
	CLJ_CM_WITH_META,
	CLJ_CM_REDUCE,
	CLJ_CORE_METHOD_COUNT
} clj_core_method;

typedef struct {
	clj_type  t;
	clj_value name;   // string; t.name points into it
	clj_value fields; // vector of symbols
	uint32_t  nfields;
	clj_value core_fns[CLJ_CORE_METHOD_COUNT]; // the fn behind each core slot, nil where not given; written at creation only
} clj_user_type;

// Identity equality and hash, as Clojure's deftype.
typedef struct {
	clj_header h;
	clj_value  fields[];
} clj_instance;

// name is a bare symbol, qualified as ns.Name. impls alternates a protocol or core interface with a method map
// ({:method fn}, nil for none): core interfaces fill the type's slots and bits, protocols extend it.
clj_value clj_user_type_new(clj_value name, clj_value fields, const clj_value *impls, size_t nimpls);
// The caller's descriptor, zero-filled under clj_type_type; record.c allocates a longer one.
clj_value clj_user_type_init(clj_user_type *ut, clj_value name, clj_value fields, const clj_value *impls, size_t nimpls);
static inline bool clj_is_type(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_type_type; }
static inline bool clj_is_user_type(clj_value v) { return clj_is_type(v) && !(clj_header_of(v)->flags & CLJ_FLAG_IMMORTAL); }
// n must equal the type's nfields; fields are retained.
clj_value clj_instance_new(clj_value type, const clj_value *fields, size_t n);
static inline bool clj_is_instance(clj_value v) { return clj_is_ptr(v) && clj_is_user_type(clj_from_ptr((void *)clj_type_of(v))); }
// Borrowed; aborts out of range.
clj_value clj_instance_field(clj_value obj, uint32_t i);
clj_value clj_is_instance_of(clj_value type, clj_value v);
// The same through a var holding the descriptor, as a catch clause naming our type does (design §4).
clj_value clj_catch_instance(clj_value var, clj_value ex);

// Interns the builtins, the builtin type names and the core interfaces into clojure.core.
void clj_proto_install(void);

#endif
