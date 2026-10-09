// @ai-generated(solo)
// What the parser fuzzers share (docs/notes/fuzzing.md, "Parser fuzzing"): the boot, a finding, the leak check.
#ifndef FUZZ_PARSERS_FUZZ_H
#define FUZZ_PARSERS_FUZZ_H

#include <stddef.h>
#include <stdint.h>

#include "clj/core.h"

// One input through one harness. It must release everything it makes and leave nothing pending.
typedef void (*fz_body)(const uint8_t *data, size_t size);

// Boots the runtime under the allocator ASan can see; once, from LLVMFuzzerInitialize.
void fz_boot(void);
// The value of src, read and evaluated in user. Owned; aborts on a throw, since the harness rests on it.
clj_value fz_eval(const char *src);
// What a sanitizer cannot see: printed, then abort(), which libFuzzer saves with the input as a crash.
_Noreturn void fz_finding(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Releases an owned result; a throw is an answer, so its exception is taken and dropped.
void fz_drop(clj_value v);
// Body twice; the second run must end with the live count it began with. The first interns keywords, which are permanent.
int fz_run(fz_body body, const uint8_t *data, size_t size);
// Strict UTF-8, no NUL: what a host may hand the runtime as a string. Other inputs are not the parser's to take.
bool fz_utf8(const uint8_t *data, size_t size);
// Owned string of a text that passed fz_utf8.
clj_value fz_string(const uint8_t *data, size_t size);
// The C string of a string value, borrowed.
static inline const char *fz_cstr(clj_value s) { return clj_string_bytes(s); }
// The first form of text, read with the runtime's resolvers; msg takes the message of an error.
clj_read_status fz_read(const char *bytes, size_t len, clj_value *out, char *msg, size_t cap);

#endif
