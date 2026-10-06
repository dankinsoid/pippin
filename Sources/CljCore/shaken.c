// @ai-generated(solo)
#include <stdio.h>

#include "clj/core.h"
#include "clj/error.h"
#include "clj/shaken.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"

typedef struct {
	clj_header h;
	clj_slot   var; // the dropped def's var: immortal, so the field is borrowed and the type has no each_child
} clj_shaken;

clj_value clj_shaken_var(clj_value v) { return ((const clj_shaken *)clj_to_ptr(v))->var.v; }

// Fatal and not a throw: a catch-all must not turn a build bug into a wrong answer.
static clj_value reached(clj_value self, const char *op) {
	clj_value var = clj_shaken_var(self);
	char      msg[400];
	snprintf(msg, sizeof msg, "%s/%s was dropped by --closed tree shaking and the program reached it (%s): the root set missed it (NOTES.md, \"Compiler\": tree shaking)",
	         clj_string_bytes(clj_symbol_name(clj_var_ns(var))), clj_string_bytes(clj_symbol_name(clj_var_name(var))), op);
	clj_fatal(msg);
}

static clj_value shaken_invoke(clj_value self, const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return reached(self, "call");
}

static clj_value shaken_seq(clj_value self) { return reached(self, "seq"); }
static clj_value shaken_first(clj_value self) { return reached(self, "first"); }
static clj_value shaken_next(clj_value self) { return reached(self, "next"); }
static clj_value shaken_rest(clj_value self) { return reached(self, "rest"); }
static clj_value shaken_count(clj_value self) { return reached(self, "count"); }

static clj_value shaken_lookup(clj_value self, clj_value key, clj_value not_found) {
	(void)key;
	(void)not_found;
	return reached(self, "get");
}

static clj_value shaken_conj(clj_value self, clj_value x) {
	(void)x;
	return reached(self, "conj");
}

static clj_value shaken_assoc(clj_value self, clj_value key, clj_value val) {
	(void)key;
	(void)val;
	return reached(self, "assoc");
}

static clj_value shaken_dissoc(clj_value self, clj_value key) {
	(void)key;
	return reached(self, "dissoc");
}

static clj_value shaken_reduce(clj_value self, clj_value f, clj_value init) {
	(void)f;
	(void)init;
	return reached(self, "reduce");
}

static clj_value shaken_with_meta(clj_value self, clj_value m) {
	(void)m;
	return reached(self, "with-meta");
}

// Identity, as an atom's: an aborting = would kill any diagnostic that compares a root with something.
static uint32_t shaken_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }
static bool     shaken_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

// IFn and nothing else: ifn? true keeps a call on the slot path, every other predicate answers false.
const clj_type clj_shaken_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "shaken",
	.core_bits = CLJ_CORE_FN,
	.hash = shaken_hash,
	.equals = shaken_equals,
	.seq = shaken_seq,
	.first = shaken_first,
	.next = shaken_next,
	.rest = shaken_rest,
	.count = shaken_count,
	.lookup = shaken_lookup,
	.conj = shaken_conj,
	.assoc = shaken_assoc,
	.dissoc = shaken_dissoc,
	.reduce = shaken_reduce,
	.invoke = shaken_invoke,
	.with_meta = shaken_with_meta,
};

// Immortal by construction: nothing ever drops one, so a debug build's live count must not carry it.
clj_value clj_shaken_new(clj_value var) {
	int64_t     before = clj_debug_live_objects();
	clj_shaken *s = clj_alloc(&clj_shaken_type, sizeof *s);
	s->h.flags |= CLJ_FLAG_IMMORTAL | CLJ_FLAG_SHARED;
	clj_slot_init(&s->h, &s->var, var);
	if (before >= 0) clj_debug_live_objects_exclude(clj_debug_live_objects() - before);
	return clj_from_ptr(s);
}
