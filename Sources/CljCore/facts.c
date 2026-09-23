// @ai-generated(solo)
// Pass 1 of the facts lattice: one forward walk per frame over an optimized tree (NOTES.md, "Facts").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/array.h"
#include "clj/atom.h"
#include "clj/bigint.h"
#include "clj/cons.h"
#include "clj/decimal.h"
#include "clj/error.h"
#include "clj/facts.h"
#include "clj/fn.h"
#include "clj/inst.h"
#include "clj/intrinsics.h"
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/long.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/number.h"
#include "clj/queue.h"
#include "clj/ratio.h"
#include "clj/regex.h"
#include "clj/seq.h"
#include "clj/set.h"
#include "clj/sorted.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/uuid.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "clj/proto.h"
#include "facts_internal.h"

// A loop variable that has not settled after this many rounds goes straight to TOP.
#define WIDEN_ROUNDS 3

#define T_INT  (CLJ_T_FIXNUM | CLJ_T_LONG | CLJ_T_BIGINT)
#define T_NUM  (T_INT | CLJ_T_RATIO | CLJ_T_DECIMAL | CLJ_T_DOUBLE)
#define T_MAPS (CLJ_T_MAP | CLJ_T_SORTED_MAP | CLJ_T_RECORD)
#define T_SETS (CLJ_T_SET | CLJ_T_SORTED_SET)
#define T_COLL (T_MAPS | T_SETS | CLJ_T_VECTOR | CLJ_T_LIST)
#define T_SEQABLE (T_COLL | CLJ_T_STRING | CLJ_T_ARRAY | CLJ_T_NIL)

typedef struct {
	uint32_t  node;
	clj_value var;
	uint32_t  nargs;
	bool      in_fn; // inside a fn body, which outlives the form
	clj_fact *args;
} facts_site;

typedef struct {
	clj_value var;
	bool      in_fn;
} facts_read;

struct clj_facts {
	const clj_node  *root; // retained: a singleton borrows from a constant of the tree
	uint32_t         nnodes;
	uint32_t         nframes, nslots, nloops, nvars;
	uint32_t         conflicts, widenings;
	uint32_t         conflict_node; // the test whose refinement first hit BOTTOM, for diagnosing a wrong signature
	clj_fact        *nodes;
	clj_facts_frame *frames;
	uint8_t         *escape;
	clj_facts_loop  *loops;
	clj_fact        *vars;
	clj_summaries   *sums; // borrowed; NULL is pass 1 alone
	clj_diagnostic  *diags;
	uint32_t         ndiags, cdiags, nerrors;
	bool             warn; // the namespace's :facts/warnings switch, read once
	uint32_t         hits, narrowed;
	clj_value       *dep_vars;
	uint32_t        *dep_epochs;
	uint32_t         ndeps, cdeps;
	facts_site      *sites;
	uint32_t         nsites, csites;
	facts_read      *value_reads;
	uint32_t         nvalue_reads, cvalue_reads;
	clj_facts_join  *joins;
	uint32_t         njoins, cjoins;
};

static void *xalloc(size_t n, size_t size) {
	void *p = calloc(n ? n : 1, size);
	if (!p) clj_fatal("out of memory");
	return p;
}

static void *xgrow(void *p, size_t n, size_t size) {
	void *q = realloc(p, (n ? n : 1) * size);
	if (!q) clj_fatal("out of memory");
	return q;
}

// ---- the lattice

static uint32_t popcount(uint32_t x) { return (uint32_t)__builtin_popcount(x); }

uint32_t clj_fact_union_size(clj_fact f) { return popcount(f.types); }

clj_fact clj_fact_top(void) {
	clj_fact f = {CLJ_T_TOP, CLJ_NULL_MAYBE, 0, 0, CLJ_UNBOUND, NULL};
	return f;
}

clj_fact clj_fact_bottom(void) {
	clj_fact f = {CLJ_T_BOTTOM, CLJ_NULL_BOTTOM, 0, 0, CLJ_UNBOUND, NULL};
	return f;
}

// A type set constrains nullability; the reverse is left to refinement, so a TOP set with a NEVER null stands.
static clj_fact normalize(clj_fact f) {
	if (!(f.types & CLJ_T_NIL)) f.null &= CLJ_NULL_NEVER;
	if (f.types == CLJ_T_NIL) f.null &= CLJ_NULL_ALWAYS;
	if (f.types != CLJ_T_BOTTOM && f.null == CLJ_NULL_BOTTOM) f.types = CLJ_T_BOTTOM;
	if (popcount(f.types) != 1) {
		f.singleton = CLJ_UNBOUND;
		f.desc = NULL;
		f.elem = 0;
	}
	return f;
}

// The numeric kinds count as one member: the design's ladder is int/double/number/⊤, so "a number" must survive
// a cap that exists to bound polymorphism, not arithmetic.
clj_fact clj_fact_cap(clj_fact f) {
	f = normalize(f);
	uint32_t members = popcount(f.types & ~(uint32_t)T_NUM) + ((f.types & T_NUM) ? 1u : 0u);
	if (members > CLJ_FACT_UNION_MAX) f.types = CLJ_T_TOP;
	return f;
}

static clj_fact fact(uint32_t types, clj_null null) {
	clj_fact f = {types, (uint8_t)null, 0, 0, CLJ_UNBOUND, NULL};
	return clj_fact_cap(f);
}

// Nullability follows the set unless a caller refines it on its own.
static clj_fact fact_of(uint32_t types) {
	clj_null n = (types & CLJ_T_NIL) ? (types == CLJ_T_NIL ? CLJ_NULL_ALWAYS : CLJ_NULL_MAYBE) : CLJ_NULL_NEVER;
	return fact(types, n);
}

bool clj_fact_eq(clj_fact a, clj_fact b) {
	return a.types == b.types && a.null == b.null && a.singleton == b.singleton && a.desc == b.desc && a.elem == b.elem;
}

clj_fact clj_fact_join(clj_fact a, clj_fact b) {
	if (a.types == CLJ_T_BOTTOM) return b;
	if (b.types == CLJ_T_BOTTOM) return a;
	clj_fact r = {a.types | b.types, (uint8_t)(a.null | b.null), 0, 0, CLJ_UNBOUND, NULL};
	// Identity, not clj_equals: two constants the reader made separately are two objects, as the codec keeps them.
	if (a.singleton == b.singleton) r.singleton = a.singleton;
	if (a.desc == b.desc) r.desc = a.desc;
	if (a.elem == b.elem) r.elem = a.elem;
	return clj_fact_cap(r);
}

clj_fact clj_fact_meet(clj_fact a, clj_fact b, uint32_t *conflicts) {
	clj_fact r = {a.types & b.types, (uint8_t)(a.null & b.null), 0, 0, CLJ_UNBOUND, NULL};
	r.singleton = a.singleton == CLJ_UNBOUND ? b.singleton : (b.singleton == CLJ_UNBOUND || a.singleton == b.singleton ? a.singleton : CLJ_UNBOUND);
	r.desc = a.desc ? a.desc : b.desc;
	r.elem = a.elem ? a.elem : b.elem;
	r = normalize(r);
	if (r.types == CLJ_T_BOTTOM && a.types != CLJ_T_BOTTOM && b.types != CLJ_T_BOTTOM && conflicts) (*conflicts)++;
	return clj_fact_cap(r);
}

// A requirement of ten kinds ("seqable") is still a requirement: the cap bounds what a node stores, not what a use asks.
clj_fact clj_fact_meet_wide(clj_fact a, clj_fact b) {
	clj_fact r = {a.types & b.types, (uint8_t)(a.null & b.null), 0, 0, CLJ_UNBOUND, NULL};
	return normalize(r);
}

uint32_t clj_facts_kind_of_type(const clj_type *t) {
	if (t == &clj_nil_dispatch_type) return CLJ_T_NIL;
	if (t == &clj_boolean_dispatch_type) return CLJ_T_BOOL;
	if (t == &clj_char_dispatch_type) return CLJ_T_CHAR;
	if (t == &clj_long_type) return CLJ_T_LONG;
	if (t == &clj_bigint_type) return CLJ_T_BIGINT;
	if (t == &clj_ratio_type) return CLJ_T_RATIO;
	if (t == &clj_decimal_type) return CLJ_T_DECIMAL;
	if (t == &clj_double_type) return CLJ_T_DOUBLE;
	if (t == &clj_string_type) return CLJ_T_STRING;
	if (t == &clj_keyword_type) return CLJ_T_KEYWORD;
	if (t == &clj_symbol_type) return CLJ_T_SYMBOL;
	if (t == &clj_vector_type) return CLJ_T_VECTOR;
	if (t == &clj_map_type) return CLJ_T_MAP;
	if (t == &clj_set_type) return CLJ_T_SET;
	if (t == &clj_sorted_map_type) return CLJ_T_SORTED_MAP;
	if (t == &clj_sorted_set_type) return CLJ_T_SORTED_SET;
	if (t == &clj_array_type) return CLJ_T_ARRAY;
	if (t == &clj_var_type) return CLJ_T_VAR;
	if (t == &clj_atom_type) return CLJ_T_ATOM;
	if (t == &clj_uuid_type) return CLJ_T_UUID;
	if (t == &clj_inst_type) return CLJ_T_INST;
	if (t == &clj_regex_type) return CLJ_T_REGEX;
	if (t->core_bits & CLJ_CORE_RECORD) return CLJ_T_RECORD;
	// a deftype is host whatever interfaces it implements: (fn? x) is false on one that implements IFn
	if (!(t->h.flags & CLJ_FLAG_IMMORTAL)) return CLJ_T_HOST;
	if (t->core_bits & (CLJ_CORE_SEQ | CLJ_CORE_LIST)) return CLJ_T_LIST;
	if (t == &clj_empty_list_type || t == &clj_lazy_seq_type) return CLJ_T_LIST;
	if (t->core_bits & CLJ_CORE_FN) return CLJ_T_FN;
	return CLJ_T_HOST;
}

uint32_t clj_fact_kind_of_value(clj_value v) {
	if (clj_is_nil(v)) return CLJ_T_NIL;
	if (clj_is_bool(v)) return CLJ_T_BOOL;
	if (clj_is_fixnum(v)) return CLJ_T_FIXNUM;
	if (clj_is_char(v)) return CLJ_T_CHAR;
	if (!clj_is_ptr(v)) return CLJ_T_HOST;
	return clj_facts_kind_of_type(clj_header_of(v)->type);
}

clj_fact clj_fact_of_value(clj_value v) {
	clj_fact f = fact_of(clj_fact_kind_of_value(v));
	f.singleton = v;
	if (clj_is_ptr(v)) {
		const clj_type *t = clj_header_of(v)->type;
		if (f.types == CLJ_T_RECORD || f.types == CLJ_T_HOST) f.desc = t;
		if (f.types == CLJ_T_ARRAY) f.elem = (uint8_t)(clj_array_kind_of(v) + 1);
	}
	return f;
}

static const char *const kind_names[] = {
	"nil", "bool", "fixnum", "long", "bigint", "ratio", "decimal", "double", "char", "string", "keyword", "symbol", "seq",
	"vector", "map", "set", "sorted-map", "sorted-set", "record", "array", "fn", "var", "atom", "uuid", "inst", "regex", "host",
};

const char *clj_fact_kind_name(uint32_t types) {
	if (popcount(types) != 1) return "?";
	return kind_names[__builtin_ctz(types)];
}

bool clj_facts_value_node(clj_node_kind kind) {
	return kind != CLJ_NODE_RECUR && kind != CLJ_NODE_THROW && kind != CLJ_NODE_DIRECT_FN;
}

// ---- signatures of the C builtins and intrinsics (why a table of its own: NOTES.md, "Facts")

typedef enum { SIG_FIXED, SIG_ARITH, SIG_DIV, SIG_CONJ, SIG_ASSOC, SIG_SAME, SIG_DISSOC, SIG_EMPTY } sig_rule;

typedef struct {
	const char *name; // unqualified, in clojure.core
	uint32_t    arity; // 0: any; with at_least, this many or more
	bool        at_least;
	uint8_t     rule;
	uint32_t    result;
	uint32_t    refine; // the argument's type set when the result is truthy, 0 when the entry refines nothing
	uint8_t     refine_null;
	uint8_t     refine_arg;
	bool        exact; // true for exactly `refine`, so the false branch may subtract it
	bool        keeps; // an argument may be stored where it outlives the call
	uint8_t     elem;  // array kind + 1 for the array constructors
} fact_sig;

#define F(nm, ar, res) {nm, ar, false, SIG_FIXED, res, 0, 0, 0, false, false, 0}
#define FK(nm, ar, res) {nm, ar, false, SIG_FIXED, res, 0, 0, 0, false, true, 0}
// The seq arities of a stage whose one-argument call is a transducer.
#define FKN(nm, ar, res) {nm, ar, true, SIG_FIXED, res, 0, 0, 0, false, true, 0}
#define R(nm, ar, rl, res) {nm, ar, false, rl, res, 0, 0, 0, false, true, 0}
// Arithmetic stores nothing, so its arguments do not escape.
#define RA(nm, ar, rl) {nm, ar, false, rl, 0, 0, 0, 0, false, false, 0}
#define P(nm, ref, ex) {nm, 1, false, SIG_FIXED, CLJ_T_BOOL, ref, CLJ_NULL_MAYBE, 0, ex, false, 0}
#define PN(nm, ref, rnul, ex) {nm, 1, false, SIG_FIXED, CLJ_T_BOOL, ref, rnul, 0, ex, false, 0}
#define ARR(nm, k) {nm, 0, false, SIG_FIXED, CLJ_T_ARRAY, 0, 0, 0, false, true, (uint8_t)(k) + 1}

