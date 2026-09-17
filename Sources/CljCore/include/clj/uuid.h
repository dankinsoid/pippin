// @ai-generated(solo)
#ifndef CLJ_UUID_H
#define CLJ_UUID_H

#include "object.h"

// java.util.UUID: the 128 bits as the JVM's two signed longs, so compare orders as UUID.compareTo does.
typedef struct {
	clj_header h;
	int64_t    hi, lo;
} clj_uuid;

extern const clj_type clj_uuid_type;

clj_value clj_uuid_new(int64_t hi, int64_t lo);
// UUID.fromString's rule: five groups of 1–16 hex digits, at most 36 characters, each group masked to its
// field. nil when the text is anything else.
clj_value clj_uuid_parse(const char *s, size_t n);
// Version 4 from the system's CSPRNG.
clj_value clj_uuid_random(void);
// The 36-character lowercase form plus a NUL.
void clj_uuid_format(clj_value u, char out[37]);

// The #uuid data reader: a uuid from its string, "Invalid UUID string" otherwise.
clj_value clj_uuid_read(clj_value form);
void      clj_uuid_builtins_install(void);

static inline bool      clj_is_uuid(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_uuid_type; }
static inline clj_uuid *clj_uuid_of(clj_value v) { return (clj_uuid *)clj_to_ptr(v); }

#endif
