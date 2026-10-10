// @ai-generated(solo)
// Drop-guided reuse for compiled code (design §7 «Perceus и Lean 4: что взято техникой»; the pairs are the compiler's).
#ifndef CLJ_REUSE_INTERNAL_H
#define CLJ_REUSE_INTERNAL_H

#include "clj/coll.h"
#include "clj/object.h"

// Frame-limited (Lorenzen and Leijen 2022): made right before its allocation, never stored, passed or held over a call.
typedef clj_header *clj_reuse_token;

clj_reuse_token clj_drop_reuse_slow(clj_header *h);
clj_value       clj_seq_cons_into(clj_reuse_token tok, clj_value x, clj_value coll);
clj_value       clj_next_owned_slow(clj_value s);
clj_value       clj_rest_owned_slow(clj_value s);

// The common answer inline: a value someone else holds costs the release it would have cost at the frame's exit.
static inline bool clj_reuse_candidate(clj_value x) {
	if (!clj_is_ptr(x)) return false;
	const clj_header *h = clj_header_of(x);
	return !(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL | CLJ_FLAG_MUTABLE)) && CLJ_RC_UNSHARED_LOAD(h) == 1;
}

// x at +1: its cell, children still in it, when x is unique with nothing to run at its death; else x released, NULL.
static inline clj_reuse_token clj_drop_reuse(clj_value x) {
	CLJ_STAT(CLJ_STAT_TOKEN_MADE);
	if (clj_reuse_candidate(x)) return clj_drop_reuse_slow(clj_header_of(x));
	clj_release(x);
	return NULL;
}

// A token no allocation took: the object dies as a plain release would kill it.
static inline void clj_reuse_free(clj_reuse_token tok) {
	if (tok) clj_release(clj_from_ptr(tok));
}

// An uninitialized object of type in tok's cell, its old children released, when the size class matches; a fresh
// cell otherwise, tok freed.
void *clj_alloc_at(clj_reuse_token tok, const clj_type *type, size_t size);

// clj_seq_cons in tok's cell. A dying cons or list keeps every field the new cell holds again.
static inline clj_value clj_seq_cons_at(clj_reuse_token tok, clj_value x, clj_value coll) {
	return tok ? clj_seq_cons_into(tok, x, coll) : clj_seq_cons(x, coll);
}

// next and rest of s at +1: a unique vector-seq, range or string-seq steps in its own cell.
static inline clj_value clj_next_owned(clj_value s) {
	CLJ_STAT(CLJ_STAT_TOKEN_MADE);
	if (clj_reuse_candidate(s)) return clj_next_owned_slow(s);
	clj_value r = clj_next(s);
	clj_release(s);
	return r;
}

static inline clj_value clj_rest_owned(clj_value s) {
	CLJ_STAT(CLJ_STAT_TOKEN_MADE);
	if (clj_reuse_candidate(s)) return clj_rest_owned_slow(s);
	clj_value r = clj_rest(s);
	clj_release(s);
	return r;
}

#endif
