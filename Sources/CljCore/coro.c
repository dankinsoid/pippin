// @ai-generated(solo)
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/guard.h"
#include "clj/profile.h"
#include "coro_internal.h"
#include "guard_internal.h"
#include "profile_internal.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
void __sanitizer_start_switch_fiber(void **fake_stack_save, const void *bottom, size_t size);
void __sanitizer_finish_switch_fiber(void *fake_stack_save, const void **bottom_old, size_t *size_old);
void __asan_unpoison_memory_region(const void *addr, size_t size);
#define ASAN_START(save, lo, size) __sanitizer_start_switch_fiber((save), (lo), (size))
// A cached mapping keeps the red zones of the frames that died on it.
#define ASAN_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#define ASAN_FINISH(save)                                                                                                                            \
	do {                                                                                                                                             \
		const void *lo_;                                                                                                                             \
		size_t      sz_;                                                                                                                             \
		__sanitizer_finish_switch_fiber((save), &lo_, &sz_);                                                                                         \
	} while (0)
#endif
#endif
#ifndef ASAN_START
#define ASAN_START(save, lo, size) ((void)(save), (void)(lo), (void)(size))
#define ASAN_FINISH(save) ((void)(save))
#define ASAN_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

// ---- the context switch: callee-saved registers and the stack pointer, nothing else (no signal mask)

#if defined(__aarch64__)
__asm__(".text\n"
        ".globl _clj_ctx_switch\n"
        ".p2align 2\n"
        "_clj_ctx_switch:\n"
        "  sub sp, sp, #160\n"
        "  stp x19, x20, [sp, #0]\n"
        "  stp x21, x22, [sp, #16]\n"
        "  stp x23, x24, [sp, #32]\n"
        "  stp x25, x26, [sp, #48]\n"
        "  stp x27, x28, [sp, #64]\n"
        "  stp x29, x30, [sp, #80]\n"
        "  stp d8, d9, [sp, #96]\n"
        "  stp d10, d11, [sp, #112]\n"
        "  stp d12, d13, [sp, #128]\n"
        "  stp d14, d15, [sp, #144]\n"
        "  mov x9, sp\n"
        "  str x9, [x0]\n"
        "  mov sp, x1\n"
        "  ldp x19, x20, [sp, #0]\n"
        "  ldp x21, x22, [sp, #16]\n"
        "  ldp x23, x24, [sp, #32]\n"
        "  ldp x25, x26, [sp, #48]\n"
        "  ldp x27, x28, [sp, #64]\n"
        "  ldp x29, x30, [sp, #80]\n"
        "  ldp d8, d9, [sp, #96]\n"
        "  ldp d10, d11, [sp, #112]\n"
        "  ldp d12, d13, [sp, #128]\n"
        "  ldp d14, d15, [sp, #144]\n"
        "  add sp, sp, #160\n"
        "  ret\n");
enum { SWITCH_FRAME = 160, SWITCH_LR = 88 };
#elif defined(__x86_64__)
__asm__(".text\n"
        ".globl _clj_ctx_switch\n"
        ".p2align 4\n"
        "_clj_ctx_switch:\n"
        "  pushq %rbp\n"
        "  pushq %rbx\n"
        "  pushq %r12\n"
        "  pushq %r13\n"
        "  pushq %r14\n"
        "  pushq %r15\n"
        "  movq %rsp, (%rdi)\n"
        "  movq %rsi, %rsp\n"
        "  popq %r15\n"
        "  popq %r14\n"
        "  popq %r13\n"
        "  popq %r12\n"
        "  popq %rbx\n"
        "  popq %rbp\n"
        "  ret\n");
// Six registers, the return address, and one pad word so the entry sees rsp ≡ 8 (mod 16) as after a call.
enum { SWITCH_FRAME = 64, SWITCH_LR = 48 };
#else
#error "clj_ctx_switch: no context switch for this architecture"
#endif

// ---- the coroutine object

_Thread_local clj_coro *clj_coro_tls;

static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static pthread_key_t  key;
static _Atomic bool   key_ready;
static _Atomic size_t live_coros;
static size_t         stack_size = 512 * 1024;
static _Atomic uint64_t switches;

