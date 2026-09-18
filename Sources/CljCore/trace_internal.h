// @ai-generated(solo)
#ifndef CLJ_TRACE_INTERNAL_H
#define CLJ_TRACE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clj/analyzer.h"

// Where a walk of the real stack starts: the registers of an interrupted context (a signal), or the caller's own
// frame when NULL is passed.
typedef struct {
	uintptr_t pc, lr, fp, sp;
} clj_trace_origin;

// One Clojure frame of a merged trace: the fn node (an interpreted fn or a compiled stub) and its position.
typedef struct {
	const clj_node *fn;
	const clj_node *at;
} clj_trace_frame;

// Innermost first, shadow and real-stack frames merged by stack address. Async-signal-safe: no allocation, no locks.
size_t clj_trace_collect(clj_trace_frame *out, size_t cap, const clj_trace_origin *origin);

// {:fn :line :column} maps of the frames (shadow.c).
clj_value clj_trace_vector(const clj_trace_frame *frames, size_t n);
// The frames of a crash, with write(2) only (shadow.c).
void clj_trace_write(int fd, const clj_trace_origin *origin);

// True when `pc` lies in the runtime's own image or in a registered compiled unit's: code the runtime wrote.
bool clj_trace_code_known(uintptr_t pc);
// The image that holds `addr` becomes known code; clj_init passes its own address.
void clj_trace_register_image(const void *addr);
// A compiled fn's frame is on the calling thread's real stack.
bool clj_trace_compiled_on_stack(void);

#endif
