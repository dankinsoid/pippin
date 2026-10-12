// @ai-generated(solo)
#ifndef CLJ_VERIFY_H
#define CLJ_VERIFY_H

#include "analyzer.h"

// Design §3 item 6; the invariants are listed in NOTES "Tree verifier".

static const uint32_t CLJ_VERIFY_NUMBERED = 1u << 0; // ids, subtree sizes and invoke sites are assigned
static const uint32_t CLJ_VERIFY_ANALYZED = 1u << 1; // straight from the analyzer: no node kind only the optimizer makes
static const uint32_t CLJ_VERIFY_MARKS    = 1u << 2; // last-use marks may be present

typedef struct {
	const char     *kind; // stable: "verify/<invariant>"
	const clj_node *at;
	char            message[256];
} clj_verify_violation;

// False with the first violation; in every build, for tests.
bool clj_verify_tree_check(const clj_node *root, uint32_t flags, clj_verify_violation *out);
// The internal error on a violation; nothing when CLJ_VERIFY=0.
void clj_verify_tree(const clj_node *root, const char *pass, uint32_t flags);
// Everything a backend reads off a tree: equal across a pass that must not write it.
uint64_t clj_verify_digest(const clj_node *root);
void     clj_verify_unchanged(const clj_node *root, uint64_t before, const char *pass);
// CLJ_VERIFY=0 turns the checks off in a debug build, for measuring them.
bool clj_verify_enabled(void);
// The body's own frame: slots a direct fn body reads through the static link, slots a closure made here
// captures, slots it rebinds.
void clj_verify_frame_slots(const clj_node *body, uint64_t *linked, uint64_t *captured, uint64_t *written);
// A compiled frame's drop points, numbered as reuse.h's cljc_reuse_drops numbers them.
typedef uint64_t (*clj_verify_drop_fn)(void *ctx, const clj_node *node, uint32_t at);
// Forward, apart from the plan's backward pass: no read after a drop, no drop of a held or pinned slot.
bool clj_verify_drops(const clj_node *body, uint64_t recur_slots, clj_verify_drop_fn drops, void *ctx, clj_verify_violation *out);

#if CLJ_DEBUG
#define CLJ_VERIFY_TREE(root, pass, flags) clj_verify_tree((root), (pass), (flags))
#define CLJ_VERIFY_DIGEST(root) (clj_verify_enabled() ? clj_verify_digest(root) : 0)
#define CLJ_VERIFY_UNCHANGED(root, before, pass) clj_verify_unchanged((root), (before), (pass))
#else
#define CLJ_VERIFY_TREE(root, pass, flags) ((void)0)
#define CLJ_VERIFY_DIGEST(root) ((uint64_t)0)
#define CLJ_VERIFY_UNCHANGED(root, before, pass) ((void)(before))
#endif

#endif
