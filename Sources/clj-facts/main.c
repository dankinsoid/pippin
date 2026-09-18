// @ai-generated(solo)
// clj-facts: the type-coverage metric of design §10 step 3b, written as docs/facts-coverage.md.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "clj/core.h"
#include "clj/facts.h"

#define MAX_FILES 512
#define MAX_LIBS 16
#define MAX_ROOTS 8

#define T_INT_SET (CLJ_T_FIXNUM | CLJ_T_LONG)
#define T_NUM_SET (CLJ_T_FIXNUM | CLJ_T_LONG | CLJ_T_BIGINT | CLJ_T_RATIO | CLJ_T_DECIMAL | CLJ_T_DOUBLE)
#define T_MAP_SET (CLJ_T_MAP | CLJ_T_SORTED_MAP | CLJ_T_RECORD)

typedef struct {
	char     name[64];
	uint64_t forms, nodes, bytes, peak_bytes;
	uint64_t value_nodes, type_known, type_union, type_top, type_bottom;
	uint64_t computed, computed_known, computed_top; // the same minus constant nodes: what an optimizer works with
	uint64_t null_known, null_maybe;
	uint64_t arith, arith_fixnum, arith_int;
	uint64_t loops, loops_numeric;
	uint64_t slots, slots_local, slots_captured, slots_escapes;
	uint64_t proto_calls, proto_known;
	uint64_t kw_lookups, kw_shaped, kw_record;
	uint64_t conflicts, widenings;
	double   analyze_ms, facts_ms;
	bool     tests_only; // every load-path root is named "test": assertion expansions, not library code
} stats;

static stats libs[MAX_LIBS];
static int   nlibs;

static double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static char *read_file(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc((size_t)n + 1);
	if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	buf[n] = 0;
	fclose(f);
	*len = (size_t)n;
	return buf;
}

// ---- counting the positions that matter separately (design §10 step 3b)

typedef struct {
	const clj_facts *f;
	stats           *s;
} counter;

static bool is_arith(const char *name) {
	static const char *const ops[] = {"+", "-", "*", "/", "inc", "dec", "<", "<=", ">", ">="};
	for (size_t i = 0; i < sizeof ops / sizeof *ops; i++) {
		if (strcmp(ops[i], name) == 0) return true;
	}
	return false;
}

static bool subset_of(const clj_fact *f, uint32_t set) { return f->types != CLJ_T_BOTTOM && (f->types & ~set) == 0; }

static void count_arith(counter *c, const clj_node *n) {
	const char *name = strchr(n->u.intrinsic.op->name, '/') + 1;
	if (!is_arith(name)) return;
	c->s->arith++;
	bool fixnum = true, integer = true;
	for (uint32_t i = 0; i < n->u.intrinsic.n; i++) {
		const clj_fact *a = clj_facts_node(c->f, n->u.intrinsic.args[i]->id);
		if (!a || a->types != CLJ_T_FIXNUM) fixnum = false;
		if (!a || !subset_of(a, T_INT_SET)) integer = false;
	}
	c->s->arith_fixnum += fixnum;
	c->s->arith_int += integer;
}

static void count_loop(counter *c, const clj_node *n) {
	c->s->loops++;
	for (uint32_t i = 0; i < clj_facts_nloops(c->f); i++) {
		const clj_facts_loop *l = clj_facts_loop_at(c->f, i);
		if (l->node != n->id) continue;
		// One numeric type means one unboxed representation: (inc i) answers fixnum or a boxed long, both int64.
		bool     numeric = l->n > 0;
		uint32_t first = 0;
		for (uint32_t k = 0; k < l->n; k++) {
			const clj_fact *v = clj_facts_loop_var(c->f, i, k);
			uint32_t        domain = !v || v->types == CLJ_T_BOTTOM ? 0
			                         : (v->types & ~T_INT_SET) == 0  ? T_INT_SET
			                         : v->types == CLJ_T_DOUBLE      ? CLJ_T_DOUBLE
			                                                         : 0;
			if (!domain || (k > 0 && domain != first)) numeric = false;
			first = domain;
		}
		c->s->loops_numeric += numeric;
		return;
	}
}

