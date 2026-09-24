// @ai-generated(solo)
#include "clj/objc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"

// The slice's pool (NOTES "ObjC bridge"); defined on every platform so the drain needs no #ifdef.
_Thread_local void *clj_objc_pool_token;

// UTF8String -> utf8-string, centerXAnchor -> center-x-anchor: a capital run is one word, digits join it.
size_t clj_objc_kebab(const char *selector, char *out, size_t cap) {
	size_t want = 0;
	bool   fresh = true; // at a word's start: no separator wanted
	for (size_t i = 0; selector[i]; i++) {
		char c = selector[i];
		if (c == ':') {
			fresh = true;
		} else if (c >= 'A' && c <= 'Z') {
			bool prev_upper = i > 0 && selector[i - 1] >= 'A' && selector[i - 1] <= 'Z';
			bool next_lower = selector[i + 1] >= 'a' && selector[i + 1] <= 'z';
			if (!fresh && (!prev_upper || next_lower)) {
				if (want < cap) out[want] = '-';
				want++;
			}
			c = (char)(c - 'A' + 'a');
			fresh = false;
		} else {
			fresh = false;
		}
		if (want < cap) out[want] = c;
		want++;
	}
	if (cap > 0) out[want < cap ? want : cap - 1] = '\0';
	return want;
}

#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/runtime.h>

// libobjc's ARC entry points: stable exported ABI, declared only in the non-public objc-internal.h.
extern id    objc_retain(id value);
extern void  objc_release(id value);
extern id    objc_autorelease(id value);
extern void *objc_autoreleasePoolPush(void);
extern void  objc_autoreleasePoolPop(void *pool);

// A pool cannot span a park, so it lives in the slice (NOTES "ObjC bridge").
void clj_objc_pool_drain_slow(void) {
	void *tok = clj_objc_pool_token;
	clj_objc_pool_token = NULL;
	objc_autoreleasePoolPop(tok);
}

// ---- the wrapper

static void objc_object_finalize(void *self) {
	clj_objc_object *o = self;
	if (!o->is_class) objc_release((id)o->obj);
}

static uint32_t objc_object_hash(void *self) {
	uintptr_t p = (uintptr_t)((clj_objc_object *)self)->obj;
	return (uint32_t)(p >> 4) ^ (uint32_t)(p >> 32);
}

// Identity, not -isEqual:. Equality must be total and must never run foreign code under a map's lock.
static bool objc_object_equals(void *self, clj_value other) {
	return clj_is_objc_object(other) && ((clj_objc_object *)self)->obj == clj_objc_id(other);
}

const clj_type clj_objc_object_type = {
    .h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
    .name = "objc-object",
    .finalize = objc_object_finalize,
    .hash = objc_object_hash,
    .equals = objc_object_equals,
};

static clj_value wrap(void *obj, bool is_class) {
	clj_objc_object *o = clj_alloc(&clj_objc_object_type, sizeof *o);
	o->obj = obj;
	o->is_class = is_class;
	return clj_from_ptr(o);
}

clj_value clj_objc_wrap(void *obj) {
	if (!obj) return CLJ_NIL;
	if (object_isClass((id)obj)) return wrap(obj, true);
	return wrap(objc_retain((id)obj), false);
}

clj_value clj_objc_wrap_owned(void *obj) {
	if (!obj) return CLJ_NIL;
	if (object_isClass((id)obj)) return wrap(obj, true);
	return wrap(obj, false);
}

clj_value clj_objc_class(const char *name) {
	Class c = objc_getClass(name);
	return c ? wrap((void *)c, true) : CLJ_NIL;
}

// ---- the call shapes

// The wrong objc_msgSend prototype is silent corruption; why eight suffice: NOTES "ObjC bridge".

#define CLJ_OBJC_MAX_INT_ARGS     6
#define CLJ_OBJC_MAX_FP_ARGS      8
#define CLJ_OBJC_MAX_ARGS         8
#define CLJ_OBJC_MAX_STRUCT_BYTES 128
#define CLJ_OBJC_MAX_FIELDS       16

#define INT_SLOTS   long long, long long, long long, long long, long long, long long
#define FP_SLOTS(t) t, t, t, t, t, t, t, t
#define INT_ARGS(a) a[0], a[1], a[2], a[3], a[4], a[5]
#define FP_ARGS(f)  f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]

// A struct return needs the real return type in the prototype: the ABI puts an HFA in v0-v3, a small
// aggregate in x0-x1 and a large one in the buffer the caller passes in x8, which C alone can set.
typedef struct { long long a, b; } ret_i2;
typedef struct { double a, b; } ret_d2;
typedef struct { double a, b, c; } ret_d3;
typedef struct { double a, b, c, d; } ret_d4;
typedef struct { float a, b; } ret_f2;
typedef struct { float a, b, c; } ret_f3;
typedef struct { float a, b, c, d; } ret_f4;
typedef struct { _Alignas(16) unsigned char b[CLJ_OBJC_MAX_STRUCT_BYTES]; } ret_mem;

