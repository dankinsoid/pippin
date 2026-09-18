// @ai-generated(solo)
// Pass 1 of the facts lattice: one forward walk per frame over an optimized tree (NOTES.md, "Facts").
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
#include "clj/keyword.h"
#include "clj/list.h"
#include "clj/long.h"
#include "clj/map.h"
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

// A loop variable that has not settled after this many rounds goes straight to TOP.
#define WIDEN_ROUNDS 3

#define T_INT  (CLJ_T_FIXNUM | CLJ_T_LONG | CLJ_T_BIGINT)
#define T_NUM  (T_INT | CLJ_T_RATIO | CLJ_T_DECIMAL | CLJ_T_DOUBLE)
#define T_MAPS (CLJ_T_MAP | CLJ_T_SORTED_MAP | CLJ_T_RECORD)
#define T_SETS (CLJ_T_SET | CLJ_T_SORTED_SET)
#define T_COLL (T_MAPS | T_SETS | CLJ_T_VECTOR | CLJ_T_LIST)
#define T_SEQABLE (T_COLL | CLJ_T_STRING | CLJ_T_ARRAY | CLJ_T_NIL)

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
	clj_fact f = {CLJ_T_TOP, CLJ_NULL_MAYBE, 0, CLJ_UNBOUND, NULL};
	return f;
}