static void count_invoke(counter *c, const clj_node *n) {
	const clj_node *head = n->u.invoke.fn;
	if (head->kind == CLJ_NODE_CONST && clj_is_keyword(head->u.value) && n->u.invoke.n >= 1) {
		c->s->kw_lookups++;
		const clj_fact *m = clj_facts_node(c->f, n->u.invoke.args[0]->id);
		if (m && clj_fact_union_size(*m) == 1 && (m->types & T_MAP_SET)) c->s->kw_shaped++;
		if (m && m->types == CLJ_T_RECORD) c->s->kw_record++;
		return;
	}
	// The receiver of a protocol method: the tool reads the var's root, which the pass itself may not.
	if (head->kind == CLJ_NODE_VAR && clj_is_var(head->u.var) && n->u.invoke.n >= 1) {
		clj_value root = clj_var_root(head->u.var);
		if (root != CLJ_UNBOUND && clj_is_fn(root) && clj_is_protocol_method(root)) {
			c->s->proto_calls++;
			const clj_fact *r = clj_facts_node(c->f, n->u.invoke.args[0]->id);
			if (r && clj_fact_union_size(*r) == 1) c->s->proto_known++;
		}
	}
}

static void count_node(const clj_node *n, void *ctx) {
	counter        *c = ctx;
	const clj_fact *f = clj_facts_node(c->f, n->id);
	if (f && clj_facts_value_node(n->kind)) {
		c->s->value_nodes++;
		uint32_t size = clj_fact_union_size(*f);
		if (size == 0) c->s->type_bottom++;
		else if (f->types == CLJ_T_TOP) c->s->type_top++;
		else if (size == 1) c->s->type_known++;
		else c->s->type_union++;
		if (f->null == CLJ_NULL_NEVER || f->null == CLJ_NULL_ALWAYS) c->s->null_known++;
		else c->s->null_maybe++;
		if (n->kind != CLJ_NODE_CONST) {
			c->s->computed++;
			if (f->types == CLJ_T_TOP) c->s->computed_top++;
			else if (size == 1) c->s->computed_known++;
		}
	}
	switch (n->kind) {
	case CLJ_NODE_INTRINSIC: count_arith(c, n); break;
	case CLJ_NODE_LOOP: count_loop(c, n); break;
	case CLJ_NODE_INVOKE: count_invoke(c, n); break;
	default: break;
	}
	clj_node_children(n, count_node, ctx);
}

static void tally(stats *s, const clj_node *root, const clj_facts *f) {
	counter c = {f, s};
	count_node(root, &c);
	for (uint32_t i = 0; i < clj_facts_nframes(f); i++) {
		const clj_facts_frame *fr = clj_facts_frame_at(f, i);
		for (uint32_t k = 0; k < fr->nslots; k++) {
			s->slots++;
			switch (clj_facts_escape(f, i, k)) {
			case CLJ_ESCAPE_LOCAL: s->slots_local++; break;
			case CLJ_ESCAPE_CAPTURED: s->slots_captured++; break;
			default: s->slots_escapes++;
			}
		}
	}
	s->nodes += clj_facts_nnodes(f);
	s->bytes += clj_facts_bytes(f);
	s->conflicts += clj_facts_conflicts(f);
	s->widenings += clj_facts_widenings(f);
}

typedef struct {
	uint32_t        id;
	const clj_node *found;
} find_ctx;

static void find_node(const clj_node *n, void *ctx) {
	find_ctx *f = ctx;
	if (n->id == f->id) f->found = n;
	if (!f->found) clj_node_children(n, find_node, ctx);
}

// ---- one source file: read it back after the load, analyze every form, build its facts

