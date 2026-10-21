// @ai-generated(solo)
// The rooting hazard analysis must fail on each line marked `expect:` and on no other (make rooting-hazards).
#include "clj/core.h"
#include "roots_internal.h"

CLJ_MAY_PARK void fx_park(void);
clj_value fx_make(void);

typedef struct {
	CLJ_NO_PARK void (*quiet)(void);
	void (*loud)(void);
} fx_hooks;

CLJ_NO_PARK typedef void (*fx_visit)(clj_value v);

static void fx_parks(void) { fx_park(); }
static void fx_still(void) {}
static void fx_visit_still(clj_value v) { (void)v; }
static void fx_visit_parks(clj_value v) {
	(void)v;
	fx_park();
}
static void fx_each(clj_value coll, fx_visit visit) { visit(coll); }

static const fx_hooks fx_good = {.quiet = fx_still, .loud = fx_parks};
static const fx_hooks fx_bad = {.quiet = fx_parks, .loud = fx_still}; // expect: fx_parks goes into the quiet slot, which is CLJ_NO_PARK

clj_value fx_through_quiet(const fx_hooks *h) {
	clj_value x = fx_make();
	h->quiet();
	return x;
}

clj_value fx_through_loud(const fx_hooks *h) {
	clj_value x = fx_make();
	h->loud(); // expect: x holds a reference (fx_make's result) across a call that may park (the loud slot)
	return x;
}

clj_value fx_through_pointer(void (*f)(void)) {
	clj_value x = fx_make();
	f(); // expect: x holds a reference (fx_make's result) across a call that may park (the function pointer f)
	return x;
}

clj_value fx_transitive(void) {
	clj_value x = fx_make();
	fx_parks(); // expect: x holds a reference (fx_make's result) across a call that may park (fx_parks, which calls fx_park)
	return x;
}

clj_value fx_visitors(void) {
	clj_value x = fx_make();
	fx_each(x, fx_visit_still);
	fx_each(x, fx_visit_parks); // expect: fx_visit_parks goes into parameter 2 of fx_each, which is CLJ_NO_PARK
	return x;
}

const fx_hooks *fx_tables(void) { return fx_good.quiet ? &fx_good : &fx_bad; }