#define SEND_TYPE(tag, R)                                              \
	typedef R(*send_##tag##_d)(id, SEL, INT_SLOTS, FP_SLOTS(double));  \
	typedef R(*send_##tag##_f)(id, SEL, INT_SLOTS, FP_SLOTS(float))

SEND_TYPE(i2, ret_i2);
SEND_TYPE(d2, ret_d2);
SEND_TYPE(d3, ret_d3);
SEND_TYPE(d4, ret_d4);
SEND_TYPE(f2, ret_f2);
SEND_TYPE(f3, ret_f3);
SEND_TYPE(f4, ret_f4);
SEND_TYPE(mem, ret_mem);

typedef long long (*send_i_d)(id, SEL, INT_SLOTS, FP_SLOTS(double));
typedef double (*send_d_d)(id, SEL, INT_SLOTS, FP_SLOTS(double));
typedef float (*send_f_d)(id, SEL, INT_SLOTS, FP_SLOTS(double));
typedef void (*send_v_d)(id, SEL, INT_SLOTS, FP_SLOTS(double));
typedef long long (*send_i_f)(id, SEL, INT_SLOTS, FP_SLOTS(float));
typedef double (*send_d_f)(id, SEL, INT_SLOTS, FP_SLOTS(float));
typedef float (*send_f_f)(id, SEL, INT_SLOTS, FP_SLOTS(float));
typedef void (*send_v_f)(id, SEL, INT_SLOTS, FP_SLOTS(float));

// ---- signatures

enum { K_INT, K_FLOAT, K_DOUBLE };

// AAPCS64 §5.4: an aggregate of at most four members of one floating type travels in v0-v3, so a 32-byte
// CGRect is registers and not memory; any other aggregate of at most 16 bytes goes in integer registers, a
// larger one by a pointer, and its return through x8.
enum { SC_INT, SC_HFA_D, SC_HFA_F, SC_MEM };

typedef struct {
	uint8_t  cls;
	uint8_t  slots; // integer words, HFA members, or the one pointer of SC_MEM
	uint16_t size;
} struct_abi;

// Why a selector cannot be called, for the message the caller gets.
enum { REJECT_NONE, REJECT_SHAPE, REJECT_VARIADIC };

typedef struct {
	SEL         sel;
	char        ret;                    // the return's encoding char, '{' for a struct
	char        arg[CLJ_OBJC_MAX_ARGS]; // each argument's, self and _cmd dropped
	const char *retenc;                 // the whole encoding of a '{' return, owned by the cache entry
	const char *argenc[CLJ_OBJC_MAX_ARGS];
	struct_abi  ret_abi;
	struct_abi  arg_abi[CLJ_OBJC_MAX_ARGS];
	uint8_t     nargs;
	uint8_t     reject;
	bool        owned;         // the alloc/new/copy/mutableCopy/init families return +1
	bool        consumes_self; // the init family takes the reference its receiver was created with
	bool        fp_is_float; // every floating-point argument is a float rather than a double
} objc_sig;

static int kind_of(char enc) {
	switch (enc) {
	case 'f': return K_FLOAT;
	case 'd': return K_DOUBLE;
	default: return K_INT;
	}
}

// An encoding a slot can carry at all. Qualifiers are stripped before this.
static bool encoding_supported(char enc, bool is_return) {
	switch (enc) {
	case 'c': case 'C': case 's': case 'S': case 'i': case 'I':
	case 'l': case 'L': case 'q': case 'Q': case 'B':
	case '@': case '#': case ':': case '*': case '^':
	case 'f': case 'd':
		return true;
	case 'v':
		return is_return;
	default:
		return false; // { struct, ( union, [ array, D long double, ? unknown
	}
}

static const char *skip_qualifiers(const char *enc) {
	while (*enc && strchr("rnNoORV", *enc)) enc++;
	return enc;
}

// ---- struct layout and its ABI class

typedef struct {
	const char *body; // the first member's encoding
	const char *name; // "?" when the struct is anonymous
	const char *end;  // past the closing brace
	size_t      namelen;
	uint32_t    size, align;
	uint8_t     nfields;
} sinfo;

static bool struct_info(const char *enc, sinfo *si);

// Every Apple ABI agrees on these; 0 is a member we cannot lay out.
static uint32_t prim_size(char enc) {
	switch (enc) {
	case 'c': case 'C': case 'B': return 1;
	case 's': case 'S': return 2;
	// The runtime documents 'l'/'L' as 32-bit even on LP64, where clang emits 'q'/'Q' for a real long.
	case 'i': case 'I': case 'l': case 'L': case 'f': return 4;
	case 'q': case 'Q': case 'd': case '@': case '#': case ':': case '*': case '^': return 8;
	default: return 0;
	}
}

// Past one encoding of any shape, laid out or not.
static bool skip_encoding(const char **p) {
	const char *e = skip_qualifiers(*p);
	switch (*e) {
	case '\0': return false;
	case '^': return *p = e + 1, skip_encoding(p);
	case '@':
		e++;
		if (*e == '?') return *p = e + 1, true; // @? is one encoding, a block pointer
		if (*e == '"') {
			while (*e && *++e != '"') {}
			if (*e != '"') return false;
			e++;
		}
		return *p = e, true;
	case 'b':
		for (e++; *e >= '0' && *e <= '9'; e++) {}
		return *p = e, true;
	case '{': case '(': case '[': {
		for (int depth = 0; *e; e++) {
			if (*e == '{' || *e == '(' || *e == '[') depth++;
			else if ((*e == '}' || *e == ')' || *e == ']') && --depth == 0) return *p = e + 1, true;
		}
		return false;
	}
	default: return *p = e + 1, true;
	}
}

static bool member_layout(const char **p, uint32_t *size, uint32_t *align) {
	const char *e = skip_qualifiers(*p);
	if (*e == '{') {
		sinfo si;
		if (!struct_info(e, &si)) return false;
		*size = si.size;
		*align = si.align;
		*p = si.end;
		return true;
	}
	if (*e == '^' || *e == '@') {
		*size = *align = 8;
		*p = e;
		return skip_encoding(p);
	}
	uint32_t n = prim_size(*e);
	if (n == 0) return false;
	*size = *align = n;
	*p = e + 1;
	return true;
}

// A union is refused here: its members overlap, so no Clojure value is faithful to one.
static bool struct_info(const char *enc, sinfo *si) {
	if (*enc != '{') return false;
	const char *eq = enc + 1;
	while (*eq && *eq != '=' && *eq != '}') eq++;
	if (*eq != '=') return false; // {Name} alone is an opaque forward declaration
	si->name = enc + 1;
	si->namelen = (size_t)(eq - si->name);
	si->body = eq + 1;
	uint32_t    off = 0, align = 1;
	uint8_t     n = 0;
	const char *p = si->body;
	while (*p && *p != '}') {
		uint32_t sz, al;
		if (n == CLJ_OBJC_MAX_FIELDS || !member_layout(&p, &sz, &al)) return false;
		off = (off + al - 1) & ~(al - 1);
		off += sz;
		if (al > align) align = al;
		n++;
	}
	if (*p != '}' || n == 0) return false;
	si->end = p + 1;
	si->nfields = n;
	si->align = align;
	si->size = (off + align - 1) & ~(align - 1);
	return true;
}

typedef struct {
	const char *p;
	uint32_t    off;
} fcursor;

static void fields_begin(const sinfo *si, fcursor *c) {
	c->p = si->body;
	c->off = 0;
}

static bool field_next(fcursor *c, const char **enc, uint32_t *off) {
	if (!*c->p || *c->p == '}') return false;
	const char *at = skip_qualifiers(c->p);
	uint32_t    sz, al;
	if (!member_layout(&c->p, &sz, &al)) return false;
	*off = (c->off + al - 1) & ~(al - 1);
	c->off = *off + sz;
	*enc = at;
	return true;
}

// The scalar members, nesting flattened out: the ABI class reads only these.
static bool leaves_of(const char *enc, char *out, unsigned cap, unsigned *n) {
	sinfo si;
	if (!struct_info(enc, &si)) return false;
	fcursor     c;
	const char *f;
	uint32_t    off;
	for (fields_begin(&si, &c); field_next(&c, &f, &off);) {
		if (*f == '{') {
			if (!leaves_of(f, out, cap, n)) return false;
		} else {
			if (*n == cap) return false;
			out[(*n)++] = *f;
		}
	}
	return true;
}

static bool classify_struct(const char *enc, struct_abi *abi) {
	sinfo si;
	if (!struct_info(enc, &si)) return false;
	if (si.size == 0 || si.size > CLJ_OBJC_MAX_STRUCT_BYTES) return false;
	char     lv[CLJ_OBJC_MAX_FIELDS * 4];
	unsigned n = 0;
	if (!leaves_of(enc, lv, sizeof lv, &n) || n == 0) return false;
	bool all_d = true, all_f = true, all_int = true;
	for (unsigned i = 0; i < n; i++) {
		if (!encoding_supported(lv[i], false)) return false;
		all_d &= lv[i] == 'd';
		all_f &= lv[i] == 'f';
		all_int &= kind_of(lv[i]) == K_INT;
	}
	abi->size = (uint16_t)si.size;
	if (all_d && n <= 4 && si.size == 8 * n) abi->cls = SC_HFA_D, abi->slots = (uint8_t)n;
	else if (all_f && n <= 4 && si.size == 4 * n) abi->cls = SC_HFA_F, abi->slots = (uint8_t)n;
	else if (si.size <= 16) abi->cls = SC_INT, abi->slots = (uint8_t)((si.size + 7) / 8);
	else abi->cls = SC_MEM, abi->slots = 1;
	(void)all_int;
#if defined(__x86_64__)
	// SysV classifies by eightbyte, not by member: two floats share one xmm, and an aggregate over 16 bytes
	// rides the stack, with objc_msgSend_stret for a return. Only the shapes that agree with arm64 pass.
	if (!(abi->cls == SC_INT && all_int) && !(abi->cls == SC_HFA_D && n <= 2)) return false;
#endif
	return true;
}

// A selector's first word after any leading underscores, as ARC reads a method family.
static bool family_is(const char *sel, const char *word) {
	while (*sel == '_') sel++;
	size_t n = strlen(word);
	if (strncmp(sel, word, n) != 0) return false;
	char after = sel[n];
	return after == '\0' || after == ':' || (after >= 'A' && after <= 'Z');
}

static bool family_is_owned(const char *sel) {
	static const char *const families[] = {"alloc", "new", "copy", "mutableCopy", "init"};
	for (size_t i = 0; i < sizeof families / sizeof *families; i++) {
		if (family_is(sel, families[i])) return true;
	}
	return false;
}

// No encoding shows variadicity, so Cocoa's known variadic selectors are refused by name, not called wrong.
static bool selector_is_variadic(const char *sel) {
	static const char *const variadic[] = {
	    "appendFormat:",
	    "arrayWithObjects:",
	    "dictionaryWithObjectsAndKeys:",
	    "initWithFormat:",
	    "initWithObjects:",
	    "initWithObjectsAndKeys:",
	    "initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:",
	    "localizedStringWithFormat:",
	    "orderedSetWithObjects:",
	    "predicateWithFormat:",
	    "raise:format:",
	    "setWithObjects:",
	    "stringByAppendingFormat:",
	    "stringWithFormat:",
	};
	for (size_t i = 0; i < sizeof variadic / sizeof *variadic; i++) {
		if (strcmp(sel, variadic[i]) == 0) return true;
	}
	return false;
}

static char *own_encoding(const char *enc) {
	char *copy = strdup(enc);
	if (!copy) clj_fatal("out of memory copying an objc type encoding");
	return copy;
}

// Everything the ABI reads, shared by a method we call and one we install; the selector's own facts
// (family, variadicity) belong to the caller.
static bool signature_shape(objc_sig *sig, const char *ret, const char *const *argv) {
	const char *r = skip_qualifiers(ret);
	bool        ok = *r == '{' ? classify_struct(r, &sig->ret_abi) : encoding_supported(*r, true);
	sig->ret = *r;
	if (ok && *r == '{') sig->retenc = own_encoding(r);
	if (!ok) return false;

	unsigned ints = 0, fps = 0, floats = 0;
	for (unsigned i = 0; i < sig->nargs; i++) {
		const char *e = skip_qualifiers(argv[i]);
		sig->arg[i] = *e;
		ok = *e == '{' ? classify_struct(e, &sig->arg_abi[i]) : encoding_supported(*e, false);
		if (ok && *e == '{') sig->argenc[i] = own_encoding(e);
		if (!ok) return false;
		if (sig->arg[i] == '{') {
			switch (sig->arg_abi[i].cls) {
			case SC_HFA_F: floats += sig->arg_abi[i].slots; // fallthrough
			case SC_HFA_D: fps += sig->arg_abi[i].slots; break;
			default: ints += sig->arg_abi[i].slots; break; // SC_INT's words, SC_MEM's pointer
			}
			continue;
		}
		switch (kind_of(sig->arg[i])) {
		case K_INT: ints++; break;
		case K_FLOAT: fps++, floats++; break;
		default: fps++; break;
		}
	}
	if (ints > CLJ_OBJC_MAX_INT_ARGS || fps > CLJ_OBJC_MAX_FP_ARGS) return false;
	if (floats != 0 && floats != fps) return false;
	sig->fp_is_float = floats != 0;
	return true;
}

// sel is set even on a refusal, so a caller can tell "no such selector" from "one we cannot call".
static bool signature_fill(Method m, objc_sig *sig) {
	sig->sel = method_getName(m);
	sig->reject = REJECT_SHAPE;
	unsigned n = method_getNumberOfArguments(m);
	if (n < 2 || n - 2 > CLJ_OBJC_MAX_ARGS) return false;
	sig->nargs = (uint8_t)(n - 2);
	sig->owned = family_is_owned(sel_getName(sig->sel));
	sig->consumes_self = family_is(sel_getName(sig->sel), "init");
	if (selector_is_variadic(sel_getName(sig->sel))) return sig->reject = REJECT_VARIADIC, false;

	char       *ret = method_copyReturnType(m);
	char       *arg[CLJ_OBJC_MAX_ARGS] = {0};
	const char *argv[CLJ_OBJC_MAX_ARGS];
	for (unsigned i = 0; i < sig->nargs; i++) argv[i] = arg[i] = method_copyArgumentType(m, i + 2);
	bool ok = signature_shape(sig, ret, argv);
	free(ret);
	for (unsigned i = 0; i < sig->nargs; i++) free(arg[i]);
	return ok;
}

static bool signature_of(Method m, objc_sig *sig) {
	if (signature_fill(m, sig)) return true;
	free((char *)sig->retenc);
	sig->retenc = NULL;
	for (unsigned i = 0; i < CLJ_OBJC_MAX_ARGS; i++) {
		free((char *)sig->argenc[i]);
		sig->argenc[i] = NULL;
	}
	return false;
}

// ---- the (class, spelling) selector cache

typedef struct {
	Class    cls;
	char    *spelling; // owned; NULL marks a free slot
	size_t   len;
	uint32_t hash;
	bool     raw; // the Objective-C text rather than the kebab spelling: the same string can mean either
	bool     found;
	objc_sig sig;
} cache_entry;

static clj_lock     cache_lock = CLJ_LOCK_INIT;
static cache_entry *cache;
static size_t       cache_cap, cache_len;

static uint32_t spelling_hash(Class cls, const char *s, size_t len, bool raw) {
	uint32_t h = (uint32_t)((uintptr_t)cls >> 4) * 2654435761u + raw;
	for (size_t i = 0; i < len; i++) h = clj_hash_combine(h, (uint32_t)(unsigned char)s[i]);
	return h;
}

static cache_entry *cache_slot(Class cls, const char *s, size_t len, bool raw, uint32_t h) {
	size_t mask = cache_cap - 1;
	for (size_t i = h & mask;; i = (i + 1) & mask) {
		cache_entry *e = &cache[i];
		if (!e->spelling) return e;
		if (e->hash == h && e->cls == cls && e->len == len && e->raw == raw && memcmp(e->spelling, s, len) == 0) return e;
	}
}

static void cache_grow(void) {
	size_t       cap = cache_cap ? cache_cap * 2 : 64;
	cache_entry *old = cache, *fresh = calloc(cap, sizeof *fresh);
	if (!fresh) clj_fatal("out of memory growing the objc selector cache");
	size_t old_cap = cache_cap;
	cache = fresh;
	cache_cap = cap;
	for (size_t i = 0; i < old_cap; i++) {
		if (old[i].spelling) *cache_slot(old[i].cls, old[i].spelling, old[i].len, old[i].raw, old[i].hash) = old[i];
	}
	free(old);
}

// Walks cls and its superclasses kebabing every selector; the first match wins, as dispatch would.
static bool resolve(Class cls, const char *spelling, size_t len, bool raw, objc_sig *out) {
	char buf[512];
	if (raw) {
		Method m = class_getInstanceMethod(cls, sel_registerName(spelling));
		return m && signature_of(m, out);
	}
	for (Class k = cls; k; k = class_getSuperclass(k)) {
		unsigned n = 0;
		Method  *ms = class_copyMethodList(k, &n);
		for (unsigned i = 0; i < n; i++) {
			const char *name = sel_getName(method_getName(ms[i]));
			size_t      want = clj_objc_kebab(name, buf, sizeof buf);
			if (want != len || memcmp(buf, spelling, len) != 0) continue;
			bool ok = signature_of(ms[i], out);
			free(ms);
			return ok;
		}
		free(ms);
	}
	return false;
}

static bool lookup(Class cls, const char *spelling, size_t len, bool raw, objc_sig *out) {
	clj_lock_lock(&cache_lock);
	if (cache_len * 2 >= cache_cap) cache_grow();
	uint32_t     h = spelling_hash(cls, spelling, len, raw);
	cache_entry *e = cache_slot(cls, spelling, len, raw, h);
	if (!e->spelling) {
		char *copy = malloc(len + 1);
		if (!copy) clj_fatal("out of memory filling the objc selector cache");
		memcpy(copy, spelling, len);
		copy[len] = '\0';
		e->cls = cls;
		e->spelling = copy;
		e->len = len;
		e->hash = h;
		e->raw = raw;
		memset(&e->sig, 0, sizeof e->sig);
		e->found = resolve(cls, copy, len, raw, &e->sig);
		cache_len++;
	}
	*out = e->sig;
	bool found = e->found;
	clj_lock_unlock(&cache_lock);
	return found;
}

// ---- Clojure <-> Objective-C values

static Class class_named(const char *name, Class *slot) {
	if (!*slot) *slot = objc_getClass(name);
	return *slot;
}

static bool is_kind_of(id obj, Class k) {
	return k && ((bool (*)(id, SEL, Class))objc_msgSend)(obj, sel_registerName("isKindOfClass:"), k);
}

// Autoreleased: it lives until the slice's pool drains, which outlasts the call it is an argument to.
static id to_ns_string(clj_value s) {
	static Class ns_string;
	Class        c = class_named("NSString", &ns_string);
	if (!c) return NULL;
	id raw = ((id (*)(id, SEL))objc_msgSend)((id)c, sel_registerName("alloc"));
	id str = ((id (*)(id, SEL, const void *, unsigned long, unsigned long))objc_msgSend)(
	    raw, sel_registerName("initWithBytes:length:encoding:"), clj_string_bytes(s), (unsigned long)clj_string_len(s), 4 /* NSUTF8StringEncoding */);
	return objc_autorelease(str);
}

// The convenience constructors return +0 already.
static id to_ns_number(clj_value v) {
	static Class ns_number;
	Class        c = class_named("NSNumber", &ns_number);
	if (!c) return NULL;
	int64_t i;
	if (clj_is_bool(v)) return ((id (*)(id, SEL, bool))objc_msgSend)((id)c, sel_registerName("numberWithBool:"), v == CLJ_TRUE);
	if (clj_int64_of(v, &i)) return ((id (*)(id, SEL, long long))objc_msgSend)((id)c, sel_registerName("numberWithLongLong:"), (long long)i);
	return ((id (*)(id, SEL, double))objc_msgSend)((id)c, sel_registerName("numberWithDouble:"), clj_num_to_double(v));
}

// A returned NSString or NSNumber crosses as a value; every other object stays an opaque wrapper. An
// NSMutableString does not convert: a snapshot would silently drop the mutation the caller went on to make.
// @YES and @1 answer the same -objCType, but the boolean ones are the CFBoolean singletons, so identity
// tells them apart where the encoding cannot.
static clj_value from_object(id obj, bool owned) {
	static Class ns_string, ns_mutable_string, ns_number;
	if (!obj) return CLJ_NIL;
	if (obj == (id)kCFBooleanTrue || obj == (id)kCFBooleanFalse) {
		if (owned) objc_release(obj);
		return obj == (id)kCFBooleanTrue ? CLJ_TRUE : CLJ_FALSE;
	}
	if (is_kind_of(obj, class_named("NSString", &ns_string)) && !is_kind_of(obj, class_named("NSMutableString", &ns_mutable_string))) {
		const char *utf8 = ((const char *(*)(id, SEL))objc_msgSend)(obj, sel_registerName("UTF8String"));
		clj_value   s = utf8 ? clj_string_from_cstr(utf8) : CLJ_NIL;
		if (owned) objc_release(obj);
		return s;
	}
	if (is_kind_of(obj, class_named("NSNumber", &ns_number))) {
		const char *t = ((const char *(*)(id, SEL))objc_msgSend)(obj, sel_registerName("objCType"));
		clj_value   n = t && (t[0] == 'f' || t[0] == 'd')
		                    ? clj_double_new(((double (*)(id, SEL))objc_msgSend)(obj, sel_registerName("doubleValue")))
		                    : clj_long_new(((long long (*)(id, SEL))objc_msgSend)(obj, sel_registerName("longLongValue")));
		if (owned) objc_release(obj);
		return n;
	}
	return owned ? clj_objc_wrap_owned(obj) : clj_objc_wrap(obj);
}

static bool to_int_slot(clj_value v, char enc, long long *out) {
	switch (enc) {
	case '@':
		if (clj_is_nil(v)) return *out = 0, true;
		if (clj_is_objc_object(v)) return *out = (long long)(intptr_t)clj_objc_id(v), true;
		if (clj_is_string(v)) return *out = (long long)(intptr_t)to_ns_string(v), true;
		if (clj_is_number(v) || clj_is_bool(v)) return *out = (long long)(intptr_t)to_ns_number(v), true;
		return false;
	case '#':
	case '^':
		if (clj_is_nil(v)) return *out = 0, true;
		return clj_is_objc_object(v) ? (*out = (long long)(intptr_t)clj_objc_id(v), true) : false;
	case ':':
		if (clj_is_nil(v)) return *out = 0, true;
		return clj_is_string(v) ? (*out = (long long)(intptr_t)sel_registerName(clj_string_bytes(v)), true) : false;
	case '*':
		if (clj_is_nil(v)) return *out = 0, true;
		return clj_is_string(v) ? (*out = (long long)(intptr_t)clj_string_bytes(v), true) : false;
	default:
		break;
	}
	int64_t i;
	if (clj_is_bool(v)) return *out = v == CLJ_TRUE, true;
	if (!clj_int64_of(v, &i)) return false;
	*out = (long long)i;
	return true;
}

// Darwin's arm64 callee leaves the top half of x0 undefined for a return narrower than 64 bits, so the
// encoding, not the register, says how wide the value is and whether it is signed.
static clj_value from_int_return(long long raw, char enc, bool owned) {
	switch (enc) {
	case 'v': return CLJ_NIL;
	case '@': return from_object((id)(intptr_t)raw, owned);
	case '#': return raw ? clj_objc_wrap((void *)(intptr_t)raw) : CLJ_NIL;
	case 'B': return raw & 1 ? CLJ_TRUE : CLJ_FALSE;
#if defined(__x86_64__)
	// BOOL is a signed char here and 'B' never appears, so a genuine char return reads as a boolean too.
	case 'c':
	case 'C': return (raw & 0xff) != 0 ? CLJ_TRUE : CLJ_FALSE;
#else
	case 'c': return clj_long_new((int8_t)raw);
	case 'C': return clj_long_new((uint8_t)raw);
#endif
	case 's': return clj_long_new((int16_t)raw);
	case 'S': return clj_long_new((uint16_t)raw);
	case 'i': return clj_long_new((int32_t)raw);
	case 'I': return clj_long_new((uint32_t)raw);
	case ':': return raw ? clj_string_from_cstr(sel_getName((SEL)(intptr_t)raw)) : CLJ_NIL;
	case '*': return raw ? clj_string_from_cstr((const char *)(intptr_t)raw) : CLJ_NIL;
	case '^': return raw ? clj_objc_wrap_owned((void *)(intptr_t)raw) : CLJ_NIL;
	// Including 'Q' and 'L': an unsigned 64-bit result wraps into a long, as it does on the JVM.
	default: return clj_long_new((int64_t)raw);
	}
}

// ---- structs as Clojure values

// An encoding names the struct and its member types but never the members, so the keys come from here; an
// unlisted struct crosses as a vector, in member order.
static const struct {
	const char *name;
	uint8_t     n;
	const char *field[6];
} known_structs[] = {
    {"CGPoint", 2, {"x", "y"}},
    {"CGSize", 2, {"width", "height"}},
    {"CGVector", 2, {"dx", "dy"}},
    {"CGRect", 2, {"origin", "size"}},
    {"_NSRange", 2, {"location", "length"}},
    {"CGAffineTransform", 6, {"a", "b", "c", "d", "tx", "ty"}},
    {"UIEdgeInsets", 4, {"top", "left", "bottom", "right"}},
    {"NSEdgeInsets", 4, {"top", "left", "bottom", "right"}},
    {"NSDirectionalEdgeInsets", 4, {"top", "leading", "bottom", "trailing"}},
    {"UIOffset", 2, {"horizontal", "vertical"}},
};

#define KNOWN_STRUCTS (sizeof known_structs / sizeof *known_structs)

// A keyword is immortal and interning is idempotent, so a race here stores the same value twice.
static clj_value known_keys[KNOWN_STRUCTS][6];

static clj_value field_key(int s, unsigned i) {
	if (!known_keys[s][i]) known_keys[s][i] = clj_keyword_from_cstr(known_structs[s].field[i]);
	return known_keys[s][i];
}

// The table's entry for this struct, or -1 when it has none or has gone stale against the real encoding.
static int known_index(const sinfo *si) {
	for (size_t i = 0; i < KNOWN_STRUCTS; i++) {
		if (strncmp(known_structs[i].name, si->name, si->namelen) != 0 || known_structs[i].name[si->namelen]) continue;
		return known_structs[i].n == si->nfields ? (int)i : -1;
	}
	return -1;
}

// Little-endian: a member's low bytes are its first ones, which every Apple target agrees on.
static bool write_scalar(char enc, clj_value v, unsigned char *at) {
	if (enc == 'd' || enc == 'f') {
		if (!clj_is_number(v)) return false;
		double d = clj_num_to_double(v);
		float  f = (float)d;
		memcpy(at, enc == 'd' ? (void *)&d : (void *)&f, prim_size(enc));
		return true;
	}
	long long raw;
	if (!to_int_slot(v, enc, &raw)) return false;
	memcpy(at, &raw, prim_size(enc));
	return true;
}

static clj_value read_scalar(char enc, const unsigned char *at) {
	if (enc == 'd') {
		double d;
		memcpy(&d, at, sizeof d);
		return clj_double_new(d);
	}
	if (enc == 'f') {
		float f;
		memcpy(&f, at, sizeof f);
		return clj_double_new(f);
	}
	long long raw = 0;
	memcpy(&raw, at, prim_size(enc));
	return from_int_return(raw, enc, false);
}

// A listed struct takes a map of its field keywords, any struct a vector; buf is zeroed and big enough.
static bool encode_struct(const char *enc, clj_value v, unsigned char *buf) {
	sinfo si;
	if (!struct_info(enc, &si)) return false;
	int  ks = known_index(&si);
	bool from_map = clj_is_map(v);
	if (from_map ? ks < 0 || clj_map_count(v) != si.nfields : !clj_is_vector(v) || clj_vector_count(v) != si.nfields) return false;
	fcursor     c;
	const char *f;
	uint32_t    off;
	fields_begin(&si, &c);
	for (unsigned i = 0; field_next(&c, &f, &off); i++) {
		clj_value fv = from_map ? clj_map_get(v, field_key(ks, i), CLJ_UNBOUND) : clj_vector_nth(v, i);
		if (fv == CLJ_UNBOUND) return false;
		if (*f == '{' ? !encode_struct(f, fv, buf + off) : !write_scalar(*f, fv, buf + off)) return false;
	}
	return true;
}

static clj_value decode_struct(const char *enc, const unsigned char *buf) {
	sinfo si;
	if (!struct_info(enc, &si)) return CLJ_NIL;
	int         ks = known_index(&si);
	clj_value   items[CLJ_OBJC_MAX_FIELDS * 2];
	unsigned    n = 0;
	fcursor     c;
	const char *f;
	uint32_t    off;
	fields_begin(&si, &c);
	for (unsigned i = 0; field_next(&c, &f, &off); i++) {
		if (ks >= 0) items[n++] = field_key(ks, i);
		items[n++] = *f == '{' ? decode_struct(f, buf + off) : read_scalar(*f, buf + off);
	}
	clj_value out = ks >= 0 ? clj_map_from_items(items, n, NULL) : clj_vector_from_array(items, n);
	for (unsigned i = ks >= 0 ? 1 : 0; i < n; i += ks >= 0 ? 2 : 1) clj_release(items[i]);
	return out;
}

// What an argument of this struct may be written as, for the error when it is written as something else.
static void describe_struct(const char *enc, char *out, size_t cap) {
	sinfo si;
	if (!struct_info(enc, &si)) {
		snprintf(out, cap, "%s", enc);
		return;
	}
	int ks = known_index(&si);
	if (ks < 0) {
		snprintf(out, cap, "%.*s, a vector of %u", (int)si.namelen, si.name, si.nfields);
		return;
	}
	size_t used = (size_t)snprintf(out, cap, "%.*s, a map of", (int)si.namelen, si.name);
	for (unsigned i = 0; i < si.nfields && used < cap; i++) used += (size_t)snprintf(out + used, cap - used, " :%s", known_structs[ks].field[i]);
	if (used < cap) snprintf(out + used, cap - used, " or a vector of %u", si.nfields);
}

// ---- collections, by hand only (design §5: a scalar bridges itself, a collection does not)

#define CLJ_OBJC_MAX_COLL_DEPTH 32

static id to_ns_object(clj_value v, unsigned depth, const char **why);

// The elements are autoreleased into the slice's pool, so the array holds the only strong reference the
// caller needs; a NULL element would end the C array early, so nil travels as NSNull -- the conversion is
// lossy by design (§5) and this is one of the places it shows.
static id to_ns_array(clj_value v, unsigned depth, const char **why) {
	static Class ns_array, ns_null;
	Class        arr = class_named("NSArray", &ns_array), nul = class_named("NSNull", &ns_null);
	if (!arr || !nul) return *why = "no NSArray class", (id)NULL;
	id null_obj = ((id (*)(id, SEL))objc_msgSend)((id)nul, sel_registerName("null"));

	clj_value count = clj_count(v);
	int64_t   i64;
	if (count == CLJ_THROWN || !clj_int64_of(count, &i64) || i64 < 0 || i64 > INT32_MAX) {
		clj_take_pending();
		return *why = "not a collection of a countable length", (id)NULL;
	}
	uint32_t n = (uint32_t)i64;
	id      *buf = n ? malloc(n * sizeof *buf) : NULL;
	if (n && !buf) clj_fatal("out of memory building an NSArray");

	uint32_t  filled = 0;
	clj_value seq = clj_seq(v);
	while (seq != CLJ_THROWN && !clj_is_nil(seq) && filled < n) {
		clj_value e = clj_first(seq);
		id        o = clj_is_nil(e) ? null_obj : to_ns_object(e, depth + 1, why);
		if (!o) {
			clj_release(seq);
			free(buf);
			return NULL;
		}
		buf[filled++] = o;
		clj_value nx = clj_next(seq);
		clj_release(seq);
		seq = nx;
	}
	if (seq == CLJ_THROWN) {
		clj_take_pending();
		free(buf);
		return *why = "the sequence threw while converting", (id)NULL;
	}
	clj_release(seq);
	if (filled != n) {
		free(buf);
		return *why = "the collection changed length while converting", (id)NULL;
	}
	id out = ((id (*)(id, SEL, const id *, unsigned long))objc_msgSend)((id)arr, sel_registerName("arrayWithObjects:count:"), buf, (unsigned long)n);
	free(buf);
	return out;
}

typedef struct {
	id         *keys, *objs;
	uint32_t    n, cap;
	unsigned    depth;
	const char *why;
	id          null_obj;
} dict_build;

static bool dict_entry(clj_value k, clj_value val, void *ctx) {
	dict_build *b = ctx;
	if (b->n == b->cap) return b->why = "the map changed size while converting", false;
	id ko = to_ns_object(k, b->depth + 1, &b->why);
	if (!ko) return false;
	id vo = clj_is_nil(val) ? b->null_obj : to_ns_object(val, b->depth + 1, &b->why);
	if (!vo) return false;
	b->keys[b->n] = ko;
	b->objs[b->n] = vo;
	b->n++;
	return true;
}

static id to_ns_dictionary(clj_value v, unsigned depth, const char **why) {
	static Class ns_dict, ns_null;
	Class        dict = class_named("NSDictionary", &ns_dict), nul = class_named("NSNull", &ns_null);
	if (!dict || !nul) return *why = "no NSDictionary class", (id)NULL;
	uint32_t   n = clj_map_count(v);
	dict_build b = {0};
	b.cap = n;
	b.depth = depth;
	b.null_obj = ((id (*)(id, SEL))objc_msgSend)((id)nul, sel_registerName("null"));
	b.keys = n ? malloc(2 * n * sizeof *b.keys) : NULL;
	if (n && !b.keys) clj_fatal("out of memory building an NSDictionary");
	b.objs = b.keys ? b.keys + n : NULL;
	clj_map_each(v, dict_entry, &b);
	if (b.why || b.n != n) {
		free(b.keys);
		*why = b.why ? b.why : "the map changed size while converting";
		return NULL;
	}
	id out = ((id (*)(id, SEL, const id *, const id *, unsigned long))objc_msgSend)(
	    (id)dict, sel_registerName("dictionaryWithObjects:forKeys:count:"), b.objs, b.keys, (unsigned long)n);
	free(b.keys);
	return out;
}

// A scalar converts itself here as it does in an argument slot; a nested collection converts too, because a
// one-level conversion would put wrappers Cocoa cannot read inside the array it was handed.
static id to_ns_object(clj_value v, unsigned depth, const char **why) {
	if (depth > CLJ_OBJC_MAX_COLL_DEPTH) return *why = "nested deeper than the bridge converts", (id)NULL;
	if (clj_is_objc_object(v)) return (id)clj_objc_id(v);
	if (clj_is_string(v)) return to_ns_string(v);
	// A keyword travels as its name, which is why the inverse hands back strings and not keywords.
	if (clj_is_keyword(v)) return to_ns_string(clj_keyword_name(v));
	if (clj_is_number(v) || clj_is_bool(v)) return to_ns_number(v);
	if (clj_is_map(v)) return to_ns_dictionary(v, depth, why);
	if (clj_is_seqable(v)) return to_ns_array(v, depth, why);
	*why = "not a string, number, boolean, keyword, Objective-C object, map or sequence";
	return NULL;
}

static clj_value from_ns_collection(id obj, unsigned depth);

static clj_value from_ns_element(id obj, unsigned depth) {
	static Class ns_null, ns_array, ns_dict;
	if (!obj) return CLJ_NIL;
	if (is_kind_of(obj, class_named("NSNull", &ns_null))) return CLJ_NIL;
	if (is_kind_of(obj, class_named("NSArray", &ns_array)) || is_kind_of(obj, class_named("NSDictionary", &ns_dict)))
		return from_ns_collection(obj, depth + 1);
	return from_object(obj, false);
}

static clj_value from_ns_collection(id obj, unsigned depth) {
	static Class ns_array;
	if (depth > CLJ_OBJC_MAX_COLL_DEPTH) return clj_throw_msg("Nested deeper than the bridge converts");
	bool is_array = is_kind_of(obj, class_named("NSArray", &ns_array));

	unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(obj, sel_registerName("count"));
	if (n > (unsigned long)INT32_MAX) return clj_throw_msg("Collection too large to convert: %lu", n);
	clj_value *items = n ? malloc((is_array ? n : 2 * n) * sizeof *items) : NULL;
	if (n && !items) clj_fatal("out of memory converting a Cocoa collection");

	uint32_t  m = 0;
	clj_value out;
	if (is_array) {
		for (unsigned long i = 0; i < n; i++)
			items[m++] = from_ns_element(((id (*)(id, SEL, unsigned long))objc_msgSend)(obj, sel_registerName("objectAtIndex:"), i), depth);
		out = clj_vector_from_array(items, m);
	} else {
		id keys = ((id (*)(id, SEL))objc_msgSend)(obj, sel_registerName("allKeys"));
		for (unsigned long i = 0; i < n; i++) {
			id k = ((id (*)(id, SEL, unsigned long))objc_msgSend)(keys, sel_registerName("objectAtIndex:"), i);
			items[m++] = from_ns_element(k, depth);
			items[m++] = from_ns_element(((id (*)(id, SEL, id))objc_msgSend)(obj, sel_registerName("objectForKey:"), k), depth);
		}
		out = clj_map_from_items(items, m, NULL);
	}
	for (uint32_t i = 0; i < m; i++) clj_release(items[i]);
	free(items);
	return out;
}

// A pool for a conversion that no send opened; the same rule as clj_objc_send's (NOTES "ObjC bridge").
typedef struct {
	bool  own;
	void *tok;
} pool_scope;

static pool_scope pool_enter(void) {
	pool_scope p = {!clj_coro_in_coroutine(), NULL};
	if (p.own) p.tok = objc_autoreleasePoolPush();
	else if (!clj_objc_pool_token) clj_objc_pool_token = objc_autoreleasePoolPush();
	return p;
}

static void pool_leave(pool_scope p) {
	if (p.own) objc_autoreleasePoolPop(p.tok);
}

clj_value clj_objc_to_collection(clj_value v, bool as_map) {
	const char *name = as_map ? "ns-dictionary" : "ns-array";
	if (as_map ? !clj_is_map(v) : (clj_is_map(v) || !clj_is_seqable(v)))
		return clj_throw_msg("%s expects a %s, got: %s", name, as_map ? "map" : "sequence", clj_type_name(v));
	pool_scope  p = pool_enter();
	const char *why = NULL;
	id          obj = as_map ? to_ns_dictionary(v, 0, &why) : to_ns_array(v, 0, &why);
	clj_value   out = obj ? clj_objc_wrap(obj) : clj_throw_msg("%s cannot convert this value: %s", name, why ? why : "unsupported");
	pool_leave(p);
	return out;
}

clj_value clj_objc_from_collection(clj_value v, bool as_map) {
	const char *name = as_map ? "ns-dictionary->map" : "ns-array->vec";
	if (clj_is_nil(v)) return CLJ_NIL;
	if (!clj_is_objc_object(v)) return clj_throw_msg("%s expects an Objective-C object, got: %s", name, clj_type_name(v));
	static Class ns_array, ns_dict;
	id           obj = (id)clj_objc_id(v);
	if (!is_kind_of(obj, class_named(as_map ? "NSDictionary" : "NSArray", as_map ? &ns_dict : &ns_array)))
		return clj_throw_msg("%s expects an %s, got a %s", name, as_map ? "NSDictionary" : "NSArray", class_getName(object_getClass(obj)));
	pool_scope p = pool_enter();
	clj_value  out = from_ns_collection(obj, 0);
	pool_leave(p);
	return out;
}

// ---- the call

// The hint is every selector of the receiver sharing the base name, so a wrong or reordered label reads
// against the right order instead of a bare "no such method" (design §3, diagnostics).
static clj_value no_such_selector(Class cls, const char *spelling, size_t len, bool raw) {
	char        buf[512], kebab[256];
	const char *sep = "";
	size_t      base = 0, used = 0;
	while (base < len && spelling[base] != ':') base++;
	buf[0] = '\0';
	for (Class k = cls; k && used + 96 < sizeof buf; k = class_getSuperclass(k)) {
		unsigned n = 0;
		Method  *ms = class_copyMethodList(k, &n);
		for (unsigned i = 0; i < n && used + 96 < sizeof buf; i++) {
			const char *name = sel_getName(method_getName(ms[i]));
			clj_objc_kebab(name, kebab, sizeof kebab);
			const char *shown = raw ? name : kebab;
			if (strncmp(shown, spelling, base) != 0 || (shown[base] != ':' && shown[base] != '\0')) continue;
			int wrote = snprintf(buf + used, sizeof buf - used, "%s%s", sep, shown);
			if (wrote < 0) break;
			used += (size_t)wrote;
			sep = ", ";
		}
		free(ms);
	}
	if (used == 0) return clj_throw_msg("No selector %.*s on %s", (int)len, spelling, class_getName(cls));
	return clj_throw_msg("No selector %.*s on %s; it has %s", (int)len, spelling, class_getName(cls), buf);
}

clj_value clj_objc_send(clj_value target, clj_value selector, const clj_value *args, uint32_t nargs, bool raw) {
	if (clj_is_nil(target)) return CLJ_NIL; // messaging nil is a no-op returning zero, as in Objective-C
	if (!clj_is_objc_object(target)) return clj_throw_msg("Cannot send to a %s: not an Objective-C object", clj_type_name(target));
	if (!clj_is_string(selector)) return clj_throw_msg("A selector must be a string, got: %s", clj_type_name(selector));

	clj_objc_object *recv = (clj_objc_object *)clj_to_ptr(target);
	Class            cls = object_getClass((id)recv->obj);
	const char      *spelling = clj_string_bytes(selector);
	size_t           len = clj_string_len(selector);

	objc_sig sig = {0};
	bool     found = lookup(cls, spelling, len, raw, &sig);
	if (!found && !sig.sel) return no_such_selector(cls, spelling, len, raw);
	if (!found && sig.reject == REJECT_VARIADIC) return clj_throw_msg("Selector %.*s on %s is variadic, and the bridge builds no stack arguments", (int)len, spelling, class_getName(cls));
	if (!found) return clj_throw_msg("Selector %.*s on %s has a shape the bridge cannot call: a union, an array, a long double, a struct over %d bytes, too many arguments, or float and double mixed", (int)len, spelling, class_getName(cls), CLJ_OBJC_MAX_STRUCT_BYTES);
	if (nargs != sig.nargs) return clj_throw_msg("Selector %.*s takes %u argument(s), got %u", (int)len, spelling, sig.nargs, nargs);

	// Off a coroutine nothing will switch, so the call keeps its own pool; on one the slice's pool is pushed
	// once and drained by clj_coro_switch_out.
	bool  own_pool = !clj_coro_in_coroutine();
	void *pool = own_pool ? objc_autoreleasePoolPush() : NULL;
	if (!own_pool && !clj_objc_pool_token) clj_objc_pool_token = objc_autoreleasePoolPush();

	long long ints[CLJ_OBJC_MAX_INT_ARGS] = {0};
	double    dbls[CLJ_OBJC_MAX_FP_ARGS] = {0};
	float     flts[CLJ_OBJC_MAX_FP_ARGS] = {0};
	// An indirect struct is passed by a pointer into this, so it outlives the marshalling loop.
	_Alignas(16) unsigned char structs[CLJ_OBJC_MAX_ARGS][CLJ_OBJC_MAX_STRUCT_BYTES];
	unsigned                   ni = 0, nf = 0;
	clj_value                  out = CLJ_NIL;
	for (uint32_t i = 0; i < sig.nargs && out != CLJ_THROWN; i++) {
		if (sig.arg[i] == '{') {
			const struct_abi *abi = &sig.arg_abi[i];
			unsigned char    *buf = structs[i];
			memset(buf, 0, (abi->size + 15u) & ~15u);
			if (!encode_struct(sig.argenc[i], args[i], buf)) {
				char want[160];
				describe_struct(sig.argenc[i], want, sizeof want);
				out = clj_throw_msg("Argument %u of %.*s: a %s is not %s", i + 1, (int)len, spelling, clj_type_name(args[i]), want);
				break;
			}
			switch (abi->cls) {
			case SC_HFA_D:
				for (unsigned m = 0; m < abi->slots; m++) memcpy(&dbls[nf++], buf + 8 * m, sizeof(double));
				break;
			case SC_HFA_F:
				for (unsigned m = 0; m < abi->slots; m++) memcpy(&flts[nf++], buf + 4 * m, sizeof(float));
				break;
			case SC_INT:
				for (unsigned w = 0; w < abi->slots; w++) memcpy(&ints[ni++], buf + 8 * w, sizeof(long long));
				break;
			default: ints[ni++] = (long long)(intptr_t)buf; break;
			}
			continue;
		}
		switch (kind_of(sig.arg[i])) {
		case K_INT:
			if (!to_int_slot(args[i], sig.arg[i], &ints[ni++]))
				out = clj_throw_msg("Argument %u of %.*s: a %s does not convert to '%c'", i + 1, (int)len, spelling, clj_type_name(args[i]), sig.arg[i]);
			break;
		case K_FLOAT:
			if (!clj_is_number(args[i])) out = clj_throw_msg("Argument %u of %.*s: a %s is not a number", i + 1, (int)len, spelling, clj_type_name(args[i]));
			else flts[nf++] = (float)clj_num_to_double(args[i]);
			break;
		default:
			if (!clj_is_number(args[i])) out = clj_throw_msg("Argument %u of %.*s: a %s is not a number", i + 1, (int)len, spelling, clj_type_name(args[i]));
			else dbls[nf++] = clj_num_to_double(args[i]);
			break;
		}
	}

	if (out != CLJ_THROWN) {
		id  self = (id)recv->obj;
		SEL sel = sig.sel;
		// -init takes over a reference and hands one back, so the wrapper's own must not be the one it takes.
		if (sig.consumes_self) objc_retain(self);
		// The prototype is chosen by the return, the slot type by fp_is_float; the arguments are the same.
#define SEND(tag) (sig.fp_is_float ? ((send_##tag##_f)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(flts)) : ((send_##tag##_d)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(dbls)))
		switch (sig.ret) {
		case 'f': out = clj_double_new(SEND(f)); break;
		case 'd': out = clj_double_new(SEND(d)); break;
		case 'v': SEND(v), out = CLJ_NIL; break;
		case '{': {
			_Alignas(16) unsigned char rbuf[CLJ_OBJC_MAX_STRUCT_BYTES] = {0};
			switch (sig.ret_abi.cls) {
			case SC_HFA_D:
				switch (sig.ret_abi.slots) {
				case 1: { double r = SEND(d); memcpy(rbuf, &r, sizeof r); break; }
				case 2: { ret_d2 r = SEND(d2); memcpy(rbuf, &r, sizeof r); break; }
				case 3: { ret_d3 r = SEND(d3); memcpy(rbuf, &r, sizeof r); break; }
				default: { ret_d4 r = SEND(d4); memcpy(rbuf, &r, sizeof r); break; }
				}
				break;
			case SC_HFA_F:
				switch (sig.ret_abi.slots) {
				case 1: { float r = SEND(f); memcpy(rbuf, &r, sizeof r); break; }
				case 2: { ret_f2 r = SEND(f2); memcpy(rbuf, &r, sizeof r); break; }
				case 3: { ret_f3 r = SEND(f3); memcpy(rbuf, &r, sizeof r); break; }
				default: { ret_f4 r = SEND(f4); memcpy(rbuf, &r, sizeof r); break; }
				}
				break;
			case SC_INT:
				if (sig.ret_abi.slots <= 1) { long long r = SEND(i); memcpy(rbuf, &r, sizeof r); }
				else { ret_i2 r = SEND(i2); memcpy(rbuf, &r, sizeof r); }
				break;
			default: { ret_mem r = SEND(mem); memcpy(rbuf, r.b, sig.ret_abi.size); break; }
			}
			out = decode_struct(sig.retenc, rbuf);
			break;
		}
		default: out = from_int_return(SEND(i), sig.ret, sig.owned); break;
		}
#undef SEND
	}
	if (own_pool) objc_autoreleasePoolPop(pool);
	return out;
}

// ---- calling in: an Objective-C object whose methods are Clojure fns (design §5, "делегат не паркуется")

typedef struct {
	SEL      sel;
	objc_sig sig;
} reify_method;

typedef struct {
	Class         cls;
	reify_method *m;
	uint32_t      n;
	char         *key; // the shape this class was minted for; NULL marks a free slot
	uint32_t      hash;
} reify_class;

// Behind the instance, where class_createInstance's extra bytes land: an ivar would cost a runtime lookup
// per callback to find its offset.
typedef struct {
	const reify_class *rc;
	clj_value          fns; // SHARED: a callback and the dealloc can each arrive on any thread
} reify_state;

// ---- the trampolines

// The IMP prototype problem is objc_msgSend's inverted, and the same two ABI facts answer it: the caller
// filled only the registers its own selector declares, and reading the rest is harmless because the
// signature says how many there are. So one entry point per return shape covers every argument list.
#define TRAMP_PARAMS(t)                                                                             \
	long long i0, long long i1, long long i2, long long i3, long long i4, long long i5, t f0, t f1, \
	    t f2, t f3, t f4, t f5, t f6, t f7
#define TRAMP_GATHER                                                          \
	_Alignas(16) unsigned char rbuf[CLJ_OBJC_MAX_STRUCT_BYTES] = {0};         \
	const long long ints[CLJ_OBJC_MAX_INT_ARGS] = {i0, i1, i2, i3, i4, i5};   \
	const double    fps[CLJ_OBJC_MAX_FP_ARGS] = {f0, f1, f2, f3, f4, f5, f6, f7}

static void reify_dispatch(id self, SEL cmd, const long long *ints, const double *fps, unsigned char *rbuf);

#define DEFINE_TRAMP(tag, R, t, suffix)                             \
	static R tramp_##tag##_##suffix(id self, SEL cmd, TRAMP_PARAMS(t)) { \
		TRAMP_GATHER;                                               \
		reify_dispatch(self, cmd, ints, fps, rbuf);                 \
		R r;                                                        \
		memcpy(&r, rbuf, sizeof r);                                 \
		return r;                                                   \
	}

#define DEFINE_TRAMP_PAIR(tag, R) \
	DEFINE_TRAMP(tag, R, double, d) \
	DEFINE_TRAMP(tag, R, float, f)

DEFINE_TRAMP_PAIR(i, long long)
DEFINE_TRAMP_PAIR(f, float)
DEFINE_TRAMP_PAIR(d, double)
DEFINE_TRAMP_PAIR(i2, ret_i2)
DEFINE_TRAMP_PAIR(d2, ret_d2)
DEFINE_TRAMP_PAIR(d3, ret_d3)
DEFINE_TRAMP_PAIR(d4, ret_d4)
DEFINE_TRAMP_PAIR(f2, ret_f2)
DEFINE_TRAMP_PAIR(f3, ret_f3)
DEFINE_TRAMP_PAIR(f4, ret_f4)

#define DEFINE_TRAMP_VOID(t, suffix)                                  \
	static void tramp_v_##suffix(id self, SEL cmd, TRAMP_PARAMS(t)) { \
		TRAMP_GATHER;                                                 \
		reify_dispatch(self, cmd, ints, fps, rbuf);                   \
	}

