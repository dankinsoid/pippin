// @ai-generated(guided)
// The optimizer pass: rewrites an analyzed tree in place before it is numbered, so what it produces is what
// serializes and what every evaluator and emitter sees. One rewrite so far: a call through a core var the
// intrinsics table lists becomes an INTRINSIC node.
#include <stdlib.h>

#include "clj/analyzer.h"
#include "clj/intrinsics.h"
#include "node.h"

// Keyed by the var the head resolved to: a local shadowing the name or a user namespace's own `+` stays an
// INVOKE. The var is kept for the runtime guard (eval.c), the VAR node itself goes.
static void rewrite_invoke(clj_node *n) {
	const clj_node *head = n->u.invoke.fn;
	if (head->kind != CLJ_NODE_VAR) return;
	const clj_intrinsic *op = clj_intrinsic_find(head->u.var, n->u.invoke.n);
	if (!op) return;
	const clj_node **args = n->u.invoke.args;
	uint32_t         nargs = n->u.invoke.n;
	clj_value        var = clj_retain(head->u.var);
	clj_release(clj_from_ptr((void *)head));
	n->kind = CLJ_NODE_INTRINSIC;
	n->u.intrinsic.op = op;
	n->u.intrinsic.var = var;
	n->u.intrinsic.args = args;
	n->u.intrinsic.n = nargs;
}

// The tree is still the analyzer's own here: the const on the visitor's argument is dropped once.
static void optimize(const clj_node *n, void *ctx) {
	clj_node *m = (clj_node *)n;
	if (m->kind == CLJ_NODE_INVOKE) rewrite_invoke(m);
	clj_node_children(m, optimize, ctx);
}

void clj_optimize(clj_node *root) { optimize(root, NULL); }
