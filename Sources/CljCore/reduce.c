// @ai-generated(guided)
#include "clj/reduce.h"
#include "clj/core.h"
#include "clj/error.h"
#include "clj/fn.h"
#include "clj/map.h"
#include "clj/vector.h"

clj_reducer clj_reducer_start(clj_value f, clj_value init, size_t nargs) {
	CLJ_ASSERT(nargs == 2 || nargs == 3, "a reducer takes 2 or 3 arguments");
	return (clj_reducer){clj_call_prepare(f, nargs), init == CLJ_UNBOUND ? CLJ_UNBOUND : clj_retain(init), {CLJ_NIL, CLJ_NIL, CLJ_NIL}};
}

// The result of one step replaces acc; a reduced one is unwrapped and ends the walk.
static bool take(clj_reducer *r, clj_value v) {
	clj_release(r->acc);
	if (v == CLJ_THROWN) {
		r->acc = CLJ_THROWN;
		return false;
	}
	if (clj_is_reduced(v)) {
		r->acc = clj_retain(clj_reduced_value(v));
		clj_release(v);
		return false;
	}
	r->acc = v;
	return true;
}

// The first element seeds as an ordinary value, a reduced box included: only step results end the walk.
bool clj_reducer_step(clj_reducer *r, clj_value item) {
	if (r->acc == CLJ_UNBOUND) {
		r->acc = clj_retain(item);
		return true;
	}
	r->args[0] = r->acc;
	r->args[1] = item;
	return take(r, clj_call_invoke(&r->call, r->args));
}

bool clj_reducer_step_kv(clj_reducer *r, clj_value key, clj_value val) {
	CLJ_ASSERT(r->acc != CLJ_UNBOUND, "reduce-kv needs an init");
	r->args[0] = r->acc;
	r->args[1] = key;
	r->args[2] = val;
	return take(r, clj_call_invoke(&r->call, r->args));
}

clj_value clj_reducer_finish(clj_reducer *r) {
	clj_value acc = r->acc;
	r->acc = CLJ_NIL;
	return acc == CLJ_UNBOUND ? clj_invoke(r->call.f, NULL, 0) : acc;
}

void clj_reducer_drop(clj_reducer *r) {
	if (r->acc != CLJ_UNBOUND && r->acc != CLJ_THROWN) clj_release(r->acc);
	r->acc = CLJ_NIL;
}

clj_value clj_reduce_empty(clj_value f, clj_value init) { return init == CLJ_UNBOUND ? clj_invoke(f, NULL, 0) : clj_retain(init); }

clj_value clj_reduce_iter(clj_value coll, clj_value f, clj_value init) {
	clj_reducer  r = clj_reducer_start(f, init, 2);
	clj_seq_iter it = clj_seq_iter_start(coll);
	clj_value    item;
	while (clj_seq_iter_next(&it, &item)) {
		if (!clj_reducer_step(&r, item)) {
			clj_seq_iter_close(&it);
			break;
		}
	}
	if (it.thrown) {
		clj_reducer_drop(&r);
		return CLJ_THROWN;
	}
	return clj_reducer_finish(&r);
}

clj_value clj_reduce(clj_value f, clj_value init, clj_value coll) {
	if (clj_is_nil(coll)) return clj_reduce_empty(f, init);
	const clj_type *t = clj_is_ptr(coll) ? clj_type_of(coll) : NULL;
	if (t && t->reduce) return t->reduce(coll, f, init);
	clj_value s = clj_seq(coll);
	if (s == CLJ_THROWN) return s;
	clj_value r = clj_is_nil(s) ? clj_reduce_empty(f, init) : clj_type_of(s)->reduce ? clj_type_of(s)->reduce(s, f, init) : clj_reduce_iter(s, f, init);
	clj_release(s);
	return r;
}

clj_value clj_reduce_kv(clj_value f, clj_value init, clj_value coll) {
	if (clj_is_nil(coll)) return clj_retain(init);
	if (clj_is_map(coll)) return clj_map_reduce_kv(coll, f, init);
	if (clj_is_vector(coll)) return clj_vector_reduce_kv(coll, f, init);
	return clj_throw_msg("reduce-kv not supported on this type: %s", clj_type_name(coll));
}