DEFINE_TRAMP_VOID(double, d)
DEFINE_TRAMP_VOID(float, f)

#define PICK(tag) (sig->fp_is_float ? (IMP)tramp_##tag##_f : (IMP)tramp_##tag##_d)

// NULL when no prototype has this return; the caller turns that into the refusal message.
static IMP trampoline_for(const objc_sig *sig) {
	switch (sig->ret) {
	case 'v': return PICK(v);
	case 'f': return PICK(f);
	case 'd': return PICK(d);
	case '{':
		switch (sig->ret_abi.cls) {
		case SC_HFA_D:
			switch (sig->ret_abi.slots) {
			case 1: return PICK(d);
			case 2: return PICK(d2);
			case 3: return PICK(d3);
			default: return PICK(d4);
			}
		case SC_HFA_F:
			switch (sig->ret_abi.slots) {
			case 1: return PICK(f);
			case 2: return PICK(f2);
			case 3: return PICK(f3);
			default: return PICK(f4);
			}
		case SC_INT: return sig->ret_abi.slots <= 1 ? PICK(i) : PICK(i2);
		// x8 points at a buffer sized for the real struct, and a prototype returning a fixed 128 bytes
		// would write past it; there is no shape to borrow, so this one is refused.
		default: return NULL;
		}
	default: return PICK(i);
	}
}