static const fact_sig sigs[] = {
	// arithmetic and comparison
	RA("+", 2, SIG_ARITH), RA("-", 2, SIG_ARITH), RA("*", 2, SIG_ARITH), RA("/", 2, SIG_DIV),
	RA("inc", 1, SIG_ARITH), RA("dec", 1, SIG_ARITH),
	F("<", 2, CLJ_T_BOOL), F("<=", 2, CLJ_T_BOOL), F(">", 2, CLJ_T_BOOL), F(">=", 2, CLJ_T_BOOL),
	F("=", 0, CLJ_T_BOOL), F("not=", 0, CLJ_T_BOOL), F("==", 0, CLJ_T_BOOL), F("identical?", 2, CLJ_T_BOOL),
	F("compare", 2, CLJ_T_FIXNUM), F("hash", 1, CLJ_T_FIXNUM), F("count", 1, CLJ_T_FIXNUM), F("alength", 1, CLJ_T_FIXNUM),
	RA("quot", 2, SIG_ARITH), RA("rem", 2, SIG_ARITH), F("mod", 2, T_NUM), F("min", 0, T_NUM), F("max", 0, T_NUM),
	F("long", 1, T_INT), F("int", 1, T_INT), F("bigint", 1, T_INT), F("double", 1, CLJ_T_DOUBLE), F("float", 1, CLJ_T_DOUBLE),
	F("bit-and", 0, T_INT), F("bit-or", 0, T_INT), F("bit-xor", 0, T_INT), F("bit-not", 1, T_INT),
	F("bit-shift-left", 2, T_INT), F("bit-shift-right", 2, T_INT), F("unsigned-bit-shift-right", 2, T_INT),
	F("parse-long", 1, T_INT | CLJ_T_NIL), F("parse-double", 1, CLJ_T_DOUBLE | CLJ_T_NIL),
	// predicates, with the refinement they carry into the branches of an if
	PN("nil?", CLJ_T_NIL, CLJ_NULL_ALWAYS, true), PN("some?", CLJ_T_TOP & ~CLJ_T_NIL, CLJ_NULL_NEVER, true),
	P("number?", T_NUM, true), P("integer?", T_INT, true), P("int?", CLJ_T_FIXNUM | CLJ_T_LONG, true), P("nat-int?", T_INT, false),
	P("pos-int?", T_INT, false), P("neg-int?", T_INT, false), P("double?", CLJ_T_DOUBLE, true), P("float?", CLJ_T_DOUBLE, true),
	P("ratio?", CLJ_T_RATIO, true), P("decimal?", CLJ_T_DECIMAL, true), P("rational?", T_INT | CLJ_T_RATIO | CLJ_T_DECIMAL, true),
	P("string?", CLJ_T_STRING, true), P("keyword?", CLJ_T_KEYWORD, true), P("symbol?", CLJ_T_SYMBOL, true),
	P("char?", CLJ_T_CHAR, true), P("boolean?", CLJ_T_BOOL, true), P("true?", CLJ_T_BOOL, false), P("false?", CLJ_T_BOOL, false),
	P("map?", T_MAPS, true), P("set?", T_SETS, true), P("vector?", CLJ_T_VECTOR, true), P("record?", CLJ_T_RECORD, true),
	P("list?", CLJ_T_LIST, false), P("seq?", CLJ_T_LIST, false), P("sequential?", CLJ_T_LIST | CLJ_T_VECTOR, false),
	P("coll?", T_COLL, false), P("seqable?", T_SEQABLE, false), P("associative?", T_MAPS | CLJ_T_VECTOR, true),
	P("fn?", CLJ_T_FN, true), P("var?", CLJ_T_VAR, true), P("uuid?", CLJ_T_UUID, true), P("inst?", CLJ_T_INST, true),
	P("ident?", CLJ_T_KEYWORD | CLJ_T_SYMBOL, true), P("simple-ident?", CLJ_T_KEYWORD | CLJ_T_SYMBOL, false),
	P("qualified-ident?", CLJ_T_KEYWORD | CLJ_T_SYMBOL, false), P("qualified-keyword?", CLJ_T_KEYWORD, false),
	P("simple-keyword?", CLJ_T_KEYWORD, false), P("qualified-symbol?", CLJ_T_SYMBOL, false), P("simple-symbol?", CLJ_T_SYMBOL, false),
	F("not", 1, CLJ_T_BOOL), F("zero?", 1, CLJ_T_BOOL), F("pos?", 1, CLJ_T_BOOL), F("neg?", 1, CLJ_T_BOOL),
	F("even?", 1, CLJ_T_BOOL), F("odd?", 1, CLJ_T_BOOL), F("empty?", 1, CLJ_T_BOOL), F("contains?", 2, CLJ_T_BOOL),
	F("ifn?", 1, CLJ_T_BOOL), F("counted?", 1, CLJ_T_BOOL), F("indexed?", 1, CLJ_T_BOOL), F("distinct?", 0, CLJ_T_BOOL),
	F("every?", 2, CLJ_T_BOOL), F("not-every?", 2, CLJ_T_BOOL), F("not-any?", 2, CLJ_T_BOOL), F("boolean", 1, CLJ_T_BOOL),
	F("instance?", 2, CLJ_T_BOOL), F("satisfies?", 2, CLJ_T_BOOL), F("extends?", 2, CLJ_T_BOOL), F("isa?", 0, CLJ_T_BOOL),
	// sequences: every lazy stage answers a seq or nil, the eager ones a seq
	F("seq", 1, CLJ_T_LIST | CLJ_T_NIL), F("next", 1, CLJ_T_LIST | CLJ_T_NIL), F("keys", 1, CLJ_T_LIST | CLJ_T_NIL),
	F("vals", 1, CLJ_T_LIST | CLJ_T_NIL), F("rest", 1, CLJ_T_LIST), FK("cons", 2, CLJ_T_LIST), FK("list", 0, CLJ_T_LIST),
	FK("list*", 0, CLJ_T_LIST | CLJ_T_NIL), FKN("map", 2, CLJ_T_LIST), FK("filter", 2, CLJ_T_LIST), FK("remove", 2, CLJ_T_LIST),
	FK("keep", 2, CLJ_T_LIST), FKN("mapcat", 2, CLJ_T_LIST), FK("concat", 0, CLJ_T_LIST), FK("take", 2, CLJ_T_LIST),
	FK("drop", 2, CLJ_T_LIST), FK("take-while", 2, CLJ_T_LIST), FK("drop-while", 2, CLJ_T_LIST), FK("partition", 0, CLJ_T_LIST),
	FKN("partition-all", 2, CLJ_T_LIST), FK("interpose", 2, CLJ_T_LIST), FK("interleave", 0, CLJ_T_LIST),
	FK("repeat", 0, CLJ_T_LIST), FK("iterate", 2, CLJ_T_LIST), FK("range", 0, CLJ_T_LIST), FK("reverse", 1, CLJ_T_LIST),
	FK("sort", 0, CLJ_T_LIST), FK("sort-by", 0, CLJ_T_LIST), FK("distinct", 1, CLJ_T_LIST), FK("flatten", 1, CLJ_T_LIST),
	FK("map-indexed", 2, CLJ_T_LIST), FK("keep-indexed", 2, CLJ_T_LIST), FK("dedupe", 1, CLJ_T_LIST), FK("shuffle", 1, CLJ_T_VECTOR),
	FK("re-seq", 2, CLJ_T_LIST | CLJ_T_NIL), F("line-seq", 1, CLJ_T_LIST | CLJ_T_NIL),
	// collections
	FK("vector", 0, CLJ_T_VECTOR), FK("vec", 1, CLJ_T_VECTOR), FK("subvec", 0, CLJ_T_VECTOR), FK("mapv", 0, CLJ_T_VECTOR),
	FK("filterv", 2, CLJ_T_VECTOR), FK("hash-map", 0, CLJ_T_MAP), FK("array-map", 0, CLJ_T_MAP), FK("zipmap", 2, CLJ_T_MAP),
	FK("frequencies", 1, CLJ_T_MAP), FK("group-by", 2, CLJ_T_MAP), FK("select-keys", 2, T_MAPS), FK("merge", 0, T_MAPS | CLJ_T_NIL),
	FK("merge-with", 0, T_MAPS | CLJ_T_NIL), FK("hash-set", 0, CLJ_T_SET), FK("set", 1, CLJ_T_SET),
	FK("sorted-map", 0, CLJ_T_SORTED_MAP), FK("sorted-map-by", 0, CLJ_T_SORTED_MAP), FK("sorted-set", 0, CLJ_T_SORTED_SET),
	FK("sorted-set-by", 0, CLJ_T_SORTED_SET), FK("update-keys", 2, T_MAPS), FK("update-vals", 2, T_MAPS),
	R("conj", 0, SIG_CONJ, 0), R("assoc", 0, SIG_ASSOC, 0), R("into", 0, SIG_CONJ, 0), R("with-meta", 2, SIG_SAME, 0),
	R("vary-meta", 0, SIG_SAME, 0), R("dissoc", 0, SIG_DISSOC, 0), R("disj", 0, SIG_SAME, 0), R("empty", 1, SIG_EMPTY, 0),
	FK("assoc-in", 3, T_MAPS | CLJ_T_VECTOR), FK("update", 0, T_MAPS | CLJ_T_VECTOR), FK("update-in", 0, T_MAPS | CLJ_T_VECTOR),
	F("find", 2, CLJ_T_VECTOR | CLJ_T_NIL), F("key", 1, CLJ_T_TOP), F("val", 1, CLJ_T_TOP),
	// strings, names, identifiers
	F("str", 0, CLJ_T_STRING), F("pr-str", 0, CLJ_T_STRING), F("print-str", 0, CLJ_T_STRING), F("println-str", 0, CLJ_T_STRING),
	F("name", 1, CLJ_T_STRING), F("namespace", 1, CLJ_T_STRING | CLJ_T_NIL), F("subs", 0, CLJ_T_STRING),
	F("keyword", 0, CLJ_T_KEYWORD | CLJ_T_NIL), F("symbol", 0, CLJ_T_SYMBOL), F("gensym", 0, CLJ_T_SYMBOL),
	F("re-pattern", 1, CLJ_T_REGEX), F("re-find", 0, CLJ_T_STRING | CLJ_T_VECTOR | CLJ_T_NIL),
	F("re-matches", 2, CLJ_T_STRING | CLJ_T_VECTOR | CLJ_T_NIL), F("random-uuid", 0, CLJ_T_UUID),
	F("char", 1, CLJ_T_CHAR), F("type", 1, CLJ_T_TOP), F("atom", 0, CLJ_T_ATOM), F("var-get", 1, CLJ_T_TOP),
	F("rand", 0, CLJ_T_DOUBLE), F("rand-int", 1, T_INT),
	// arrays
	ARR("byte-array", CLJ_ARRAY_I8), ARR("short-array", CLJ_ARRAY_I16), ARR("int-array", CLJ_ARRAY_I32),
	ARR("long-array", CLJ_ARRAY_I64), ARR("float-array", CLJ_ARRAY_F32), ARR("double-array", CLJ_ARRAY_F64),
	ARR("boolean-array", CLJ_ARRAY_BOOL), ARR("char-array", CLJ_ARRAY_CHAR), ARR("object-array", CLJ_ARRAY_OBJECT),
	F("make-array", 0, CLJ_T_ARRAY), F("aclone", 1, CLJ_T_ARRAY), F("to-array", 1, CLJ_T_ARRAY),
};

enum { NSIGS = sizeof sigs / sizeof *sigs };

// The canonical builtin type names (proto.c); a user type's var root is runtime state, so it refines nothing.
static const struct {
	const char *name;
	uint32_t    types;
	bool        exact;
} type_names[] = {
	{"String", CLJ_T_STRING, true},         {"Keyword", CLJ_T_KEYWORD, true},   {"Symbol", CLJ_T_SYMBOL, true},
	{"Long", T_INT, false},                 {"Integer", T_INT, false},          {"Double", CLJ_T_DOUBLE, true},
	{"Boolean", CLJ_T_BOOL, true},          {"Character", CLJ_T_CHAR, true},    {"PersistentVector", CLJ_T_VECTOR, true},
	{"PersistentHashMap", CLJ_T_MAP, true}, {"PersistentHashSet", CLJ_T_SET, true},
	{"PersistentTreeMap", CLJ_T_SORTED_MAP, true}, {"PersistentTreeSet", CLJ_T_SORTED_SET, true},
	{"PersistentList", CLJ_T_LIST, false},  {"Cons", CLJ_T_LIST, false},        {"EmptyList", CLJ_T_LIST, false},
	{"LazySeq", CLJ_T_LIST, false},         {"Range", CLJ_T_LIST, false},       {"Fn", CLJ_T_FN, true},
	{"Var", CLJ_T_VAR, true},               {"Atom", CLJ_T_ATOM, true},         {"UUID", CLJ_T_UUID, true},
	{"java.util.UUID", CLJ_T_UUID, true},   {"Date", CLJ_T_INST, true},         {"java.util.Date", CLJ_T_INST, true},
	{"Pattern", CLJ_T_REGEX, true},
};

