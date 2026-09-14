// @ai-generated(guided)
#ifndef CLJ_PROTO_H
#define CLJ_PROTO_H

#include "object.h"

// Protocol tables are immutable snapshots: heap descriptors own theirs in user_protos, immortal ones sit in a side table (NOTES.md).
typedef struct {
	clj_header h;
	uint32_t   id;
	uint64_t   core_bits; // nonzero: a core interface with no methods; satisfies? reads the bits, extend refuses it
	clj_value  name;      // qualified symbol
	clj_value  methods;   // vector of unqualified symbols
	clj_value  sigs;      // vector, per method, of param vectors
} clj_protocol;

extern const clj_type clj_protocol_type;

static inline bool          clj_is_protocol(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_protocol_type; }
static inline clj_protocol *clj_protocol_of(clj_value v) { return (clj_protocol *)clj_to_ptr(v); }

// A bare name is qualified with the current namespace; sigs is a vector of [method-symbol [params...]].
clj_value clj_protocol_new(clj_value name, clj_value sigs);
// The dispatching fn of method idx; its min arity is at least 1.
clj_value clj_protocol_method(clj_value proto, uint32_t idx);

// Pseudo-descriptors stand in for nil, fixnums, booleans and chars.
const clj_type *clj_dispatch_type(clj_value v);
// The fallback every type reaches: (extend-type Object ...).
const clj_type *clj_object_type(void);
// Bumped by every extend; an inline cache keyed by type must check it.
uint64_t clj_proto_epoch(void);

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
static inline bool clj_is_type(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_type_type; }
static inline bool clj_is_user_type(clj_value v) { return clj_is_type(v) && !(clj_header_of(v)->flags & CLJ_FLAG_IMMORTAL); }
// n must equal the type's nfields; fields are retained.
clj_value clj_instance_new(clj_value type, const clj_value *fields, size_t n);
static inline bool clj_is_instance(clj_value v) { return clj_is_ptr(v) && clj_is_user_type(clj_from_ptr((void *)clj_type_of(v))); }
// Borrowed; aborts out of range.
clj_value clj_instance_field(clj_value obj, uint32_t i);
clj_value clj_is_instance_of(clj_value type, clj_value v);

// Interns the builtins, the builtin type names and the core interfaces into clojure.core.
void clj_proto_install(void);

#endif