static void coro_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_coro *c = self;
	visit(c->fn, ctx);
	for (size_t i = 0; i < c->nargs; i++) visit(c->args[i], ctx);
	visit(c->result, ctx);
}

static void coro_finalize(void *self) {
	clj_coro *c = self;
	CLJ_ASSERT(!c->map, "a coroutine died with its stack mapped");
	free(c->args);
	pthread_mutex_destroy(&c->lock);
	pthread_cond_destroy(&c->cond);
	clj_coro_free_spawn_trace(c);
	atomic_fetch_sub_explicit(&live_coros, 1, memory_order_relaxed);
}

static uint32_t coro_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool coro_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

const clj_type clj_coro_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "coroutine",
	.each_child = coro_each_child,
	.finalize = coro_finalize,
	.hash = coro_hash,
	.equals = coro_equals,
};

static void coro_init(clj_coro *c) {
	pthread_mutex_init(&c->lock, NULL);
	pthread_cond_init(&c->cond, NULL);
	c->state = CLJ_CORO_NEW;
}

static size_t page_size(void) {
	static size_t page;
	if (!page) page = (size_t)sysconf(_SC_PAGESIZE);
	return page;
}

static size_t round_up(size_t n, size_t to) { return (n + to - 1) / to * to; }

// Finished coroutines leave their mappings here: mmap, mprotect and munmap per spawn cost more than the spawn.
enum { STACK_CACHE_MAX = 256 };

static clj_lock cache_lock = CLJ_LOCK_INIT;
static void    *cache[STACK_CACHE_MAX];
static size_t   ncached;
static size_t   cache_map_size;

static void *map_take(size_t size) {
	void *base = NULL;
	clj_lock_lock(&cache_lock);
	if (ncached && cache_map_size == size) base = cache[--ncached];
	clj_lock_unlock(&cache_lock);
	return base;
}

static bool map_keep(void *base, size_t size) {
	bool kept = false;
	clj_lock_lock(&cache_lock);
	if (ncached < STACK_CACHE_MAX && (ncached == 0 || cache_map_size == size)) {
		cache_map_size = size;
		cache[ncached++] = base;
		kept = true;
	}
	clj_lock_unlock(&cache_lock);
	return kept;
}

// The ring starts half a page past the stack's top: a shallow parked coroutine touches one page for both.
clj_coro *clj_coro_alloc(void) {
	size_t page = page_size(), guard = page, stack = round_up(stack_size, page);
	size_t ring = round_up(page / 2 + sizeof(clj_shadow_stack), page);
	size_t size = guard + stack + ring;
	void  *base = map_take(size);
	if (base) ASAN_UNPOISON((char *)base + guard, size - guard);
	if (!base) {
		base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (base == MAP_FAILED) clj_fatal("mmap of a coroutine stack failed");
		if (mprotect(base, guard, PROT_NONE) != 0) clj_fatal("mprotect of a coroutine guard page failed");
	}
	clj_coro *c = clj_alloc(&clj_coro_type, sizeof *c); // zeroed by the allocator
	coro_init(c);
	// The handle is held by the spawner and released by a carrier: atomic RC from birth.
	c->h.flags |= CLJ_FLAG_SHARED;
	atomic_fetch_add_explicit(&live_coros, 1, memory_order_relaxed);
	c->map = base;
	c->map_size = size;
	char             *top = (char *)base + guard + stack + page / 2;
	clj_shadow_stack *s = (clj_shadow_stack *)top;
	// A cached ring keeps stale frames; the header is reset, the overflow array is valid up to noverflow only.
	memset(s, 0, offsetof(clj_shadow_stack, overflow));
	s->mask = CLJ_SHADOW_CAPACITY - 1;
	s->stack_lo = (char *)base + guard;
	s->stack_hi = top;
	s->stack_limit = s->stack_lo + 64 * 1024;
	c->shadow = s;
	// The first switch "returns" into clj_coro_entry with an empty frame chain.
	char *sp = top - SWITCH_FRAME;
	memset(sp, 0, SWITCH_FRAME);
	*(void **)(sp + SWITCH_LR) = (void *)clj_coro_entry;
	c->sp = sp;
	return c;
}

