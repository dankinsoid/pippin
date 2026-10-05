// @ai-generated(solo)
#ifndef CLJ_DIAGNOSTIC_H
#define CLJ_DIAGNOSTIC_H

#include "object.h"

// Design §3 «Диагностика»: the structure is in the ex-data, the rendering outside it, one for every
// consumer. The keys are the rich meta's (§4 «Локация в коде») — :file :line :column :end-line
// :end-column — plus :form-line/:form-column, :suggestion, :arities/:variadic/:given.

// Position keys of the deepest link of the cause chain that carries a :line, owned; nil when none.
// with_file demands a :file beside it: a node has none of its own, so a call site inside an
// already-loaded function answers a line alone, and quoting source off it would quote the wrong file.
clj_value clj_diagnostic_position(clj_value ex, bool with_file);

// Message of the deepest link that has one: a loader's wrapper ends with it, and the inner wording is
// what a reader acts on. Owned, nil when none.
clj_value clj_diagnostic_cause_message(clj_value ex);

// ex for a human and an agent: message, position, the source line with the span underlined, the notes.
// Owned string with a trailing newline, never truncated; CLJ_THROWN only if printing the value threw.
clj_value clj_diagnostic_render(clj_value ex);

// Interns the keywords this module otherwise makes on first use; clj_init calls it (runtime.c).
void clj_diagnostic_intern_keywords(void);

#endif
