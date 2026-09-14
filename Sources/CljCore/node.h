// @ai-generated(guided)
#ifndef CLJ_NODE_H
#define CLJ_NODE_H

#include "clj/analyzer.h"

// Shared by the analyzer and the tree codec (node_data.c); a finished tree is const to everyone else.

// Zeroed node of the kind; arrays and children are the caller's.
clj_node *clj_node_alloc(clj_node_kind kind);
// Rewrites the tree in place (optimizer.c): INVOKE of a listed core var into INTRINSIC. Runs on an analyzed,
// unnumbered tree; a tree read from data is taken as already optimized.
void clj_optimize(clj_node *root);
// Assigns ids in pre-order from 0 and every nnodes; the last step before a tree is handed out.
void clj_node_number(clj_node *root);

#endif