#undef PICK

// ---- the dispatch

// A callback has nowhere to throw to, so an error in a body is reported where it happened.
static void report_uncaught(const char *what) {
	clj_value ex = clj_take_pending(), trace = clj_take_pending_trace();
	clj_value msg = clj_ex_message(ex);
	clj_value text = clj_is_string(msg) ? clj_retain(msg) : clj_pr_str(ex);
	(void)!write(2, "clj: uncaught error in ", 23);
	(void)!write(2, what, strlen(what));
	(void)!write(2, ": ", 2);
	if (text != CLJ_THROWN) (void)!write(2, clj_string_bytes(text), clj_string_len(text));
	(void)!write(2, "\n", 1);
	if (text != CLJ_THROWN) clj_release(text);
	clj_release(msg);
	clj_release(ex);
	clj_release(trace);
}

// ints and fps start at the callback's own first argument: the trampoline ate x0 and, for a method, x1.
static void call_in(const objc_sig *sig, clj_value fn, clj_value self, const char *what,
                    const long long *ints, const double *fps, unsigned char *rbuf) {
	// The slice's token is hidden: popping our pool pops any pool a send opened inside it.
	void *saved = clj_objc_pool_token;
	clj_objc_pool_token = NULL;
	void *pool = objc_autoreleasePoolPush();

	clj_value argv[CLJ_OBJC_MAX_ARGS + 1];
	uint32_t  argc = 0;
	if (self != CLJ_UNBOUND) argv[argc++] = self;
	unsigned ni = 0, nf = 0;
	for (unsigned i = 0; i < sig->nargs; i++) {
		if (sig->arg[i] == '{') {
			const struct_abi          *abi = &sig->arg_abi[i];
			_Alignas(16) unsigned char buf[CLJ_OBJC_MAX_STRUCT_BYTES] = {0};
			switch (abi->cls) {
			case SC_HFA_D:
				for (unsigned m = 0; m < abi->slots; m++) memcpy(buf + 8 * m, &fps[nf++], sizeof(double));
				break;
			case SC_HFA_F:
				for (unsigned m = 0; m < abi->slots; m++) {
					float f = (float)fps[nf++];
					memcpy(buf + 4 * m, &f, sizeof f);
				}
				break;
			case SC_INT:
				for (unsigned w = 0; w < abi->slots; w++) memcpy(buf + 8 * w, &ints[ni++], sizeof(long long));
				break;
			default: memcpy(buf, (const void *)(intptr_t)ints[ni++], abi->size); break;
			}
			argv[argc++] = decode_struct(sig->argenc[i], buf);
			continue;
		}
		switch (kind_of(sig->arg[i])) {
		case K_INT: {
			long long raw = ints[ni++];
			// A raw pointer in is borrowed: releasing it at the wrapper's death would be a release of
			// something that is not an object at all.
			argv[argc++] = sig->arg[i] == '^' ? (raw ? wrap((void *)(intptr_t)raw, true) : CLJ_NIL)
			                                  : from_int_return(raw, sig->arg[i], false);
			break;
		}
		case K_FLOAT: argv[argc++] = clj_double_new((float)fps[nf++]); break;
		default: argv[argc++] = clj_double_new(fps[nf++]); break;
		}
	}

	clj_value r = clj_host_invoke(fn, argv, argc);
	for (uint32_t i = 0; i < argc; i++) clj_release(argv[i]);

	// A returned object outlives our pool only if it is autoreleased into the caller's.
	id autorelease_me = NULL;
	if (r == CLJ_THROWN) {
		report_uncaught(what);
	} else if (sig->ret == '{') {
		if (!encode_struct(sig->retenc, r, rbuf)) {
			memset(rbuf, 0, sig->ret_abi.size);
			clj_throw_msg("%s must return %s", what, sig->retenc);
			report_uncaught(what);
		}
	} else if (sig->ret == 'f' || sig->ret == 'd') {
		if (!clj_is_number(r)) {
			clj_throw_msg("%s must return a number, got: %s", what, clj_type_name(r));
			report_uncaught(what);
		} else if (sig->ret == 'f') {
			float f = (float)clj_num_to_double(r);
			memcpy(rbuf, &f, sizeof f);
		} else {
			double d = clj_num_to_double(r);
			memcpy(rbuf, &d, sizeof d);
		}
	} else if (sig->ret != 'v') {
		long long raw = 0;
		if (!to_int_slot(r, sig->ret, &raw)) {
			clj_throw_msg("%s cannot return a %s as '%c'", what, clj_type_name(r), sig->ret);
			report_uncaught(what);
		}
		if (sig->ret == '@' && raw) autorelease_me = objc_retain((id)(intptr_t)raw);
		memcpy(rbuf, &raw, sizeof raw);
	}
	if (r != CLJ_THROWN) clj_release(r);

	clj_objc_pool_token = NULL;
	objc_autoreleasePoolPop(pool);
	clj_objc_pool_token = saved;
	if (autorelease_me) objc_autorelease(autorelease_me);
}

