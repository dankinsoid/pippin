// @ai-generated(solo)
// The rooting hazard analysis must fail on each line marked `expect:` and on no other (make rooting-hazards).
#include <stdlib.h>

#include "clj/core.h"
#include "roots_internal.h"

CLJ_MAY_PARK void fx_park(void);
clj_value fx_make(void);
clj_value fx_pair(CLJ_OWNED clj_value a, clj_value b);
clj_value fx_parks_too(void);
void      fx_take(CLJ_OWNED clj_value v);

// A heap stack: each entry owns its meta; its view is borrowed.
typedef struct {
	clj_value              meta;
	CLJ_BORROWED clj_value form;
	int                    kind;
} fx_entry;

typedef struct {
	fx_entry *items;
	size_t    n, cap;
} fx_stack;

typedef struct {
	CLJ_BORROWED fx_stack *stack;
	int                    depth;
} fx_walk;

clj_value fx_stack_unnamed(void) {
	fx_stack s = {0};
	s.items = calloc(1, sizeof *s.items);
	s.items[s.n++].meta = fx_make();
	fx_park(); // expect: s.items.*.meta holds a reference
	return s.items[0].meta;
}

clj_value fx_stack_named(void) {
	fx_stack s = {0};
	CLJ_ROOTS_FIELD(&s.items, &s.n, meta);
	s.items = calloc(1, sizeof *s.items);
	s.items[s.n++].meta = fx_make();
	fx_park();
	return s.items[0].meta;
}

// A field registration names its field alone.
typedef struct {
	clj_value meta, extra;
} fx_wide;

typedef struct {
	fx_wide *items;
	size_t   n;
} fx_wide_stack;

clj_value fx_stack_other_field(void) {
	fx_wide_stack s = {0};
	CLJ_ROOTS_FIELD(&s.items, &s.n, meta);
	s.items = calloc(1, sizeof *s.items);
	s.n = 1;
	fx_park(); // expect: s.items.*.extra holds a reference
	return s.items[0].meta;
}

// A pointer into a heap stack is a view, and so is a borrowed back pointer.
clj_value fx_stack_view(fx_stack *s, fx_walk *w) {
	fx_entry *top = &s->items[s->n - 1];
	fx_walk   inner = {w->stack, w->depth + 1};
	fx_park();
	return top->meta + (clj_value)inner.depth;
}

// A linked list is a heap stack too; a walker names it.
typedef struct fx_node {
	clj_value       value;
	struct fx_node *next;
} fx_node;

typedef struct {
	fx_node *head;
} fx_list;

clj_value fx_list_unnamed(void) {
	fx_list l = {calloc(1, sizeof(fx_node))};
	fx_park(); // expect: l.head.*.value holds a reference
	return l.head->value;
}

static void fx_list_walk(const void *var, void (*named)(clj_value v, void *ctx), void *ctx) {
	for (const fx_node *n = ((const fx_list *)var)->head; n; n = n->next) named(n->value, ctx);
}

clj_value fx_list_walked(void) {
	fx_list l = {calloc(1, sizeof(fx_node))};
	CLJ_ROOTS_WALK(&l, fx_list_walk);
	fx_park();
	return l.head->value;
}

// A call's result no variable holds lives while a sibling operand parks.
clj_value fx_temporary(void) {
	return fx_pair(fx_make(), fx_parks_too()); // expect: fx_make's result is held by no variable while fx_parks_too
}

clj_value fx_temporary_in_initializer(void) {
	clj_value pair[2] = {fx_make(), fx_parks_too()}; // expect: fx_make's result is held by no variable
	return pair[0] + pair[1];
}

clj_value fx_temporary_ordered(void) {
	clj_value b = fx_parks_too();
	CLJ_ROOTS(&b);
	clj_value r = fx_pair(fx_make(), b);
	return r;
}

clj_value fx_parks_too(void) {
	fx_park();
	return CLJ_NIL;
}
