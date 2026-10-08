// @ai-generated(solo)
#ifndef CLJ_URI_H
#define CLJ_URI_H

#include "object.h"

// A component of the text: absent when len < 0.
typedef struct {
	int32_t from, len;
} clj_uri_span;

// An RFC 3986 URI reference, equal and hashed by its text (design §3 «"Наш хост" — это C-ядро»).
typedef struct {
	clj_header   h;
	clj_slot     text;
	clj_uri_span scheme, user_info, host, path, query, fragment;
	int64_t      port; // -1 when absent
} clj_uri;

extern const clj_type clj_uri_type;

static inline bool     clj_is_uri(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_uri_type; }
static inline clj_uri *clj_uri_of(clj_value v) { return (clj_uri *)clj_to_ptr(v); }
// Borrowed: valid while u is.
static inline clj_value clj_uri_text(clj_value u) { return clj_uri_of(u)->text.v; }

// nil when text is no URI reference.
clj_value clj_uri_parse(clj_value text);
void      clj_uri_builtins_install(void);

#endif