static bool var_in_ns(clj_value var, const char *ns_name) {
	clj_value ns = clj_var_ns(var);
	return clj_is_symbol(ns) && strcmp(clj_string_bytes(clj_symbol_name(ns)), ns_name) == 0;
}

static bool is_core_var(clj_value var) { return var_in_ns(var, "clojure.core"); }

// Named, not walked: under -DCLJ_COMPILED_CORE these bodies are gone, and a park the lint rests on must not
// depend on how the library was built.
static bool is_park_var(clj_value var) {
	static const char *const waits[] = {"<!", ">!", "<!!", ">!!", "alts!", "alts!!"};
	const char              *name = clj_string_bytes(clj_symbol_name(clj_var_name(var)));
	if (var_in_ns(var, "Thread")) return strcmp(name, "sleep") == 0;
	if (!var_in_ns(var, "clojure.core.async")) return false;
	for (size_t i = 0; i < sizeof waits / sizeof *waits; i++) {
		if (strcmp(waits[i], name) == 0) return true;
	}
	return false;
}

// What a var with no summary does: a core builtin by name, a parking primitive, anything otherwise.
static uint32_t unknown_var_effects(clj_value var) {
	if (!clj_is_var(var)) return CLJ_EFFECT_ANY;
	if (is_core_var(var)) return clj_facts_core_effects(clj_string_bytes(clj_symbol_name(clj_var_name(var))));
	return is_park_var(var) ? CLJ_EFFECT_ANY | CLJ_EFFECT_PARK : CLJ_EFFECT_ANY;
}

static const fact_sig *sig_named(const char *name, uint32_t arity) {
	for (uint32_t i = 0; i < NSIGS; i++) {
		if (strcmp(sigs[i].name, name) != 0) continue;
		if (sigs[i].arity == arity || sigs[i].arity == 0 || (sigs[i].at_least && arity >= sigs[i].arity)) return &sigs[i];
	}
	return NULL;
}

// Keyed on the var's own name, never its root: the pass stays pure and speculates (NOTES.md, "Facts").
static const fact_sig *sig_of_var(clj_value var, uint32_t arity) {
	if (!clj_is_var(var) || !is_core_var(var)) return NULL;
	return sig_named(clj_string_bytes(clj_symbol_name(clj_var_name(var))), arity);
}

static const fact_sig *sig_of_intrinsic(const clj_intrinsic *op) {
	return sig_named(strchr(op->name, '/') + 1, op->arity);
}

static uint32_t arith_result(uint32_t a, uint32_t b, bool div) {
	a &= T_NUM;
	b &= T_NUM;
	if (!a) a = T_NUM;
	if (!b) b = T_NUM;
	uint32_t r = 0, ia = a & T_INT, ib = b & T_INT;
	if ((a | b) & CLJ_T_DOUBLE) r |= CLJ_T_DOUBLE;
	if ((a | b) & CLJ_T_DECIMAL) r |= CLJ_T_DECIMAL;
	// A ratio operation normalizes back to an integer when the denominator cancels.
	if ((a | b) & CLJ_T_RATIO) r |= T_INT | CLJ_T_RATIO;
	if (ia && ib) r |= ((ia | ib) & CLJ_T_BIGINT) ? T_INT : (CLJ_T_FIXNUM | CLJ_T_LONG);
	if (div && ia && ib) r |= CLJ_T_RATIO;
	return r ? r : T_NUM;
}

// ---- the walk

// What a truthy value tells about another slot: how (and (map? x) ...) reaches x through the macro's temporary.
typedef struct {
	uint32_t slot; // UINT32_MAX when the value is no predicate over a local
	clj_fact narrow;
	bool     exact; // the false branch may subtract it
} refinement;

typedef struct {
	clj_fact   *slots;
	refinement *pred; // per slot, the refinement its value carries
	clj_fact   *req;  // per slot, what the uses so far require of its value (TOP: nothing)
	uint32_t   *req_line, *req_col; // the use that last narrowed req
	uint32_t    n;
} env;

typedef struct frame_ctx frame_ctx;
struct frame_ctx {
	const clj_fact  *captured;
	uint32_t         ncaptured;
	uint32_t         index;  // into facts->frames
	const frame_ctx *parent; // the defining frame of a direct fn body, what an OUTER read climbs
};

typedef struct {
	clj_fact       *slots; // joined with every recur argument
	const uint32_t *index; // frame slot of each binding
	uint32_t        n;
	bool            changed;
} recur_target;

typedef struct {
	clj_facts    *f;
	frame_ctx    *frame;
	recur_target *recur;
	bool          record;
	uint8_t       dead; // clj_dead: inside a branch a refinement proved unreachable
	uint32_t     *alias_from, *alias_to;
	uint32_t      nalias, calias;
	uint32_t      effects; // of the frame being walked
	uint32_t      caught;  // try bodies with a handler the walk is inside of
	clj_value     def_var; // the var whose def's init is the fn about to be walked, else nil
	bool          head;    // the node being inferred is the head of a call, not a value read
	uint32_t      joined;  // frames whose parameters came from a caller join the walk is inside of
	uint32_t      in_fn;   // fn bodies the walk is inside of
	uint32_t      in_fused; // fused programs the walk is inside of
	bool          summary; // a summary walk: nothing is stored, and a BOTTOM argument makes a call unreachable
	bool          bounded; // a store's walk: cut past CLJ_FACTS_MAX_WALK_DEPTH, the C stack being shared with the asker
	// The def'd arity being walked, whose own sites (same var, same arity, in its own frame) are no recorded sites but
	// the self fixpoint's: joined into self_join when it is set, dropped otherwise (NOTES.md "Facts", the caller join).
	clj_value           self_var;
	const clj_node     *self_fn;
	const clj_fn_arity *self_arity;
	uint32_t            self_depth; // in_fn at that arity's frame
	clj_fact           *self_join;
	bool                self_hit;
} pass;

typedef enum { USE_NONE, USE_CAPTURE, USE_ESCAPE } use_kind;

static clj_fact infer(pass *p, const clj_node *n, env *e, use_kind use);

// One block per environment: an if clones two, so the count of allocations is what the pass's cost is made of.
static size_t env_bytes(uint32_t n) { return (size_t)n * (2 * sizeof(clj_fact) + sizeof(refinement) + 2 * sizeof(uint32_t)); }

static env env_layout(void *block, uint32_t n) {
	env e;
	e.slots = block;
	e.req = e.slots + n;
	e.pred = (refinement *)(e.req + n);
	e.req_line = (uint32_t *)(e.pred + n);
	e.req_col = e.req_line + n;
	e.n = n;
	return e;
}

static env env_new(uint32_t n) {
	env e = env_layout(xalloc(1, env_bytes(n) + 1), n);
	for (uint32_t i = 0; i < n; i++) {
		e.slots[i] = clj_fact_top();
		e.pred[i].slot = UINT32_MAX;
		e.req[i] = clj_fact_top();
	}
	return e;
}

static void env_copy(env *dst, const env *src) { memcpy(dst->slots, src->slots, env_bytes(src->n)); }

static env env_clone(const env *src) {
	void *block = malloc(env_bytes(src->n) + 1);
	if (!block) clj_fatal("out of memory");
	env e = env_layout(block, src->n);
	env_copy(&e, src);
	return e;
}

static void env_free(env *e) { free(e->slots); }

// A branch's requirement holds only on its path: the two are joined, which keeps what the entry already had.
static void env_join_req(env *e, const env *yes, const env *no) {
	if (memcmp(yes->req, no->req, e->n * sizeof(clj_fact)) == 0) {
		memcpy(e->req, yes->req, e->n * sizeof(clj_fact));
		memcpy(e->req_line, yes->req_line, e->n * sizeof(uint32_t));
		memcpy(e->req_col, yes->req_col, e->n * sizeof(uint32_t));
		return;
	}
	for (uint32_t i = 0; i < e->n; i++) {
		clj_fact j = clj_fact_join(yes->req[i], no->req[i]);
		e->req[i] = j;
		bool from_yes = clj_fact_eq(j, yes->req[i]);
		e->req_line[i] = from_yes ? yes->req_line[i] : no->req_line[i];
		e->req_col[i] = from_yes ? yes->req_col[i] : no->req_col[i];
	}
}

// Rebinding a slot retires both its own refinement and every one that was taken over its old value.
static void env_bind(env *e, uint32_t slot, clj_fact f, refinement r) {
	if (slot >= e->n) return;
	e->slots[slot] = f;
	e->pred[slot] = r;
	e->req[slot] = clj_fact_top();
	e->req_line[slot] = e->req_col[slot] = 0;
	for (uint32_t i = 0; i < e->n; i++) {
		if (i != slot && e->pred[i].slot == slot) e->pred[i].slot = UINT32_MAX;
	}
}

// A slot at ⊥ holds no value, so nothing below can run.
static bool env_bottom(const env *e) {
	for (uint32_t i = 0; i < e->n; i++) {
		if (e->slots[i].types == CLJ_T_BOTTOM) return true;
	}
	return false;
}

static void mark_escape_in(pass *p, const frame_ctx *frame, uint32_t slot, clj_escape level) {
	const clj_facts_frame *fr = &p->f->frames[frame->index];
	if (slot >= fr->nslots) return;
	uint8_t *cell = &p->f->escape[fr->slot + slot];
	if (level > *cell) *cell = (uint8_t)level;
}

static void mark_escape(pass *p, uint32_t slot, clj_escape level) { mark_escape_in(p, p->frame, slot, level); }

// A read through the static link is a read of the slot by address: the defining frame's slot is captured.
static void mark_outer(pass *p, uint32_t depth, uint32_t slot) {
	const frame_ctx *frame = p->frame;
	for (uint32_t d = 0; d < depth && frame; d++) frame = frame->parent;
	if (frame) mark_escape_in(p, frame, slot, CLJ_ESCAPE_CAPTURED);
}

// (let [b a] ...): whatever b does to the value, a does too.
static void alias(pass *p, const clj_node *init, uint32_t bound) {
	if (init->kind != CLJ_NODE_LOCAL) return;
	if (p->nalias == p->calias) {
		p->calias = p->calias ? p->calias * 2 : 16;
		p->alias_from = xgrow(p->alias_from, p->calias, sizeof(uint32_t));
		p->alias_to = xgrow(p->alias_to, p->calias, sizeof(uint32_t));
	}
	p->alias_from[p->nalias] = init->u.local.index;
	p->alias_to[p->nalias] = bound;
	p->nalias++;
}

static void resolve_aliases(pass *p, uint32_t base) {
	const clj_facts_frame *fr = &p->f->frames[p->frame->index];
	uint8_t               *esc = &p->f->escape[fr->slot];
	for (bool again = true; again;) {
		again = false;
		for (uint32_t i = base; i < p->nalias; i++) {
			uint32_t from = p->alias_from[i], to = p->alias_to[i];
			if (from >= fr->nslots || to >= fr->nslots) continue;
			if (esc[to] > esc[from]) {
				esc[from] = esc[to];
				again = true;
			}
		}
	}
	p->nalias = base;
}

// The slots one frame needs: the analyzer numbers them per frame, so the highest index plus one covers it.
static void max_slot(const clj_node *n, void *ctx) {
	uint32_t *m = ctx;
#define BUMP(s) do { if ((s) + 1 > *m) *m = (s) + 1; } while (0)
	switch (n->kind) {
	case CLJ_NODE_LOCAL: BUMP(n->u.local.index); return;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP:
		for (uint32_t i = 0; i < n->u.let.n; i++) BUMP(n->u.let.slots[i]);
		break;
	case CLJ_NODE_RECUR:
		for (uint32_t i = 0; i < n->u.recur.n; i++) BUMP(n->u.recur.slots[i]);
		break;
	case CLJ_NODE_TRY:
		for (uint32_t i = 0; i < n->u.try_.ncatches; i++) BUMP(n->u.try_.catches[i].slot);
		break;
	case CLJ_NODE_FN:
		for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
			if (n->u.fn.captures[i].kind == CLJ_CAPTURE_LOCAL) BUMP(n->u.fn.captures[i].index);
		}
		return; // its arities are frames of their own
	case CLJ_NODE_DIRECT_FN: return;
	case CLJ_NODE_FUSED:
		for (uint32_t i = 0; i < n->u.fused.nargs; i++) max_slot(n->u.fused.args[i], ctx);
		return;
	default: break;
	}
	clj_node_children(n, max_slot, ctx);
#undef BUMP
}

static uint32_t push_frame(pass *p, uint32_t owner, uint32_t arity, const clj_node *body, uint32_t nslots) {
	clj_facts *f = p->f;
	f->frames = xgrow(f->frames, f->nframes + 1, sizeof(clj_facts_frame));
	f->escape = xgrow(f->escape, f->nslots + nslots + 1, 1);
	memset(f->escape + f->nslots, 0, nslots + 1);
	clj_facts_frame *fr = &f->frames[f->nframes];
	fr->owner = owner;
	fr->arity = arity;
	fr->first = body->id;
	fr->nnodes = body->nnodes;
	fr->slot = f->nslots;
	fr->nslots = nslots;
	f->nslots += nslots;
	return f->nframes++;
}

