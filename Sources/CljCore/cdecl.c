// @ai-generated(solo)
#include "clj/cdecl.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/objc.h"

// Only a global and a function need an address: a header's constants arrive by value (design §5 «C — уровень 0»).

typedef struct {
	const char *kind;
	size_t      size;
	bool        floating;
	bool        signed_;
} global_kind;

static const global_kind kinds[] = {
	{"double", sizeof(double), true, true},
	{"float", sizeof(float), true, true},
	{"char", sizeof(signed char), false, true},
	{"uchar", sizeof(unsigned char), false, false},
	{"short", sizeof(short), false, true},
	{"ushort", sizeof(unsigned short), false, false},
	{"int", sizeof(int), false, true},
	{"uint", sizeof(unsigned int), false, false},
	// The runtime and the header are compiled for one target, so the C type's own width is the right one.
	{"long", sizeof(long), false, true},
	{"ulong", sizeof(unsigned long), false, false},
	{"llong", sizeof(long long), false, true},
	{"ullong", sizeof(unsigned long long), false, false},
	{"bool", sizeof(bool), false, false},
};

static clj_value read_global(const void *addr, const global_kind *k) {
	if (k->floating) {
		double d;
		if (k->size == sizeof(float)) {
			float f;
			memcpy(&f, addr, sizeof f);
			d = f;
		} else {
			memcpy(&d, addr, sizeof d);
		}
		return clj_double_new(d);
	}
	if (strcmp(k->kind, "bool") == 0) {
		bool b;
		memcpy(&b, addr, sizeof b);
		return b ? CLJ_TRUE : CLJ_FALSE;
	}
	if (k->signed_) {
		int64_t v = 0;
		switch (k->size) {
		case 1: {
			signed char c;
			memcpy(&c, addr, sizeof c);
			v = c;
			break;
		}
		case sizeof(short): {
			short s;
			memcpy(&s, addr, sizeof s);
			v = s;
			break;
		}
		case sizeof(int): {
			int i;
			memcpy(&i, addr, sizeof i);
			v = i;
			break;
		}
		default: {
			long long l;
			memcpy(&l, addr, sizeof l);
			v = l;
			break;
		}
		}
		return clj_long_new(v);
	}
	uint64_t u = 0;
	switch (k->size) {
	case 1: {
		unsigned char c;
		memcpy(&c, addr, sizeof c);
		u = c;
		break;
	}
	case sizeof(unsigned short): {
		unsigned short s;
		memcpy(&s, addr, sizeof s);
		u = s;
		break;
	}
	case sizeof(unsigned int): {
		unsigned int i;
		memcpy(&i, addr, sizeof i);
		u = i;
		break;
	}
	default: {
		unsigned long long l;
		memcpy(&l, addr, sizeof l);
		u = l;
		break;
	}
	}
	// A long is 64-bit signed, so an unsigned value past its end has no honest reading here.
	if (u > (uint64_t)INT64_MAX) return clj_throw_msg("c-global*: the value does not fit a long");
	return clj_long_new((int64_t)u);
}

static clj_value b_c_global_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("c-global* expects a symbol name string, got: %s", clj_type_name(args[0]));
	if (!clj_is_keyword(args[1])) return clj_throw_msg("c-global* expects a type keyword, got: %s", clj_type_name(args[1]));
	const char       *want = clj_string_bytes(clj_keyword_name(args[1]));
	const global_kind *kind = NULL;
	for (size_t i = 0; i < sizeof kinds / sizeof *kinds && !kind; i++) {
		if (strcmp(kinds[i].kind, want) == 0) kind = &kinds[i];
	}
	if (!kind) return clj_throw_msg("c-global* does not read a %s", want);
	const char *name = clj_string_bytes(args[0]);
	void       *addr = dlsym(RTLD_DEFAULT, name);
	if (!addr) {
		return clj_throw_msg("Unable to resolve C global: %s is declared in the header but not exported by anything "
		                     "this program links (design §5 «C — уровень 0»)",
		                     name);
	}
	return read_global(addr, kind);
}

// ---- a C function: dlsym plus the level-1 dispatcher, whose prototypes are function-pointer casts already

// @ai-generated(solo)
typedef struct {
	const void *sig; // immortal: one signature per declared function, for the process
	void       *fn;
	char       *name; // for a message, since the fn value carries no C spelling
} c_fn;

static clj_value c_fn_invoke(void *ctx, const clj_value *args, size_t n) {
	const c_fn *f = (const c_fn *)ctx;
	return clj_objc_c_call(f->sig, f->fn, f->name, args, (uint32_t)n);
}

static void c_fn_release(void *ctx) {
	c_fn *f = (c_fn *)ctx;
	free(f->name);
	free(f);
}

static clj_value b_c_fn_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("c-fn* expects a symbol name string, got: %s", clj_type_name(args[0]));
	if (!clj_is_string(args[1])) return clj_throw_msg("c-fn* expects a return encoding string, got: %s", clj_type_name(args[1]));
	if (!clj_is_vector(args[2])) return clj_throw_msg("c-fn* expects a vector of argument encodings, got: %s", clj_type_name(args[2]));
	const char *name = clj_string_bytes(args[0]);
	uint32_t    nargs = clj_vector_count(args[2]);
	const char *argv[16];
	if (nargs > sizeof argv / sizeof *argv) return clj_throw_msg("c-fn* %s: %u arguments is past any ABI the bridge knows", name, nargs);
	for (uint32_t i = 0; i < nargs; i++) {
		clj_value e = clj_vector_nth(args[2], i);
		if (!clj_is_string(e)) return clj_throw_msg("c-fn* %s: argument %u's encoding is a %s", name, i + 1, clj_type_name(e));
		argv[i] = clj_string_bytes(e);
	}
	void *addr = dlsym(RTLD_DEFAULT, name);
	if (!addr) {
		return clj_throw_msg("Unable to resolve C function: %s is declared in the header but not exported by anything "
		                     "this program links (design §5 «C — уровень 0»)",
		                     name);
	}
	const char *why = NULL;
	const void *sig = clj_objc_c_signature(clj_string_bytes(args[1]), argv, nargs, &why);
	if (!sig) return clj_throw_msg("Unable to resolve C function: %s has %s", name, why);
	c_fn *f = (c_fn *)calloc(1, sizeof *f);
	char *copy = strdup(name);
	if (!f || !copy) clj_fatal("out of memory binding a C function");
	f->sig = sig;
	f->fn = addr;
	f->name = copy;
	return clj_fn_native_ctx(clj_symbol_from_cstr(name), c_fn_invoke, f, c_fn_release, nargs, nargs);
}

void clj_cdecl_builtins_install(void) {
	clj_builtin_bind_extension("c-global*", b_c_global_star, 2, 2);
	clj_builtin_bind_extension("c-fn*", b_c_fn_star, 3, 3);
}