clj_fact clj_fact_bottom(void) {
	clj_fact f = {CLJ_T_BOTTOM, CLJ_NULL_BOTTOM, 0, CLJ_UNBOUND, NULL};
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

clj_fact clj_fact_cap(clj_fact f) {
	f = normalize(f);
	if (popcount(f.types) > CLJ_FACT_UNION_MAX) f.types = CLJ_T_TOP;
	return f;
}

static clj_fact fact(uint32_t types, clj_null null) {
	clj_fact f = {types, (uint8_t)null, 0, CLJ_UNBOUND, NULL};
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
	clj_fact r = {a.types | b.types, (uint8_t)(a.null | b.null), 0, CLJ_UNBOUND, NULL};
	// Identity, not clj_equals: two constants the reader made separately are two objects, as the codec keeps them.
	if (a.singleton == b.singleton) r.singleton = a.singleton;
	if (a.desc == b.desc) r.desc = a.desc;
	if (a.elem == b.elem) r.elem = a.elem;
	return clj_fact_cap(r);
}

clj_fact clj_fact_meet(clj_fact a, clj_fact b, uint32_t *conflicts) {
	clj_fact r = {a.types & b.types, (uint8_t)(a.null & b.null), 0, CLJ_UNBOUND, NULL};
	r.singleton = a.singleton == CLJ_UNBOUND ? b.singleton : (b.singleton == CLJ_UNBOUND || a.singleton == b.singleton ? a.singleton : CLJ_UNBOUND);
	r.desc = a.desc ? a.desc : b.desc;
	r.elem = a.elem ? a.elem : b.elem;
	r = normalize(r);
	if (r.types == CLJ_T_BOTTOM && a.types != CLJ_T_BOTTOM && b.types != CLJ_T_BOTTOM && conflicts) (*conflicts)++;
	return clj_fact_cap(r);
}

uint32_t clj_fact_kind_of_value(clj_value v) {
	if (clj_is_nil(v)) return CLJ_T_NIL;
	if (clj_is_bool(v)) return CLJ_T_BOOL;
	if (clj_is_fixnum(v)) return CLJ_T_FIXNUM;
	if (clj_is_char(v)) return CLJ_T_CHAR;
	if (!clj_is_ptr(v)) return CLJ_T_HOST;
	const clj_type *t = clj_header_of(v)->type;
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
	if (t->core_bits & (CLJ_CORE_SEQ | CLJ_CORE_LIST)) return CLJ_T_LIST;
	if (t == &clj_empty_list_type || t == &clj_lazy_seq_type) return CLJ_T_LIST;
	if (t->core_bits & CLJ_CORE_FN) return CLJ_T_FN;
	return CLJ_T_HOST;
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

typedef enum { SIG_FIXED, SIG_ARITH, SIG_DIV, SIG_CONJ, SIG_ASSOC, SIG_SAME } sig_rule;

typedef struct {
	const char *name; // unqualified, in clojure.core
	uint32_t    arity; // 0: any
	uint8_t     rule;
	uint32_t    result;
	uint32_t    refine; // the argument's type set when the result is truthy, 0 when the entry refines nothing
	uint8_t     refine_null;
	uint8_t     refine_arg;
	bool        exact; // true for exactly `refine`, so the false branch may subtract it
	bool        keeps; // an argument may be stored where it outlives the call
	uint8_t     elem;  // array kind + 1 for the array constructors
} fact_sig;

#define F(nm, ar, res) {nm, ar, SIG_FIXED, res, 0, 0, 0, false, false, 0}
#define FK(nm, ar, res) {nm, ar, SIG_FIXED, res, 0, 0, 0, false, true, 0}
#define R(nm, ar, rl, res) {nm, ar, rl, res, 0, 0, 0, false, true, 0}
#define P(nm, ref, ex) {nm, 1, SIG_FIXED, CLJ_T_BOOL, ref, CLJ_NULL_MAYBE, 0, ex, false, 0}
#define PN(nm, ref, rnul, ex) {nm, 1, SIG_FIXED, CLJ_T_BOOL, ref, rnul, 0, ex, false, 0}
#define ARR(nm, k) {nm, 0, SIG_FIXED, CLJ_T_ARRAY, 0, 0, 0, false, true, (uint8_t)(k) + 1}

static const fact_sig sigs[] = {
	// arithmetic and comparison
	R("+", 2, SIG_ARITH, 0), R("-", 2, SIG_ARITH, 0), R("*", 2, SIG_ARITH, 0), R("/", 2, SIG_DIV, 0),
	R("inc", 1, SIG_ARITH, 0), R("dec", 1, SIG_ARITH, 0),
	F("<", 2, CLJ_T_BOOL), F("<=", 2, CLJ_T_BOOL), F(">", 2, CLJ_T_BOOL), F(">=", 2, CLJ_T_BOOL),
	F("=", 0, CLJ_T_BOOL), F("not=", 0, CLJ_T_BOOL), F("==", 0, CLJ_T_BOOL), F("identical?", 2, CLJ_T_BOOL),
	F("compare", 2, CLJ_T_FIXNUM), F("hash", 1, CLJ_T_FIXNUM), F("count", 1, CLJ_T_FIXNUM), F("alength", 1, CLJ_T_FIXNUM),
	F("quot", 2, T_NUM), F("rem", 2, T_NUM), F("mod", 2, T_NUM), F("min", 0, T_NUM), F("max", 0, T_NUM),
	F("long", 1, T_INT), F("int", 1, T_INT), F("bigint", 1, T_INT), F("double", 1, CLJ_T_DOUBLE), F("float", 1, CLJ_T_DOUBLE),
	F("bit-and", 0, T_INT), F("bit-or", 0, T_INT), F("bit-xor", 0, T_INT), F("bit-not", 1, T_INT),
	F("bit-shift-left", 2, T_INT), F("bit-shift-right", 2, T_INT), F("unsigned-bit-shift-right", 2, T_INT),
	F("parse-long", 1, T_INT | CLJ_T_NIL), F("parse-double", 1, CLJ_T_DOUBLE | CLJ_T_NIL),
	// predicates, with the refinement they carry into the branches of an if
	PN("nil?", CLJ_T_NIL, CLJ_NULL_ALWAYS, true), PN("some?", CLJ_T_TOP & ~CLJ_T_NIL, CLJ_NULL_NEVER, true),
	P("number?", T_NUM, true), P("integer?", T_INT, true), P("int?", T_INT, true), P("nat-int?", T_INT, false),
	P("pos-int?", T_INT, false), P("neg-int?", T_INT, false), P("double?", CLJ_T_DOUBLE, true), P("float?", CLJ_T_DOUBLE, true),
	P("ratio?", CLJ_T_RATIO, true), P("decimal?", CLJ_T_DECIMAL, true), P("rational?", T_INT | CLJ_T_RATIO | CLJ_T_DECIMAL, true),
	P("string?", CLJ_T_STRING, true), P("keyword?", CLJ_T_KEYWORD, true), P("symbol?", CLJ_T_SYMBOL, true),
	P("char?", CLJ_T_CHAR, true), P("boolean?", CLJ_T_BOOL, true), P("true?", CLJ_T_BOOL, false), P("false?", CLJ_T_BOOL, false),
	P("map?", T_MAPS, true), P("set?", T_SETS, true), P("vector?", CLJ_T_VECTOR, true), P("record?", CLJ_T_RECORD, true),
	P("list?", CLJ_T_LIST, false), P("seq?", CLJ_T_LIST, false), P("sequential?", CLJ_T_LIST | CLJ_T_VECTOR, true),
	P("coll?", T_COLL, true), P("seqable?", T_SEQABLE, false), P("associative?", T_MAPS | CLJ_T_VECTOR, true),
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
	FK("list*", 0, CLJ_T_LIST), FK("map", 0, CLJ_T_LIST), FK("filter", 2, CLJ_T_LIST), FK("remove", 2, CLJ_T_LIST),
	FK("keep", 2, CLJ_T_LIST), FK("mapcat", 0, CLJ_T_LIST), FK("concat", 0, CLJ_T_LIST), FK("take", 2, CLJ_T_LIST),
	FK("drop", 2, CLJ_T_LIST), FK("take-while", 2, CLJ_T_LIST), FK("drop-while", 2, CLJ_T_LIST), FK("partition", 0, CLJ_T_LIST),
	FK("partition-all", 0, CLJ_T_LIST), FK("interpose", 2, CLJ_T_LIST), FK("interleave", 0, CLJ_T_LIST),
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
	R("conj", 0, SIG_CONJ, 0), R("assoc", 0, SIG_ASSOC, 0), R("into", 0, SIG_SAME, 0), R("with-meta", 2, SIG_SAME, 0),
	R("vary-meta", 0, SIG_SAME, 0), R("dissoc", 0, SIG_SAME, 0), R("disj", 0, SIG_SAME, 0), R("empty", 1, SIG_SAME, 0),
	FK("assoc-in", 3, T_MAPS), FK("update", 0, T_MAPS | CLJ_T_VECTOR), FK("update-in", 0, T_MAPS | CLJ_T_VECTOR),
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

static bool is_core_var(clj_value var) {
	clj_value ns = clj_var_ns(var);
	return clj_is_symbol(ns) && strcmp(clj_string_bytes(clj_symbol_name(ns)), "clojure.core") == 0;
}

static const fact_sig *sig_named(const char *name, uint32_t arity) {
	for (uint32_t i = 0; i < NSIGS; i++) {
		if (strcmp(sigs[i].name, name) != 0) continue;
		if (sigs[i].arity == arity || sigs[i].arity == 0) return &sigs[i];
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

typedef struct {
	clj_fact *slots;
	uint32_t  n;
} env;

typedef struct {
	const clj_fact *captured;
	uint32_t        ncaptured;
	uint32_t        index; // into facts->frames
} frame_ctx;

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
	uint32_t     *alias_from, *alias_to;
	uint32_t      nalias, calias;
} pass;

typedef enum { USE_NONE, USE_CAPTURE, USE_ESCAPE } use_kind;

static clj_fact infer(pass *p, const clj_node *n, env *e, use_kind use);

static env env_new(uint32_t n) {
	env e = {xalloc(n, sizeof(clj_fact)), n};
	for (uint32_t i = 0; i < n; i++) e.slots[i] = clj_fact_top();
	return e;
}

static env env_clone(const env *src) {
	env e = {xalloc(src->n, sizeof(clj_fact)), src->n};
	memcpy(e.slots, src->slots, src->n * sizeof(clj_fact));
	return e;
}

static void env_free(env *e) { free(e->slots); }

static void mark_escape(pass *p, uint32_t slot, clj_escape level) {
	const clj_facts_frame *fr = &p->f->frames[p->frame->index];
	if (slot >= fr->nslots) return;
	uint8_t *cell = &p->f->escape[fr->slot + slot];
	if (level > *cell) *cell = (uint8_t)level;
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

// One frame: parameters and free variables are TOP, the body's value leaves the frame.
static void run_frame(pass *p, uint32_t owner, const clj_fn_arity *a, const clj_node *body, const clj_fact *captured,
                      uint32_t ncaptured, const clj_fact *self) {
	uint32_t nslots = a ? a->nslots : 0;
	max_slot(body, &nslots);
	uint32_t  fi = push_frame(p, owner, a ? a->nparams : 0, body, nslots);
	env       e = env_new(nslots);
	frame_ctx fc = {captured, ncaptured, fi};
	if (a) {
		// the rest parameter is the seq of the extra arguments, or nil when there are none
		if (a->variadic && a->nparams < nslots) e.slots[a->nparams] = fact_of(CLJ_T_LIST | CLJ_T_NIL);
		if (a->self_slot >= 0 && (uint32_t)a->self_slot < nslots && self) e.slots[a->self_slot] = *self;
	}
	frame_ctx    *saved_frame = p->frame;
	recur_target *saved_recur = p->recur;
	uint32_t      base = p->nalias;
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
	p->frame = &fc;
	p->recur = &t;
	infer(p, body, &e, USE_ESCAPE);
	resolve_aliases(p, base);
	p->frame = saved_frame;
	p->recur = saved_recur;
	env_free(&e);
}

static void run_fn(pass *p, const clj_node *n, const clj_fact *captured, uint32_t ncaptured) {
	clj_fact self = fact_of(CLJ_T_FN);
	for (uint32_t i = 0; i <= CLJ_FN_MAX_FIXED + 1; i++) {
		const clj_fn_arity *a = i <= CLJ_FN_MAX_FIXED ? n->u.fn.fixed[i] : n->u.fn.variadic;
		if (a) run_frame(p, n->id, a, a->body, captured, ncaptured, &self);
	}
}

// ---- refinement (occurrence typing on predicates, design §3)

// A round of a loop's fixpoint sees the variables before they are widened, so only the recording pass counts.
static uint32_t *conflict_sink(pass *p) { return p->record ? &p->f->conflicts : NULL; }

static void refine_slot(pass *p, env *e, uint32_t slot, uint32_t types, clj_null null, bool subtract) {
	if (slot >= e->n) return;
	clj_fact narrow = {types, (uint8_t)null, 0, CLJ_UNBOUND, NULL};
	if (subtract) {
		clj_fact cur = e->slots[slot];
		cur.types &= ~types;
		// MAYBE is "says nothing about nil", so its complement must subtract nothing
		if (null != CLJ_NULL_MAYBE) cur.null = (uint8_t)(cur.null & ~null);
		if (cur.types == CLJ_T_BOTTOM && e->slots[slot].types != CLJ_T_BOTTOM && p->record) p->f->conflicts++;
		e->slots[slot] = clj_fact_cap(cur);
		return;
	}
	e->slots[slot] = clj_fact_meet(e->slots[slot], narrow, conflict_sink(p));
}

static void refine_singleton(pass *p, env *e, uint32_t slot, clj_value v) {
	if (slot >= e->n) return;
	e->slots[slot] = clj_fact_meet(e->slots[slot], clj_fact_of_value(v), conflict_sink(p));
}

static void refine(pass *p, const clj_node *test, env *yes, env *no);

static void refine_call(pass *p, const clj_node *test, const fact_sig *s, const clj_node *const *args, uint32_t nargs, env *yes,
                        env *no) {
	(void)test;
	if (!s || !s->refine || s->refine_arg >= nargs) return;
	const clj_node *arg = args[s->refine_arg];
	if (arg->kind != CLJ_NODE_LOCAL) return;
	refine_slot(p, yes, arg->u.local.index, s->refine, (clj_null)s->refine_null, false);
	if (s->exact) refine_slot(p, no, arg->u.local.index, s->refine, (clj_null)s->refine_null, true);
}

// (= x <const>) pins x to that constant in the true branch; the false branch learns nothing.
static void refine_equality(pass *p, const clj_node *const *args, uint32_t nargs, env *yes) {
	if (nargs != 2) return;
	for (uint32_t i = 0; i < 2; i++) {
		const clj_node *a = args[i], *b = args[1 - i];
		if (a->kind == CLJ_NODE_LOCAL && b->kind == CLJ_NODE_CONST) refine_singleton(p, yes, a->u.local.index, b->u.value);
	}
}

static void refine_instance(pass *p, const clj_node *const *args, uint32_t nargs, env *yes, env *no) {
	if (nargs != 2 || args[0]->kind != CLJ_NODE_VAR || args[1]->kind != CLJ_NODE_LOCAL) return;
	if (!is_core_var(args[0]->u.var)) return;
	const char *name = clj_string_bytes(clj_symbol_name(clj_var_name(args[0]->u.var)));
	for (size_t i = 0; i < sizeof type_names / sizeof *type_names; i++) {
		if (strcmp(type_names[i].name, name) != 0) continue;
		refine_slot(p, yes, args[1]->u.local.index, type_names[i].types, CLJ_NULL_NEVER, false);
		if (type_names[i].exact) refine_slot(p, no, args[1]->u.local.index, type_names[i].types, CLJ_NULL_BOTTOM, true);
		return;
	}
}

static void refine(pass *p, const clj_node *test, env *yes, env *no) {
	switch (test->kind) {
	case CLJ_NODE_LOCAL:
		// only nil and false are falsy, so the true branch is non-nil and the false branch is one of the two
		refine_slot(p, yes, test->u.local.index, CLJ_T_TOP & ~CLJ_T_NIL, CLJ_NULL_NEVER, false);
		refine_slot(p, no, test->u.local.index, CLJ_T_NIL | CLJ_T_BOOL, CLJ_NULL_MAYBE, false);
		return;
	case CLJ_NODE_INTRINSIC: {
		const fact_sig *s = sig_of_intrinsic(test->u.intrinsic.op);
		const char     *name = strchr(test->u.intrinsic.op->name, '/') + 1;
		if (strcmp(name, "not") == 0 && test->u.intrinsic.n == 1) {
			refine(p, test->u.intrinsic.args[0], no, yes);
			return;
		}
		if (strcmp(name, "=") == 0 || strcmp(name, "identical?") == 0) refine_equality(p, test->u.intrinsic.args, test->u.intrinsic.n, yes);
		refine_call(p, test, s, test->u.intrinsic.args, test->u.intrinsic.n, yes, no);
		return;
	}
	case CLJ_NODE_INVOKE: {
		if (test->u.invoke.fn->kind != CLJ_NODE_VAR || !clj_is_var(test->u.invoke.fn->u.var)) return;
		clj_value var = test->u.invoke.fn->u.var;
		if (!is_core_var(var)) return;
		const char *name = clj_string_bytes(clj_symbol_name(clj_var_name(var)));
		if (strcmp(name, "not") == 0 && test->u.invoke.n == 1) {
			refine(p, test->u.invoke.args[0], no, yes);
			return;
		}
		if (strcmp(name, "instance?") == 0) {
			refine_instance(p, test->u.invoke.args, test->u.invoke.n, yes, no);
			return;
		}
		if (strcmp(name, "=") == 0) refine_equality(p, test->u.invoke.args, test->u.invoke.n, yes);
		refine_call(p, test, sig_of_var(var, test->u.invoke.n), test->u.invoke.args, test->u.invoke.n, yes, no);
		return;
	}
	default: return;
	}
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
	default: break;
	}
	clj_fact r = fact_of(s->result);
	if (s->elem) {
		r.types = CLJ_T_ARRAY;
		r.elem = s->elem;
	}
	return clj_fact_cap(r);
}

static clj_fact infer_call(pass *p, const clj_node *const *args, uint32_t n, env *e, const fact_sig *s) {
	clj_fact fs[4];
	use_kind use = (s && !s->keeps) ? USE_NONE : USE_ESCAPE;
	for (uint32_t i = 0; i < n; i++) {
		clj_fact a = infer(p, args[i], e, use);
		if (i < 4) fs[i] = a;
	}
	if (!s) return clj_fact_top();
	return sig_result(s, fs, n < 4 ? n : 4);
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
		memcpy(scratch.slots, e->slots, e->n * sizeof(clj_fact));
		for (uint32_t i = 0; i < nb; i++) {
			if (n->u.let.slots[i] < scratch.n) scratch.slots[n->u.let.slots[i]] = vars[i];
		}
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
	memcpy(scratch.slots, e->slots, e->n * sizeof(clj_fact));
	for (uint32_t i = 0; i < nb; i++) {
		if (n->u.let.slots[i] < scratch.n) scratch.slots[n->u.let.slots[i]] = vars[i];
	}
	clj_fact r = infer(p, n->u.let.body, &scratch, use);
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
	env      entry = env_clone(e);
	clj_fact r = infer(p, n->u.try_.body, e, use);
	for (uint32_t i = 0; i < n->u.try_.ncatches; i++) {
		const clj_catch *k = &n->u.try_.catches[i];
		env              ce = env_clone(&entry);
		if (k->slot < ce.n) ce.slots[k->slot] = clj_fact_top();
		r = clj_fact_join(r, infer(p, k->handler, &ce, use));
		env_free(&ce);
	}
	if (n->u.try_.finally_) infer(p, n->u.try_.finally_, e, USE_NONE);
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
		frame_ctx fc = {NULL, 0, fi};
		for (uint32_t i = 0; i < n->u.fused.nargs && i < nslots; i++) fe.slots[i] = args[i];
		frame_ctx    *saved_frame = p->frame;
		recur_target *saved_recur = p->recur;
		uint32_t      base = p->nalias;
		p->frame = &fc;
		p->recur = NULL;
		env      copy = env_clone(&fe);
		clj_fact a = infer(p, n->u.fused.fused, &fe, USE_ESCAPE);
		clj_fact b = infer(p, n->u.fused.original, &copy, USE_ESCAPE);
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

static clj_fact infer(pass *p, const clj_node *n, env *e, use_kind use) {
	clj_fact r;
	switch (n->kind) {
	case CLJ_NODE_CONST: r = clj_fact_of_value(n->u.value); break;
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
	case CLJ_NODE_VAR: r = clj_fact_top(); break; // no interprocedural pass: a var's root is not read
	case CLJ_NODE_IF: {
		infer(p, n->u.if_.test, e, USE_NONE);
		env      yes = env_clone(e), no = env_clone(e);
		uint32_t before = p->f->conflicts;
		refine(p, n->u.if_.test, &yes, &no);
		if (p->f->conflicts > before && p->f->conflict_node == UINT32_MAX) p->f->conflict_node = n->u.if_.test->id;
		clj_fact a = infer(p, n->u.if_.then, &yes, use);
		clj_fact b = n->u.if_.else_ ? infer(p, n->u.if_.else_, &no, use) : fact_of(CLJ_T_NIL);
		for (uint32_t i = 0; i < e->n; i++) e->slots[i] = clj_fact_join(yes.slots[i], no.slots[i]);
		env_free(&yes);
		env_free(&no);
		r = clj_fact_join(a, b);
		break;
	}
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
			if (n->u.let.slots[i] < e->n) e->slots[n->u.let.slots[i]] = f;
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
	case CLJ_NODE_FN: {
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
			default: caps[i] = clj_fact_top();
			}
		}
		if (p->record) run_fn(p, n, caps, n->u.fn.ncaptures);
		free(caps);
		r = fact_of(CLJ_T_FN);
		break;
	}
	case CLJ_NODE_DIRECT_FN:
		// its slot holds nil at run time and nothing reads it as a value; the node still denotes the function
		if (p->record) run_fn(p, n, p->frame->captured, p->frame->ncaptured);
		r = fact_of(CLJ_T_FN);
		break;
	case CLJ_NODE_INVOKE: {
		infer(p, n->u.invoke.fn, e, USE_NONE);
		const fact_sig *s = n->u.invoke.fn->kind == CLJ_NODE_VAR ? sig_of_var(n->u.invoke.fn->u.var, n->u.invoke.n) : NULL;
		r = infer_call(p, n->u.invoke.args, n->u.invoke.n, e, s);
		break;
	}
	case CLJ_NODE_DIRECT_CALL:
		infer_args(p, n->u.direct.args, n->u.direct.n, e, NULL, NULL);
		r = clj_fact_top();
		break;
	case CLJ_NODE_INTRINSIC:
		r = infer_call(p, n->u.intrinsic.args, n->u.intrinsic.n, e, sig_of_intrinsic(n->u.intrinsic.op));
		break;
	case CLJ_NODE_DEF:
		if (n->u.def.init) infer(p, n->u.def.init, e, USE_ESCAPE);
		if (n->u.def.meta) infer(p, n->u.def.meta, e, USE_ESCAPE);
		r = fact_of(CLJ_T_VAR);
		break;
	case CLJ_NODE_VECTOR:
	case CLJ_NODE_MAP:
	case CLJ_NODE_SET:
		for (uint32_t i = 0; i < n->u.seq.n; i++) infer(p, n->u.seq.items[i], e, USE_ESCAPE);
		r = fact_of(n->kind == CLJ_NODE_VECTOR ? CLJ_T_VECTOR : n->kind == CLJ_NODE_MAP ? CLJ_T_MAP : CLJ_T_SET);
		break;
	case CLJ_NODE_TRY: r = infer_try(p, n, e, use); break;
	case CLJ_NODE_THROW:
		infer(p, n->u.throw_, e, USE_ESCAPE);
		r = clj_fact_bottom();
		break;
	case CLJ_NODE_FUSED: r = infer_fused(p, n, e); break;
	default: clj_fatal("unknown node kind");
	}
	if (p->record && n->id < p->f->nnodes) p->f->nodes[n->id] = r;
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
	pass p = {f, NULL, NULL, true, NULL, NULL, 0, 0};
	run_frame(&p, UINT32_MAX, NULL, root, NULL, 0, NULL);
	free(p.alias_from);
	free(p.alias_to);
	return f;
}

void clj_facts_free(clj_facts *f) {
	if (!f) return;
	clj_release(clj_from_ptr((void *)f->root));
	free(f->nodes);
	free(f->frames);
	free(f->escape);
	free(f->loops);
	free(f->vars);
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