static void run_frame(pass *p, uint32_t owner, const clj_fn_arity *a, const clj_node *body, const clj_fact *captured,
                      uint32_t ncaptured, const clj_fact *self, clj_summary *out, const clj_node *fn, clj_value join_var,
                      const clj_fact *params_at);
static void free_scratch(clj_facts *f);

typedef struct {
	clj_value           var;
	const clj_node     *fn;
	const clj_fn_arity *arity;
	bool                found;
} self_scan;

// Whether the arity's own frame calls its var at its own arity; nested fns and fused programs are other frames.
static void scan_self_sites(const clj_node *n, void *ctx) {
	self_scan *sc = ctx;
	if (sc->found) return;
	switch (n->kind) {
	case CLJ_NODE_FN:
	case CLJ_NODE_DIRECT_FN:
	case CLJ_NODE_FUSED: return;
	case CLJ_NODE_INVOKE:
		if (n->u.invoke.fn->kind == CLJ_NODE_VAR && n->u.invoke.fn->u.var == sc->var && clj_facts_arity_for(sc->fn, n->u.invoke.n) == sc->arity)
			sc->found = true;
		break;
	default: break;
	}
	clj_node_children(n, scan_self_sites, ctx);
}

// What the arity's own sites pass with the parameters at params, joined per position into out (BOTTOM where none
// passes): the body walked once more, storing nothing, entering no nested fn. False when no site was reached.
static bool self_sites_join(pass *p, clj_value var, const clj_node *fn, const clj_fn_arity *a, const clj_fact *captured, uint32_t ncaptured,
                            const clj_fact *self, const clj_fact *params, clj_fact *out) {
	clj_facts scratch = {0};
	scratch.conflict_node = UINT32_MAX;
	scratch.sums = p->f->sums;
	pass q = {.f = &scratch, .def_var = CLJ_NIL, .summary = true, .self_var = var, .self_fn = fn, .self_arity = a, .self_join = out};
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED; i++) out[i] = clj_fact_bottom();
	run_frame(&q, fn->id, a, a->body, captured, ncaptured, self, NULL, NULL, CLJ_NIL, params);
	free(q.alias_from);
	free(q.alias_to);
	free_scratch(&scratch);
	return q.self_hit;
}

// The entry's own join: the parameters start at what the recorded (external) callers pass and take in what the
// arity's own sites pass under them, until nothing moves; a position still moving after WIDEN_ROUNDS goes to TOP,
// and every round after that widens what moved, so the loop ends within nparams more (NOTES.md "Facts").
static void self_fixpoint(pass *p, clj_facts_join *j, const clj_node *fn, const clj_fn_arity *a, const clj_fact *captured, uint32_t ncaptured,
                          const clj_fact *self) {
	for (uint32_t round = 0;; round++) {
		clj_fact passed[CLJ_FN_MAX_FIXED + 1];
		j->self_rounds++;
		if (!self_sites_join(p, j->var, fn, a, captured, ncaptured, self, j->params, passed)) return;
		uint64_t moved = 0;
		for (uint32_t i = 0; i < a->nparams; i++) {
			clj_fact next = clj_fact_join(j->params[i], passed[i]);
			if (clj_fact_eq(next, j->params[i])) continue;
			j->params[i] = next;
			moved |= (uint64_t)1 << i;
		}
		if (!moved) return;
		if (round + 1 < WIDEN_ROUNDS) continue;
		for (uint32_t i = 0; i < a->nparams; i++) {
			if ((moved >> i) & 1) j->params[i] = clj_fact_top();
		}
		j->self_widened = true;
	}
}

// The parameters of a def'd fn's arity enter at the join of what every recorded caller passes (summary.h), closed
// over the arity's own sites; true when some position is narrower than TOP, so the frame's conflicts are reported
// against the recorded callers, not proven.
static bool enter_join(pass *p, clj_value var, const clj_node *fn, const clj_fn_arity *a, env *e, const clj_fact *captured, uint32_t ncaptured,
                       const clj_fact *self) {
	clj_facts *f = p->f;
	if (!p->record || !f->sums || !clj_summaries_callers_on(f->sums) || !clj_is_var(var)) return false;
	if (f->njoins == f->cjoins) {
		f->cjoins = f->cjoins ? f->cjoins * 2 : 4;
		f->joins = xgrow(f->joins, f->cjoins, sizeof(clj_facts_join));
	}
	clj_facts_join *j = &f->joins[f->njoins++];
	memset(j, 0, sizeof *j);
	j->var = var;
	j->epoch = clj_var_callers_epoch(var);
	j->fn = fn->id;
	j->arity = a->variadic ? CLJ_FN_MAX_FIXED + 1 : a->nparams;
	j->nparams = a->nparams;
	j->reason = clj_callers_join(var, fn, a, j->callers);
	memcpy(j->params, j->callers, sizeof j->params);
	bool narrowed = false;
	for (uint32_t i = 0; i < a->nparams; i++) narrowed = narrowed || !clj_fact_is_top(j->params[i]);
	if (narrowed) {
		self_scan sc = {var, fn, a, false};
		scan_self_sites(a->body, &sc);
		if (sc.found) self_fixpoint(p, j, fn, a, captured, ncaptured, self);
	}
	narrowed = false;
	for (uint32_t i = 0; i < a->nparams && i < e->n; i++) {
		if (clj_fact_is_top(j->params[i])) {
			if (j->reason == CLJ_JOIN_OK) j->reason = CLJ_JOIN_TOP_ARG;
			continue;
		}
		e->slots[i] = j->params[i];
		narrowed = true;
	}
	return narrowed;
}

// One frame: parameters and free variables are TOP, the body's value leaves the frame; out takes the summary of an arity.
static void run_frame(pass *p, uint32_t owner, const clj_fn_arity *a, const clj_node *body, const clj_fact *captured,
                      uint32_t ncaptured, const clj_fact *self, clj_summary *out, const clj_node *fn, clj_value join_var,
                      const clj_fact *params_at) {
	uint32_t nslots = a ? a->nslots : 0;
	max_slot(body, &nslots);
	uint32_t  fi = push_frame(p, owner, a ? a->nparams : 0, body, nslots);
	env       e = env_new(nslots);
	frame_ctx fc = {captured, ncaptured, fi, p->frame};
	bool      joined = false;
	if (a) {
		// the rest parameter is the seq of the extra arguments, or nil when there are none
		if (a->variadic && a->nparams < nslots) e.slots[a->nparams] = fact_of(CLJ_T_LIST | CLJ_T_NIL);
		if (a->self_slot >= 0 && (uint32_t)a->self_slot < nslots && self) e.slots[a->self_slot] = *self;
		for (uint32_t i = 0; params_at && i < a->nparams && i < nslots; i++) e.slots[i] = params_at[i];
		if (fn) joined = enter_join(p, join_var, fn, a, &e, captured, ncaptured, self);
	}
	frame_ctx    *saved_frame = p->frame;
	recur_target *saved_recur = p->recur;
	uint32_t      base = p->nalias;
	// a def'd arity's own sites are never recorded sites, join or no join: the site list must not depend on the mode
	clj_value           saved_self_var = p->self_var;
	const clj_node     *saved_self_fn = p->self_fn;
	const clj_fn_arity *saved_self_arity = p->self_arity;
	uint32_t            saved_self_depth = p->self_depth;
	if (fn && clj_is_var(join_var)) {
		p->self_var = join_var;
		p->self_fn = fn;
		p->self_arity = a;
		p->self_depth = p->in_fn;
	}
	uint32_t      params[CLJ_FN_MAX_FIXED + 1];
	clj_fact      pf[CLJ_FN_MAX_FIXED + 1];
	uint32_t      np = 0;
	if (a) {
		np = a->nparams + (a->variadic ? 1u : 0u);
		for (uint32_t i = 0; i < np && i < nslots; i++) {
			params[i] = i;
			pf[i] = e.slots[i];
		}
		if (np > nslots) np = nslots;
	}
	recur_target t = {pf, params, np, false};
	uint32_t     effects = p->effects;
	p->frame = &fc;
	p->recur = &t;
	p->effects = 0;
	p->joined += joined;
	clj_fact ret = infer(p, body, &e, USE_ESCAPE);
	p->joined -= joined;
	resolve_aliases(p, base);
	if (out && a) {
		out->nparams = a->nparams;
		out->variadic = a->variadic;
		out->inferred = true;
		out->effects = p->effects;
		out->ret = ret;
		for (uint32_t i = 0; i < np; i++) {
			out->params[i] = e.req[i];
			out->param_line[i] = e.req_line[i];
			out->param_col[i] = e.req_col[i];
		}
	}
	p->frame = saved_frame;
	p->recur = saved_recur;
	p->effects = effects; // a closure body's effects are its own, not the definer's
	p->self_var = saved_self_var;
	p->self_fn = saved_self_fn;
	p->self_arity = saved_self_arity;
	p->self_depth = saved_self_depth;
	env_free(&e);
}

// join_var is the var when the fn is the init of its def: the parameters may enter at the caller join.
static void run_fn(pass *p, const clj_node *n, const clj_fact *captured, uint32_t ncaptured, clj_value join_var) {
	clj_fact self = fact_of(CLJ_T_FN);
	p->in_fn++;
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED + 1; i++) {
		const clj_fn_arity *a = i <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[i] : n->u.fn.variadic;
		if (a) run_frame(p, n->id, a, a->body, captured, ncaptured, &self, NULL, n, join_var, NULL);
	}
	p->in_fn--;
}

// ---- refinement (occurrence typing on predicates, design §3)

// A round of a loop's fixpoint sees the variables before they are widened, so only the recording pass counts.
static uint32_t *conflict_sink(pass *p) { return p->record ? &p->f->conflicts : NULL; }

static void refine_slot(pass *p, env *e, uint32_t slot, clj_fact narrow, bool subtract) {
	if (slot >= e->n) return;
	if (subtract) {
		clj_fact cur = e->slots[slot];
		cur.types &= ~narrow.types;
		// MAYBE is "says nothing about nil", so its complement must subtract nothing
		if (narrow.null != CLJ_NULL_MAYBE) cur.null = (uint8_t)(cur.null & ~narrow.null);
		if (cur.types == CLJ_T_BOTTOM && e->slots[slot].types != CLJ_T_BOTTOM && p->record) p->f->conflicts++;
		e->slots[slot] = clj_fact_cap(cur);
		return;
	}
	e->slots[slot] = clj_fact_meet(e->slots[slot], narrow, conflict_sink(p));
}

static refinement no_refinement(void) {
	refinement r = {UINT32_MAX, clj_fact_top(), false};
	return r;
}

static refinement refinement_at(const clj_node *arg, uint32_t types, clj_null null, bool exact) {
	refinement r = no_refinement();
	if (arg->kind != CLJ_NODE_LOCAL) return r;
	clj_fact narrow = {types, (uint8_t)null, 0, 0, CLJ_UNBOUND, NULL};
	r.slot = arg->u.local.index;
	r.narrow = narrow;
	r.exact = exact;
	return r;
}

// (= x <const>) pins x to that constant when true and says nothing when false.
static refinement refinement_equality(const clj_node *const *args, uint32_t nargs) {
	if (nargs == 2) {
		for (uint32_t i = 0; i < 2; i++) {
			const clj_node *a = args[i], *b = args[1 - i];
			if (a->kind != CLJ_NODE_LOCAL || b->kind != CLJ_NODE_CONST) continue;
			refinement r = {a->u.local.index, clj_fact_of_value(b->u.value), false};
			return r;
		}
	}
	return no_refinement();
}

static refinement refinement_instance(const clj_node *const *args, uint32_t nargs) {
	if (nargs != 2 || args[0]->kind != CLJ_NODE_VAR || !clj_is_var(args[0]->u.var) || !is_core_var(args[0]->u.var))
		return no_refinement();
	const char *name = clj_string_bytes(clj_symbol_name(clj_var_name(args[0]->u.var)));
	for (size_t i = 0; i < sizeof type_names / sizeof *type_names; i++) {
		if (strcmp(type_names[i].name, name) == 0)
			return refinement_at(args[1], type_names[i].types, CLJ_NULL_NEVER, type_names[i].exact);
	}
	return no_refinement();
}

static refinement refinement_call(const fact_sig *s, const clj_node *const *args, uint32_t nargs) {
	if (!s || !s->refine || s->refine_arg >= nargs) return no_refinement();
	return refinement_at(args[s->refine_arg], s->refine, (clj_null)s->refine_null, s->exact);
}

