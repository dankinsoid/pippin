// @ai-generated(solo)
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "clj/error.h"
#include "clj/lock.h"
#include "clj/string.h"
#include "guard_internal.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
// Unpoisons the abandoned frames; the sanitizer's own siglongjmp hook ignores a jump off the alternate stack.
void __asan_handle_no_return(void);
#define ASAN_LANDED() __asan_handle_no_return()
// Every instrumented entry calls into the sanitizer's runtime, so the fault lands there as often as in ours.
#define ASAN_IMAGE() clj_trace_register_image((const void *)&__asan_handle_no_return)
#endif
#endif
#ifndef ASAN_LANDED
#define ASAN_LANDED() ((void)0)
#define ASAN_IMAGE() ((void)0)
#endif

_Thread_local uint32_t clj_locks_held;

enum {
	ALT_STACK_SIZE = 256 * 1024,
	// A fault this far below the stack's low end is still its guard, whatever page size the frame skipped.
	GUARD_BELOW = 1024 * 1024,
	// A fault this close above the low end counts too: the guard was hit by a store the frame made above it.
	GUARD_ABOVE = 64 * 1024,
};

static struct sigaction previous[2]; // SIGSEGV, SIGBUS

// The alternate stack is a mapping of its own: a sanitizer's thread teardown unmaps whatever stack it finds installed.
void clj_guard_thread_init(clj_shadow_stack *s) {
	s->overflow = calloc(CLJ_TRACE_MAX, sizeof(clj_trace_frame));
	if (!s->overflow) clj_fatal("out of memory");
	s->altstack = mmap(NULL, ALT_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (s->altstack == MAP_FAILED) clj_fatal("mmap of the alternate signal stack failed");
	stack_t ss = {.ss_sp = s->altstack, .ss_size = ALT_STACK_SIZE, .ss_flags = 0};
	if (sigaltstack(&ss, NULL) != 0) clj_fatal("sigaltstack failed");
	(void)clj_locks_held; // touched now, so the handler's read of it allocates nothing
}

// Only the stack still installed is ours to unmap: the sanitizer's teardown may have taken it already.
void clj_guard_thread_exit(clj_shadow_stack *s) {
	stack_t cur;
	if (sigaltstack(NULL, &cur) == 0 && cur.ss_sp == s->altstack && !(cur.ss_flags & SS_DISABLE)) {
		stack_t off = {.ss_flags = SS_DISABLE};
		sigaltstack(&off, NULL);
		munmap(s->altstack, ALT_STACK_SIZE);
	}
	free(s->overflow);
}

void clj_guard_origin(const void *uap, clj_trace_origin *out) {
	const ucontext_t *uc = uap;
#if defined(__APPLE__) && defined(__aarch64__)
	const _STRUCT_ARM_THREAD_STATE64 *ss = &uc->uc_mcontext->__ss;
	*out = (clj_trace_origin){__darwin_arm_thread_state64_get_pc(*ss), __darwin_arm_thread_state64_get_lr(*ss), __darwin_arm_thread_state64_get_fp(*ss),
	                          __darwin_arm_thread_state64_get_sp(*ss)};
#elif defined(__APPLE__) && defined(__x86_64__)
	const _STRUCT_X86_THREAD_STATE64 *ss = &uc->uc_mcontext->__ss;
	*out = (clj_trace_origin){ss->__rip, 0, ss->__rbp, ss->__rsp};
#else
	(void)uc;
	*out = (clj_trace_origin){0, 0, (uintptr_t)__builtin_frame_address(0), 0};
#endif
}

// Where the interrupted thread resumes: on its alternate stack's memory as a plain stack, with the kernel's
// "on the signal stack" state cleared by the return from the handler, which a siglongjmp out of it would leave set
// and the next overflow's frame then lands on the overflowed stack (SIGILL from XNU).
static void __attribute__((noreturn)) land(clj_recovery *r) { siglongjmp(r->buf, 1); }

// Rewrites the interrupted context so that the handler's return resumes in land(): pc, sp and the first argument.
static void land_after_return(void *uap, const clj_shadow_stack *s) {
	ucontext_t *uc = uap;
	uintptr_t   top = ((uintptr_t)s->altstack + ALT_STACK_SIZE - 256) & ~(uintptr_t)15;
#if defined(__APPLE__) && defined(__aarch64__)
	_STRUCT_ARM_THREAD_STATE64 *ss = &uc->uc_mcontext->__ss;
	__darwin_arm_thread_state64_set_pc_fptr(*ss, (void (*)(void))land);
	__darwin_arm_thread_state64_set_sp(*ss, top);
	__darwin_arm_thread_state64_set_fp(*ss, 0);
	__darwin_arm_thread_state64_set_lr_fptr(*ss, (void *)0);
	ss->__x[0] = (uintptr_t)s->recovery;
#elif defined(__APPLE__) && defined(__x86_64__)
	_STRUCT_X86_THREAD_STATE64 *ss = &uc->uc_mcontext->__ss;
	ss->__rip = (uintptr_t)land;
	ss->__rsp = top - 8;
	ss->__rbp = 0;
	ss->__rdi = (uintptr_t)s->recovery;
#else
	(void)uc;
	(void)top;
	land(s->recovery);
#endif
}

static void chain(int sig, siginfo_t *info, void *uap) {
	struct sigaction *p = &previous[sig == SIGBUS];
	if (p->sa_flags & SA_SIGINFO && p->sa_sigaction) {
		p->sa_sigaction(sig, info, uap);
	} else if (p->sa_handler != SIG_DFL && p->sa_handler != SIG_IGN && p->sa_handler) {
		p->sa_handler(sig);
	} else {
		signal(sig, SIG_DFL);
	}
}

static void put(const char *s) { (void)!write(2, s, strlen(s)); }

// A fault is a stack overflow when it lands just under the thread's stack, or with the stack pointer at its end.
static bool is_overflow(const clj_shadow_stack *s, const siginfo_t *info, const clj_trace_origin *o) {
	uintptr_t lo = (uintptr_t)s->stack_lo, addr = (uintptr_t)info->si_addr;
	bool      below = addr < lo && lo - addr <= GUARD_BELOW;
	bool      at_end = o->sp >= lo - GUARD_BELOW && o->sp < lo + GUARD_ABOVE && addr < lo + GUARD_ABOVE && addr + GUARD_BELOW >= lo;
	return below || at_end;
}

bool clj_guard_signal(int sig, siginfo_t *info, void *uap) {
	const clj_shadow_stack *s = clj_shadow_stack_current();
	if (!s || !info) return false;
	clj_trace_origin origin;
	clj_guard_origin(uap, &origin);
	if (!is_overflow(s, info, &origin)) return false;
	clj_shadow_stack *w = (clj_shadow_stack *)s;
	w->noverflow = clj_trace_collect(w->overflow, CLJ_TRACE_MAX, &origin);
	const char *fatal = NULL;
	if (!s->recovery) fatal = "no recovery point on this thread";
	else if (!clj_trace_code_known(origin.pc)) fatal = "the fault is outside the runtime's own code";
	else if (clj_locks_held) fatal = "a runtime lock is held";
	if (fatal) {
		put("clj: fatal stack overflow (");
		put(fatal);
		put(")");
		clj_trace_write(2, &origin);
		signal(sig, SIG_DFL);
		return true;
	}
	land_after_return(uap, s);
	return true;
}

static void on_signal(int sig, siginfo_t *info, void *uap) {
	if (!clj_guard_signal(sig, info, uap)) chain(sig, info, uap);
}

void clj_guard_install(void) {
	ASAN_IMAGE();
	static const int signals[] = {SIGSEGV, SIGBUS};
	for (size_t i = 0; i < 2; i++) {
		struct sigaction sa;
		memset(&sa, 0, sizeof sa);
		sa.sa_sigaction = on_signal;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_ONSTACK | SA_NODEFER | SA_SIGINFO;
		sigaction(signals[i], &sa, &previous[i]);
	}
}

// ---- recovery points

void clj_recovery_push(clj_recovery *r) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (!s) s = clj_shadow_stack_init();
	r->shadow = s;
	r->prev = s->recovery;
	r->depth = s->depth;
	r->exec_depth = clj_eval_exec_depth();
	r->bindings = clj_var_bindings_mark();
	s->recovery = r;
}

void clj_recovery_pop(clj_recovery *r) {
	clj_shadow_stack *s = r->shadow;
	s->recovery = r->prev;
}

clj_value clj_recovery_throw(clj_recovery *r) {
	ASAN_LANDED();
	clj_shadow_stack *s = r->shadow;
	s->depth = r->depth;
	clj_eval_exec_depth_set(r->exec_depth);
	clj_var_bindings_unwind(r->bindings);
	clj_release(clj_take_pending());
	clj_value trace = clj_trace_vector(s->overflow, s->noverflow);
	s->noverflow = 0;
	clj_value message = clj_string_from_cstr("Stack overflow");
	clj_value ex = clj_ex_info(message, CLJ_NIL);
	clj_release(message);
	return clj_throw_traced(ex, trace);
}