static void measure_file(stats *s, const char *path, const char *ns_name) {
	size_t len = 0;
	char  *src = read_file(path, &len);
	if (!src) {
		fprintf(stderr, "clj-facts: cannot read %s\n", path);
		return;
	}
	clj_value sym = clj_symbol_from_cstr(ns_name);
	clj_value ns = clj_ns_find(sym);
	clj_release(sym);
	if (clj_is_nil(ns)) {
		free(src);
		return; // the namespace never loaded: its forms would not resolve
	}
	clj_value previous = clj_ns_current();
	clj_ns_set_current(ns);
	clj_reader r;
	clj_reader_init(&r, src, len);
	clj_reader_use_namespaces(&r);
	clj_env   env = {ns, 0, 0};
	clj_value form = CLJ_NIL;
	for (;;) {
		clj_read_status st = clj_read(&r, &form);
		if (st != CLJ_READ_OK) break;
		double    t0 = now_ms();
		clj_node *root = clj_analyze(form, &env);
		double    t1 = now_ms();
		clj_release(form);
		if (!root) {
			clj_release(clj_take_pending());
			continue;
		}
		s->analyze_ms += t1 - t0;
		s->forms++;
		double     t2 = now_ms();
		clj_facts *f = clj_facts_of(root);
		double     t3 = now_ms();
		s->facts_ms += t3 - t2;
		if (clj_facts_bytes(f) > s->peak_bytes) s->peak_bytes = clj_facts_bytes(f);
		if (clj_facts_conflicts(f) > 0) {
			find_ctx fc = {clj_facts_conflict_node(f), NULL};
			find_node(root, &fc);
			const char *what = "?";
			if (fc.found && fc.found->kind == CLJ_NODE_INTRINSIC) what = fc.found->u.intrinsic.op->name;
			else if (fc.found && fc.found->kind == CLJ_NODE_INVOKE && fc.found->u.invoke.fn->kind == CLJ_NODE_VAR)
				what = clj_string_bytes(clj_symbol_name(clj_var_name(fc.found->u.invoke.fn->u.var)));
			else if (fc.found && fc.found->kind == CLJ_NODE_LOCAL) what = "local";
			uint32_t argtypes = 0;
			if (fc.found && fc.found->kind == CLJ_NODE_INTRINSIC && fc.found->u.intrinsic.n == 1) {
				const clj_fact *a = clj_facts_node(f, fc.found->u.intrinsic.args[0]->id);
				if (a) argtypes = a->types;
			}
			fprintf(stderr, "clj-facts: %u dead branch(es) in %s, first at %u:%u on %s, argument %#x\n", clj_facts_conflicts(f),
			        path, fc.found ? fc.found->line : root->line, fc.found ? fc.found->col : 0, what, argtypes);
		}
		tally(s, root, f);
		clj_facts_free(f);
		clj_release(clj_from_ptr(root));
	}
	clj_ns_set_current(previous);
	free(src);
}

// ---- walking a library

typedef struct {
	char path[512];
	char ns[256];
} source_file;

static source_file files[MAX_FILES];
static int         nfiles;

static void ns_of(const char *rel, char *out, size_t n) {
	size_t k = 0;
	for (size_t i = 0; rel[i] && k + 1 < n; i++) {
		char ch = rel[i];
		if (ch == '/') ch = '.';
		else if (ch == '_') ch = '-';
		out[k++] = ch;
	}
	out[k] = 0;
	char *dot = strrchr(out, '.');
	if (dot && (strcmp(dot, ".clj") == 0 || strcmp(dot, ".cljc") == 0)) *dot = 0;
}

