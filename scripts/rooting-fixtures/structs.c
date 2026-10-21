// @ai-generated(solo)
// The rooting hazard analysis must fail on each line marked `expect:` and on no other (make rooting-hazards).
#include "clj/core.h"
#include "roots_internal.h"

CLJ_MAY_PARK void fx_park(void);
clj_value fx_make(void);

typedef struct {
	clj_value              acc;
	CLJ_BORROWED clj_value f;
	CLJ_NO_CYCLE clj_value name;
	clj_value              args[2];
	int                    n;
} fx_state;

typedef struct {
	fx_state  inner;
	clj_value extra;
} fx_outer;

clj_value fx_struct_unnamed(clj_value f) {
	fx_state s = {.acc = fx_make(), .f = f};
	fx_park(); // expect: s.acc holds a reference (a field of fx_state not declared CLJ_BORROWED)
	return s.acc + s.args[0];
}

clj_value fx_struct_named(clj_value f) {
	fx_state s = {.acc = fx_make(), .f = f};
	CLJ_ROOTS(&s.acc, &s.args[0], &s.args[1]);
	fx_park();
	return s.acc + s.args[0];
}

clj_value fx_nested(void) {
	fx_outer o = {{0}};
	CLJ_ROOTS(&o.inner.acc, &o.inner.args[0], &o.inner.args[1]);
	fx_park(); // expect: o.extra holds a reference
	return o.extra;
}

clj_value fx_struct_borrowed(fx_state *from) {
	CLJ_BORROWED fx_state s = *from;
	fx_park();
	return s.acc;
}

clj_value fx_by_address(void) {
	fx_state s = {0};
	CLJ_ROOTS(&s.acc, &s.args[0], &s.args[1]);
	fx_park();
	clj_value r = s.acc;
	s.acc = CLJ_NIL;
	return r;
}
