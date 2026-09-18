// @ai-generated(solo)
#ifndef CLJ_READER_H
#define CLJ_READER_H

#include "value.h"

typedef enum {
	CLJ_READ_OK,
	CLJ_READ_EOF,
	CLJ_READ_ERROR,
} clj_read_status;

// Qualifies a symbol under syntax-quote: returns an owned symbol, the same one when it stays as is.
typedef clj_value (*clj_symbol_resolver)(clj_value sym, void *ctx);
// The namespace name (owned string) an alias reaches, or the current one for nil; nil when the alias is unknown. For ::kw and #::{}.
typedef clj_value (*clj_ns_resolver)(clj_value alias, void *ctx);
// The value of `#tag form`, owned; CLJ_THROWN with the exception pending, or CLJ_UNBOUND when no reader takes the tag.
typedef clj_value (*clj_tag_reader)(clj_value tag, clj_value form, void *ctx);

// The buffer stays borrowed across calls. Lines and columns are 1-based; a column counts code points.
typedef struct {
	const char *bytes;
	size_t      len;
	size_t      pos;
	uint32_t    line;
	uint32_t    col;
	uint32_t    form_line; // start of the last form returned
	uint32_t    form_col;
	uint32_t    error_line;
	uint32_t    error_col;
	char        message[160]; // set on CLJ_READ_ERROR; read through clj_reader_message
	// Namespace resolution lives outside the reader: NULL leaves syntax-quoted symbols unqualified and ::kw an error.
	clj_symbol_resolver resolve;
	clj_ns_resolver     resolve_ns;
	clj_tag_reader      read_tag; // NULL: clj_default_data_reader alone
	void               *resolve_ctx;
	clj_value           features; // set of keywords #?( ) selects on, borrowed; nil means :default alone
} clj_reader;

// Leaves the resolvers NULL and takes the process-wide features; set them after the call.
void clj_reader_init(clj_reader *r, const char *bytes, size_t len);
// The features every clj_reader_init takes: a set of keywords or nil (:default alone). Retained; process-wide.
void      clj_reader_set_features(clj_value features);
clj_value clj_reader_features(void);
// *out is owned (+1) on CLJ_READ_OK and untouched otherwise. After an error the position is unspecified.
clj_read_status clj_read(clj_reader *r, clj_value *out);

// Interns the keywords this module otherwise makes on first use; clj_init calls it (runtime.c).
void clj_reader_intern_keywords(void);
// Borrowed: valid until the next clj_read.
const char *clj_reader_message(const clj_reader *r);
// The built-in tags, #inst and #uuid: what clojure.core/default-data-readers holds. CLJ_UNBOUND for any other tag.
clj_value clj_default_data_reader(clj_value tag, clj_value form);

#endif