// A fresh self wrapper per call: a cached one would own the instance that owns it.
static void reify_dispatch(id self, SEL cmd, const long long *ints, const double *fps, unsigned char *rbuf) {
	reify_state       *st = object_getIndexedIvars(self);
	const reify_class *rc = st->rc;
	uint32_t           at = 0;
	while (at < rc->n && rc->m[at].sel != cmd) at++;
	if (at == rc->n) clj_fatal("an Objective-C trampoline was reached by a selector it was not installed for");
	call_in(&rc->m[at].sig, clj_vector_nth(st->fns, at), clj_objc_wrap(self), sel_getName(cmd), ints, fps, rbuf);
}

static void reify_dealloc(id self, SEL cmd) {
	reify_state *st = object_getIndexedIvars(self);
	clj_release(st->fns);
	struct objc_super sup = {self, class_getSuperclass(object_getClass(self))};
	((void (*)(struct objc_super *, SEL))objc_msgSendSuper)(&sup, cmd);
}

// ---- resolving a selector's type encoding

// The whole of one encoding, plus the frame offsets a protocol's types string carries between them.
static bool next_encoding(const char **p, char *out, size_t cap) {
	const char *e = skip_qualifiers(*p);
	const char *q = e;
	if (!*e || !skip_encoding(&q)) return false;
	size_t n = (size_t)(q - e);
	if (n >= cap) return false;
	memcpy(out, e, n);
	out[n] = '\0';
	*p = q;
	while (**p >= '0' && **p <= '9') (*p)++;
	return true;
}