// The refinement a value carries when it is truthy; `negated` flips through a `not`.
static refinement predicate_of(const clj_node *n, bool *negated) {
	*negated = false;
	switch (n->kind) {
	case CLJ_NODE_INTRINSIC: {
		const char *name = strchr(n->u.intrinsic.op->name, '/') + 1;
		if (strcmp(name, "not") == 0 && n->u.intrinsic.n == 1) {
			refinement r = predicate_of(n->u.intrinsic.args[0], negated);
			*negated = !*negated;
			return r;
		}
		if (strcmp(name, "=") == 0 || strcmp(name, "identical?") == 0)
			return refinement_equality(n->u.intrinsic.args, n->u.intrinsic.n);
		return refinement_call(sig_of_intrinsic(n->u.intrinsic.op), n->u.intrinsic.args, n->u.intrinsic.n);
	}
	case CLJ_NODE_INVOKE: {
		if (n->u.invoke.fn->kind != CLJ_NODE_VAR || !clj_is_var(n->u.invoke.fn->u.var) || !is_core_var(n->u.invoke.fn->u.var))
			return no_refinement();
		const char *name = clj_string_bytes(clj_symbol_name(clj_var_name(n->u.invoke.fn->u.var)));
		if (strcmp(name, "not") == 0 && n->u.invoke.n == 1) {
			refinement r = predicate_of(n->u.invoke.args[0], negated);
			*negated = !*negated;
			return r;
		}
		if (strcmp(name, "=") == 0) return refinement_equality(n->u.invoke.args, n->u.invoke.n);
		if (strcmp(name, "instance?") == 0) return refinement_instance(n->u.invoke.args, n->u.invoke.n);
		return refinement_call(sig_of_var(n->u.invoke.fn->u.var, n->u.invoke.n), n->u.invoke.args, n->u.invoke.n);
	}
	default: return no_refinement();
	}
}

static void apply_refinement(pass *p, refinement r, env *yes, env *no) {
	if (r.slot == UINT32_MAX) return;
	refine_slot(p, yes, r.slot, r.narrow, false);
	if (r.exact) refine_slot(p, no, r.slot, r.narrow, true);
}

// What a let binding remembers about its init, so the temporary `and`/`or` expand to carries the predicate on.
static refinement refinement_of_init(const clj_node *init) {
	bool       negated = false;
	refinement r = predicate_of(init, &negated);
	return negated ? no_refinement() : r;
}

static void refine(pass *p, const clj_node *test, env *yes, env *no) {
	if (test->kind == CLJ_NODE_LOCAL) {
		uint32_t slot = test->u.local.index;
		clj_fact truthy = {CLJ_T_TOP & ~(uint32_t)CLJ_T_NIL, CLJ_NULL_NEVER, 0, 0, CLJ_UNBOUND, NULL};
		clj_fact falsy = {CLJ_T_NIL | CLJ_T_BOOL, CLJ_NULL_MAYBE, 0, 0, CLJ_UNBOUND, NULL};
		refine_slot(p, yes, slot, truthy, false);
		refine_slot(p, no, slot, falsy, false);
		if (slot < yes->n) apply_refinement(p, yes->pred[slot], yes, no);
		return;
	}
	bool       negated = false;
	refinement r = predicate_of(test, &negated);
	apply_refinement(p, r, negated ? no : yes, negated ? yes : no);
}

// ---- transfer functions

static clj_fact infer_args(pass *p, const clj_node *const *args, uint32_t n, env *e, const fact_sig *s, clj_fact *first) {
	use_kind use = (s && !s->keeps) ? USE_NONE : USE_ESCAPE;
	clj_fact f0 = clj_fact_top();
	for (uint32_t i = 0; i < n; i++) {
		clj_fact a = infer(p, args[i], e, use);
		if (i == 0) f0 = a;
	}
	if (first) *first = f0;
	return f0;
}

static clj_fact sig_result(const fact_sig *s, const clj_fact *args, uint32_t n) {
	switch (s->rule) {
	case SIG_ARITH:
		return fact_of(arith_result(n > 0 ? args[0].types : CLJ_T_TOP, n > 1 ? args[1].types : CLJ_T_FIXNUM, false));
	case SIG_DIV: return fact_of(arith_result(n > 0 ? args[0].types : CLJ_T_TOP, n > 1 ? args[1].types : CLJ_T_TOP, true));
	case SIG_CONJ: {
		uint32_t a = n > 0 ? args[0].types : CLJ_T_TOP;
		uint32_t r = a & T_COLL;
		if (a & CLJ_T_NIL) r |= CLJ_T_LIST;
		return fact_of(r ? r : T_COLL);
	}
	case SIG_ASSOC: {
		uint32_t a = n > 0 ? args[0].types : CLJ_T_TOP;
		uint32_t r = a & (T_MAPS | CLJ_T_VECTOR);
		if (a & CLJ_T_NIL) r |= CLJ_T_MAP;
		return fact_of(r ? r : (T_MAPS | CLJ_T_VECTOR));
	}
	case SIG_SAME: {
		clj_fact r = n > 0 ? args[0] : clj_fact_top();
		r.singleton = CLJ_UNBOUND;
		return r;
	}
	case SIG_DISSOC: {
		// a record loses its kind with a basis key and keeps it with any other
		uint32_t a = n > 0 ? args[0].types : CLJ_T_TOP;
		return fact_of((a & CLJ_T_RECORD) ? a | CLJ_T_MAP : a);
	}
	case SIG_EMPTY: {
		// nil for whatever is not a collection, a string included
		uint32_t a = n > 0 ? args[0].types : CLJ_T_TOP;
		uint32_t r = a & T_COLL;
		if (a & ~(uint32_t)T_COLL) r |= CLJ_T_NIL;
		return fact_of(r);
	}
	default: break;
	}
	clj_fact r = fact_of(s->result);
	if (s->elem) {
		r.types = CLJ_T_ARRAY;
		r.elem = s->elem;
	}
	return clj_fact_cap(r);
}

// ---- pass 2: summaries at call sites (design §3)

static void add_dep(clj_facts *f, clj_value var) {
	for (uint32_t i = 0; i < f->ndeps; i++) {
		if (f->dep_vars[i] == var) return;
	}
	if (f->ndeps == f->cdeps) {
		f->cdeps = f->cdeps ? f->cdeps * 2 : 16;
		f->dep_vars = xgrow(f->dep_vars, f->cdeps, sizeof(clj_value));
		f->dep_epochs = xgrow(f->dep_epochs, f->cdeps, sizeof(uint32_t));
	}
	f->dep_vars[f->ndeps] = var;
	f->dep_epochs[f->ndeps] = clj_var_epoch(var);
	f->ndeps++;
}

static const clj_summary *summary_of(pass *p, clj_value var, uint32_t nargs) {
	if (!p->f->sums || !clj_is_var(var)) return NULL;
	const clj_summary *s = clj_summary_of_var(p->f->sums, var, nargs);
	add_dep(p->f, var);
	return s;
}

static void add_diagnostic(pass *p, clj_diag_kind kind, const clj_node *arg, uint32_t use_line, uint32_t use_col, clj_value callee,
                           uint32_t i, clj_fact have, clj_fact req) {
	clj_facts *f = p->f;
	if (f->ndiags == f->cdiags) {
		f->cdiags = f->cdiags ? f->cdiags * 2 : 8;
		f->diags = xgrow(f->diags, f->cdiags, sizeof(clj_diagnostic));
	}
	bool caught = p->caught > 0;
	// inside a frame entered at a caller join the argument's fact may rest on the join: a conflict says no recorded
	// call takes this path, which is not a proof that a call throws
	if (kind == CLJ_DIAG_CALL_CONFLICT && p->joined) kind = CLJ_DIAG_CALLERS_CONFLICT;
	clj_diagnostic d = {kind, kind == CLJ_DIAG_CALL_CONFLICT && !caught ? CLJ_DIAG_ERROR : CLJ_DIAG_WARNING,
	                    arg->line, arg->col, use_line, use_col, callee, i, have, req, 0, 0, caught};
	if (d.severity == CLJ_DIAG_ERROR) f->nerrors++;
	f->diags[f->ndiags++] = d;
}

// A conflict is reported and not stored, so a ⊥ node keeps meaning "the lattice is wrong".
static void require_arg(pass *p, env *e, const clj_node *arg, clj_fact have, clj_fact req, uint32_t use_line, uint32_t use_col,
                        clj_value callee, uint32_t i, bool declared) {
	if (clj_fact_is_top(req) || have.types == CLJ_T_BOTTOM) return;
	uint32_t dummy = 0;
	clj_fact m = clj_fact_meet(have, req, &dummy);
	if (m.types == CLJ_T_BOTTOM) {
		if (p->record && !p->dead) add_diagnostic(p, CLJ_DIAG_CALL_CONFLICT, arg, use_line, use_col, callee, i, have, req);
		return;
	}
	// a declaration is an explicit ask to be typed here: TOP at its door is what was asked to be told (design §3)
	if (declared && p->f->warn && p->record && !p->dead && clj_fact_is_top(have))
		add_diagnostic(p, CLJ_DIAG_TOP_INTO_DECL, arg, 0, 0, callee, i, have, req);
	if (p->record && arg->id < p->f->nnodes && !clj_fact_eq(m, have)) {
		uint8_t dead = p->f->nodes[arg->id].unreachable;
		p->f->nodes[arg->id] = m;
		p->f->nodes[arg->id].unreachable = dead;
		p->f->narrowed++;
	}
	if (arg->kind != CLJ_NODE_LOCAL || arg->u.local.index >= e->n) return;
	uint32_t slot = arg->u.local.index;
	e->slots[slot] = m;
	clj_fact rq = clj_fact_meet_wide(e->req[slot], req);
	if (rq.types != CLJ_T_BOTTOM && !clj_fact_eq(rq, e->req[slot])) {
		e->req[slot] = rq;
		e->req_line[slot] = arg->line;
		e->req_col[slot] = arg->col;
	}
}

// What a function passed as an argument does, OPAQUE where the walk cannot reach a body (design §4).
static uint32_t arg_effects(pass *p, const clj_node *a, uint32_t nargs) {
	if (!p->f->sums) return CLJ_EFFECT_OPAQUE;
	if (a->kind == CLJ_NODE_FN) {
		uint32_t r = 0;
		for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED + 1; i++) {
			const clj_fn_arity *ar = i <= CLJ_FN_MAX_FIXED ? a->u.fn.fixed[i] : a->u.fn.variadic;
			if (!ar) continue;
			const clj_summary *s = clj_summary_of_arity(p->f->sums, a, ar);
			r |= s ? s->effects : CLJ_EFFECT_OPAQUE;
		}
		return r;
	}
	if (a->kind == CLJ_NODE_VAR && clj_is_var(a->u.var)) {
		const clj_summary *s = summary_of(p, a->u.var, nargs);
		return s ? s->effects : unknown_var_effects(a->u.var);
	}
	return CLJ_EFFECT_OPAQUE;
}

// The ladder of design §4, by whether the effect is impossible here or only bad practice.
static void require_effects(pass *p, const clj_node *arg, const clj_effects_req *req, clj_value callee, uint32_t i) {
	if (req->allowed == CLJ_EFFECTS_FREE || !p->record || p->dead) return;
	uint32_t forbidden = ~req->allowed & CLJ_EFFECTS_CHECKED;
	if (!forbidden) return;
	uint32_t have = arg_effects(p, arg, req->nargs);
	bool     proven = (have & forbidden) != 0;
	// against a lint ⊤ would fire on every higher-order call: design §3 keeps it silent there
	if (!proven && !(req->strict && (have & CLJ_EFFECT_OPAQUE))) return;
	bool error = proven && req->strict;
	if (!error && !p->f->warn) return;
	add_diagnostic(p, CLJ_DIAG_EFFECTS, arg, 0, 0, callee, i, clj_fact_top(), clj_fact_top());
	clj_diagnostic *d = &p->f->diags[p->f->ndiags - 1];
	d->effects = have;
	d->effects_allowed = req->allowed;
	if (error) {
		d->severity = CLJ_DIAG_ERROR;
		p->f->nerrors++;
	}
}

static void apply_summary(pass *p, env *e, const clj_summary *sum, const clj_node *const *args, uint32_t n, const clj_fact *have,
                          clj_value callee) {
	if (!sum) return;
	if (p->record) p->f->hits++;
	p->effects |= sum->effects;
	// the rest parameter's requirement is on the seq, not on one element
	for (uint32_t i = 0; i < n && i < sum->nparams; i++) {
		bool declared = sum->annotated && sum->param_line[i] == 0;
		require_arg(p, e, args[i], have[i], sum->params[i], sum->param_line[i], sum->param_col[i], callee, i, declared);
		require_effects(p, args[i], &sum->param_effects[i], callee, i);
	}
}

static clj_fact result_with_summary(const pass *p, clj_fact r, const clj_summary *sum) {
	if (!sum) return r;
	// the optimistic start of a fixpoint in flight; a finished summary never answers BOTTOM (summary.c)
	if (sum->ret.types == CLJ_T_BOTTOM) return p->record ? r : sum->ret;
	uint32_t dummy = 0;
	clj_fact m = clj_fact_meet(r, sum->ret, &dummy);
	// a signature and a summary that disagree is a bug in one of them; the signature has the differential test behind it
	return m.types == CLJ_T_BOTTOM ? r : m;
}

// (new* T ...) and (record-map* T m): the constructor bodies defrecord and deftype expand to.
static clj_fact construct_result(pass *p, const char *name, const clj_node *const *args, uint32_t n) {
	if (!p->f->sums || n < 1 || args[0]->kind != CLJ_NODE_VAR || !clj_is_var(args[0]->u.var)) return clj_fact_top();
	if (strcmp(name, "new*") != 0 && strcmp(name, "record-map*") != 0) return clj_fact_top();
	clj_value root = clj_var_root(args[0]->u.var);
	add_dep(p->f, args[0]->u.var);
	if (root == CLJ_UNBOUND || !clj_is_user_type(root)) return clj_fact_top();
	const clj_type *t = (const clj_type *)clj_to_ptr(root);
	clj_fact        r = fact_of((t->core_bits & CLJ_CORE_RECORD) ? CLJ_T_RECORD : CLJ_T_HOST);
	r.desc = t;
	return r;
}

