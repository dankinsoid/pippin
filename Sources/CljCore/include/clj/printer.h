// @ai-generated(solo)
#ifndef CLJ_PRINTER_H
#define CLJ_PRINTER_H

#include "value.h"

// Clojure `pr-str`; the result is an owned string. Walks nested collections iteratively.
// Printing realizes lazy seqs: CLJ_THROWN when a thunk throws.
clj_value clj_pr_str(clj_value v);
// Clojure `print-str` for one value: strings and chars unquoted at every depth.
clj_value clj_print_str(clj_value v);

#endif