#define CLJ_OBJC_MAX_ENC 256

// "v16@0:8" and "v@:" both parse. nhidden is 2 for a method (self, _cmd) and 1 for a block.
static bool signature_from_types(const char *types, SEL sel, objc_sig *sig, unsigned nhidden) {
	char        ret[CLJ_OBJC_MAX_ENC], arg[CLJ_OBJC_MAX_ARGS][CLJ_OBJC_MAX_ENC], skip[CLJ_OBJC_MAX_ENC];
	const char *argv[CLJ_OBJC_MAX_ARGS];
	const char *p = types;
	sig->sel = sel;
	sig->reject = REJECT_SHAPE;
	if (!next_encoding(&p, ret, sizeof ret)) return false;
	for (unsigned i = 0; i < nhidden; i++)
		if (!next_encoding(&p, skip, sizeof skip)) return false;
	unsigned n = 0;
	while (*p) {
		if (n == CLJ_OBJC_MAX_ARGS || !next_encoding(&p, arg[n], sizeof arg[n])) return false;
		argv[n] = arg[n];
		n++;
	}
	sig->nargs = (uint8_t)n;
	return signature_shape(sig, ret, argv);
}

// A protocol's own methods and those of the protocols it adopts, required and optional alike.
static const char *protocol_types(Protocol *p, const char *spelling, size_t len, SEL *sel) {
	char buf[512];
	for (int req = 0; req < 2; req++) {
		unsigned                        n = 0;
		struct objc_method_description *ds = protocol_copyMethodDescriptionList(p, req == 0, true, &n);
		for (unsigned i = 0; i < n; i++) {
			const char *name = sel_getName(ds[i].name);
			if (clj_objc_kebab(name, buf, sizeof buf) != len || memcmp(buf, spelling, len) != 0) continue;
			const char *types = ds[i].types;
			*sel = ds[i].name;
			free(ds);
			return types; // a static string of the image the protocol came from
		}
		free(ds);
	}
	unsigned    n = 0;
	Protocol  **ps = protocol_copyProtocolList(p, &n);
	const char *found = NULL;
	for (unsigned i = 0; i < n && !found; i++) found = protocol_types(ps[i], spelling, len, sel);
	free(ps);
	return found;
}