void clj_coro_free_stack(clj_coro *c) {
	if (!c->map) return;
	if (!map_keep(c->map, c->map_size)) munmap(c->map, c->map_size);
	c->map = NULL;
	c->shadow = NULL;
}

// Hands the pages below the parked stack pointer back once; a deeper run since a shallower park stays resident.
void clj_coro_advise_stack(clj_coro *c) {
	if (!c->map) return;
	size_t    page = page_size();
	uintptr_t lo = (uintptr_t)c->shadow->stack_lo, sp = (uintptr_t)c->sp;
	uintptr_t hi = (sp & ~(page - 1)) - page;
	if (hi <= lo || hi - lo < page) return;
	if (c->advised_lo == lo && c->advised_hi == hi) return;
#ifdef __APPLE__
	madvise((void *)lo, hi - lo, MADV_FREE_REUSABLE);
#else
	madvise((void *)lo, hi - lo, MADV_DONTNEED);
#endif
	c->advised_lo = lo;
	c->advised_hi = hi;
}

void clj_coro_set_stack_size(size_t bytes) { stack_size = bytes < 64 * 1024 ? 64 * 1024 : bytes; }

size_t clj_coro_stack_size(void) { return stack_size; }

// ---- the carrier of a bare thread and its implicit coroutine

static void thread_exit(void *p) {
	clj_carrier *car = p;
	clj_guard_thread_exit(car);
	clj_coro *c = car->implicit;
	if (c) {
		free(c->shadow);
		pthread_mutex_destroy(&c->lock);
		pthread_cond_destroy(&c->cond);
		free(c);
	}
	free(car);
}

static void make_key(void) {
	if (pthread_key_create(&key, thread_exit) != 0) clj_fatal("pthread_key_create failed");
	atomic_store_explicit(&key_ready, true, memory_order_release);
}

// Off the pool allocator and immortal: a thread's own execution outlives every test's live-object baseline.
static clj_coro *implicit_init(void) {
	clj_carrier *car = calloc(1, sizeof *car);
	clj_coro    *c = calloc(1, sizeof *c);
	if (!car || !c) clj_fatal("out of memory");
	c->h = (clj_header){1, CLJ_FLAG_IMMORTAL | CLJ_FLAG_SHARED, &clj_coro_type};
	coro_init(c);
	c->implicit = true;
	c->state = CLJ_CORO_RUNNING;
	c->shadow = calloc(1, sizeof *c->shadow);
	if (!c->shadow) clj_fatal("out of memory");
	c->shadow->mask = CLJ_SHADOW_CAPACITY - 1;
	clj_shadow_stack_bounds(c->shadow);
	c->carrier = car;
	car->implicit = car->current = c;
	clj_coro_tls = c;
	clj_shadow_tls = c->shadow;
	pthread_once(&key_once, make_key);
	pthread_setspecific(key, car);
	clj_guard_thread_init(car);
	return c;
}

clj_coro *clj_coro_current(void) {
	clj_coro *c = clj_coro_tls;
	return c ? c : implicit_init();
}

// Not inlined and not pure: two calls around a park must both reach the TLS afresh (lock.h).
__attribute__((noinline)) uint32_t *clj_locks_held_slot(void) {
	__asm__ volatile("" ::: "memory");
	clj_coro *c = clj_coro_tls;
	if (!c) c = implicit_init();
	return &c->locks_held;
}

clj_shadow_stack *clj_shadow_stack_init(void) { return clj_coro_current()->shadow; }

const clj_carrier *clj_carrier_current(void) {
	if (!atomic_load_explicit(&key_ready, memory_order_acquire)) return NULL;
	return pthread_getspecific(key);
}

clj_carrier *clj_carrier_here(void) { return clj_coro_current()->carrier; }

const clj_shadow_stack *clj_shadow_stack_current(void) {
	const clj_carrier *car = clj_carrier_current();
	return car && car->current ? car->current->shadow : NULL;
}