#define ARGS_INLINE 8

uint32_t clj_facts_nsignatures(void) { return NSIGS; }

bool clj_facts_signature(uint32_t i, const char **name, uint32_t *arity, clj_fact *result, bool *transfer) {
	if (i >= NSIGS) return false;
	const fact_sig *s = &sigs[i];
	*name = s->name;
	*arity = s->arity;
	*transfer = s->rule != SIG_FIXED;
	*result = *transfer ? clj_fact_top() : sig_result(s, NULL, 0);
	return true;
}

// A call of the def'd arity being walked from its own frame: what it passes rests on the parameters, so it enters the
// entry's own join (the self fixpoint) rather than the index. A fused program's sites stay recorded: the fixpoint's
// walk does not enter one.
static bool self_site(const pass *p, clj_value var, uint32_t n) {
	if (!clj_is_var(p->self_var) || var != p->self_var || p->in_fn != p->self_depth || p->in_fused) return false;
	return clj_facts_arity_for(p->self_fn, n) == p->self_arity;
}

// What a join keeps of a passed fact: the kinds and the nullability, never a borrowed constant or a descriptor.
static clj_fact site_fact(clj_fact f) {
	f.singleton = CLJ_UNBOUND;
	f.desc = NULL;
	f.elem = 0;
	f.unreachable = 0;
	return f;
}

// What the call passes, before the callee's requirement narrows it: a site of the reverse index (summary.h).
static void record_site(pass *p, const clj_node *site, clj_value var, uint32_t n, const clj_fact *have) {
	clj_facts *f = p->f;
	if (!p->record || !clj_is_var(var)) return;
	if (f->nsites == f->csites) {
		f->csites = f->csites ? f->csites * 2 : 16;
		f->sites = xgrow(f->sites, f->csites, sizeof(facts_site));
	}
	facts_site *st = &f->sites[f->nsites++];
	st->node = site->id;
	st->var = var;
	st->nargs = n;
	st->in_fn = p->in_fn > 0;
	st->args = xalloc(n, sizeof(clj_fact));
	memcpy(st->args, have, n * sizeof(clj_fact));
}

static void record_value_read(pass *p, clj_value var) {
	clj_facts *f = p->f;
	if (!p->record || !clj_is_var(var)) return;
	bool in_fn = p->in_fn > 0;
	for (uint32_t i = 0; i < f->nvalue_reads; i++) {
		if (f->value_reads[i].var == var && f->value_reads[i].in_fn == in_fn) return;
	}
	if (f->nvalue_reads == f->cvalue_reads) {
		f->cvalue_reads = f->cvalue_reads ? f->cvalue_reads * 2 : 8;
		f->value_reads = xgrow(f->value_reads, f->cvalue_reads, sizeof(facts_read));
	}
	f->value_reads[f->nvalue_reads++] = (facts_read){var, in_fn};
}

// The callee's result over these arguments: its body walked with the parameters at the arguments' numeric domains
// (summary.h), asked only when some argument has one; the requirements stay the generic entry's.
static const clj_summary *specialized_of(pass *p, clj_value var, uint32_t n, const clj_fact *have, const clj_summary *sum) {
	if (!sum || !sum->inferred || n == 0 || n > CLJ_SUMMARY_DOMAINS_MAX) return NULL;
	clj_domain domains[CLJ_SUMMARY_DOMAINS_MAX];
	bool       any = false;
	for (uint32_t i = 0; i < n; i++) {
		domains[i] = clj_domain_of(have[i]);
		any = any || domains[i] != CLJ_DOMAIN_ANY;
	}
	return any ? clj_summary_of_var_at(p->f->sums, var, n, domains) : NULL;
}

static clj_fact infer_call(pass *p, const clj_node *site, const clj_node *const *args, uint32_t n, env *e, const fact_sig *s, clj_value var) {
	clj_fact  inline_have[ARGS_INLINE];
	clj_fact *have = n <= ARGS_INLINE ? inline_have : xalloc(n, sizeof(clj_fact));
	use_kind  use = (s && !s->keeps) ? USE_NONE : USE_ESCAPE;
	for (uint32_t i = 0; i < n; i++) have[i] = infer(p, args[i], e, use);
	// the optimistic value of a fixpoint in flight, or a branch a refinement killed: the call is not reached, so its
	// result joins nothing, and a recursive numeric fn climbs from its base case instead of from "a number"
	for (uint32_t i = 0; p->summary && i < n; i++) {
		if (have[i].types == CLJ_T_BOTTOM) {
			if (have != inline_have) free(have);
			return clj_fact_bottom();
		}
	}
	if (self_site(p, var, n)) {
		for (uint32_t i = 0; p->self_join && i < n && i <= CLJ_FN_MAX_FIXED; i++) p->self_join[i] = clj_fact_join(p->self_join[i], site_fact(have[i]));
		p->self_hit = true;
	}
	else record_site(p, site, var, n, have);
	const char *name = clj_is_var(var) && is_core_var(var) ? clj_string_bytes(clj_symbol_name(clj_var_name(var))) : NULL;
	const clj_summary *sum = summary_of(p, var, n);
	if (!sum) {
		uint32_t eff = unknown_var_effects(var);
		// @atom never parks, @future and @promise do: the fact is the argument's, not the symbol's (design §4)
		if (name && n >= 1 && have[0].types == CLJ_T_ATOM && strcmp(name, "deref") == 0) eff &= ~(uint32_t)CLJ_EFFECT_OPAQUE;
		p->effects |= eff;
	}
	apply_summary(p, e, sum, args, n, have, var);
	clj_fact r = s ? sig_result(s, have, n < 4 ? n : 4) : (name ? construct_result(p, name, args, n) : clj_fact_top());
	const clj_summary *spec = specialized_of(p, var, n, have, sum);
	if (have != inline_have) free(have);
	return result_with_summary(p, r, spec ? spec : sum);
}

static clj_fact infer_loop(pass *p, const clj_node *n, env *e, use_kind use) {
	uint32_t  nb = n->u.let.n;
	clj_fact *vars = xalloc(nb, sizeof(clj_fact));
	for (uint32_t i = 0; i < nb; i++) {
		vars[i] = infer(p, n->u.let.inits[i], e, USE_NONE);
		alias(p, n->u.let.inits[i], n->u.let.slots[i]);
	}
	recur_target  t = {vars, n->u.let.slots, nb, false};
	recur_target *saved = p->recur;
	bool          rec = p->record;
	env           scratch = env_clone(e);
	p->recur = &t;
	p->record = false;
	for (uint32_t round = 0;; round++) {
		t.changed = false;
		env_copy(&scratch, e);
		for (uint32_t i = 0; i < nb; i++) env_bind(&scratch, n->u.let.slots[i], vars[i], no_refinement());
		infer(p, n->u.let.body, &scratch, use);
		if (!t.changed) break;
		if (round + 1 >= WIDEN_ROUNDS) {
			for (uint32_t i = 0; i < nb; i++) {
				if (!clj_fact_is_top(vars[i])) {
					vars[i] = clj_fact_top();
					if (rec) p->f->widenings++;
				}
			}
		}
	}
	p->record = rec;
	env_copy(&scratch, e);
	for (uint32_t i = 0; i < nb; i++) env_bind(&scratch, n->u.let.slots[i], vars[i], no_refinement());
	clj_fact r = infer(p, n->u.let.body, &scratch, use);
	// The body ran at least once, so what it required of the outer slots on its way to the loop's exit holds.
	memcpy(e->req, scratch.req, e->n * sizeof(clj_fact));
	memcpy(e->req_line, scratch.req_line, e->n * sizeof(uint32_t));
	memcpy(e->req_col, scratch.req_col, e->n * sizeof(uint32_t));
	for (uint32_t i = 0; i < nb; i++) {
		if (n->u.let.slots[i] < e->n) {
			e->req[n->u.let.slots[i]] = clj_fact_top();
		}
	}
	p->recur = saved;
	if (rec) {
		clj_facts *f = p->f;
		f->loops = xgrow(f->loops, f->nloops + 1, sizeof(clj_facts_loop));
		f->vars = xgrow(f->vars, f->nvars + nb + 1, sizeof(clj_fact));
		f->loops[f->nloops] = (clj_facts_loop){n->id, f->nvars, nb};
		memcpy(f->vars + f->nvars, vars, nb * sizeof(clj_fact));
		f->nvars += nb;
		f->nloops++;
	}
	env_free(&scratch);
	free(vars);
	return r;
}

static clj_fact infer_try(pass *p, const clj_node *n, env *e, use_kind use) {
	// No recur leaves a try body and no binding made inside is in scope in a handler: the entry environment holds.
	env entry = env_clone(e);
	if (n->u.try_.ncatches) p->caught++;
	clj_fact r = infer(p, n->u.try_.body, e, use);
	if (n->u.try_.ncatches) p->caught--;
	for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
		const clj_catch *k = &n->u.try_.catches[i];
		env              ce = env_clone(&entry);
		env_bind(&ce, k->slot, clj_fact_top(), no_refinement());
		r = clj_fact_join(r, infer(p, k->handler, &ce, use));
		env_free(&ce);
	}
	if (n->u.try_.finally_) infer(p, n->u.try_.finally_, e, USE_NONE);
	// a throw skips the rest of the body, so nothing it required is required
	memcpy(e->req, entry.req, e->n * sizeof(clj_fact));
	memcpy(e->req_line, entry.req_line, e->n * sizeof(uint32_t));
	memcpy(e->req_col, entry.req_col, e->n * sizeof(uint32_t));
	env_free(&entry);
	return r;
}

static clj_fact infer_fused(pass *p, const clj_node *n, env *e) {
	clj_fact *args = xalloc(n->u.fused.nargs + 1, sizeof(clj_fact));
	for (uint32_t i = 0; i < n->u.fused.nargs; i++) args[i] = infer(p, n->u.fused.args[i], e, USE_ESCAPE);
	clj_fact r = clj_fact_top();
	if (p->record) {
		uint32_t nslots = n->u.fused.nargs;
		max_slot(n->u.fused.fused, &nslots);
		max_slot(n->u.fused.original, &nslots);
		uint32_t  fi = push_frame(p, n->id, n->u.fused.nargs, n->u.fused.fused, nslots);
		env       fe = env_new(nslots);
		frame_ctx fc = {NULL, 0, fi, p->frame};
		for (uint32_t i = 0; i < n->u.fused.nargs && i < nslots; i++) fe.slots[i] = args[i];
		frame_ctx    *saved_frame = p->frame;
		recur_target *saved_recur = p->recur;
		uint32_t      base = p->nalias;
		p->frame = &fc;
		p->recur = NULL;
		env      copy = env_clone(&fe);
		p->in_fused++;
		clj_fact a = infer(p, n->u.fused.fused, &fe, USE_ESCAPE);
		clj_fact b = infer(p, n->u.fused.original, &copy, USE_ESCAPE);
		p->in_fused--;
		r = clj_fact_join(a, b);
		resolve_aliases(p, base);
		p->frame = saved_frame;
		p->recur = saved_recur;
		env_free(&copy);
		env_free(&fe);
	}
	free(args);
	return r;
}

// Nested summary walks share the C stack with the walk that asked, and a sanitizer build's frames are large: a
// summary walk stops descending past this depth and answers TOP for the subtree, the recording walk never does.
static _Thread_local uint32_t walk_depth;

uint32_t clj_facts_walk_depth(void) { return walk_depth; }

static clj_fact infer_node(pass *p, const clj_node *n, env *e, use_kind use);

static clj_fact infer(pass *p, const clj_node *n, env *e, use_kind use) {
	if (p->bounded && walk_depth >= CLJ_FACTS_MAX_WALK_DEPTH) {
		p->effects |= CLJ_EFFECT_ANY;
		return clj_fact_top();
	}
	walk_depth++;
	clj_fact r = infer_node(p, n, e, use);
	walk_depth--;
	return r;
}

typedef struct {
	clj_value *vars;
	uint32_t  *nargs;
	uint32_t   n, cap;
} call_list;

static void collect_calls(const clj_node *n, void *ctx) {
	call_list *l = ctx;
	clj_value  var = CLJ_NIL;
	uint32_t   nargs = 0;
	if (n->kind == CLJ_NODE_INVOKE && n->u.invoke.fn->kind == CLJ_NODE_VAR) {
		var = n->u.invoke.fn->u.var;
		nargs = n->u.invoke.n;
	}
	else if (n->kind == CLJ_NODE_INTRINSIC) {
		var = n->u.intrinsic.var;
		nargs = n->u.intrinsic.n;
	}
	if (clj_is_var(var)) {
		if (l->n == l->cap) {
			l->cap = l->cap ? l->cap * 2 : 64;
			l->vars = xgrow(l->vars, l->cap, sizeof(clj_value));
			l->nargs = xgrow(l->nargs, l->cap, sizeof(uint32_t));
		}
		l->vars[l->n] = var;
		l->nargs[l->n] = nargs;
		l->n++;
	}
	clj_node_children(n, collect_calls, ctx);
}