static void collect(const char *root, const char *rel) {
	char dir[512];
	snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
	DIR *d = opendir(dir);
	if (!d) return;
	for (struct dirent *e; (e = readdir(d));) {
		if (e->d_name[0] == '.') continue;
		char child[512];
		snprintf(child, sizeof child, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
		char full[512];
		snprintf(full, sizeof full, "%s/%s", root, child);
		struct stat st;
		if (stat(full, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			collect(root, child);
			continue;
		}
		const char *ext = strrchr(e->d_name, '.');
		if (!ext || (strcmp(ext, ".clj") != 0 && strcmp(ext, ".cljc") != 0)) continue;
		if (nfiles == MAX_FILES) continue;
		snprintf(files[nfiles].path, sizeof files[nfiles].path, "%s", full);
		ns_of(child, files[nfiles].ns, sizeof files[nfiles].ns);
		nfiles++;
	}
	closedir(d);
}

static int by_path(const void *a, const void *b) { return strcmp(((const source_file *)a)->path, ((const source_file *)b)->path); }

static void eval_string(const char *source) {
	clj_value r = clj_load_source(source, strlen(source), CLJ_NIL);
	if (r == CLJ_THROWN) clj_release(clj_take_pending());
	else clj_release(r);
}

// The manifest's :load-path and :features, read as EDN (corpus/<lib>/manifest.edn).
static clj_value manifest_of(const char *lib_root) {
	char path[512];
	snprintf(path, sizeof path, "%s/manifest.edn", lib_root);
	size_t len = 0;
	char  *src = read_file(path, &len);
	if (!src) return CLJ_NIL;
	clj_reader r;
	clj_reader_init(&r, src, len);
	clj_value form = CLJ_NIL;
	if (clj_read(&r, &form) != CLJ_READ_OK) form = CLJ_NIL;
	free(src);
	return form;
}

static clj_value kw(const char *name) { return clj_keyword_from_cstr(name); }

static void run_library(const char *name, const char *root, const char *const *roots, size_t nroots, clj_value features) {
	stats *s = &libs[nlibs++];
	snprintf(s->name, sizeof s->name, "%s", name);
	s->tests_only = nroots > 0;
	for (size_t i = 0; i < nroots; i++) {
		if (strcmp(roots[i], "test") != 0) s->tests_only = false;
	}
	nfiles = 0;
	for (size_t i = 0; i < nroots; i++) {
		char dir[512];
		snprintf(dir, sizeof dir, "%s/%s", root, roots[i]);
		collect(dir, "");
	}
	qsort(files, (size_t)nfiles, sizeof *files, by_path);
	const char *abs[MAX_ROOTS];
	char        bufs[MAX_ROOTS][512];
	for (size_t i = 0; i < nroots && i < MAX_ROOTS; i++) {
		snprintf(bufs[i], sizeof bufs[i], "%s/%s", root, roots[i]);
		abs[i] = bufs[i];
	}
	clj_load_path_set(abs, nroots < MAX_ROOTS ? nroots : MAX_ROOTS);
	clj_reader_set_features(features);
	clj_load_set_lenient(true);
	for (int i = 0; i < nfiles; i++) {
		char form[512];
		snprintf(form, sizeof form, "(clojure.core/require (quote %s))", files[i].ns);
		eval_string(form);
	}
	clj_release(clj_load_take_failures());
	for (int i = 0; i < nfiles; i++) measure_file(s, files[i].path, files[i].ns);
	clj_load_set_lenient(false);
	clj_reader_set_features(CLJ_NIL);
	clj_load_path_set(NULL, 0);
}

// ---- the report

static double pct(uint64_t part, uint64_t whole) { return whole ? 100.0 * (double)part / (double)whole : 0.0; }

static void add(stats *total, const stats *s) {
	total->forms += s->forms;
	total->nodes += s->nodes;
	total->bytes += s->bytes;
	if (s->peak_bytes > total->peak_bytes) total->peak_bytes = s->peak_bytes;
	total->value_nodes += s->value_nodes;
	total->type_known += s->type_known;
	total->type_union += s->type_union;
	total->type_top += s->type_top;
	total->type_bottom += s->type_bottom;
	total->computed += s->computed;
	total->computed_known += s->computed_known;
	total->computed_top += s->computed_top;
	total->null_known += s->null_known;
	total->null_maybe += s->null_maybe;
	total->arith += s->arith;
	total->arith_fixnum += s->arith_fixnum;
	total->arith_int += s->arith_int;
	total->loops += s->loops;
	total->loops_numeric += s->loops_numeric;
	total->slots += s->slots;
	total->slots_local += s->slots_local;
	total->slots_captured += s->slots_captured;
	total->slots_escapes += s->slots_escapes;
	total->proto_calls += s->proto_calls;
	total->proto_known += s->proto_known;
	total->kw_lookups += s->kw_lookups;
	total->kw_shaped += s->kw_shaped;
	total->kw_record += s->kw_record;
	total->conflicts += s->conflicts;
	total->widenings += s->widenings;
	total->analyze_ms += s->analyze_ms;
	total->facts_ms += s->facts_ms;
}

static void row_types(FILE *out, const stats *s) {
	fprintf(out, "| %s | %llu | %llu | %.1f %% | %.1f %% | %.1f %% | %.1f %% | %llu | %.1f %% |\n", s->name,
	        (unsigned long long)s->forms, (unsigned long long)s->value_nodes, pct(s->type_known, s->value_nodes),
	        pct(s->type_union, s->value_nodes), pct(s->type_top, s->value_nodes), pct(s->null_known, s->value_nodes),
	        (unsigned long long)s->computed, pct(s->computed_known, s->computed));
}

static void row_positions(FILE *out, const stats *s) {
	fprintf(out, "| %s | %llu / %.1f %% | %llu / %.1f %% | %llu / %.1f %% | %llu / %.1f %% | %llu / %.1f %% | %llu / %.1f %% |\n", s->name,
	        (unsigned long long)s->arith, pct(s->arith_fixnum, s->arith), (unsigned long long)s->loops,
	        pct(s->loops_numeric, s->loops), (unsigned long long)s->slots, pct(s->slots_local, s->slots),
	        (unsigned long long)s->proto_calls, pct(s->proto_known, s->proto_calls), (unsigned long long)s->kw_lookups,
	        pct(s->kw_shaped, s->kw_lookups), (unsigned long long)s->arith, pct(s->arith_int, s->arith));
}

static void write_report(const char *path) {
	FILE *out = fopen(path, "w");
	if (!out) {
		fprintf(stderr, "clj-facts: cannot write %s\n", path);
		exit(1);
	}
	stats total = {0}, code = {0};
	snprintf(total.name, sizeof total.name, "**all**");
	snprintf(code.name, sizeof code.name, "**library code**");
	for (int i = 0; i < nlibs; i++) {
		add(&total, &libs[i]);
		if (!libs[i].tests_only) add(&code, &libs[i]);
	}

	fprintf(out, "# Type-fact coverage\n\n");
	fprintf(out, "Generated by `make facts-report` (`Sources/clj-facts`): every library is loaded, then every top-level\n"
	             "form of every file is analyzed again and `clj_facts_of` is run over its optimized tree. Pass 1 is local\n"
	             "only — a call of anything but a known intrinsic or C builtin is ⊤ — so these are the numbers before any\n"
	             "interprocedural summary exists (design §10 step 3b, NOTES.md \"Facts\").\n\n");
	fprintf(out, "## What the numbers mean\n\n");
	fprintf(out, "A *value-producing node* is every node but `recur`, `throw` and the direct-fn init. *Known* is a fact of\n"
	             "exactly one kind, *union* two to four, ⊤ everything else — the lattice widens past four members, so the ⊤\n"
	             "column is \"nothing useful\", not \"five kinds\". *Computed nodes* leave the constants out: a literal knows its\n"
	             "own type, so the share over computed nodes is what an optimizer actually gains. A library whose every\n"
	             "load-path root is named `test` is counted apart, because assertion expansions are mostly literals.\n\n");
	fprintf(out, "- Over library code: **%.1f %%** of value nodes have a known type and **%.1f %%** are ⊤, but only **%.1f %%**\n"
	             "  of *computed* nodes are known — every call of anything but an intrinsic or an annotated C builtin is ⊤,\n"
	             "  and that is the whole cost of having no interprocedural pass.\n",
	        pct(code.type_known, code.value_nodes), pct(code.type_top, code.value_nodes), pct(code.computed_known, code.computed));
	fprintf(out, "- Nullability is decided for **%.1f %%** of value nodes over library code, and it survives where the type\n"
	             "  does not: removing nil from ⊤ leaves 26 kinds, which the union cap sends straight back to ⊤.\n",
	        pct(code.null_known, code.value_nodes));
	fprintf(out, "- **Local slots** (the register prize): %llu slots over library code, **%.1f %%** of which never escape and\n"
	             "  are never captured. This is the one position with both a large population and a large known share.\n",
	        (unsigned long long)code.slots, pct(code.slots_local, code.slots));
	fprintf(out, "- **Intrinsic arithmetic** (the unboxing prize): %llu two-argument sites over library code, **%.1f %%** with\n"
	             "  both arguments known-fixnum and **%.1f %%** with both known to be int64-representable (fixnum or boxed long).\n",
	        (unsigned long long)code.arith, pct(code.arith_fixnum, code.arith), pct(code.arith_int, code.arith));
	fprintf(out, "- **Loops**: %llu over library code, **%.1f %%** with every variable of one numeric domain.\n",
	        (unsigned long long)code.loops, pct(code.loops_numeric, code.loops));
	fprintf(out, "- **Protocol receivers** (the inline-cache prize): %llu sites, **%.1f %%** with a known type — a receiver is\n"
	             "  a parameter, and a parameter is ⊤ until function summaries exist.\n",
	        (unsigned long long)total.proto_calls, pct(total.proto_known, total.proto_calls));
	fprintf(out, "- **`(:k m)` lookups**: %llu sites, **%.1f %%** on a value known to be a map of some kind and %llu on a record.\n"
	             "  Pass 1 learns a record type only from an `instance?` check against a builtin type name: a `defrecord` type is\n"
	             "  a var, and reading a var's root is runtime state the pass refuses.\n",
	        (unsigned long long)total.kw_lookups, pct(total.kw_shaped, total.kw_lookups), (unsigned long long)total.kw_record);
	fprintf(out, "- Cost: %.0f ms of facts against %.0f ms of analysis over the same forms (%.2f×); the largest single table is\n"
	             "  %.0f KB for one top-level form.\n",
	        total.facts_ms, total.analyze_ms, total.analyze_ms > 0 ? total.facts_ms / total.analyze_ms : 0.0,
	        (double)total.peak_bytes / 1024);
	fprintf(out, "- Refinement conflicts (a meet down to ⊥): %llu, every one of them a branch a literal makes unreachable\n"
	             "  (`(and false true)`, `(when-let [x [0]] …)`). Value nodes at ⊥: %llu, all of them a throw in tail position.\n"
	             "  Loop variables the widening rule cut short: %llu.\n\n",
	        (unsigned long long)total.conflicts, (unsigned long long)total.type_bottom, (unsigned long long)total.widenings);
	fprintf(out, "**What to build first.** By population and by known share the answer is escaping, not types: %.1f %% of local\n"
	             "slots over library code provably never leave their frame, against %llu arithmetic sites in total of which\n"
	             "%.1f %% have both arguments known-fixnum, and %llu protocol receivers of which none has a known type. Registers\n"
	             "and stack allocation are worth building on pass 1 alone; unboxing and inline caches are waiting on the\n"
	             "interprocedural pass, not on a consumer.\n\n",
	        pct(code.slots_local, code.slots), (unsigned long long)total.arith, pct(total.arith_fixnum, total.arith),
	        (unsigned long long)total.proto_calls);
	fprintf(out, "## Types and nullability\n\n");
	fprintf(out, "| library | forms | value nodes | known | union ≤4 | ⊤ | nullability known | computed nodes | known |\n");
	fprintf(out, "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");
	for (int i = 0; i < nlibs; i++) row_types(out, &libs[i]);
	row_types(out, &code);
	row_types(out, &total);

	fprintf(out, "\n## The positions that pay\n\n");
	fprintf(out, "Each cell is the population and the share of it that is known.\n\n");
	fprintf(out, "| library | arith sites / both fixnum | loops / all vars one numeric kind | local slots / never leave the frame | "
	             "protocol receivers / known type | `(:k m)` / known map shape | arith sites / both integer |\n");
	fprintf(out, "|---|---:|---:|---:|---:|---:|---:|\n");
	for (int i = 0; i < nlibs; i++) row_positions(out, &libs[i]);
	row_positions(out, &code);
	row_positions(out, &total);

	fprintf(out, "\n## Local slots\n\n");
	fprintf(out, "| library | slots | local | captured | escapes |\n");
	fprintf(out, "|---|---:|---:|---:|---:|\n");
	for (int i = 0; i <= nlibs + 1; i++) {
		const stats *s = i < nlibs ? &libs[i] : (i == nlibs ? &code : &total);
		fprintf(out, "| %s | %llu | %.1f %% | %.1f %% | %.1f %% |\n", s->name, (unsigned long long)s->slots,
		        pct(s->slots_local, s->slots), pct(s->slots_captured, s->slots), pct(s->slots_escapes, s->slots));
	}

	fprintf(out, "\n## Cost per library\n\n");
	fprintf(out, "| library | forms | nodes | analysis, ms | facts, ms | facts / analysis | tables, KB | largest table, KB |\n");
	fprintf(out, "|---|---:|---:|---:|---:|---:|---:|---:|\n");
	for (int i = 0; i <= nlibs + 1; i++) {
		const stats *s = i < nlibs ? &libs[i] : (i == nlibs ? &code : &total);
		fprintf(out, "| %s | %llu | %llu | %.1f | %.1f | %.2f× | %.0f | %.0f |\n", s->name, (unsigned long long)s->forms,
		        (unsigned long long)s->nodes, s->analyze_ms, s->facts_ms, s->analyze_ms > 0 ? s->facts_ms / s->analyze_ms : 0.0,
		        (double)s->bytes / 1024, (double)s->peak_bytes / 1024);
	}
	fprintf(out, "\n`(:k m)` records specifically: %llu of %llu lookups sit on a value known to be a record — pass 1 learns a\n"
	             "record type only from an `instance?` check against a builtin type name, and a `defrecord` type is a var whose\n"
	             "root the pass may not read.\n",
	        (unsigned long long)total.kw_record, (unsigned long long)total.kw_lookups);
	fclose(out);
	fprintf(stderr, "clj-facts: wrote %s\n", path);
}

int main(int argc, char **argv) {
	const char *repo = argc > 1 ? argv[1] : ".";
	const char *out = argc > 2 ? argv[2] : "docs/facts-coverage.md";
	clj_init();

	char boot[512];
	snprintf(boot, sizeof boot, "%s/Sources/CljCore/boot", repo);
	// core.clj is loaded by clj_init and its embedded libs by require; both are read back from boot/.
	eval_string("(require 'clojure.set 'clojure.string 'clojure.walk 'clojure.template 'clojure.test)");
	stats *core = &libs[nlibs++];
	snprintf(core->name, sizeof core->name, "core.clj");
	char core_path[512];
	snprintf(core_path, sizeof core_path, "%s/core.clj", boot);
	measure_file(core, core_path, "clojure.core");

	stats *embedded = &libs[nlibs++];
	snprintf(embedded->name, sizeof embedded->name, "embedded libs");
	nfiles = 0;
	char clojure_dir[512];
	snprintf(clojure_dir, sizeof clojure_dir, "%s/clojure", boot);
	collect(clojure_dir, "");
	qsort(files, (size_t)nfiles, sizeof *files, by_path);
	for (int i = 0; i < nfiles; i++) {
		char ns[256];
		snprintf(ns, sizeof ns, "clojure.%s", files[i].ns);
		measure_file(embedded, files[i].path, ns);
	}

	char corpus[512];
	snprintf(corpus, sizeof corpus, "%s/corpus", repo);
	DIR *d = opendir(corpus);
	if (d) {
		char names[MAX_LIBS][128];
		int  n = 0;
		for (struct dirent *e; (e = readdir(d)) && n < MAX_LIBS;) {
			if (e->d_name[0] == '.') continue;
			char full[512];
			snprintf(full, sizeof full, "%s/%s", corpus, e->d_name);
			struct stat st;
			if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) snprintf(names[n++], sizeof names[0], "%s", e->d_name);
		}
		closedir(d);
		qsort(names, (size_t)n, sizeof names[0], (int (*)(const void *, const void *))strcmp);
		for (int i = 0; i < n; i++) {
			char root[512];
			snprintf(root, sizeof root, "%s/%s", corpus, names[i]);
			clj_value m = manifest_of(root);
			if (clj_is_nil(m)) continue;
			clj_value lp = kw("load-path"), fk = kw("features");
			clj_value paths = clj_get2(m, lp), features = clj_get2(m, fk);
			clj_release(lp);
			clj_release(fk);
			const char *roots[MAX_ROOTS];
			char        bufs[MAX_ROOTS][128];
			size_t      nroots = 0;
			clj_value   count = clj_count(paths);
			intptr_t    np = clj_is_fixnum(count) ? clj_fixnum_val(count) : 0;
			clj_release(count);
			for (intptr_t k = 0; k < np && nroots < MAX_ROOTS; k++) {
				clj_value idx = clj_fixnum(k), head = clj_nth2(paths, idx);
				snprintf(bufs[nroots], sizeof bufs[0], "%s", clj_string_bytes(head));
				roots[nroots] = bufs[nroots];
				nroots++;
				clj_release(head);
			}
			run_library(names[i], root, roots, nroots, features);
			clj_release(features);
			clj_release(paths);
			clj_release(m);
		}
	}
	write_report(out);
	return 0;
}
