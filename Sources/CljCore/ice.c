// @ai-generated(solo)
// Internal errors and the kill switch (design §3 «Корректность реализации» items 1; NOTES "Internal errors").
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clj/analyzer.h"
#include "clj/box.h"
#include "clj/eval.h"
#include "clj/map.h"
#include "clj/runtime.h"
#include "clj/shape.h"
#include "clj/string.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "ice_internal.h"
#include "shadow_internal.h"
#include "trace_internal.h"

#ifdef CLJ_NO_REUSE
_Atomic uint32_t clj_no_opt_bits = CLJ_NO_OPT_REUSE;
#else
_Atomic uint32_t clj_no_opt_bits = 0;
#endif

// ---- the frames

void clj_ice_enter(clj_ice_frame *f) {
	clj_shadow_stack *s = clj_shadow_stack_init();
	f->prev = s->ice;
	s->ice = f;
}

void clj_ice_leave(clj_ice_frame *f) {
	clj_shadow_stack *s = clj_shadow_stack_init();
	// A frame left out of order would leave the chain pointing into a dead C frame.
	if (s->ice != f) clj_ice("ice/frame-order", NULL, "a pass left a frame that is not the innermost");
	s->ice = f->prev;
}

void clj_ice_pass(const char *pass) {
	clj_shadow_stack *s = clj_shadow_stack_init();
	clj_ice_frame    *f = s->ice;
	if (f) f->pass = pass;
}

clj_value clj_ice_current_file(void) {
	clj_value box = clj_var_thread_binding(clj_load_file_var());
	if (clj_is_nil(box)) return CLJ_NIL;
	clj_value v = clj_volatile_value(box);
	return clj_is_string(v) ? v : CLJ_NIL;
}

static const char *const kind_names[] = {
	"const", "local", "captured", "outer", "var", "if", "do", "let", "loop", "recur", "fn", "invoke", "def", "vector",
	"map", "set", "try", "throw", "intrinsic", "fused", "direct-fn", "direct-call", "objc-send", "root-is", "inline", "lowered",
};

const char *clj_node_kind_name(int kind) {
	return kind >= 0 && (size_t)kind < sizeof kind_names / sizeof *kind_names ? kind_names[kind] : "unknown";
}

// ---- the report: write(2) or a buffer, no allocation, so a fatal signal may write it too

typedef struct {
	int    fd; // -1: into buf
	char  *buf;
	size_t cap, len;
} sink;

static void put_n(sink *s, const char *p, size_t n) {
	if (s->fd >= 0) {
		(void)!write(s->fd, p, n);
	} else if (s->cap) {
		size_t room = s->len < s->cap - 1 ? s->cap - 1 - s->len : 0, k = n < room ? n : room;
		memcpy(s->buf + s->len, p, k);
		s->buf[s->len + k] = '\0';
	}
	s->len += n;
}

static void put(sink *s, const char *p) { put_n(s, p, strlen(p)); }

