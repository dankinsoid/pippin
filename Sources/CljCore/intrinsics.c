// @ai-generated(guided)
#include <stdatomic.h>
#include <string.h>

#include "clj/coll.h"
#include "clj/compare.h"
#include "clj/fn.h"
#include "clj/ice.h"
#include "clj/intrinsics.h"
#include "clj/ns.h"
#include "clj/proto.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"

#define I1(nm, f, p) {"clojure.core/" nm, 1, CLJ_INTRINSIC_1, {.f1 = f}, #f, NULL, p, {.f2 = NULL}}
#define I2(nm, f, p) {"clojure.core/" nm, 2, CLJ_INTRINSIC_2, {.f2 = f}, #f, NULL, p, {.f2 = NULL}}
#define I3(nm, f, p) {"clojure.core/" nm, 3, CLJ_INTRINSIC_3, {.f3 = f}, #f, NULL, p, {.f2 = NULL}}
// The entry's function retains the collection before c, its consuming form.
// A static of a class namespace (builtins_number.c), named in full.
#define S2(full, f, p) {full, 2, CLJ_INTRINSIC_2, {.f2 = f}, #f, NULL, p, {.f2 = NULL}}
#define C2(nm, f, c, p) {"clojure.core/" nm, 2, CLJ_INTRINSIC_2, {.f2 = f}, #f, #c, p, {.f2 = c}}
#define C3(nm, f, c, p) {"clojure.core/" nm, 3, CLJ_INTRINSIC_3, {.f3 = f}, #f, #c, p, {.f3 = c}}

static const clj_intrinsic table[] = {
	I2("+", clj_add, true),
	I2("-", clj_sub, true),
	I2("*", clj_mul, true),
	I2("/", clj_div, true),
	I2("quot", clj_quot, true),
	I2("rem", clj_rem, true),
	I1("inc", clj_inc, true),
	I1("dec", clj_dec, true),
	I2("<", clj_lt, true),
	I2("<=", clj_le, true),
	I2(">", clj_gt, true),
	I2(">=", clj_ge, true),
	I2("=", clj_eq, true),
	I2("not=", clj_neq, true),
	I2("identical?", clj_identical, true),
	I2("compare", clj_compare2, true),
	I2("==", clj_num_eq2, true),
	I1("long", clj_long_cast, true),
	I1("int", clj_int_cast, true),
	S2("Integer/compare", clj_integer_compare, true),
	I2("unchecked-add", clj_unchecked_add, true),
	I2("unchecked-subtract", clj_unchecked_subtract, true),
	I1("unchecked-inc", clj_unchecked_inc, true),
	I1("unchecked-dec", clj_unchecked_dec, true),
	I2("unchecked-add-int", clj_unchecked_add_int, true),
	I2("unchecked-subtract-int", clj_unchecked_subtract_int, true),
	I1("unchecked-inc-int", clj_unchecked_inc_int, true),
	I1("unchecked-dec-int", clj_unchecked_dec_int, true),
	I1("not", clj_not, true),
	I1("nil?", clj_nil_p, true),
	I1("zero?", clj_zero_p, true),
	I1("pos?", clj_pos_p, true),
	I1("neg?", clj_neg_p, true),
	I1("even?", clj_even_p, true),
	I1("odd?", clj_odd_p, true),
	I1("number?", clj_number_p, true),
	I1("integer?", clj_integer_p, true),
	I1("char?", clj_char_p, true),
	I1("string?", clj_string_p, true),
	I1("keyword?", clj_keyword_p, true),
	I1("symbol?", clj_symbol_p, true),
	I1("fn?", clj_fn_p, true),
	I1("vector?", clj_vector_p, true),
	I1("map?", clj_map_p, true),
	I1("set?", clj_set_p, true),
	I1("list?", clj_list_p, true),
	I1("seq?", clj_seq_p, true),
	I1("seqable?", clj_seqable_p, true),
	I1("sequential?", clj_sequential_p, true),
	I1("coll?", clj_coll_p, true),
	I1("counted?", clj_counted_p, true),
	I1("ifn?", clj_ifn_p, true),
	I1("associative?", clj_associative_p, true),
	I1("indexed?", clj_indexed_p, true),
	I1("empty?", clj_empty_p, true),
	I1("first", clj_first, true),
	I1("rest", clj_rest, true),
	I1("next", clj_next, true),
	I1("seq", clj_seq, true),
	I1("count", clj_count, true),
	I2("cons", clj_seq_cons, true),
	I2("get", clj_get2, true),
	I3("get", clj_get, true),
	I2("nth", clj_nth2, true),
	I3("nth", clj_nth3, true),
	C2("conj", clj_conj2, clj_conj, true),
	C3("assoc", clj_assoc3, clj_assoc_owned, true),
	C2("dissoc", clj_dissoc2, clj_dissoc_owned, true),
	C2("disj", clj_disj2, clj_disj_owned, true),
	C2("with-meta", clj_with_meta2, clj_with_meta, false),
	// Not folded: x's text may be a deftype's, the caller's code.
	C2("str", clj_str2, clj_str_owned, false),
	I2("contains?", clj_contains_p, true),
	// Not pure: an array is mutable, so a call on the same arguments need not answer the same.
	I2("aget", clj_aget, false),
	I3("aset", clj_aset, false),
	I1("alength", clj_alength, false),
	// A deftype's mutable field (design §4 «Роли конструкций»): set! inside the type's methods, not pure for the same reason.
	I2("mutable-field*", clj_mutable_field, false),
	I3("set-field!*", clj_set_field, false),
};

