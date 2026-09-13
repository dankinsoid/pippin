// @ai-generated(solo)
#include "clj/core.h"

const char *clj_type_name(clj_value v) {
	if (clj_is_nil(v)) return "nil";
	if (clj_is_fixnum(v)) return "fixnum";
	if (clj_is_bool(v)) return "boolean";
	if (clj_is_char(v)) return "char";
	return clj_header_of(v)->type->name;
}