static void put_u(sink *s, uint64_t n) {
	char   b[24];
	size_t i = sizeof b;
	do {
		b[--i] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	put_n(s, b + i, sizeof b - i);
}

static const char *backend_name(clj_backend b) {
	switch (b) {
	case CLJ_BACKEND_INTERPRETER: return "interpreter";
	case CLJ_BACKEND_DEV: return "compiled dev unit";
	case CLJ_BACKEND_CLOSED: return "compiled closed unit";
	case CLJ_BACKEND_INHERIT: break;
	}
	return "interpreter";
}

static const clj_ice_frame *innermost(void) {
	const clj_shadow_stack *s = clj_shadow_stack_current();
	return s ? s->ice : NULL;
}

static const char *const opt_names[] = {"reuse", "passes", "specialize", "lazy-seq", "layouts", "compiled", "emit", "inline", "beta", "path", "owned", "lower", "drop", "scalar"};
enum { NOPT = sizeof opt_names / sizeof *opt_names };

static void put_opts(sink *s, uint32_t mask) {
	bool first = true;
	for (uint32_t i = 0; i < NOPT; i++) {
		if (!(mask & (1u << i))) continue;
		if (!first) put(s, ", ");
		put(s, opt_names[i]);
		first = false;
	}
}

// Signal-safe unless `full`: then it may ask whether compiled code is on the stack.
static void write_context(sink *s, const clj_node *at, bool full) {
	const clj_ice_frame *top = innermost();
	put(s, "clj:   pass: ");
	put(s, top && top->pass ? top->pass : "run");
	const clj_ice_frame *b = top;
	while (b && b->backend == CLJ_BACKEND_INHERIT) b = b->prev;
	put(s, "\nclj:   backend: ");
	if (b) put(s, backend_name(b->backend));
	else if (full && clj_trace_compiled_on_stack()) put(s, "compiled code on the stack");
	else put(s, "interpreter");
	const clj_ice_frame *p = top;
	while (p && !p->line) p = p->prev;
	if (p) {
		const clj_ice_frame *named = p;
		while (named && !clj_is_string(named->file)) named = named->prev;
		put(s, "\nclj:   form: ");
		if (named) put_n(s, CLJ_STR(named->file).bytes, clj_string_len(named->file));
		else put(s, "<no file>");
		put(s, ":");
		put_u(s, p->line);
		put(s, ":");
		put_u(s, p->col);
	}
	if (top && top->prev) {
		put(s, "\nclj:   within:");
		for (const clj_ice_frame *f = top->prev; f; f = f->prev) {
			put(s, " ");
			put(s, f->pass ? f->pass : "?");
		}
	}
	if (at) {
		put(s, "\nclj:   node: ");
		put(s, clj_node_kind_name((int)at->kind));
		if (at->line) {
			put(s, " at ");
			put_u(s, at->line);
			put(s, ":");
			put_u(s, at->col);
		}
		put(s, ", id ");
		put_u(s, at->id);
	}
	uint32_t off = atomic_load_explicit(&clj_no_opt_bits, memory_order_relaxed);
	put(s, "\nclj:   optimizations: ");
	if (off) {
		put(s, "off: ");
		put_opts(s, off);
	} else {
		put(s, "all on; with CLJ_NO_OPT=1 they are off, and an answer that differs there is a pippin bug too");
	}
	put(s, "\n");
}

// A failure while reporting one (a frame chain gone bad) prints its first line only.
static _Atomic bool reporting;

void clj_ice_write_context(int fd) {
	if (atomic_exchange(&reporting, true)) return;
	sink s = {fd, NULL, 0, 0};
	write_context(&s, NULL, false);
}

static void head(sink *s, const char *kind, const char *message) {
	put(s, "clj: internal error [");
	put(s, kind);
	put(s, "]: ");
	put(s, message);
	put(s, "\nclj: this is a bug in pippin, not in the program it ran\n");
}

size_t clj_ice_render(char *buf, size_t cap, const char *kind, const clj_node *at, const char *message) {
	sink s = {-1, buf, cap, 0};
	if (cap) buf[0] = '\0';
	head(&s, kind, message);
	write_context(&s, at, true);
	return s.len;
}

// CLJ_CRASH_EXIT: a plain exit, since a wedged crash reporter can leave the aborting process unkillable (NOTES "Guard").
static _Noreturn void die(void) {
	const char *seed = getenv("CLJ_SCHED_SEED");
	if (seed && *seed) fprintf(stderr, "clj: under the seeded scheduler, CLJ_SCHED_SEED=%s\n", seed);
	fflush(stderr);
	const char *e = getenv("CLJ_CRASH_EXIT");
	if (e && *e && strcmp(e, "0") != 0) _exit(134);
	abort();
}

static _Noreturn void report(const char *kind, const clj_node *at, const char *message, const char *cfile, int cline) {
	fflush(stdout);
	bool again = atomic_exchange(&reporting, true);
	sink s = {2, NULL, 0, 0};
	head(&s, kind, message);
	if (!again) {
		write_context(&s, at, true);
		if (cfile) {
			put(&s, "clj:   C: ");
			put(&s, cfile);
			put(&s, ":");
			put_u(&s, (uint64_t)cline);
			put(&s, "\n");
		}
		put(&s, "clj:   trace");
		clj_trace_origin origin = {0, 0, (uintptr_t)__builtin_frame_address(0), 0};
		clj_trace_write(2, &origin);
	}
	die();
}

void clj_ice(const char *kind, const clj_node *at, const char *fmt, ...) {
	char    msg[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	if (n < 0) snprintf(msg, sizeof msg, "%s", fmt);
	report(kind, at, msg, NULL, 0);
}

void clj_ice_assert(const char *msg, const char *file, int line) { report("assert", NULL, msg, file, line); }

void clj_fatal(const char *msg) {
	fflush(stdout);
	bool again = atomic_exchange(&reporting, true);
	sink s = {2, NULL, 0, 0};
	put(&s, "clj: fatal: ");
	put(&s, msg);
	put(&s, "\n");
	if (!again) write_context(&s, NULL, false);
	die();
}

// ---- the kill switch

uint32_t clj_no_opt_parse(const char *spec) {
	if (!spec || !*spec || strcmp(spec, "0") == 0) return 0;
	if (strcmp(spec, "1") == 0 || strcmp(spec, "all") == 0) return CLJ_NO_OPT_ALL;
	uint32_t    mask = 0;
	const char *p = spec;
	while (*p) {
		const char *end = strchr(p, ',');
		size_t      n = end ? (size_t)(end - p) : strlen(p);
		uint32_t    i = 0;
		while (i < NOPT && (strlen(opt_names[i]) != n || strncmp(opt_names[i], p, n) != 0)) i++;
		if (i == NOPT) return UINT32_MAX;
		mask |= 1u << i;
		p += n;
		if (*p == ',') p++;
	}
	return mask;
}

void clj_no_opt_set(uint32_t mask) {
	uint32_t now = atomic_fetch_or_explicit(&clj_no_opt_bits, mask, memory_order_relaxed) | mask;
	if (now & CLJ_NO_OPT_LAYOUTS) {
		clj_tuples_enable(false);
		clj_shapes_enable(false);
		clj_flat_enable(false);
	}
	if (now & CLJ_NO_OPT_SPECIALIZE) clj_specialize_enable(false);
	if (now & CLJ_NO_OPT_LAZY_SEQ) clj_lazy_seq_inline_enable(false);
}

uint32_t clj_no_opt(void) { return atomic_load_explicit(&clj_no_opt_bits, memory_order_relaxed); }

const char *clj_no_opt_describe(uint32_t mask, char *buf, size_t cap) {
	sink s = {-1, buf, cap, 0};
	if (cap) buf[0] = '\0';
	if (mask) put_opts(&s, mask);
	else put(&s, "none");
	return buf;
}

void clj_no_opt_configure(void) {
	const char *spec = getenv("CLJ_NO_OPT");
	uint32_t    mask = clj_no_opt_parse(spec);
	if (mask == UINT32_MAX) {
		char msg[300];
		snprintf(msg, sizeof msg, "CLJ_NO_OPT=%.200s names no optimization: 1, all, or reuse,passes,specialize,lazy-seq,layouts,compiled,emit,inline", spec);
		clj_fatal(msg);
	}
	clj_no_opt_set(mask);
}
