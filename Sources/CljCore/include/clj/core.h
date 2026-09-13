#ifndef CLJ_CORE_H
#define CLJ_CORE_H

#include "object.h"
#include "value.h"

// Static string; for heap objects the type descriptor's name.
const char *clj_type_name(clj_value v);

#endif