// Every var the tree calls is summarized at the top of the stack, so the walk below mostly hits the cache.
static void warm_summaries(const clj_node *root, clj_summaries *sums) {
	call_list l = {NULL, NULL, 0, 0};
	collect_calls(root, &l);
	for (uint32_t i = 0; i < l.n; i++) clj_summary_of_var(sums, l.vars[i], l.nargs[i]);
	free(l.vars);
	free(l.nargs);
}

// A fact of one value: a singleton, or exactly nil (a var whose root is nil reads as nil without a singleton).
static bool pinned(clj_fact f) { return f.singleton != CLJ_UNBOUND || f.types == CLJ_T_NIL; }

// Whether the test decides on a pinned value: the slot it reads or refines (through the predicate a let init
// carried) is pinned, or the test is `(= x <const>)`. A branch such a test kills is dead by a literal.
static bool literal_test(const clj_node *test, const env *e) {
	bool       negated = false;
	refinement r = test->kind == CLJ_NODE_LOCAL ? no_refinement() : predicate_of(test, &negated);
	uint32_t   slots[2] = {test->kind == CLJ_NODE_LOCAL ? test->u.local.index : r.slot, UINT32_MAX};
	if (slots[0] < e->n && test->kind == CLJ_NODE_LOCAL) slots[1] = e->pred[slots[0]].slot;
	if (r.slot != UINT32_MAX && r.narrow.singleton != CLJ_UNBOUND) return true;
	for (uint32_t i = 0; i < 2; i++) {
		if (slots[i] < e->n && pinned(e->slots[slots[i]])) return true;
	}
	return false;
}

static clj_fact infer_if(pass *p, const clj_node *n, env *e, use_kind use) {
	infer(p, n->u.if_.test, e, USE_NONE);
	env      yes = env_clone(e), no = env_clone(e);
	uint32_t before = p->f->conflicts;
	uint8_t  cause = literal_test(n->u.if_.test, e) ? CLJ_DEAD_LITERAL : CLJ_DEAD_REFINED;
	refine(p, n->u.if_.test, &yes, &no);
	if (p->f->conflicts > before && p->f->conflict_node == UINT32_MAX) p->f->conflict_node = n->u.if_.test->id;
	uint8_t  outer = p->dead;
	p->dead = outer ? outer : env_bottom(&yes) ? cause : CLJ_DEAD_NONE;
	clj_fact a = infer(p, n->u.if_.then, &yes, use);
	p->dead = outer ? outer : env_bottom(&no) ? cause : CLJ_DEAD_NONE;
	clj_fact b = n->u.if_.else_ ? infer(p, n->u.if_.else_, &no, use) : fact_of(CLJ_T_NIL);
	p->dead = outer;
	for (uint32_t i = 0; i < e->n; i++) e->slots[i] = clj_fact_join(yes.slots[i], no.slots[i]);
	env_join_req(e, &yes, &no);
	env_free(&yes);
	env_free(&no);
	return clj_fact_join(a, b);
}

static clj_fact infer_fn(pass *p, const clj_node *n, env *e) {
	clj_fact *caps = xalloc(n->u.fn.ncaptures + 1, sizeof(clj_fact));
	for (uint32_t i = 0; i < n->u.fn.ncaptures; i++) {
		const clj_capture *c = &n->u.fn.captures[i];
		switch (c->kind) {
		case CLJ_CAPTURE_LOCAL:
			mark_escape(p, c->index, CLJ_ESCAPE_CAPTURED);
			caps[i] = c->index < e->n ? e->slots[c->index] : clj_fact_top();
			break;
		case CLJ_CAPTURE_CAPTURED:
			caps[i] = p->frame->captured && c->index < p->frame->ncaptured ? p->frame->captured[c->index] : clj_fact_top();
			break;
		default:
			mark_outer(p, c->depth, c->index);
			caps[i] = clj_fact_top();
		}
	}
	clj_value join_var = p->def_var;
	p->def_var = CLJ_NIL; // the fns nested in the body are not the def's
	if (p->record) run_fn(p, n, caps, n->u.fn.ncaptures, join_var);
	free(caps);
	p->effects |= CLJ_EFFECT_ALLOC;
	return fact_of(CLJ_T_FN);
}

static clj_fact infer_invoke(pass *p, const clj_node *n, env *e) {
	const clj_node *head = n->u.invoke.fn;
	p->head = head->kind == CLJ_NODE_VAR;
	infer(p, head, e, USE_NONE);
	p->head = false;
	if (head->kind == CLJ_NODE_VAR) return infer_call(p, n, n->u.invoke.args, n->u.invoke.n, e, sig_of_var(head->u.var, n->u.invoke.n), head->u.var);
	if (head->kind == CLJ_NODE_CONST && clj_is_keyword(head->u.value)) {
		// (:k m) answers nil rather than throwing on anything, so it requires nothing (design §3)
		infer_args(p, n->u.invoke.args, n->u.invoke.n, e, NULL, NULL);
		return clj_fact_top();
	}
	return infer_call(p, n, n->u.invoke.args, n->u.invoke.n, e, NULL, CLJ_NIL);
}

static clj_fact infer_direct_call(pass *p, const clj_node *n, env *e) {
	clj_fact  inline_have[ARGS_INLINE];
	clj_fact *have = n->u.direct.n <= ARGS_INLINE ? inline_have : xalloc(n->u.direct.n, sizeof(clj_fact));
	for (uint32_t i = 0; i < n->u.direct.n; i++) have[i] = infer(p, n->u.direct.args[i], e, USE_ESCAPE);
	const clj_summary *sum = p->f->sums ? clj_summary_of_arity(p->f->sums, n->u.direct.fn, n->u.direct.arity) : NULL;
	if (!sum) p->effects |= CLJ_EFFECT_ANY;
	apply_summary(p, e, sum, n->u.direct.args, n->u.direct.n, have, CLJ_NIL);
	if (have != inline_have) free(have);
	return result_with_summary(p, clj_fact_top(), sum);
}

static clj_fact infer_node(pass *p, const clj_node *n, env *e, use_kind use) {
	clj_fact r;
	switch (n->kind) {
	case CLJ_NODE_CONST:
		r = clj_fact_of_value(n->u.value);
		if (clj_is_var(n->u.value)) record_value_read(p, n->u.value);
		break;
	case CLJ_NODE_LOCAL: {
		uint32_t i = n->u.local.index;
		if (use == USE_ESCAPE) mark_escape(p, i, CLJ_ESCAPE_ESCAPES);
		r = i < e->n ? e->slots[i] : clj_fact_top();
		break;
	}
	case CLJ_NODE_CAPTURED:
		r = p->frame->captured && n->u.index < p->frame->ncaptured ? p->frame->captured[n->u.index] : clj_fact_top();
		break;
	case CLJ_NODE_OUTER:
		mark_outer(p, n->u.outer.depth, n->u.outer.index);
		r = clj_fact_top();
		break;
	case CLJ_NODE_VAR:
		// pass 1 alone reads no root; with a store the read is guarded by the var's epoch (clj_facts_valid)
		r = clj_fact_top();
		if (!p->head) record_value_read(p, n->u.var);
		if (p->f->sums && clj_is_var(n->u.var)) {
			r = clj_summary_var_fact(p->f->sums, n->u.var);
			add_dep(p->f, n->u.var);
		}
		break;
	case CLJ_NODE_IF: r = infer_if(p, n, e, use); break;
	case CLJ_NODE_DO: {
		uint32_t last = n->u.seq.n - 1;
		for (uint32_t i = 0; i < last; i++) infer(p, n->u.seq.items[i], e, USE_NONE);
		r = infer(p, n->u.seq.items[last], e, use);
		break;
	}
	case CLJ_NODE_LET: {
		for (uint32_t i = 0; i < n->u.let.n; i++) {
			clj_fact f = infer(p, n->u.let.inits[i], e, USE_NONE);
			alias(p, n->u.let.inits[i], n->u.let.slots[i]);
			env_bind(e, n->u.let.slots[i], f, refinement_of_init(n->u.let.inits[i]));
		}
		r = infer(p, n->u.let.body, e, use);
		break;
	}
	case CLJ_NODE_LOOP: r = infer_loop(p, n, e, use); break;
	case CLJ_NODE_RECUR: {
		for (uint32_t i = 0; i < n->u.recur.n; i++) {
			clj_fact a = infer(p, n->u.recur.args[i], e, USE_NONE);
			alias(p, n->u.recur.args[i], n->u.recur.slots[i]);
			if (!p->recur || i >= p->recur->n) continue;
			clj_fact j = clj_fact_join(p->recur->slots[i], a);
			if (!clj_fact_eq(j, p->recur->slots[i])) {
				p->recur->slots[i] = j;
				p->recur->changed = true;
			}
		}
		r = clj_fact_bottom();
		break;
	}
	case CLJ_NODE_FN: r = infer_fn(p, n, e); break;
	case CLJ_NODE_DIRECT_FN:
		// its slot holds nil at run time and nothing reads it as a value; the node still denotes the function
		if (p->record) run_fn(p, n, p->frame->captured, p->frame->ncaptured, CLJ_NIL);
		r = fact_of(CLJ_T_FN);
		break;
	case CLJ_NODE_INVOKE: r = infer_invoke(p, n, e); break;
	case CLJ_NODE_DIRECT_CALL: r = infer_direct_call(p, n, e); break;
	case CLJ_NODE_INTRINSIC:
		r = infer_call(p, n, n->u.intrinsic.args, n->u.intrinsic.n, e, sig_of_intrinsic(n->u.intrinsic.op), n->u.intrinsic.var);
		break;
	case CLJ_NODE_DEF:
		if (n->u.def.init) {
			// a dynamic var may be bound to anything: its fn's parameters take no caller join
			if (n->u.def.init->kind == CLJ_NODE_FN && !n->u.def.dynamic) p->def_var = n->u.def.var;
			infer(p, n->u.def.init, e, USE_ESCAPE);
			p->def_var = CLJ_NIL;
		}
		if (n->u.def.meta) infer(p, n->u.def.meta, e, USE_ESCAPE);
		p->effects |= CLJ_EFFECT_ANY; // registration: a root bind moves epochs
		r = fact_of(CLJ_T_VAR);
		break;
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET:
		for (uint32_t i = 0; i < n->u.seq.n; i++) infer(p, n->u.seq.items[i], e, USE_ESCAPE);
		p->effects |= CLJ_EFFECT_ALLOC;
		r = fact_of(n->kind == CLJ_NODE_VECTOR ? CLJ_T_VECTOR : n->kind == CLJ_NODE_MAP ? CLJ_T_MAP : CLJ_T_SET);
		break;
	case CLJ_NODE_TRY: r = infer_try(p, n, e, use); break;
	case CLJ_NODE_THROW:
		infer(p, n->u.throw_, e, USE_ESCAPE);
		p->effects |= CLJ_EFFECT_THROW;
		r = clj_fact_bottom();
		break;
	case CLJ_NODE_FUSED:
		p->effects |= CLJ_EFFECT_ANY;
		r = infer_fused(p, n, e);
		break;
	default: clj_fatal("unknown node kind");
	}
	if (p->record && n->id < p->f->nnodes) {
		r.unreachable = p->dead;
		p->f->nodes[n->id] = r;
	}
	return r;
}

// ---- the table

clj_facts *clj_facts_of(const clj_node *root) {
	clj_facts *f = xalloc(1, sizeof *f);
	f->root = (const clj_node *)clj_to_ptr(clj_retain(clj_from_ptr((void *)root)));
	f->nnodes = root->nnodes;
	f->conflict_node = UINT32_MAX;
	f->nodes = xalloc(f->nnodes ? f->nnodes : 1, sizeof(clj_fact));
	for (uint32_t i = 0; i < f->nnodes; i++) f->nodes[i] = clj_fact_top();
	pass p = {.f = f, .record = true, .def_var = CLJ_NIL, .self_var = CLJ_NIL};
	run_frame(&p, UINT32_MAX, NULL, root, NULL, 0, NULL, NULL, NULL, CLJ_NIL, NULL);
	free(p.alias_from);
	free(p.alias_to);
	return f;
}

clj_facts *clj_facts_of_with(const clj_node *root, clj_summaries *sums) {
	clj_facts *f = xalloc(1, sizeof *f);
	f->root = (const clj_node *)clj_to_ptr(clj_retain(clj_from_ptr((void *)root)));
	f->nnodes = root->nnodes;
	f->conflict_node = UINT32_MAX;
	f->sums = sums;
	f->warn = clj_facts_warnings_enabled(clj_ns_current());
	f->nodes = xalloc(f->nnodes ? f->nnodes : 1, sizeof(clj_fact));
	for (uint32_t i = 0; i < f->nnodes; i++) f->nodes[i] = clj_fact_top();
	pass p = {.f = f, .record = true, .def_var = CLJ_NIL, .self_var = CLJ_NIL};
	if (sums) warm_summaries(root, sums);
	run_frame(&p, UINT32_MAX, NULL, root, NULL, 0, NULL, NULL, NULL, CLJ_NIL, NULL);
	free(p.alias_from);
	free(p.alias_to);
	if (sums) clj_summaries_forget_arities(sums);
	return f;
}

