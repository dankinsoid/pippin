// @ai-generated(solo)
#ifndef CLJC_COMPILER_H
#define CLJC_COMPILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clj/analyzer.h"
#include "clj/compiled.h"
#include "clj/runtime.h"

// The C generator over the analyzer's trees (NOTES.md, "Compiler"). A compiler installs the load hook, collects one
// unit per source file from the forms the loader analyzes, and writes each unit as one C translation unit.

typedef struct cljc_compiler cljc_compiler;

typedef struct {
	bool closed;       // direct calls of compiled vars, no intrinsic or fusion guards, eval refused
	bool line;         // #line directives
	bool toplevel;     // the hook also fires on host evals (compiled eval), not only on loads
	bool skip_embedded; // leave <embedded>/ libs to the interpreter (units are not collected for them)
	bool eval_result;   // the init returns the last form's value instead of nil (compiled eval)
	const char *guard_macro; // wrap the unit in #ifdef <macro> (the boot's core.c)
} cljc_options;

// A form the generator could not express; the unit throws at that form instead.
typedef struct {
	const char *file;
	uint32_t    line, col;
	const char *kind;   // node kind name
	const char *reason;
} cljc_refusal;

cljc_compiler *cljc_new(const cljc_options *opts);
void           cljc_free(cljc_compiler *c);
// Installs the load hook; every top-level tree the loader analyzes from now on is emitted.
void cljc_begin(cljc_compiler *c);
void cljc_end(cljc_compiler *c);

// Units collected so far, in the order their files were first seen.
size_t      cljc_unit_count(const cljc_compiler *c);
const char *cljc_unit_file(const cljc_compiler *c, size_t i);
// The C text of unit i, owned by the caller (free). init_name is the exported init symbol; NULL names it after the unit,
// exporting a `clj_compiled_unit` descriptor for dlopen; a named init is what the boot links (clj_compiled_core_init).
char *cljc_unit_text(cljc_compiler *c, size_t i, const char *init_name);
// A C file name for the unit: the file's path munged.
char *cljc_unit_cname(const cljc_compiler *c, size_t i);
// One form as a unit of its own (compiled eval); the init returns its value under eval_result. Owned text.
char *cljc_form_text(cljc_compiler *c, const clj_load_form *form, const clj_node *node);

size_t              cljc_refusal_count(const cljc_compiler *c);
const cljc_refusal *cljc_refusal_at(const cljc_compiler *c, size_t i);

// The C identifier of a Clojure name (NOTES.md, the demangling rule); owned by the caller.
char *cljc_mangle(const char *ns, const char *name);

// ---- compiled eval: every host-level form compiled to a dylib and run (CLJ_EVAL=compiled)
typedef struct {
	const char *root;    // the package root: Sources/CljCore/include and Sources/CljCore are under it
	const char *dir;     // where the C files and dylibs go
	const char *clang;   // NULL: "xcrun clang"
	const char *opt;     // the optimization flag, NULL: -O0 (no -g either: dsymutil doubles the clang time)
	bool        closed;
	bool        keep;    // keep the C files
} cljc_eval_options;

// Installs the hook that compiles each host eval; returns false with a message on stderr when clang is unusable.
bool cljc_eval_enable(const cljc_eval_options *o);
void cljc_eval_disable(void);
// Forms compiled and run so far, and the clang time spent, for reports.
uint64_t cljc_eval_count(void);
uint64_t cljc_eval_clang_ns(void);
uint64_t cljc_eval_dlopen_ns(void);
// Objects the units' constant pools hold for the process: what a live-object count under compiled eval includes.
int64_t cljc_eval_pool_objects(void);

// Builds a unit's C text as a dylib and loads it in-process; NULL with the exception pending when clang fails.
const clj_compiled_unit *cljc_load_dylib(const cljc_eval_options *o, const char *cname, const char *text);
// dlopens a built unit; NULL with the exception pending.
const clj_compiled_unit *cljc_open_dylib(const char *path);

#endif