bool clj_coro_in_coroutine(void) {
	clj_coro *c = clj_coro_tls;
	return c && !c->implicit;
}

bool clj_coro_on_main_carrier(void) {
	clj_coro *c = clj_coro_tls;
	return c && c->carrier->is_main;
}

// ---- switching

void clj_coro_switch_in(clj_carrier *car, clj_coro *c) {
	c->carrier = car;
	car->current = c;
	clj_coro_tls = c;
	clj_shadow_tls = c->shadow;
	atomic_store_explicit(&c->state, CLJ_CORO_RUNNING, memory_order_relaxed);
	atomic_fetch_add_explicit(&switches, 1, memory_order_relaxed);
	ASAN_START(&car->asan_fake, c->shadow->stack_lo, (size_t)(c->shadow->stack_hi - c->shadow->stack_lo));
	clj_ctx_switch(&car->return_sp, c->sp);
	ASAN_FINISH(car->asan_fake);
	car->current = car->implicit;
	clj_coro_tls = car->implicit;
	clj_shadow_tls = car->implicit->shadow;
}

void clj_coro_switch_out(clj_coro *c) {
	clj_carrier      *car = c->carrier;
	clj_shadow_stack *cs = car->implicit->shadow;
	bool              done = atomic_load_explicit(&c->state, memory_order_relaxed) == CLJ_CORO_DONE;
	ASAN_START(done ? NULL : &c->asan_fake, cs->stack_lo, (size_t)(cs->stack_hi - cs->stack_lo));
	clj_ctx_switch(&c->sp, car->return_sp);
	ASAN_FINISH(c->asan_fake);
}

void clj_coro_entry(void) {
	clj_coro *c = clj_coro_tls;
	ASAN_FINISH(c->asan_fake);
	clj_eval_top_enter();
	clj_recovery rec;
	clj_recovery_push(&rec);
	clj_value r;
	if (sigsetjmp(rec.buf, 0)) r = clj_recovery_throw(&rec);
	else r = clj_invoke(c->fn, c->args, c->nargs);
	clj_recovery_pop(&rec);
	clj_eval_top_leave();
	c->threw = r == CLJ_THROWN;
	c->result = c->threw ? clj_take_pending() : r;
	clj_share(c->result);
	atomic_store_explicit(&c->state, CLJ_CORO_DONE, memory_order_release);
	clj_coro_switch_out(c);
	clj_fatal("a finished coroutine was resumed");
}

uint64_t clj_debug_coro_switches(void) { return atomic_load_explicit(&switches, memory_order_relaxed); }

size_t clj_debug_live_coros(void) { return atomic_load_explicit(&live_coros, memory_order_relaxed); }

// A finished coroutine is released last on its carrier, so the count reaching the target means every finish ran.
bool clj_debug_coro_settle(size_t target, uint64_t ms) {
	uint64_t deadline = clj_profile_now() + ms * 1000000u;
	while (atomic_load_explicit(&live_coros, memory_order_acquire) > target) {
		if (clj_profile_now() > deadline) return false;
		usleep(200);
	}
	return true;
}

size_t clj_debug_phys_footprint(void) {
#ifdef __APPLE__
	task_vm_info_data_t    info;
	mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
	return (size_t)info.phys_footprint;
#else
	return 0;
#endif
}

// ---- bench: a coroutine that switches straight back, n times

static void bounce(void) {
	clj_coro *c = clj_coro_tls;
	for (;;) clj_coro_switch_out(c);
}

double clj_bench_switch_ns(size_t n) {
	clj_carrier *car = clj_carrier_here();
	clj_coro    *c = clj_coro_alloc();
	*(void **)((char *)c->sp + SWITCH_LR) = (void *)bounce;
	uint64_t t0 = clj_profile_now();
	for (size_t i = 0; i < n; i++) clj_coro_switch_in(car, c);
	uint64_t t1 = clj_profile_now();
	atomic_store_explicit(&c->state, CLJ_CORO_DONE, memory_order_relaxed);
	clj_coro_free_stack(c);
	clj_release(clj_from_ptr(c));
	return (double)(t1 - t0) / (double)(2 * n);
}
