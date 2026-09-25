// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>

#include "clj/error.h"
#include "clj/hosttype.h"
#include "clj/lock.h"
#include "clj/string.h"

static uint32_t host_type_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool host_type_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

// Interned by mangled name, so identity is equality, as for a keyword.
const clj_type clj_host_type_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "host-type",
	.hash = host_type_hash,
	.equals = host_type_equals,
};

typedef struct {
	const char *name; // the spelling looked up, which may differ from the type's own
	clj_value   type; // nil records a name this host reaches no type for
} alias;

static clj_lock table_lock = CLJ_LOCK_INIT;
static clj_value *types;
static size_t     ntypes, captypes;
static alias     *aliases;
static size_t     naliases, capaliases;

static char *copy(const char *s, size_t len) {
	char *out = malloc(len + 1);
	if (!out) clj_fatal("out of memory");
	memcpy(out, s, len);
	out[len] = '\0';
	return out;
}

static clj_value find_mangled(const char *mangled) {
	for (size_t i = 0; i < ntypes; i++) {
		if (strcmp(clj_host_type_mangled(types[i]), mangled) == 0) return types[i];
	}
	return CLJ_NIL;
}

// Immortal by construction: the table is the only owner and never drops one, so a debug build's live count
// must not carry it.
static clj_value make(const char *name, size_t namelen, const char *mangled, const void *meta) {
	int64_t         before = clj_debug_live_objects();
	clj_host_type  *t = clj_alloc(&clj_host_type_type, sizeof *t);
	t->h.flags |= CLJ_FLAG_IMMORTAL | CLJ_FLAG_SHARED;
	t->name = copy(name, namelen);
	t->mangled = copy(mangled, strlen(mangled));
	t->meta = meta;
	if (before >= 0) clj_debug_live_objects_exclude(clj_debug_live_objects() - before);
	if (ntypes == captypes) {
		captypes = captypes ? captypes * 2 : 8;
		types = realloc(types, captypes * sizeof *types);
		if (!types) clj_fatal("out of memory");
	}
	return types[ntypes++] = clj_from_ptr(t);
}

static void remember(const char *name, size_t len, clj_value type) {
	if (naliases == capaliases) {
		capaliases = capaliases ? capaliases * 2 : 8;
		aliases = realloc(aliases, capaliases * sizeof *aliases);
		if (!aliases) clj_fatal("out of memory");
	}
	aliases[naliases].name = copy(name, len);
	aliases[naliases++].type = type;
}

static clj_host_type_resolver resolver;
static clj_host_type_checker  checker;

void clj_host_type_install(clj_host_type_resolver resolve, clj_host_type_checker check) {
	resolver = resolve;
	checker = check;
}

bool clj_host_type_available(void) { return resolver != NULL; }

clj_value clj_host_type_named(const char *name, size_t len) {
	clj_lock_lock(&table_lock);
	clj_value found = CLJ_NIL;
	bool      known = false;
	for (size_t i = 0; i < naliases && !known; i++) {
		if (strlen(aliases[i].name) == len && memcmp(aliases[i].name, name, len) == 0) {
			found = aliases[i].type;
			known = true;
		}
	}
	if (!known) {
		const char *mangled = NULL;
		const void *meta = resolver ? resolver(name, len, &mangled) : NULL;
		if (meta && mangled) {
			found = find_mangled(mangled);
			if (clj_is_nil(found)) found = make(name, len, mangled, meta);
		}
		remember(name, len, found);
	}
	clj_lock_unlock(&table_lock);
	return found;
}

clj_value clj_host_type_intern(const char *name, const char *mangled, const void *meta) {
	if (!mangled) return CLJ_NIL;
	clj_lock_lock(&table_lock);
	clj_value found = find_mangled(mangled);
	if (clj_is_nil(found)) found = make(name, strlen(name), mangled, meta);
	clj_lock_unlock(&table_lock);
	return found;
}

bool clj_host_type_instance(clj_value type, clj_value v) {
	if (!clj_is_host_type(type) || !clj_is_host_error(v) || !checker) return false;
	return checker(((const clj_host_type *)clj_to_ptr(type))->meta, clj_host_error_payload(v));
}

clj_value clj_host_type_catches(clj_value name, clj_value ex) {
	const char *text = clj_string_bytes(name);
	clj_value   type = clj_host_type_named(text, clj_string_len(name));
	if (clj_is_nil(type)) {
		return clj_throw_msg(clj_host_type_available() ? "Unable to resolve host type: %s"
		                                               : "No host type resolver: %s cannot be caught by this host",
		                     text);
	}
	return clj_bool(clj_host_type_instance(type, ex));
}
