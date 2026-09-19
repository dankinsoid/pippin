// @ai-generated(guided)
#ifndef CLJ_ATOM_H
#define CLJ_ATOM_H

#include "cmutex.h"
#include "object.h"

// A publication point: everything stored into it is shared first (design §4, "Атомы").
typedef struct {
	clj_header        h;
	clj_cmutex        lock;
	uint8_t           affinity;  // CLJ_AFFINITY_MAIN: every access checks the carrier (dev guarantee)
	_Atomic uintptr_t owner;     // execution holding the lock, 0 when none: the nested-swap trap reads it
	_Atomic clj_value value;     // read without the lock inside a reader window (deref)
	clj_value         meta;      // map or nil
	clj_value         validator; // fn or nil
	clj_value         watches;   // map key -> fn, or nil
} clj_atom;

extern const clj_type clj_atom_type;

// value, meta and validator are shared and retained; a validator that rejects value throws "Invalid reference state".
clj_value clj_atom_new(clj_value value, clj_value meta, clj_value validator);

static inline bool      clj_is_atom(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_atom_type; }
static inline clj_atom *clj_atom_of(clj_value v) { return (clj_atom *)clj_to_ptr(v); }

// The affinity of a new atom, before it is shared; :main makes every access off the main carrier throw.
void clj_atom_set_affinity(clj_value atom, int affinity);
// Borrowed current value, no window: for a caller that knows no writer runs (tests, the bench).
static inline clj_value clj_atom_value_borrowed(clj_value atom) { return atomic_load_explicit(&clj_atom_of(atom)->value, memory_order_acquire); }
// Owned current value, without the lock: a UI read never waits for another execution's f.
clj_value clj_atom_deref(clj_value atom);
// (reset! a v): validates, stores v shared, notifies the watches; returns v owned.
clj_value clj_atom_reset(clj_value atom, clj_value value);
// f runs once under the lock on the value borrowed from the atom; a throw from f or a rejecting
// validator leaves the value as it was.
clj_value clj_atom_swap(clj_value atom, clj_value f, const clj_value *args, size_t nargs);
// [old new] as a vector.
clj_value clj_atom_swap_vals(clj_value atom, clj_value f, const clj_value *args, size_t nargs);
clj_value clj_atom_reset_vals(clj_value atom, clj_value value);
// true when the current value is identical to expected and was replaced.
clj_value clj_atom_compare_and_set(clj_value atom, clj_value expected, clj_value value);
// (f key atom old new) after every change; key by equality. Return the atom owned.
clj_value clj_atom_add_watch(clj_value atom, clj_value key, clj_value f);
clj_value clj_atom_remove_watch(clj_value atom, clj_value key);
// fn or nil; setting validates the current value first. Return nil owned.
clj_value clj_atom_set_validator(clj_value atom, clj_value f);
clj_value clj_atom_get_validator(clj_value atom);
// Owned meta; setting shares and retains the map (or nil) under the lock and returns it owned.
clj_value clj_atom_meta(clj_value atom);
clj_value clj_atom_reset_meta(clj_value atom, clj_value m);
// (alter-meta! a f args...): f runs once under the lock on the current meta; returns the new meta owned.
clj_value clj_atom_alter_meta(clj_value atom, clj_value f, const clj_value *args, size_t nargs);

#endif
