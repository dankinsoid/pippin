// @ai-generated(solo)
// The rooting hazard analysis must fail on each line marked `expect:` and on no other (make rooting-hazards).
#include "clj/core.h"

typedef struct {
	clj_header        h;
	clj_slot          first;
	clj_private_value own;
	clj_value         raw; // expect: fx_cell declares a clj_value field raw
} fx_cell;

typedef struct {
	fx_cell   base;
	clj_value more; // expect: fx_sub declares a clj_value field more
} fx_sub;

typedef struct {
	clj_value a;
	int       n;
} fx_plain;

void fx_store(fx_cell *c, clj_value v) {
	clj_slot_store(&c->h, &c->first, v);
	c->own = v;
}

void fx_raw(fx_cell *c, clj_value v) {
	c->first.v = v; // expect: writes a slot's word directly
}

clj_value *fx_address(fx_cell *c) {
	return &c->first.v; // expect: writes a slot's word directly
}

clj_value fx_read(const fx_cell *c, fx_plain *p) {
	p->a = c->first.v;
	return p->a;
}