static const char *inherited_types(Class cls, const char *spelling, size_t len, SEL *sel) {
	char buf[512];
	for (Class k = cls; k; k = class_getSuperclass(k)) {
		unsigned n = 0;
		Method  *ms = class_copyMethodList(k, &n);
		for (unsigned i = 0; i < n; i++) {
			const char *name = sel_getName(method_getName(ms[i]));
			if (clj_objc_kebab(name, buf, sizeof buf) != len || memcmp(buf, spelling, len) != 0) continue;
			const char *types = method_getTypeEncoding(ms[i]);
			*sel = method_getName(ms[i]);
			free(ms);
			return types;
		}
		free(ms);
	}
	return NULL;
}

// ---- the class, one per reify shape

static clj_lock   reify_lock = CLJ_LOCK_INIT;
static reify_class *reify_cache;
static size_t       reify_cap, reify_len;
static unsigned     reify_counter;

static reify_class *reify_slot(const char *key, uint32_t h) {
	size_t mask = reify_cap - 1;
	for (size_t i = h & mask;; i = (i + 1) & mask) {
		reify_class *e = &reify_cache[i];
		if (!e->key || (e->hash == h && strcmp(e->key, key) == 0)) return e;
	}
}

static void reify_grow(void) {
	size_t       cap = reify_cap ? reify_cap * 2 : 16;
	reify_class *old = reify_cache, *fresh = calloc(cap, sizeof *fresh);
	if (!fresh) clj_fatal("out of memory growing the objc reify cache");
	size_t old_cap = reify_cap;
	reify_cache = fresh;
	reify_cap = cap;
	for (size_t i = 0; i < old_cap; i++) {
		if (old[i].key) *reify_slot(old[i].key, old[i].hash) = old[i];
	}
	free(old);
}

static bool string_vector(clj_value v, uint32_t want, bool nils_ok) {
	if (!clj_is_vector(v) || clj_vector_count(v) != want) return false;
	for (uint32_t i = 0; i < want; i++) {
		clj_value e = clj_vector_nth(v, i);
		if (!clj_is_string(e) && !(nils_ok && clj_is_nil(e))) return false;
	}
	return true;
}

static void key_append(char **key, size_t *len, size_t *cap, const char *s) {
	size_t n = strlen(s) + 1;
	if (*len + n > *cap) {
		*cap = (*len + n) * 2;
		*key = realloc(*key, *cap);
		if (!*key) clj_fatal("out of memory keying an objc reify site");
	}
	memcpy(*key + *len, s, n);
	*len += n;
}

// Never disposed: objc_disposeClassPair refuses while an instance lives, and a reify in a loop must not
// mint a class per instance anyway. One class per shape, immortal like an interned keyword.
static reify_class *reify_class_for(clj_value super, clj_value protos, clj_value sels, clj_value encs, clj_value *err) {
	uint32_t n = clj_vector_count(sels);
	char    *key = NULL;
	size_t   klen = 0, kcap = 0;
	key_append(&key, &klen, &kcap, clj_is_nil(super) ? "NSObject" : clj_string_bytes(super));
	for (uint32_t i = 0; i < clj_vector_count(protos); i++) key_append(&key, &klen, &kcap, clj_string_bytes(clj_vector_nth(protos, i)));
	key_append(&key, &klen, &kcap, "|");
	for (uint32_t i = 0; i < n; i++) {
		key_append(&key, &klen, &kcap, clj_string_bytes(clj_vector_nth(sels, i)));
		clj_value e = clj_vector_nth(encs, i);
		key_append(&key, &klen, &kcap, clj_is_nil(e) ? "" : clj_string_bytes(e));
	}
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < klen; i++) h = clj_hash_combine(h, (uint32_t)(unsigned char)key[i]);

	clj_lock_lock(&reify_lock);
	if (reify_len * 2 >= reify_cap) reify_grow();
	reify_class *rc = reify_slot(key, h);
	if (rc->key) {
		free(key);
		clj_lock_unlock(&reify_lock);
		return rc;
	}

	const char *super_name = clj_is_nil(super) ? "NSObject" : clj_string_bytes(super);
	Class       sup = objc_getClass(super_name);
	if (!sup) {
		*err = clj_throw_msg("objc-reify: no such class: %s", super_name);
		goto fail;
	}
	char name[64];
	Class cls = NULL;
	while (!cls) {
		snprintf(name, sizeof name, "CljReify_%u", reify_counter++);
		cls = objc_allocateClassPair(sup, name, 0);
	}
	rc->m = calloc(n ? n : 1, sizeof *rc->m);
	if (!rc->m) clj_fatal("out of memory building an objc reify class");
	rc->n = n;

	for (uint32_t i = 0; i < clj_vector_count(protos); i++) {
		const char *pn = clj_string_bytes(clj_vector_nth(protos, i));
		Protocol   *p = objc_getProtocol(pn);
		if (!p) {
			*err = clj_throw_msg("objc-reify: no such protocol: %s", pn);
			goto fail_class;
		}
		class_addProtocol(cls, p);
	}

	for (uint32_t i = 0; i < n; i++) {
		clj_value   sv = clj_vector_nth(sels, i), ev = clj_vector_nth(encs, i);
		const char *spelling = clj_string_bytes(sv);
		size_t      len = clj_string_len(sv);
		const char *types = clj_is_nil(ev) ? NULL : clj_string_bytes(ev);
		SEL         sel = NULL;
		const char *found = NULL;
		for (uint32_t k = 0; k < clj_vector_count(protos) && !found; k++)
			found = protocol_types(objc_getProtocol(clj_string_bytes(clj_vector_nth(protos, k))), spelling, len, &sel);
		if (!found) found = inherited_types(sup, spelling, len, &sel);
		if (!types) {
			if (!found) {
				*err = clj_throw_msg("objc-reify: %s is on none of the protocols nor on %s, so it needs a type encoding", spelling, super_name);
				goto fail_class;
			}
			types = found;
		} else if (!sel) {
			sel = sel_registerName(spelling); // a selector of our own: the spelling is its Objective-C text
		}
		if (!signature_from_types(types, sel, &rc->m[i].sig, 2)) {
			*err = clj_throw_msg("objc-reify: %s has a shape the bridge cannot implement: %s", spelling, types);
			goto fail_class;
		}
		IMP imp = trampoline_for(&rc->m[i].sig);
		if (!imp) {
			*err = clj_throw_msg("objc-reify: %s returns a struct the bridge can only return through x8, which no trampoline shape fits: %s", spelling, types);
			goto fail_class;
		}
		rc->m[i].sel = sel;
		if (!class_addMethod(cls, sel, imp, types)) {
			*err = clj_throw_msg("objc-reify: %s is named twice", spelling);
			goto fail_class;
		}
	}
	class_addMethod(cls, sel_registerName("dealloc"), (IMP)reify_dealloc, "v@:");
	objc_registerClassPair(cls);
	rc->cls = cls;
	rc->key = key;
	rc->hash = h;
	reify_len++;
	clj_lock_unlock(&reify_lock);
	return rc;

fail_class:
	objc_disposeClassPair(cls);
	free(rc->m);
	memset(rc, 0, sizeof *rc);
fail:
	free(key);
	clj_lock_unlock(&reify_lock);
	return NULL;
}

clj_value clj_objc_reify(clj_value super, clj_value protos, clj_value sels, clj_value encs, clj_value fns) {
	if (!clj_is_nil(super) && !clj_is_string(super)) return clj_throw_msg("objc-reify: the superclass must be a string or nil");
	if (!clj_is_vector(protos) || !string_vector(protos, clj_vector_count(protos), false)) return clj_throw_msg("objc-reify: the protocols must be a vector of strings");
	if (!clj_is_vector(sels) || !clj_is_vector(fns) || !clj_is_vector(encs)) return clj_throw_msg("objc-reify: the methods must be vectors");
	uint32_t n = clj_vector_count(sels);
	if (!string_vector(sels, n, false)) return clj_throw_msg("objc-reify: every selector must be a string");
	if (!string_vector(encs, n, true) || clj_vector_count(fns) != n) return clj_throw_msg("objc-reify: one encoding and one fn per selector");

	pool_scope   p = pool_enter();
	clj_value    err = CLJ_NIL;
	reify_class *rc = reify_class_for(super, protos, sels, encs, &err);
	pool_leave(p);
	if (!rc) return err == CLJ_NIL ? clj_throw_msg("objc-reify failed") : err;

	id           obj = class_createInstance(rc->cls, sizeof(reify_state));
	reify_state *st = object_getIndexedIvars(obj);
	st->rc = rc;
	// A callback may arrive on a thread the runtime has never seen, and dealloc on any thread at all.
	clj_share(fns);
	st->fns = clj_retain(fns);
	return clj_objc_wrap_owned(obj);
}

// ---- blocks

// The ABI of <Block_private.h>, which is not a public header; the flags and the descriptor tail are what
// libclosure reads, so they are copied here rather than assumed.
enum {
	BLOCK_HAS_COPY_DISPOSE = 1 << 25,
	BLOCK_HAS_SIGNATURE = 1 << 30,
};

extern void *_NSConcreteStackBlock[32];
extern void *_Block_copy(const void *);

typedef struct {
	unsigned long reserved, size;
	void (*copy)(void *dst, const void *src);
	void (*dispose)(const void *);
	const char *signature;
	const char *layout;
} block_descriptor;

typedef struct block_kind block_kind;

typedef struct {
	void             *isa;
	int               flags;
	int               reserved;
	void             *invoke;
	block_descriptor *descriptor;
	const block_kind *kind;
	clj_value         fn; // SHARED: the dispose helper can run on any thread
} clj_block;

struct block_kind {
	block_descriptor desc;
	objc_sig         sig;
	void            *invoke;
	char            *signature; // owned; NULL marks a free slot
	uint32_t         hash;
};

static void block_copy_helper(void *dst, const void *src) {
	(void)src;
	clj_retain(((clj_block *)dst)->fn);
}

static void block_dispose_helper(const void *b) {
	clj_release(((const clj_block *)b)->fn);
}

// A block's invoke pointer has the IMP problem without the _cmd: x0 is the block, so x1-x6 carry the same
// six integer arguments the method trampolines refuse to exceed and x7 is never read.
#define BTRAMP_PARAMS(t)                                                                       \
	long long i0, long long i1, long long i2, long long i3, long long i4, long long i5, t f0,  \
	    t f1, t f2, t f3, t f4, t f5, t f6, t f7

