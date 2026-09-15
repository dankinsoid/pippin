// @ai-generated(guided)
#ifndef CLJ_RUNTIME_H
#define CLJ_RUNTIME_H

#include <stddef.h>

#include "value.h"

// Bootstraps clojure.core (builtins, then the embedded core.clj) and user. Once per process; a boot failure is fatal.
void clj_init(void);

// Runs at the end of clj_init, after core.clj, when the host defines it: where a host binds its own vars into
// clojure.core (the Swift module binds its primitives here). Weak, so a C-only host needs nothing.
void clj_host_boot(void) __attribute__((weak));

// The embedded boot/core.clj, borrowed for the life of the process.
const char *clj_core_source(size_t *len);

// Where println/prn write. Process-wide; NULL restores stdout.
typedef void (*clj_output_fn)(const char *bytes, size_t len, void *ctx);
void clj_set_output(clj_output_fn fn, void *ctx);
void clj_output(const char *bytes, size_t len);

// Interns every builtin into clojure.core; clj_init calls it.
void clj_builtins_install(void);

// clj_reader.resolve: qualifies in the current namespace, or the var's own; special forms stay bare. ctx unused.
clj_value clj_syntax_quote_resolve(clj_value sym, void *ctx);

#endif