static void free_sites(clj_facts *f) {
	for (uint32_t i = 0; i < f->nsites; i++) free(f->sites[i].args);
	free(f->sites);
	free(f->value_reads);
	free(f->joins);
}

// A table no consumer sees, walked for its side answers alone (a summary, the self fixpoint): everything but nodes.
static void free_scratch(clj_facts *f) {
	free(f->frames);
	free(f->escape);
	free(f->loops);
	free(f->vars);
	free(f->diags);
	free(f->dep_vars);
	free(f->dep_epochs);
	free_sites(f);
}

void clj_facts_walk_arity(const clj_node *fn, const clj_fn_arity *a, clj_summaries *sums, clj_summary *out, const clj_fact *params) {
	clj_facts f = {0};
	f.conflict_node = UINT32_MAX;
	f.sums = sums;
	pass     p = {.f = &f, .def_var = CLJ_NIL, .summary = true, .bounded = true, .self_var = CLJ_NIL};
	clj_fact self = fact_of(CLJ_T_FN);
	run_frame(&p, fn->id, a, a->body, NULL, 0, &self, out, NULL, CLJ_NIL, params);
	free(p.alias_from);
	free(p.alias_to);
	free_scratch(&f);
}

void clj_facts_free(clj_facts *f) {
	if (!f) return;
	clj_release(clj_from_ptr((void *)f->root));
	free_sites(f);
	free(f->nodes);
	free(f->frames);
	free(f->escape);
	free(f->loops);
	free(f->vars);
	free(f->diags);
	free(f->dep_vars);
	free(f->dep_epochs);
	free(f);
}

size_t clj_facts_bytes(const clj_facts *f) {
	return sizeof *f + (size_t)f->nnodes * sizeof(clj_fact) + (size_t)f->nframes * sizeof(clj_facts_frame) + f->nslots +
	       (size_t)f->nloops * sizeof(clj_facts_loop) + (size_t)f->nvars * sizeof(clj_fact);
}

uint32_t        clj_facts_nnodes(const clj_facts *f) { return f->nnodes; }
const clj_fact *clj_facts_node(const clj_facts *f, uint32_t id) { return id < f->nnodes ? &f->nodes[id] : NULL; }
uint32_t        clj_facts_nframes(const clj_facts *f) { return f->nframes; }

const clj_facts_frame *clj_facts_frame_at(const clj_facts *f, uint32_t i) { return i < f->nframes ? &f->frames[i] : NULL; }

uint32_t clj_facts_frame_of(const clj_facts *f, uint32_t id) {
	uint32_t best = UINT32_MAX, size = UINT32_MAX;
	for (uint32_t i = 0; i < f->nframes; i++) {
		const clj_facts_frame *fr = &f->frames[i];
		if (id < fr->first || id >= fr->first + fr->nnodes) continue;
		if (fr->nnodes < size) {
			size = fr->nnodes;
			best = i;
		}
	}
	return best;
}

clj_escape clj_facts_escape(const clj_facts *f, uint32_t frame, uint32_t slot) {
	if (frame >= f->nframes || slot >= f->frames[frame].nslots) return CLJ_ESCAPE_ESCAPES;
	return (clj_escape)f->escape[f->frames[frame].slot + slot];
}

uint32_t              clj_facts_nloops(const clj_facts *f) { return f->nloops; }
const clj_facts_loop *clj_facts_loop_at(const clj_facts *f, uint32_t i) { return i < f->nloops ? &f->loops[i] : NULL; }

const clj_fact *clj_facts_loop_var(const clj_facts *f, uint32_t loop, uint32_t i) {
	if (loop >= f->nloops || i >= f->loops[loop].n) return NULL;
	return &f->vars[f->loops[loop].first + i];
}

uint32_t clj_facts_conflicts(const clj_facts *f) { return f->conflicts; }
uint32_t clj_facts_widenings(const clj_facts *f) { return f->widenings; }
uint32_t clj_facts_conflict_node(const clj_facts *f) { return f->conflict_node; }

uint32_t              clj_facts_ndiagnostics(const clj_facts *f) { return f->ndiags; }
const clj_diagnostic *clj_facts_diagnostic(const clj_facts *f, uint32_t i) { return i < f->ndiags ? &f->diags[i] : NULL; }
uint32_t              clj_facts_nerrors(const clj_facts *f) { return f->nerrors; }
uint32_t              clj_facts_summary_hits(const clj_facts *f) { return f->hits; }

bool clj_facts_warnings_enabled(clj_value ns) {
	if (!clj_is_ns(ns)) return true;
	clj_value meta = clj_ns_meta(ns);
	if (clj_is_nil(meta) || !clj_has_core(meta, CLJ_CORE_MAP)) return true;
	clj_value v = clj_get2(meta, clj_keyword_from_cstr("facts/warnings"));
	if (v == CLJ_THROWN) {
		clj_release(clj_take_pending());
		return true;
	}
	bool on = v != CLJ_FALSE;
	clj_release(v);
	return on;
}
uint32_t                 clj_facts_narrowed_args(const clj_facts *f) { return f->narrowed; }
uint32_t                 clj_facts_ndeps(const clj_facts *f) { return f->ndeps; }

clj_value clj_facts_dep(const clj_facts *f, uint32_t i, uint32_t *epoch) {
	if (i >= f->ndeps) return CLJ_NIL;
	*epoch = f->dep_epochs[i];
	return f->dep_vars[i];
}

bool clj_facts_valid(const clj_facts *f) {
	for (uint32_t i = 0; i < f->ndeps; i++) {
		if (clj_var_epoch(f->dep_vars[i]) != f->dep_epochs[i]) return false;
	}
	for (uint32_t i = 0; i < f->njoins; i++) {
		if (clj_var_callers_epoch(f->joins[i].var) != f->joins[i].epoch) return false;
	}
	return true;
}

uint32_t clj_facts_nsites(const clj_facts *f) { return f->nsites; }

bool clj_facts_site(const clj_facts *f, uint32_t i, uint32_t *node, clj_value *var, uint32_t *nargs, const clj_fact **args, bool *in_fn) {
	if (i >= f->nsites) return false;
	*node = f->sites[i].node;
	*var = f->sites[i].var;
	*nargs = f->sites[i].nargs;
	*args = f->sites[i].args;
	*in_fn = f->sites[i].in_fn;
	return true;
}

uint32_t  clj_facts_nvalue_reads(const clj_facts *f) { return f->nvalue_reads; }

clj_value clj_facts_value_read(const clj_facts *f, uint32_t i, bool *in_fn) {
	if (i >= f->nvalue_reads) return CLJ_NIL;
	*in_fn = f->value_reads[i].in_fn;
	return f->value_reads[i].var;
}

uint32_t              clj_facts_njoins(const clj_facts *f) { return f->njoins; }
const clj_facts_join *clj_facts_join_at(const clj_facts *f, uint32_t i) { return i < f->njoins ? &f->joins[i] : NULL; }

// "map|record" for a set, the kind name for one member.
static void fact_text(clj_fact f, char *buf, size_t n) {
	size_t k = 0;
	if (f.types == CLJ_T_TOP) {
		snprintf(buf, n, "anything");
		return;
	}
	for (uint32_t bit = 0; bit < 27 && k + 1 < n; bit++) {
		if (!(f.types & (1u << bit))) continue;
		k += (size_t)snprintf(buf + k, n - k, "%s%s", k ? "|" : "", kind_names[bit]);
	}
	if (k == 0) snprintf(buf, n, "nothing");
}

const char *clj_diagnostic_message(const clj_diagnostic *d, char *buf, size_t n) {
	char req[256], have[256], name[256];
	fact_text(d->required, req, sizeof req);
	fact_text(d->passed, have, sizeof have);
	if (clj_is_var(d->callee)) {
		snprintf(name, sizeof name, "%s/%s", clj_string_bytes(clj_symbol_name(clj_var_ns(d->callee))),
		         clj_string_bytes(clj_symbol_name(clj_var_name(d->callee))));
	}
	else snprintf(name, sizeof name, "a direct fn");
	const char *tail = d->caught ? ", caught by the enclosing try" : "";
	switch (d->kind) {
	case CLJ_DIAG_CALL_CONFLICT:
		if (d->use_line) {
			snprintf(buf, n, "%s uses argument %u as %s at %u:%u, %s is passed at %u:%u%s", name, d->arg, req, d->use_line, d->use_col, have,
			         d->line, d->col, tail);
		}
		else snprintf(buf, n, "%s requires argument %u to be %s, %s is passed at %u:%u%s", name, d->arg, req, have, d->line, d->col, tail);
		break;
	case CLJ_DIAG_DECL_CONFLICT:
		if (d->arg == UINT32_MAX) snprintf(buf, n, "%s: the declaration says the result is %s, the body answers %s", name, req, have);
		else snprintf(buf, n, "%s: the declaration says argument %u is %s, the body uses it as %s at %u:%u", name, d->arg, req, have, d->use_line, d->use_col);
		break;
	case CLJ_DIAG_TOP_INTO_DECL:
		snprintf(buf, n, "%s declares argument %u as %s, nothing is known about what is passed at %u:%u", name, d->arg, req, d->line, d->col);
		break;
	case CLJ_DIAG_TOP_RESULT: snprintf(buf, n, "%s: the declaration says the result is %s, nothing is known about what the body answers", name, req); break;
	case CLJ_DIAG_EFFECTS: {
		uint32_t    bad = d->effects & ~d->effects_allowed & CLJ_EFFECTS_CHECKED;
		const char *what = bad ? "parks" : "may park";
		snprintf(buf, n, "%s does not allow argument %u to park, the function passed at %u:%u %s", name, d->arg, d->line, d->col, what);
		break;
	}
	case CLJ_DIAG_CALLERS_CONFLICT:
		snprintf(buf, n, "%s requires argument %u to be %s, %s is passed at %u:%u under what the recorded callers pass", name, d->arg, req, have,
		         d->line, d->col);
		break;
	}
	return buf;
}

// ---- effects of the core calls the walk names

static const char *const pure_names[] = {
	"nil?", "some?", "number?", "integer?", "int?", "nat-int?", "pos-int?", "neg-int?", "double?", "float?", "ratio?", "decimal?",
	"rational?", "string?", "keyword?", "symbol?", "char?", "boolean?", "true?", "false?", "map?", "set?", "vector?", "record?",
	"list?", "seq?", "sequential?", "coll?", "seqable?", "associative?", "fn?", "var?", "uuid?", "inst?", "ident?", "simple-ident?",
	"qualified-ident?", "qualified-keyword?", "simple-keyword?", "qualified-symbol?", "simple-symbol?", "not", "identical?",
	"identity", "boolean", "ifn?", "counted?", "indexed?", "instance?", "satisfies?", "type", "meta", "name", "namespace",
	"new*", "record-map*", "field*", "first", "next", "rest", "seq", "count", "nth", "get", "hash", "keyword", "symbol", "str",
	"=", "not=", "==", "<", "<=", ">", ">=", "compare", "vector", "list", "hash-map", "hash-set", "conj", "assoc", "dissoc",
	"cons", "inc", "dec", "+", "-", "*", "/", "quot", "rem", "mod", "min", "max", "zero?", "pos?", "neg?", "even?", "odd?",
	"empty?", "contains?", "keys", "vals", "vec", "set", "into", "with-meta", "subs", "long", "int", "double", "char",
};
static const char *const io_names[] = {"print", "println", "pr", "prn", "printf", "newline", "flush", "slurp", "spit", "read-line", "load", "require"};
static const char *const atom_names[] = {"swap!", "reset!", "swap-vals!", "reset-vals!", "compare-and-set!", "vreset!", "vswap!",
                                         "alter-var-root", "set-validator!", "add-watch", "remove-watch", "alter-meta!", "reset-meta!"};
// The waits the coroutine parks on; deref is not among them because @atom does not park and @future does (design §4).
static const char *const park_names[] = {"chan-take*", "chan-put*", "chan-alts*", "chan-deref*", "sleep*"};

static bool named_in(const char *name, const char *const *list, size_t n) {
	for (size_t i = 0; i < n; i++) {
		if (strcmp(list[i], name) == 0) return true;
	}
	return false;
}

// A predicate neither allocates nor throws; the rest of the pure list allocates or throws on a wrong argument but no more.
uint32_t clj_facts_core_effects(const char *name) {
	if (named_in(name, park_names, sizeof park_names / sizeof *park_names)) return CLJ_EFFECT_ANY | CLJ_EFFECT_PARK;
	if (named_in(name, io_names, sizeof io_names / sizeof *io_names)) return CLJ_EFFECT_ANY;
	if (named_in(name, atom_names, sizeof atom_names / sizeof *atom_names)) return CLJ_EFFECT_ATOM | CLJ_EFFECT_ALLOC | CLJ_EFFECT_THROW;
	if (named_in(name, pure_names, sizeof pure_names / sizeof *pure_names)) {
		static const char *const total[] = {"not", "identity", "boolean", "meta", "type"};
		size_t                   len = strlen(name);
		if ((len > 1 && name[len - 1] == '?') || named_in(name, total, sizeof total / sizeof *total)) return 0;
		return CLJ_EFFECT_ALLOC | CLJ_EFFECT_THROW;
	}
	return CLJ_EFFECT_ANY;
}
