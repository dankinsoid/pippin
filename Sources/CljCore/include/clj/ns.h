// @ai-generated(guided)
#ifndef CLJ_NS_H
#define CLJ_NS_H

#include "object.h"

// Immortal, like vars. Every lookup and mutation goes through one process-wide mutex.
typedef struct {
	clj_header h;
	clj_value  name;     // symbol
	clj_value  mappings; // map symbol → var interned here
	clj_value  refers;   // map symbol → var referred from other namespaces
	clj_value  aliases;  // map symbol → namespace
	clj_value  excludes; // set of clojure.core names this namespace does not see unqualified, or nil
} clj_ns;

extern const clj_type clj_ns_type;

static inline bool    clj_is_ns(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_ns_type; }
static inline clj_ns *clj_ns_of(clj_value v) { return (clj_ns *)clj_to_ptr(v); }
// Borrowed: the namespace never dies.
static inline clj_value clj_ns_name(clj_value ns) { return clj_ns_of(ns)->name; }

// name is an unqualified symbol. Borrowed results; nil when not found.
clj_value clj_ns_find(clj_value name);
clj_value clj_ns_find_or_create(clj_value name);
// Every namespace, as an owned vector in creation order.
clj_value clj_ns_all(void);
// The var mapped to sym in ns, created unbound when missing. sym must be unqualified.
clj_value clj_ns_intern(clj_value ns, clj_value sym);
void      clj_ns_refer(clj_value ns, clj_value sym, clj_value var);
// Removes sym from the namespace's own mappings and refers; a var that was mapped stays alive (vars are immortal).
void      clj_ns_unmap(clj_value ns, clj_value sym);
// alias → target; replaces a previous alias of the same name.
void      clj_ns_alias(clj_value ns, clj_value alias, clj_value target);
// Borrowed snapshots of the maps, valid until the next mutation of the namespace on any thread; retain to keep.
clj_value clj_ns_mappings(clj_value ns);
clj_value clj_ns_refers(clj_value ns);
clj_value clj_ns_aliases(clj_value ns);
// The clojure.core names the unqualified fallback skips (:refer-clojure :exclude). excludes is a set or nil.
void      clj_ns_set_excludes(clj_value ns, clj_value excludes);
// Qualified symbols reach private vars too: the analyzer refuses those, (var ns/x) does not.
clj_value clj_ns_resolve(clj_value ns, clj_value sym);
// The namespace an alias or a namespace name reaches from ns; nil when neither.
clj_value clj_ns_resolve_ns(clj_value ns, clj_value name);

// The value of clojure.core/*ns*: this thread's binding when it has one, else the root (`user` until set).
clj_value clj_ns_current(void);
// Sets the thread's binding of *ns* when there is one, else its root.
void      clj_ns_set_current(clj_value ns);
// The var clojure.core/*ns* itself (dynamic, root `user`).
clj_value clj_ns_var(void);
clj_value clj_ns_core(void);
clj_value clj_ns_user(void);

#endif
