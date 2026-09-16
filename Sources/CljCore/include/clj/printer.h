// @ai-generated(solo)
#ifndef CLJ_PRINTER_H
#define CLJ_PRINTER_H

#include "value.h"

// Clojure `pr-str`; the result is an owned string. Walks nested collections iteratively.
// Printing realizes lazy seqs: CLJ_THROWN when a thunk throws.
clj_value clj_pr_str(clj_value v);
// pr-str that stops after `max` bytes, closing what it opened after "...": what an error message quotes a
// value with, so a message about an unbounded lazy seq does not realize it. max == 0 is clj_pr_str.
clj_value clj_pr_str_max(clj_value v, size_t max);
// How much of a value an error message shows.
#define CLJ_ERROR_PRINT_MAX 64

// Clojure `print-str` for one value: strings and chars unquoted at every depth.
clj_value clj_print_str(clj_value v);

#endif