enum { N = sizeof table / sizeof *table };

// Resolved at boot, parallel to the table.
static clj_value vars[N];
static clj_value builtins[N];

bool clj_intrinsic_is_get(const clj_intrinsic *op) { return op->arity == 2 ? op->fn.f2 == clj_get2 : op->arity == 3 && op->fn.f3 == clj_get; }

const clj_intrinsic *clj_intrinsic_table(size_t *n) {
	*n = N;
	return table;
}

const clj_intrinsic *clj_intrinsic_find(clj_value var, uint32_t arity) {
	for (size_t i = 0; i < N; i++) {
		if (vars[i] == var && table[i].arity == arity) return &table[i];
	}
	return NULL;
}

const clj_intrinsic *clj_intrinsic_find_named(clj_value qualified_sym, uint32_t arity) {
	if (!clj_is_symbol(qualified_sym) || clj_is_nil(clj_symbol_ns(qualified_sym))) return NULL;
	const char *ns = CLJ_STR(clj_symbol_ns(qualified_sym)).bytes, *name = CLJ_STR(clj_symbol_name(qualified_sym)).bytes;
	size_t      ns_len = strlen(ns);
	for (size_t i = 0; i < N; i++) {
		if (table[i].arity != arity) continue;
		const char *full = table[i].name;
		if (strncmp(full, ns, ns_len) == 0 && full[ns_len] == '/' && strcmp(full + ns_len + 1, name) == 0) return &table[i];
	}
	return NULL;
}

clj_value clj_intrinsic_var(const clj_intrinsic *op) { return vars[op - table]; }

clj_value clj_intrinsic_builtin(const clj_intrinsic *op) { return builtins[op - table]; }

clj_value clj_intrinsic_call(const clj_intrinsic *op, const clj_value *args) {
	switch (op->kind) {
	case CLJ_INTRINSIC_1: return op->fn.f1(args[0]);
	case CLJ_INTRINSIC_2: return op->fn.f2(args[0], args[1]);
	case CLJ_INTRINSIC_3: return op->fn.f3(args[0], args[1], args[2]);
	}
	clj_ice("intrinsic/kind", NULL, "%s of kind %u", op->name, (unsigned)op->kind);
}

#if CLJ_DEBUG
static _Atomic int64_t consuming_calls;
#endif

int64_t clj_debug_consuming_calls(void) {
#if CLJ_DEBUG
	return atomic_load_explicit(&consuming_calls, memory_order_relaxed);
#else
	return -1;
#endif
}

clj_value clj_intrinsic_call_consuming(const clj_intrinsic *op, const clj_value *args) {
	CLJ_ASSERT(clj_intrinsic_consumes(op), "intrinsic does not consume");
#if CLJ_DEBUG
	atomic_fetch_add_explicit(&consuming_calls, 1, memory_order_relaxed);
#endif
	if (op->kind == CLJ_INTRINSIC_3) return op->consume.f3(args[0], args[1], args[2]);
	return op->consume.f2(args[0], args[1]);
}

// One load off the fn: a per-call caller (swap!) cannot afford the table scan.
const clj_intrinsic *clj_intrinsic_consuming(clj_value fn, uint32_t arity) {
	const clj_intrinsic *op = clj_fn_of(fn)->u.native.consuming;
	return op && op->arity == arity ? op : NULL;
}

void clj_intrinsics_install(void) {
	for (size_t i = 0; i < N; i++) {
		if (table[i].arity != (uint32_t)table[i].kind) clj_ice("boot/intrinsic-table", NULL, "%s: arity does not match its kind", table[i].name);
		const char            *slash = strchr(table[i].name, '/');
		CLJ_NO_CYCLE clj_value ns_name = clj_string_new(table[i].name, (size_t)(slash - table[i].name));
		CLJ_NO_CYCLE clj_value ns_sym = clj_symbol_new(CLJ_NIL, ns_name);
		clj_value              ns = clj_ns_find(ns_sym);
		clj_release(ns_sym);
		clj_release(ns_name);
		CLJ_NO_CYCLE clj_value sym = clj_symbol_from_cstr(slash + 1);
		clj_value              var = clj_is_nil(ns) ? CLJ_NIL : clj_ns_resolve(ns, sym);
		clj_release(sym);
		if (clj_is_nil(var)) clj_ice("boot/intrinsic-table", NULL, "%s names no var", table[i].name);
		clj_value fn = clj_var_root(var);
		if (!clj_is_fn(fn) || clj_fn_of(fn)->kind != CLJ_FN_NATIVE) clj_ice("boot/intrinsic-table", NULL, "%s names a core var with no builtin root", table[i].name);
		vars[i] = var;
		builtins[i] = clj_retain(fn);
		if (clj_intrinsic_consumes(&table[i])) clj_fn_of(fn)->u.native.consuming = &table[i];
	}
}
