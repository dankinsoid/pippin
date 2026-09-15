// @ai-generated(guided)
#ifndef CLJ_VALUE_H
#define CLJ_VALUE_H

#include <stdbool.h>
#include <stdint.h>

// A value is a tagged word. Low 3 bits:
//   xx1  fixnum, 63-bit signed payload in the upper bits
//   000  heap object pointer (8-byte aligned), or nil when zero
//   010  special constant: nil is 0 so zeroed memory reads as nil
//   110  char, Unicode scalar in the upper bits
typedef uintptr_t clj_value;

// Constants are static const, not macros: Swift imports the former and not expressions like these.
static const uintptr_t CLJ_TAG_MASK    = 0x7;
static const uintptr_t CLJ_TAG_PTR     = 0x0;
static const uintptr_t CLJ_TAG_SPECIAL = 0x2;
static const uintptr_t CLJ_TAG_CHAR    = 0x6;

static const clj_value CLJ_NIL   = 0;
static const clj_value CLJ_FALSE = (1 << 3) | 0x2;
static const clj_value CLJ_TRUE  = (2 << 3) | 0x2;
// Returned by a function that failed; the exception is pending in the thread (error.h). Never stored.
static const clj_value CLJ_THROWN = (3 << 3) | 0x2;
// Root of a var before its first def (var.h), and the "no init" argument of a reduce slot (object.h). Never stored anywhere else.
static const clj_value CLJ_UNBOUND = (4 << 3) | 0x2;
// Returned by a recur node after rebinding its target's slots (eval.h). Never stored.
static const clj_value CLJ_RECUR = (5 << 3) | 0x2;

static const intptr_t CLJ_FIXNUM_MAX = INTPTR_MAX >> 1;
static const intptr_t CLJ_FIXNUM_MIN = INTPTR_MIN >> 1;

static inline bool clj_is_nil(clj_value v)    { return v == CLJ_NIL; }
static inline bool clj_is_fixnum(clj_value v) { return (v & 1) != 0; }
static inline bool clj_is_bool(clj_value v)   { return v == CLJ_TRUE || v == CLJ_FALSE; }
static inline bool clj_is_char(clj_value v)   { return (v & CLJ_TAG_MASK) == CLJ_TAG_CHAR; }
static inline bool clj_is_ptr(clj_value v)    { return v != CLJ_NIL && (v & CLJ_TAG_MASK) == CLJ_TAG_PTR; }

// Clojure truthiness: only nil and false are falsy.
static inline bool clj_truthy(clj_value v) { return v != CLJ_NIL && v != CLJ_FALSE; }

// Caller guarantees the range; out-of-range promotes to a bigint at a higher layer.
static inline clj_value clj_fixnum(intptr_t n) { return ((uintptr_t)n << 1) | 1; }
static inline intptr_t  clj_fixnum_val(clj_value v) { return (intptr_t)v >> 1; }

static inline clj_value clj_bool(bool b) { return b ? CLJ_TRUE : CLJ_FALSE; }

static inline clj_value clj_char(uint32_t scalar) { return ((uintptr_t)scalar << 3) | CLJ_TAG_CHAR; }
static inline uint32_t  clj_char_val(clj_value v) { return (uint32_t)(v >> 3); }

static inline clj_value clj_from_ptr(void *p) { return (clj_value)p; }
static inline void     *clj_to_ptr(clj_value v) { return (void *)v; }

#endif
