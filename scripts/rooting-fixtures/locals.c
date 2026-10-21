// @ai-generated(solo)
// The rooting hazard analysis must fail on each line marked `expect:` and on no other (make rooting-hazards).
#include <stdlib.h>

#include "clj/core.h"
#include "roots_internal.h"

CLJ_MAY_PARK void fx_park(void);
clj_value fx_make(void);
CLJ_BORROWED clj_value fx_peek(clj_value coll);
CLJ_NO_CYCLE clj_value fx_name(void);
void fx_fill(clj_value *out);
clj_value fx_consume(CLJ_OWNED clj_value coll);

static clj_value fx_parking_consume(CLJ_OWNED clj_value coll) {
	CLJ_ROOTS(&coll);
	fx_park();
	return coll;
}

clj_value fx_unnamed(void) {
	clj_value x = fx_make();
	fx_park(); // expect: x holds a reference (fx_make's result
	return x;
}

clj_value fx_registered(void) {
	clj_value x = fx_make();
	CLJ_ROOTS(&x);
	fx_park();
	return x;
}

clj_value fx_dead_after(void) {
	clj_value x = fx_make();
	clj_release(x);
	fx_park();
	return CLJ_NIL;
}

clj_value fx_borrowed(clj_value coll) {
	clj_value              a = fx_peek(coll);
	clj_value              b = coll;
	CLJ_BORROWED clj_value c = fx_make();
	clj_value              d = fx_name();
	fx_park();
	return a + b + c + d;
}

clj_value fx_param_released(clj_value coll) {
	fx_park(); // expect: coll holds a reference (handed to clj_release
	clj_release(coll);
	return CLJ_NIL;
}

clj_value fx_param_owned(CLJ_OWNED clj_value coll) {
	fx_park(); // expect: coll holds a reference (CLJ_OWNED)
	return coll;
}

clj_value fx_out_param(void) {
	clj_value x;
	fx_fill(&x);
	fx_park(); // expect: x holds a reference (written through its address by fx_fill
	return x;
}

clj_value fx_escaped(void) {
	clj_value  x = fx_make();
	clj_value *p = &x;
	fx_park(); // expect: x holds a reference
	return *p;
}

clj_value fx_loop(void) {
	clj_value acc = fx_make();
	for (int i = 0; i < 3; i++) {
		fx_park(); // expect: acc holds a reference
		acc = fx_consume(acc);
	}
	return acc;
}

clj_value fx_released_then_park(void) {
	clj_value x = fx_make();
	CLJ_ROOTS(&x);
	clj_release(x);
	fx_park(); // expect: x is named for the collector across a call that may park (fx_park, which is CLJ_MAY_PARK), but it was handed over before
	return CLJ_NIL;
}

clj_value fx_released_cleared(void) {
	clj_value x = fx_make();
	CLJ_ROOTS(&x);
	clj_release(x);
	x = CLJ_NIL;
	fx_park();
	return CLJ_NIL;
}

clj_value fx_handed_to_parker(void) {
	clj_value x = fx_make();
	CLJ_ROOTS(&x);
	x = fx_parking_consume(x); // expect: it is handed to the call itself; clear it first
	return x;
}

clj_value fx_handed_cleared(void) {
	clj_value x = fx_make();
	CLJ_ROOTS(&x);
	clj_value t = x;
	x = CLJ_NIL;
	x = fx_parking_consume(t);
	return x;
}

clj_value fx_unassigned(void) {
	clj_value x;
	CLJ_ROOTS(&x); // expect: CLJ_ROOTS names x before it is assigned on every path
	x = fx_make();
	fx_park();
	return x;
}

clj_value fx_rooted_copy(clj_value coll) {
	clj_value x = coll;
	CLJ_ROOTS(&x); // expect: CLJ_ROOTS names x, which only copies +0 references here
	fx_park();
	return x;
}

clj_value fx_rooted_borrowed(void) {
	CLJ_BORROWED clj_value x = fx_make();
	CLJ_ROOTS(&x); // expect: which is declared CLJ_BORROWED
	fx_park();
	return x;
}

clj_value fx_scope_ended(void) {
	clj_value x = fx_make();
	{
		CLJ_ROOTS(&x);
		fx_park();
	}
	fx_park(); // expect: x holds a reference (fx_make's result
	return x;
}

clj_value fx_span(size_t n) {
	clj_value *items = malloc(n * sizeof *items);
	size_t     k = 0;
	CLJ_ROOTS_SPAN(&items, &k);
	for (; k < n; k++) {
		items[k] = fx_make();
		fx_park();
	}
	clj_value r = items[0];
	free(items);
	return r;
}

clj_value fx_span_unnamed(size_t n) {
	clj_value *items = malloc(n * sizeof *items);
	for (size_t k = 0; k < n; k++) {
		items[k] = fx_make();
		fx_park(); // expect: items holds a reference (malloc's result
	}
	clj_value r = items[0];
	free(items);
	return r;
}

clj_value fx_sizeof(void) {
	fx_park();
	clj_value *q = malloc(sizeof *q);
	*q = CLJ_NIL;
	clj_value r = *q;
	free(q);
	return r;
}

clj_value fx_for_init(clj_value specs) {
	for (clj_value s = fx_peek(specs); !clj_is_nil(s); s = fx_consume(s)) {
		fx_park(); // expect: s holds a reference (handed to fx_consume
	}
	return CLJ_NIL;
}
