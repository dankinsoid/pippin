// @ai-generated(solo)
#include "thunk.h"

#include <string.h>

#include "clj/ice.h"
#include "ice_internal.h"

typedef struct {
	const clj_node        *fn;
	const cljc_reuse_plan *plan;
	cljc_let_go           *out;
	size_t                 n;
} walk;

static bool is_cons(const clj_node *n) {
	return n->kind == CLJ_NODE_INTRINSIC && n->u.intrinsic.op->arity == 2 && strcmp(n->u.intrinsic.op->name, "clojure.core/cons") == 0;
}

static uint64_t captures_mask(uint32_t ncaptures) { return ncaptures >= 64 ? UINT64_MAX : ((uint64_t)1 << ncaptures) - 1; }

// Only the forms that hand their tail's value back untouched: a try's finally or a catch may still fail.
static void tail_walk(const clj_node *n, walk *w) {
	switch (n->kind) {
	case CLJ_NODE_IF:
		tail_walk(n->u.if_.then, w);
		if (n->u.if_.else_) tail_walk(n->u.if_.else_, w);
		return;
	case CLJ_NODE_LET:
	case CLJ_NODE_LOOP: tail_walk(n->u.let.body, w); return;
	case CLJ_NODE_DO:
		if (n->u.seq.n) tail_walk(n->u.seq.items[n->u.seq.n - 1], w);
		return;
	case CLJ_NODE_INLINE: tail_walk(n->u.inline_.body, w); return;
	case CLJ_NODE_INTRINSIC: {
		const cljc_reuse_pair *p = cljc_reuse_at(w->plan, n->id);
		if (!is_cons(n) || !p || p->kind != CLJC_REUSE_TOKEN || w->n == CLJC_LET_GO_MAX) return;
		uint64_t mask = captures_mask(w->fn->u.fn.ncaptures);
		for (uint32_t i = 0; i < 2; i++) {
			const clj_node *a = n->u.intrinsic.args[i];
			if (a->kind == CLJ_NODE_CAPTURED && a->u.index < 64) mask &= ~((uint64_t)1 << a->u.index);
		}
		if (mask) w->out[w->n++] = (cljc_let_go){n->id, mask};
		return;
	}
	default: return;
	}
}

size_t cljc_thunk_let_go(const clj_node *site, const cljc_reuse_plan *plan, cljc_let_go out[CLJC_LET_GO_MAX]) {
	if (!clj_lazy_seq_site(site)) clj_ice("thunk/not-a-lazy-seq-site", site, "a let-go plan asked of a call no lazy-seq cell is built at");
	const clj_node *fn = site->u.invoke.args[0];
	if (clj_opt_off(CLJ_NO_OPT_OWNED) || clj_opt_off(CLJ_NO_OPT_EMIT) || !plan || !fn->u.fn.ncaptures) return 0;
	walk w = {fn, plan, out, 0};
	tail_walk(fn->u.fn.fixed[0]->body, &w);
	return w.n;
}
