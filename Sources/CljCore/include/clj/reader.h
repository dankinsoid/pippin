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
	// Namespace resolution lives outside the reader: NULL leaves syntax-quoted symbols unqualified.
	clj_symbol_resolver resolve;
	void               *resolve_ctx;
} clj_reader;

// Leaves resolve NULL; set it after the call.
void clj_reader_init(clj_reader *r, const char *bytes, size_t len);
// *out is owned (+1) on CLJ_READ_OK and untouched otherwise. After an error the position is unspecified.
clj_read_status clj_read(clj_reader *r, clj_value *out);
// Borrowed: valid until the next clj_read.
const char *clj_reader_message(const clj_reader *r);

#endif
