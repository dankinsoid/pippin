// @ai-generated(guided)
#ifndef CLJ_RUNTIME_H
#define CLJ_RUNTIME_H

#include <stddef.h>

#include "fn.h"
#include "reader.h"
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
// Binds one native fn as clojure.core/name.
void clj_builtin_bind(const char *name, clj_native_fn fn, uint32_t min_arity, uint32_t max_arity);
// The namespace, var and load builtins (builtins_ns.c) and the string ones (builtins_string.c); clj_builtins_install calls them.
void clj_ns_builtins_install(void);
void clj_string_builtins_install(void);
void clj_number_builtins_install(void);
void clj_array_builtins_install(void);

// clj_reader.resolve: qualifies in the current namespace, or the var's own; an alias prefix is expanded. ctx unused.
clj_value clj_syntax_quote_resolve(clj_value sym, void *ctx);
// clj_reader.resolve_ns: the current namespace's name, or the one an alias reaches. ctx unused.
clj_value clj_reader_resolve_ns(clj_value alias, void *ctx);
// clj_reader.read_tag: record literals, *data-readers*, the built-in tags, *default-data-reader-fn*. ctx unused.
clj_value clj_reader_read_tag(clj_value tag, clj_value form, void *ctx);
// Sets the resolvers and the tag reader on a reader: how a host reads code for the current namespace.
void clj_reader_use_namespaces(clj_reader *r);

// println/prn go to the innermost capture on this thread while one is open (with-out-str).
void      clj_output_push_capture(void);
// The bytes written since the matching push, as an owned string.
clj_value clj_output_pop_capture(void);

// ---- loading source files (load.c)
// Directories `require` searches, copied; replaces the previous list.
void clj_load_path_set(const char *const *roots, size_t n);
// The file for a lib path such as "medley/core": an embedded lib as "<embedded>/…", else <root>/path.cljc or .clj.
// Owned string, nil when none.
clj_value clj_load_resource_path(const char *lib_path);
// Reads and evaluates the file at path (an embedded one through its "<embedded>/" marker) with *ns* and *file* bound. nil or CLJ_THROWN.
clj_value clj_load_file(clj_value path);
// The same over bytes; file names the source for *file* and error positions (a string or nil).
clj_value clj_load_source(const char *bytes, size_t len, clj_value file);
// A boot lib embedded in the binary ("clojure/set"); NULL when none.
const char *clj_embedded_source(const char *lib_path, size_t *len);
// Lenient loading: a failing top-level form is recorded and skipped rather than ending the load. For the corpus harness.
void      clj_load_set_lenient(bool on);
// The failures recorded so far as an owned vector of {:file :line :column :name :message}; clears them.
clj_value clj_load_take_failures(void);
// clojure.core/*file*: the path being loaded, bound by load.
clj_value clj_load_file_var(void);

#endif
