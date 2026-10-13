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
	bool instrument;    // the profiler and signpost hooks in every fn (CLJC_INSTRUMENT at the unit's top)
	bool no_shake;      // keep every def of a closed set: the before half of the size measurement
	const char *guard_macro; // wrap the unit in #ifdef <macro> (the boot's core.c)
	// A qualified name the shaker drops however reachable it is: what the gate's negative run arms to prove the
	// tripwire fires. NUL-terminated list of names, NULL when empty.
	const char *const *force_drop;
	size_t             nforce_drop;
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

// Frame slots of a unit's trees against the facts pass's escape classification (NOTES.md, "Compiler": promoted
// slots). `slots` counts every slot of every frame as docs/facts-coverage.md does; the rest partition it.
typedef struct {
	uint64_t slots;
	uint64_t local;          // escape fact `local`
	uint64_t promoted;       // emitted as C variables
	uint64_t promoted_local; // of those, `local` by the fact
	uint64_t local_param;    // `local` but a parameter or the self slot: ownership is the caller's
	uint64_t local_pinned;   // `local` but read through the slot array: a capture, an OUTER read, a frame past 64 slots
	uint64_t local_fused;    // `local` but a fused node's argument frame, which is the argument array itself
	uint64_t int_slots;      // promoted slots emitted as int64_t (closed only)
	uint64_t double_slots;   // promoted slots emitted as double (closed only)
	uint64_t unboxed;        // arithmetic nodes computed on untagged values with no boxed path
	uint64_t tag_checked;    // arithmetic nodes with the inline operation behind the tags of their boxed arguments
	uint64_t entry_checked;  // lets and loops emitted twice, typed behind the tags of their entry values (closed only)
	uint64_t proto_direct;   // protocol call sites with one direct arm covering the whole receiver fact (closed)
	uint64_t proto_switch;   // sites with several arms, or one beside kinds the cache keeps
	uint64_t proto_cache;    // sites with the inline cache alone; an arm whose target no unit emitted counts here, as written
	uint64_t proto_folded;   // satisfies?/extends? sites answered by a verified constant (closed)
	uint64_t direct_slots;   // frame slots of the callees of direct call sites, summed per site
	uint64_t direct_array;   // of those, the entries the caller's ds[] holds: what the callee's promotion left in the array
	uint64_t workers;        // def'd arities with a primitive entry (closed only)
	uint64_t prim_sites;     // direct call sites written with a worker path
	uint64_t prim_bound;     // of those, bound to a worker of the set
	uint64_t kw_sites;       // keyword-lookup sites with an inline cache: (:k m) and (get m :k) with a literal keyword
	uint64_t reuse_tokens;   // cons sites built in the cell of a value dying there (drop-guided reuse)
	uint64_t reuse_consumes; // next/rest sites that hand their dying operand over
	uint64_t inlined;        // inlined calls written with their body (optimizer.c), the guard 1 in a closed set binding the fn
	uint64_t inlined_cross;  // of those, copies of another unit's fn: a dev unit guards them by the tree's digest
	uint64_t outlined;       // of those, sites calling the tree's one compiled copy in the unit instead of their own
	uint64_t outlined_fns;   // those compiled copies
	uint64_t mono_fused;     // fused sites whose stages over literal fns run as one step function
	uint64_t literals;       // literal fn arguments written as their body where the inlined callee calls them
	uint64_t paths;          // get-in, assoc-in and update-in sites written over their literal path
	uint64_t lowered;        // reduce, reduce-kv, swap! and swap-vals! sites over a literal fn written without its closure
	uint64_t defs;           // top-level defs of the unit
	uint64_t defs_dropped;   // of those, the ones the shaker left out (closed only)
	uint64_t def_bytes;      // C bytes the kept defs' functions take, as emitted
	uint64_t owned_arities;  // def'd arities emitted with an owned entry beside the +0 one (design §7 «Владеющие параметры»)
	uint64_t owned_params;   // their owned parameters
	uint64_t owned_array;    // of those, kept in the slot array with the owned bit: read by address after all
	uint64_t owned_sites;    // direct call sites written with the owned entry
	uint64_t owned_handed;   // arguments those sites hand over from a dying slot; the rest arrive fresh or retained
	uint64_t reuse_takes;    // conj/assoc/dissoc/with-meta sites that take a slot dying at the call
	uint64_t reuse_takeouts; // gets whose value an assoc of the same slot and key puts back: moved out at +1
	uint64_t owned_lowered;  // lowered literals' arities emitted with an owned entry for the accumulator
	uint64_t owned_steps;    // lowered sites whose reducer hands the accumulator over through an owned step
	uint64_t thunk_let_go;   // lazy-seq thunks' tail cons sites that release the dead captures before the token
	uint64_t drops;          // slots released where they die at a read that only borrowed (reuse.c, cljc_reuse_drops)
} cljc_slot_stats;

// After cljc_end: the protocol counters are resolved against the set as the units' text is.
void cljc_unit_slots(const cljc_compiler *c, size_t i, cljc_slot_stats *out);

// ---- tree shaking of a closed set (NOTES.md, "Compiler": tree shaking)

// What one top-level def cost the unit, for the "biggest survivors" half of the report.
typedef struct {
	const char *name;  // qualified
	uint64_t    bytes; // the C text of its functions; 0 for a dropped def
	bool        dropped;
} cljc_def_size;

size_t cljc_unit_def_count(const cljc_compiler *c, size_t i);
void   cljc_unit_def_at(const cljc_compiler *c, size_t i, size_t k, cljc_def_size *out);

// The whole set's shake, filled by cljc_end.
typedef struct {
	bool        ran;
	const char *skipped;    // why it did not run, NULL when it did
	uint64_t    defs;       // top-level defs of every unit
	uint64_t    candidates; // defs the root set did not keep outright
	uint64_t    dropped;    // of those, the ones nothing reached
	uint64_t    entry_units, embedded_units;
} cljc_shake_report;

void cljc_shake_report_of(const cljc_compiler *c, cljc_shake_report *out);
// Reachable sites that reach a var by a name this pass cannot read: where the tripwire, and not the analysis,
// is what keeps a wrong claim from becoming a wrong program. Reported, never fatal.
size_t              cljc_shake_escape_count(const cljc_compiler *c);
const cljc_refusal *cljc_shake_escape_at(const cljc_compiler *c, size_t i);

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

// Writes <dir>/<cname>.c and builds <dir>/<cname>.dylib without loading it; false with clang's words in err.
// Safe from several threads at once, on different cnames.
bool cljc_build_dylib(const cljc_eval_options *o, const char *cname, const char *text, char *err, size_t errcap);
// Builds a unit's C text as a dylib and loads it in-process; NULL with the exception pending when clang fails.
const clj_compiled_unit *cljc_load_dylib(const cljc_eval_options *o, const char *cname, const char *text);
// dlopens a built unit; NULL with the exception pending.
const clj_compiled_unit *cljc_open_dylib(const char *path);

#endif