static void block_dispatch(void *blk, const long long *ints, const double *fps, unsigned char *rbuf) {
	clj_block *b = blk;
	call_in(&b->kind->sig, b->fn, CLJ_UNBOUND, b->kind->signature, ints, fps, rbuf);
}

#define DEFINE_BTRAMP(tag, R, t, suffix)                             \
	static R btramp_##tag##_##suffix(void *blk, BTRAMP_PARAMS(t)) {  \
		TRAMP_GATHER;                                                \
		block_dispatch(blk, ints, fps, rbuf);                        \
		R r;                                                         \
		memcpy(&r, rbuf, sizeof r);                                  \
		return r;                                                    \
	}

#define DEFINE_BTRAMP_PAIR(tag, R)  \
	DEFINE_BTRAMP(tag, R, double, d) \
	DEFINE_BTRAMP(tag, R, float, f)

DEFINE_BTRAMP_PAIR(i, long long)
DEFINE_BTRAMP_PAIR(f, float)
DEFINE_BTRAMP_PAIR(d, double)
DEFINE_BTRAMP_PAIR(i2, ret_i2)
DEFINE_BTRAMP_PAIR(d2, ret_d2)
DEFINE_BTRAMP_PAIR(d3, ret_d3)
DEFINE_BTRAMP_PAIR(d4, ret_d4)
DEFINE_BTRAMP_PAIR(f2, ret_f2)
DEFINE_BTRAMP_PAIR(f3, ret_f3)
DEFINE_BTRAMP_PAIR(f4, ret_f4)

#define DEFINE_BTRAMP_VOID(t, suffix)                                  \
	static void btramp_v_##suffix(void *blk, BTRAMP_PARAMS(t)) {       \
		TRAMP_GATHER;                                                  \
		block_dispatch(blk, ints, fps, rbuf);                          \
	}

DEFINE_BTRAMP_VOID(double, d)
DEFINE_BTRAMP_VOID(float, f)

#define PICK(tag) (sig->fp_is_float ? (void *)btramp_##tag##_f : (void *)btramp_##tag##_d)

static void *block_invoke_for(const objc_sig *sig) {
	switch (sig->ret) {
	case 'v': return PICK(v);
	case 'f': return PICK(f);
	case 'd': return PICK(d);
	case '{':
		switch (sig->ret_abi.cls) {
		case SC_HFA_D:
			switch (sig->ret_abi.slots) {
			case 1: return PICK(d);
			case 2: return PICK(d2);
			case 3: return PICK(d3);
			default: return PICK(d4);
			}
		case SC_HFA_F:
			switch (sig->ret_abi.slots) {
			case 1: return PICK(f);
			case 2: return PICK(f2);
			case 3: return PICK(f3);
			default: return PICK(f4);
			}
		case SC_INT: return sig->ret_abi.slots <= 1 ? PICK(i) : PICK(i2);
		default: return NULL;
		}
	default: return PICK(i);
	}
}

#undef PICK

// One kind per signature, never freed: the descriptor a block points at must outlive every copy of it.
static clj_lock    block_lock = CLJ_LOCK_INIT;
static block_kind *block_kinds;
static size_t      block_cap, block_len;

static block_kind *block_slot(const char *sig, uint32_t h) {
	size_t mask = block_cap - 1;
	for (size_t i = h & mask;; i = (i + 1) & mask) {
		block_kind *e = &block_kinds[i];
		if (!e->signature || (e->hash == h && strcmp(e->signature, sig) == 0)) return e;
	}
}

static void block_grow(void) {
	size_t      cap = block_cap ? block_cap * 2 : 16;
	block_kind *old = block_kinds, *fresh = calloc(cap, sizeof *fresh);
	if (!fresh) clj_fatal("out of memory growing the objc block cache");
	size_t old_cap = block_cap;
	block_kinds = fresh;
	block_cap = cap;
	for (size_t i = 0; i < old_cap; i++) {
		if (old[i].signature) *block_slot(old[i].signature, old[i].hash) = old[i];
	}
	free(old);
}

// A block's signature is the method one with the block itself in place of self and no _cmd.
static block_kind *block_kind_for(const char *signature, clj_value *err) {
	uint32_t h = 2166136261u;
	for (const char *p = signature; *p; p++) h = clj_hash_combine(h, (uint32_t)(unsigned char)*p);
	clj_lock_lock(&block_lock);
	if (block_len * 2 >= block_cap) block_grow();
	block_kind *k = block_slot(signature, h);
	if (k->signature) {
		clj_lock_unlock(&block_lock);
		return k;
	}
	if (!signature_from_types(signature, NULL, &k->sig, 1)) {
		*err = clj_throw_msg("objc-block: a shape the bridge cannot implement: %s", signature);
		memset(k, 0, sizeof *k);
		clj_lock_unlock(&block_lock);
		return NULL;
	}
	void *invoke = block_invoke_for(&k->sig);
	if (!invoke) {
		*err = clj_throw_msg("objc-block: a struct returned through x8 fits no trampoline shape: %s", signature);
		memset(k, 0, sizeof *k);
		clj_lock_unlock(&block_lock);
		return NULL;
	}
	k->signature = strdup(signature);
	if (!k->signature) clj_fatal("out of memory keying an objc block signature");
	k->hash = h;
	k->desc.size = sizeof(clj_block);
	k->desc.copy = block_copy_helper;
	k->desc.dispose = block_dispose_helper;
	k->desc.signature = k->signature;
	k->invoke = invoke;
	block_len++;
	clj_lock_unlock(&block_lock);
	return k;
}

clj_value clj_objc_block(clj_value signature, clj_value fn) {
	if (!clj_is_string(signature)) return clj_throw_msg("objc-block expects a signature string, got: %s", clj_type_name(signature));
	clj_value   err = CLJ_NIL;
	block_kind *k = block_kind_for(clj_string_bytes(signature), &err);
	if (!k) return err == CLJ_NIL ? clj_throw_msg("objc-block failed") : err;

	// Built on the stack and copied, exactly as a compiler-generated block is: _Block_copy runs the copy
	// helper, so the heap block owns the fn and the dispose helper gives it back.
	clj_share(fn);
	clj_block stack = {
	    .isa = &_NSConcreteStackBlock,
	    .flags = BLOCK_HAS_COPY_DISPOSE | BLOCK_HAS_SIGNATURE,
	    .invoke = k->invoke,
	    .descriptor = &k->desc,
	    .kind = k,
	    .fn = fn,
	};
	return clj_objc_wrap_owned(_Block_copy(&stack));
}

#else // !__APPLE__

// The gap the ledger asks to be named: without an Objective-C runtime the level-1 bridge is absent rather
// than emulated, and everything that would reach it says so.

const clj_type clj_objc_object_type = {
    .h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
    .name = "objc-object",
};

void clj_objc_pool_drain_slow(void) {}

clj_value clj_objc_wrap(void *obj) {
	(void)obj;
	return CLJ_NIL;
}

clj_value clj_objc_wrap_owned(void *obj) {
	(void)obj;
	return CLJ_NIL;
}

clj_value clj_objc_class(const char *name) {
	(void)name;
	return CLJ_NIL;
}

clj_value clj_objc_send(clj_value target, clj_value selector, const clj_value *args, uint32_t nargs, bool raw) {
	(void)target, (void)selector, (void)args, (void)nargs, (void)raw;
	return clj_throw_msg("The Objective-C bridge needs an Apple platform");
}

clj_value clj_objc_reify(clj_value super, clj_value protos, clj_value sels, clj_value encs, clj_value fns) {
	(void)super, (void)protos, (void)sels, (void)encs, (void)fns;
	return clj_throw_msg("The Objective-C bridge needs an Apple platform");
}

clj_value clj_objc_block(clj_value signature, clj_value fn) {
	(void)signature, (void)fn;
	return clj_throw_msg("The Objective-C bridge needs an Apple platform");
}

clj_value clj_objc_to_collection(clj_value v, bool as_map) {
	(void)v, (void)as_map;
	return clj_throw_msg("The Objective-C bridge needs an Apple platform");
}

clj_value clj_objc_from_collection(clj_value v, bool as_map) {
	(void)v, (void)as_map;
	return clj_throw_msg("The Objective-C bridge needs an Apple platform");
}

#endif

// ---- builtins

static clj_value b_objc_class(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("objc-class expects a string, got: %s", clj_type_name(args[0]));
	return clj_objc_class(clj_string_bytes(args[0]));
}

// The escape hatch of design §5: the full Objective-C selector text, for a name that is not a literal.
static clj_value b_objc_send(const clj_value *args, size_t n) {
	return clj_objc_send(args[0], args[1], args + 2, (uint32_t)(n - 2), true);
}

static clj_value b_objc_object_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_is_objc_object(args[0]) ? CLJ_TRUE : CLJ_FALSE;
}

static clj_value b_objc_kebab(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("objc-kebab* expects a string, got: %s", clj_type_name(args[0]));
	char   buf[512];
	size_t want = clj_objc_kebab(clj_string_bytes(args[0]), buf, sizeof buf);
	if (want >= sizeof buf) return clj_throw_msg("Selector too long: %s", clj_string_bytes(args[0]));
	return clj_string_new(buf, want);
}

static clj_value b_ns_array(const clj_value *args, size_t n) {
	(void)n;
	return clj_objc_to_collection(args[0], false);
}

static clj_value b_ns_dictionary(const clj_value *args, size_t n) {
	(void)n;
	return clj_objc_to_collection(args[0], true);
}

static clj_value b_ns_array_to_vec(const clj_value *args, size_t n) {
	(void)n;
	return clj_objc_from_collection(args[0], false);
}

static clj_value b_ns_dictionary_to_map(const clj_value *args, size_t n) {
	(void)n;
	return clj_objc_from_collection(args[0], true);
}

static clj_value b_objc_reify(const clj_value *args, size_t n) {
	(void)n;
	return clj_objc_reify(args[0], args[1], args[2], args[3], args[4]);
}

static clj_value b_objc_block(const clj_value *args, size_t n) {
	(void)n;
	return clj_objc_block(args[0], args[1]);
}

void clj_objc_builtins_install(void) {
	clj_builtin_bind("objc-reify*", b_objc_reify, 5, 5);
	clj_builtin_bind("objc-block*", b_objc_block, 2, 2);
	clj_builtin_bind("ns-array", b_ns_array, 1, 1);
	clj_builtin_bind("ns-dictionary", b_ns_dictionary, 1, 1);
	clj_builtin_bind("ns-array->vec", b_ns_array_to_vec, 1, 1);
	clj_builtin_bind("ns-dictionary->map", b_ns_dictionary_to_map, 1, 1);
	clj_builtin_bind("objc-class", b_objc_class, 1, 1);
	clj_builtin_bind("objc-send", b_objc_send, 2, CLJ_ARITY_ANY);
	clj_builtin_bind("objc-object?", b_objc_object_p, 1, 1);
	clj_builtin_bind("objc-kebab*", b_objc_kebab, 1, 1);
}
