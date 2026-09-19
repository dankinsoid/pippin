// @ai-generated(solo)
#ifndef CLJ_GUARD_INTERNAL_H
#define CLJ_GUARD_INTERNAL_H

#include <signal.h>
#include <stdbool.h>

#include "clj/guard.h"
#include "shadow_internal.h"
#include "trace_internal.h"

// clj_init: SIGSEGV and SIGBUS handlers that turn a guard-page fault into a landing at the thread's recovery point.
void clj_guard_install(void);
// The alternate signal stack of a thread, made with its carrier and freed with it.
typedef struct clj_carrier clj_carrier;
void clj_guard_thread_init(clj_carrier *car);
void clj_guard_thread_exit(clj_carrier *car);
// The overflow check of a handler: lands at the recovery point, or returns true after reporting a fatal overflow.
bool clj_guard_signal(int sig, siginfo_t *info, void *uap);
// The interrupted registers of a signal's ucontext, as a walk origin.
void clj_guard_origin(const void *uap, clj_trace_origin *out);

// eval.c: the clj_exec_run nesting of the thread, restored at a landing.
uint32_t clj_eval_exec_depth(void);
void     clj_eval_exec_depth_set(uint32_t depth);
// var.c: the thread's binding frames above a mark are popped at a landing.
void *clj_var_bindings_mark(void);
void  clj_var_bindings_unwind(void *mark);

#endif
