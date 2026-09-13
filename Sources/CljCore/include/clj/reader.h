// @ai-generated(solo)
#ifndef CLJ_READER_H
#define CLJ_READER_H

#include "value.h"

typedef enum {
	CLJ_READ_OK,
	CLJ_READ_EOF,
	CLJ_READ_ERROR,
} clj_read_status;

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
} clj_reader;

void clj_reader_init(clj_reader *r, const char *bytes, size_t len);
// *out is owned (+1) on CLJ_READ_OK and untouched otherwise. After an error the position is unspecified.
clj_read_status clj_read(clj_reader *r, clj_value *out);
// Borrowed: valid until the next clj_read.
const char *clj_reader_message(const clj_reader *r);

#endif
