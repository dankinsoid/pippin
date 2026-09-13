// @ai-generated(solo)
#ifndef CLJ_ALLOC_H
#define CLJ_ALLOC_H

#include "clj/object.h"

// Returns the object's memory; the caller has already released its children.
void clj_dealloc(clj_header *h);

#endif
