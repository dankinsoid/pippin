// @ai-generated(solo)
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "clj/core.h"
#include "cljc/compiler.h"

extern char **environ;

static cljc_eval_options options;
static char             *root_copy, *dir_copy, *clang_copy, *opt_copy;
static cljc_compiler    *compiler;
static uint64_t          forms, clang_ns, seq;
static int64_t           pool_objects;

static uint64_t now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static bool write_file(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	size_t n = strlen(text);
	bool   ok = fwrite(text, 1, n, f) == n;
	return fclose(f) == 0 && ok;
}

// clang through xcrun with the package's flags; the output of a failed run is left in <dylib>.log.
static bool run_clang(const cljc_eval_options *o, const char *cfile, const char *dylib, char *err, size_t errcap) {
	char inc1[1024], inc2[1024], log[1100];
	snprintf(inc1, sizeof inc1, "-I%s/Sources/CljCore/include", o->root);
	snprintf(inc2, sizeof inc2, "-I%s/Sources/CljCore", o->root);
	snprintf(log, sizeof log, "%s.log", dylib);
	const char *clang = o->clang ? o->clang : "xcrun";
	const char *argv[32];
	int         n = 0;
	argv[n++] = clang;
	if (!o->clang) argv[n++] = "clang";
	argv[n++] = "-shared";
	argv[n++] = "-std=c17";
	argv[n++] = o->opt ? o->opt : "-O0";
	argv[n++] = "-Wall";
	argv[n++] = "-Wextra";
	argv[n++] = "-Wpedantic";
	argv[n++] = "-Werror";
	argv[n++] = "-fno-common";
#if CLJ_DEBUG
	argv[n++] = "-DCLJ_DEBUG=1";
#endif
	argv[n++] = inc1;
	argv[n++] = inc2;
	argv[n++] = "-Wl,-undefined,dynamic_lookup";
	argv[n++] = "-o";
	argv[n++] = dylib;
	argv[n++] = cfile;
	argv[n] = NULL;
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 2, log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	posix_spawn_file_actions_adddup2(&fa, 2, 1);
	pid_t pid;
	int   rc = posix_spawnp(&pid, clang, &fa, NULL, (char *const *)argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	if (rc != 0) {
		snprintf(err, errcap, "cannot spawn %s: %s", clang, strerror(rc));
		return false;
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
	FILE *lf = fopen(log, "rb");
	char  text[1500] = "";
	if (lf) {
		size_t got = fread(text, 1, sizeof text - 1, lf);
		text[got] = '\0';
		fclose(lf);
	}
	snprintf(err, errcap, "clang failed on %s: %s", cfile, text);
	return false;
}

const clj_compiled_unit *cljc_open_dylib(const char *path) {
	void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	if (!h) {
		clj_throw_msg("dlopen failed: %s", dlerror());
		return NULL;
	}
	const clj_compiled_unit *unit = dlsym(h, "clj_compiled_unit_desc");
	if (!unit) {
		clj_throw_msg("no clj_compiled_unit_desc in %s", path);
		return NULL;
	}
	return unit;
}

const clj_compiled_unit *cljc_load_dylib(const cljc_eval_options *o, const char *cname, const char *text) {
	char cfile[1200], dylib[1200], err[2048];
	mkdir(o->dir, 0755);
	snprintf(cfile, sizeof cfile, "%s/%s.c", o->dir, cname);
	snprintf(dylib, sizeof dylib, "%s/%s.dylib", o->dir, cname);
	if (!write_file(cfile, text)) {
		clj_throw_msg("cannot write %s", cfile);
		return NULL;
	}
	uint64_t t0 = now_ns();
	bool     ok = run_clang(o, cfile, dylib, err, sizeof err);
	clang_ns += now_ns() - t0;
	if (!ok) {
		clj_throw_msg("%s", err);
		return NULL;
	}
	if (!o->keep) unlink(cfile);
	return cljc_open_dylib(dylib);
}

// Each host form becomes a unit of its own: emitted, built, loaded and run in place of the interpreter.
static clj_value on_form(const clj_load_form *form, const clj_node *node, void *ctx, bool *handled) {
	(void)ctx;
	*handled = true;
	size_t before = cljc_refusal_count(compiler);
	char  *text = cljc_form_text(compiler, form, node);
	if (cljc_refusal_count(compiler) > before) {
		const cljc_refusal *r = cljc_refusal_at(compiler, before);
		clj_value           e = clj_throw_msg("compiler refused a %s node at %u:%u: %s", r->kind, r->line, r->col, r->reason);
		free(text);
		return e;
	}
	char cname[64];
	snprintf(cname, sizeof cname, "form%llu", (unsigned long long)++seq);
	const clj_compiled_unit *unit = cljc_load_dylib(&options, cname, text);
	free(text);
	if (!unit) return CLJ_THROWN;
	forms++;
	int64_t live0 = clj_debug_live_objects();
	unit->pools();
	if (live0 >= 0) {
		int64_t made = clj_debug_live_objects() - live0;
		pool_objects += made;
		clj_debug_live_objects_exclude(made);
	}
	return unit->init();
}

bool cljc_eval_enable(const cljc_eval_options *o) {
	cljc_eval_disable();
	root_copy = strdup(o->root);
	dir_copy = strdup(o->dir);
	clang_copy = o->clang ? strdup(o->clang) : NULL;
	opt_copy = o->opt ? strdup(o->opt) : NULL;
	options = *o;
	options.root = root_copy;
	options.dir = dir_copy;
	options.clang = clang_copy;
	options.opt = opt_copy;
	cljc_options co = {.closed = o->closed, .line = true, .toplevel = true, .eval_result = true};
	compiler = cljc_new(&co);
	clj_load_hook h = {on_form, NULL, NULL, true};
	clj_load_set_hook(&h);
	return true;
}

void cljc_eval_disable(void) {
	if (!compiler) return;
	clj_load_set_hook(NULL);
	cljc_free(compiler);
	compiler = NULL;
	free(root_copy);
	free(dir_copy);
	free(clang_copy);
	free(opt_copy);
	root_copy = dir_copy = clang_copy = opt_copy = NULL;
}

uint64_t cljc_eval_count(void) { return forms; }
int64_t  cljc_eval_pool_objects(void) { return pool_objects; }
uint64_t cljc_eval_clang_ns(void) { return clang_ns; }

// clj_init's weak hook: CLJ_EVAL=compiled turns the compiled eval on for the process (the tests' opt-in gate).
void clj_compiled_eval_boot(void) {
	const char *mode = getenv("CLJ_EVAL");
	if (!mode || strcmp(mode, "compiled") != 0) return;
	const char *root = getenv("CLJ_EVAL_ROOT");
	const char *dir = getenv("CLJ_EVAL_DIR");
	if (!root) {
		fprintf(stderr, "CLJ_EVAL=compiled needs CLJ_EVAL_ROOT (the package root)\n");
		return;
	}
	char default_dir[1200];
	snprintf(default_dir, sizeof default_dir, "%s/.build/compiled-eval", root);
	cljc_eval_options o = {root, dir ? dir : default_dir, NULL, getenv("CLJ_EVAL_OPT"), getenv("CLJ_EVAL_CLOSED") != NULL, getenv("CLJ_EVAL_KEEP") != NULL};
	cljc_eval_enable(&o);
}
