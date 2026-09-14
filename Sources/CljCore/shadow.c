// @ai-generated(guided)
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clj/analyzer.h"
#include "clj/keyword.h"
#include "clj/map.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/vector.h"
#include "shadow_internal.h"

_Thread_local clj_shadow_stack *clj_shadow_tls;

// Signal handlers read through the key: a first touch of a _Thread_local mallocs under dyld.
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static pthread_key_t  key;
static _Atomic bool   key_ready;

static void make_key(void) {
	if (pthread_key_create(&key, free) != 0) clj_fatal("pthread_key_create failed");
	atomic_store_explicit(&key_ready, true, memory_order_release);
}

clj_shadow_stack *clj_shadow_stack_init(void) {
	clj_shadow_stack *s = calloc(1, sizeof *s);
	if (!s) clj_fatal("out of memory");
	s->mask = CLJ_SHADOW_CAPACITY - 1;
	clj_shadow_tls = s;
	pthread_once(&key_once, make_key);
	pthread_setspecific(key, s);
	return s;
}

static const clj_shadow_stack *current(void) {
	if (!atomic_load_explicit(&key_ready, memory_order_acquire)) return NULL;
	return pthread_getspecific(key);
}

static size_t held(const clj_shadow_stack *s) { return s->depth < s->mask + 1 ? s->depth : s->mask + 1; }

size_t clj_shadow_stack_snapshot(clj_shadow_frame *out, size_t cap) {
	const clj_shadow_stack *s = current();
	if (!s) return 0;
	size_t n = held(s);
	if (n > cap) n = cap;
	for (size_t i = 0; i < n; i++) out[i] = s->frames[(s->depth - 1 - i) & s->mask];
	return n;
}

size_t clj_shadow_stack_depth(void) {
	const clj_shadow_stack *s = current();
	return s ? s->depth : 0;
}

size_t clj_shadow_stack_dropped(void) {
	const clj_shadow_stack *s = current();
	return s ? s->depth - held(s) : 0;
}

void clj_debug_shadow_stack_set_capacity(size_t cap) {
	CLJ_ASSERT(cap && !(cap & (cap - 1)) && cap <= CLJ_SHADOW_CAPACITY, "shadow stack capacity must be a power of two");
	clj_shadow_stack *s = clj_shadow_tls ? clj_shadow_tls : clj_shadow_stack_init();
	CLJ_ASSERT(s->depth == 0, "shadow stack capacity changes only while empty");
	s->mask = cap - 1;
}

// ---- trace vectors

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_fn, kw_line, kw_column;

static void intern_keywords(void) {
	kw_fn = clj_keyword_from_cstr("fn");
	kw_line = clj_keyword_from_cstr("line");
	kw_column = clj_keyword_from_cstr("column");
}

static const clj_node *position_of(const clj_shadow_frame *f) {
	return f->call_site && f->call_site->line ? f->call_site : f->fn_node;
}

enum { TRACE_MAX = 256 };

clj_value clj_shadow_stack_trace(size_t max) {
	pthread_once(&keywords_once, intern_keywords);
	clj_shadow_frame frames[TRACE_MAX];
	size_t           n = clj_shadow_stack_snapshot(frames, max < TRACE_MAX ? max : TRACE_MAX);
	clj_value        trace = clj_vector_empty();
	for (size_t i = 0; i < n; i++) {
		const clj_node *at = position_of(&frames[i]);
		clj_value       m = clj_map_assoc(clj_map_empty(), kw_fn, frames[i].fn_node->u.fn.name);
		m = clj_map_assoc(m, kw_line, clj_fixnum(at->line));
		m = clj_map_assoc(m, kw_column, clj_fixnum(at->col));
		trace = clj_vector_conj(trace, m);
		clj_release(m);
	}
	return trace;
}

// ---- crash handler: write(2) only, no allocation, no locks, no printf

static int crash_fd = 2;
static int test_signal;

static void put(const char *s, size_t len) {
	while (len) {
		ssize_t w = write(crash_fd, s, len);
		if (w <= 0) return;
		s += (size_t)w;
		len -= (size_t)w;
	}
}

static void put_cstr(const char *s) { put(s, strlen(s)); }

static void put_num(size_t n) {
	char   buf[24];
	size_t i = sizeof buf;
	do {
		buf[--i] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	put(buf + i, sizeof buf - i);
}

static void put_symbol(clj_value sym) {
	if (clj_is_nil(sym)) {
		put_cstr("fn");
		return;
	}
	clj_value ns = clj_symbol_ns(sym);
	if (!clj_is_nil(ns)) {
		put(clj_string_bytes(ns), clj_string_len(ns));
		put_cstr("/");
	}
	put(clj_string_bytes(clj_symbol_name(sym)), clj_string_len(clj_symbol_name(sym)));
}

static void write_frames(int sig) {
	put_cstr("clj: signal ");
	put_num((size_t)sig);
	put_cstr(", Clojure frames (innermost first):\n");
	const clj_shadow_stack *s = current();
	size_t                  n = s ? held(s) : 0;
	for (size_t i = 0; i < n; i++) {
		const clj_shadow_frame *f = &s->frames[(s->depth - 1 - i) & s->mask];
		const clj_node         *at = position_of(f);
		put_cstr("  at ");
		put_symbol(f->fn_node->u.fn.name);
		put_cstr(" (");
		put_num(at->line);
		put_cstr(":");
		put_num(at->col);
		put_cstr(")\n");
	}
	if (s && s->depth > n) {
		put_cstr("  ... ");
		put_num(s->depth - n);
		put_cstr(" outer frames dropped\n");
	}
	if (n == 0) put_cstr("  (none)\n");
}

static void on_signal(int sig) {
	write_frames(sig);
	if (sig == test_signal) return;
	signal(sig, SIG_DFL);
	raise(sig);
}

static void install(int sig) {
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONSTACK | SA_NODEFER;
	sigaction(sig, &sa, NULL);
}

void clj_crash_handler_install(void) {
	static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE};
	for (size_t i = 0; i < sizeof signals / sizeof *signals; i++) install(signals[i]);
}

void clj_crash_handler_install_test(int sig, int fd) {
	crash_fd = fd;
	test_signal = sig;
	install(sig);
	// Test harnesses block SIGUSR1 on their threads; raise on this thread needs it open.
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}
