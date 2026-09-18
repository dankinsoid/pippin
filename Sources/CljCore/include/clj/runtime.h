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
// Runs last in clj_init; the compiler library's strong definition replaces the empty weak one here (CLJ_EVAL=compiled).
void clj_compiled_eval_boot(void);

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
void clj_format_builtins_install(void);
// (str v) of one value, owned; CLJ_THROWN when printing it throws.
clj_value clj_str_value(clj_value v);

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
// Records a failure of a top-level form (lenient loading), or wraps the pending exception as Clojure's CompilerException
// and leaves it pending: true when loading may go on. What a compiled unit's init calls where the loader would.
bool clj_load_form_failed(clj_value file, uint32_t line, uint32_t col, clj_value name);

// ---- the load hook: what a compiler plugs in to see every top-level tree the loader analyzes
typedef struct clj_node clj_node;

typedef struct {
	clj_value file;      // *file* at the time, a string or nil
	uint32_t  line, col; // of the top-level form the node came from
	clj_value name;      // the symbol a (def... name) form defines, else nil
	uint64_t  serial;    // one per top-level form: the nodes of a (do ...) share it
} clj_load_form;

// Called with the optimized tree of a top-level form before it runs, the node borrowed for the call. A hook that
// runs the form itself sets *handled and returns its owned result (or CLJ_THROWN); else the interpreter runs it.
typedef clj_value (*clj_load_hook_form)(const clj_load_form *form, const clj_node *node, void *ctx, bool *handled);
// A form that failed to read or analyze under lenient loading, after the nodes of its serial that did analyze.
typedef void (*clj_load_hook_failed)(const clj_load_form *form, clj_value message, void *ctx);

typedef struct {
	clj_load_hook_form   form;
	clj_load_hook_failed failed; // may be NULL
	void                *ctx;
	// Fires on every top-level clj_eval a host makes while nothing runs on the thread, not only on loads.
	bool toplevel;
} clj_load_hook;

// Process-wide; NULL clears it. The hook sees loads (clj_load_source, core.clj at boot) and, with toplevel, host evals.
void clj_load_set_hook(const clj_load_hook *hook);
const clj_load_hook *clj_load_hook_get(void);

// ---- compiled units (compiled.h): a unit registered for a path replaces reading its source in clj_load_file
typedef clj_value (*clj_compiled_init)(void);
void clj_compiled_register(const char *path, clj_compiled_init init);
clj_compiled_init clj_compiled_find(const char *path);
// The direct-call symbols of closed units, by their C name: units load RTLD_LOCAL and bind through this at first use.
typedef clj_value (*clj_compiled_fn)(clj_value self, const clj_value *captured, const clj_value *args, size_t nargs);
void            clj_compiled_register_symbol(const char *name, clj_compiled_fn fn);
clj_compiled_fn clj_compiled_symbol(const char *name);
// The dispatchers of a closed unit's protocol method impls, by C name: what a site's fill compares an impl's code
// against, and what the compiler names an impl from another unit by (its name is never freed).
void              clj_compiled_register_impl(const char *name, clj_native_ctx_fn fn);
clj_native_ctx_fn clj_compiled_impl(const char *name);
const char       *clj_compiled_impl_name(clj_native_ctx_fn fn);

#endif
