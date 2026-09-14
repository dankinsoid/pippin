// @ai-generated(guided)
#include <string.h>

#include "clj/coll.h"
#include "clj/fn.h"
#include "clj/intrinsics.h"
#include "clj/ns.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"

#define I1(nm, f, p) {"clojure.core/" nm, 1, CLJ_INTRINSIC_1, {.f1 = f}, p}
#define I2(nm, f, p) {"clojure.core/" nm, 2, CLJ_INTRINSIC_2, {.f2 = f}, p}
#define I3(nm, f, p) {"clojure.core/" nm, 3, CLJ_INTRINSIC_3, {.f3 = f}, p}

static const clj_intrinsic table[] = {
	I2("+", clj_add, true),
	I2("-", clj_sub, true),
	I2("*", clj_mul, true),
	I2("/", clj_div, true),
	I1("inc", clj_inc, true),
	I1("dec", clj_dec, true),
	I2("<", clj_lt, true),
	I2("<=", clj_le, true),
	I2(">", clj_gt, true),
	I2(">=", clj_ge, true),
	I2("=", clj_eq, true),
	I2("not=", clj_neq, true),
	I2("identical?", clj_identical, true),
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
	I1("list?", clj_list_p, true),
	I1("seq?", clj_seq_p, true),
	I1("seqable?", clj_seqable_p, true),
	I1("sequential?", clj_sequential_p, true),
	I1("coll?", clj_coll_p, true),
	I1("counted?", clj_counted_p, true),
	I1("ifn?", clj_ifn_p, true),
	I1("associative?", clj_associative_p, true),
	I1("indexed?", clj_indexed_p, true),
	I1("empty?", clj_empty_p, false),
	I1("first", clj_first, false),
	I1("rest", clj_rest, false),
	I1("next", clj_next, false),
	I1("seq", clj_seq, false),
	I1("count", clj_count, false),
	I2("cons", clj_seq_cons, false),
	I2("get", clj_get2, false),
	I3("get", clj_get, false),
	I2("nth", clj_nth2, false),
	I3("nth", clj_nth3, false),
	I2("conj", clj_conj2, false),
	I3("assoc", clj_assoc3, false),
	I2("contains?", clj_contains_p, false),
};

enum { N = sizeof table / sizeof *table };

// Resolved at boot, parallel to the table.
static clj_value vars[N];
static clj_value builtins[N];

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
	const char *ns = clj_string_bytes(clj_symbol_ns(qualified_sym)), *name = clj_string_bytes(clj_symbol_name(qualified_sym));
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
	clj_fatal("unknown intrinsic kind");
}

void clj_intrinsics_install(void) {
	clj_value core = clj_ns_core();
	for (size_t i = 0; i < N; i++) {
		if (table[i].arity != (uint32_t)table[i].kind) clj_fatal("intrinsic arity does not match its kind");
		clj_value sym = clj_symbol_from_cstr(strchr(table[i].name, '/') + 1);
		clj_value var = clj_ns_resolve(core, sym);
		clj_release(sym);
		if (clj_is_nil(var)) clj_fatal("intrinsic names no core var");
		clj_value fn = clj_var_root(var);
		if (!clj_is_fn(fn) || clj_fn_of(fn)->kind != CLJ_FN_NATIVE) clj_fatal("intrinsic names a core var with no builtin root");
		vars[i] = var;
		builtins[i] = clj_retain(fn);
	}
}
