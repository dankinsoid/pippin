// @ai-generated(solo)
#ifndef CLJ_INST_H
#define CLJ_INST_H

#include "object.h"

// java.util.Date: milliseconds since the epoch, UTC.
typedef struct {
	clj_header h;
	int64_t    ms;
} clj_inst;

extern const clj_type clj_inst_type;

clj_value clj_inst_new(int64_t ms);
// clojure.instant's read-instant-date: "yyyy[-MM[-dd[THH[:mm[:ss[.fff…]]]]]][Z|±hh:mm]", every field
// range-checked. CLJ_THROWN with that reader's message otherwise.
clj_value clj_inst_parse(const char *s, size_t n);
// "yyyy-MM-ddTHH:mm:ss.SSS-00:00" plus a NUL; cap of 40 always fits.
void clj_inst_format(clj_value inst, char *out, size_t cap);

// The #inst data reader: clj_inst_parse of a string form.
clj_value clj_inst_read(clj_value form);
void      clj_inst_builtins_install(void);

static inline bool      clj_is_inst(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_inst_type; }
static inline clj_inst *clj_inst_of(clj_value v) { return (clj_inst *)clj_to_ptr(v); }
static inline int64_t   clj_inst_ms(clj_value v) { return clj_inst_of(v)->ms; }

#endif
