// @ai-generated(solo)
#ifndef CLJ_HOSTTYPE_H
#define CLJ_HOSTTYPE_H

#include "object.h"

// A host (Swift) type as a value: recognition is a cast, since type(of:) answers NSError for every
// _BridgedStoredNSError (design.md §4).
typedef struct {
	clj_header  h;
	const char *name;    // how the source spelled it ("Foundation/CocoaError"), for printing; never freed
	const char *mangled; // the identity; never freed
	const void *meta;    // the host's metatype, opaque here
} clj_host_type;

extern const clj_type clj_host_type_type;

static inline bool clj_is_host_type(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_host_type_type; }
// Borrowed, valid for the life of the process.
static inline const char *clj_host_type_name(clj_value v) { return ((const clj_host_type *)clj_to_ptr(v))->name; }
static inline const char *clj_host_type_mangled(clj_value v) { return ((const clj_host_type *)clj_to_ptr(v))->mangled; }

// The type "Module/Name" names, interned and immortal, else nil. Never throws; caches both answers.
clj_value clj_host_type_named(const char *name, size_t len);
// False in a C-only host, which has no resolver at all (design §5).
bool clj_host_type_available(void);
// Whether v is a host error whose payload casts to type.
bool clj_host_type_instance(clj_value type, clj_value v);
// The type of a host error, interned by its mangled name; both strings are copied.
clj_value clj_host_type_intern(const char *name, const char *mangled, const void *meta);
// CLJ_TRUE/CLJ_FALSE, or CLJ_THROWN when the name reaches no type here: an undecidable clause is an error.
clj_value clj_host_type_catches(clj_value name, clj_value ex);

// ---- the host's side of it, installed at boot; a C-only host installs nothing (design §4, §5)

// Resolves "Module/Name"; *mangled takes the type's mangled name, owned by the host for the whole process.
typedef const void *(*clj_host_type_resolver)(const char *name, size_t len, const char **mangled);
// Whether the payload of a host error (clj_host_error_payload) is an instance of metatype.
typedef bool (*clj_host_type_checker)(const void *metatype, void *payload);
// From clj_host_boot, before any user code runs; both must be non-NULL or neither.
void clj_host_type_install(clj_host_type_resolver resolve, clj_host_type_checker check);

#endif
