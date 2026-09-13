// @ai-generated(guided)
#ifndef CLJ_RUNTIME_H
#define CLJ_RUNTIME_H

#include <stddef.h>

// Creates clojure.core and user and installs the builtins. Once per process; later calls return at once.
void clj_init(void);

// Where println/prn write. Process-wide; NULL restores stdout.
typedef void (*clj_output_fn)(const char *bytes, size_t len, void *ctx);
void clj_set_output(clj_output_fn fn, void *ctx);
void clj_output(const char *bytes, size_t len);

// Interns every builtin into clojure.core; clj_init calls it.
void clj_builtins_install(void);

#endif
