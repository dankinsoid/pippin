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

#define CLJ_OBJC_MAX_INT_ARGS 6
#define CLJ_OBJC_MAX_FP_ARGS  8
#define CLJ_OBJC_MAX_ARGS     8

#define INT_SLOTS   long long, long long, long long, long long, long long, long long
#define FP_SLOTS(t) t, t, t, t, t, t, t, t
#define INT_ARGS(a) a[0], a[1], a[2], a[3], a[4], a[5]
#define FP_ARGS(f)  f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]

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

typedef struct {
	SEL     sel;
	char    ret;                    // the return's encoding char
	char    arg[CLJ_OBJC_MAX_ARGS]; // each argument's, self and _cmd dropped
	uint8_t nargs;
	bool    owned;       // the alloc/new/copy/mutableCopy/init families return +1
	bool    fp_is_float; // every floating-point argument is a float rather than a double
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

static char strip_qualifiers(const char *enc) {
	while (*enc && strchr("rnNoORV", *enc)) enc++;
	return *enc;
}

// The ARC method families, read off the selector's first word after any leading underscores.
static bool family_is_owned(const char *sel) {
	while (*sel == '_') sel++;
	static const char *const families[] = {"alloc", "new", "copy", "mutableCopy", "init"};
	for (size_t i = 0; i < sizeof families / sizeof *families; i++) {
		size_t n = strlen(families[i]);
		if (strncmp(sel, families[i], n) != 0) continue;
		char after = sel[n];
		if (after == '\0' || after == ':' || (after >= 'A' && after <= 'Z')) return true;
	}
	return false;
}

// Fills sig from the method; false when the shape is none of the eight prototypes. sel is set either way, so
// a caller can tell "no such selector" from "a selector we cannot call".
static bool signature_of(Method m, objc_sig *sig) {
	sig->sel = method_getName(m);
	unsigned n = method_getNumberOfArguments(m);
	if (n < 2 || n - 2 > CLJ_OBJC_MAX_ARGS) return false;
	sig->nargs = (uint8_t)(n - 2);
	sig->owned = family_is_owned(sel_getName(sig->sel));

	char *ret = method_copyReturnType(m);
	sig->ret = strip_qualifiers(ret);
	free(ret);
	if (!encoding_supported(sig->ret, true)) return false;

	unsigned ints = 0, fps = 0, floats = 0;
	for (unsigned i = 0; i < sig->nargs; i++) {
		char *a = method_copyArgumentType(m, i + 2);
		sig->arg[i] = strip_qualifiers(a);
		free(a);
		if (!encoding_supported(sig->arg[i], false)) return false;
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

// ---- the (class, spelling) selector cache

typedef struct {
	Class    cls;
	char    *spelling; // owned; NULL marks a free slot
	size_t   len;
	uint32_t hash;
	bool     found;
	objc_sig sig;
} cache_entry;

static clj_lock     cache_lock = CLJ_LOCK_INIT;
static cache_entry *cache;
static size_t       cache_cap, cache_len;

static uint32_t spelling_hash(Class cls, const char *s, size_t len) {
	uint32_t h = (uint32_t)((uintptr_t)cls >> 4) * 2654435761u;
	for (size_t i = 0; i < len; i++) h = clj_hash_combine(h, (uint32_t)(unsigned char)s[i]);
	return h;
}

static cache_entry *cache_slot(Class cls, const char *s, size_t len, uint32_t h) {
	size_t mask = cache_cap - 1;
	for (size_t i = h & mask;; i = (i + 1) & mask) {
		cache_entry *e = &cache[i];
		if (!e->spelling) return e;
		if (e->hash == h && e->cls == cls && e->len == len && memcmp(e->spelling, s, len) == 0) return e;
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
		if (old[i].spelling) *cache_slot(old[i].cls, old[i].spelling, old[i].len, old[i].hash) = old[i];
	}
	free(old);
}

// Walks cls and its superclasses kebabing every selector; the first match wins, as dispatch would.
static bool resolve(Class cls, const char *spelling, size_t len, objc_sig *out) {
	char buf[512];
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

static bool lookup(Class cls, const char *spelling, size_t len, objc_sig *out) {
	clj_lock_lock(&cache_lock);
	if (cache_len * 2 >= cache_cap) cache_grow();
	uint32_t     h = spelling_hash(cls, spelling, len);
	cache_entry *e = cache_slot(cls, spelling, len, h);
	if (!e->spelling) {
		char *copy = malloc(len + 1);
		if (!copy) clj_fatal("out of memory filling the objc selector cache");
		memcpy(copy, spelling, len);
		copy[len] = '\0';
		e->cls = cls;
		e->spelling = copy;
		e->len = len;
		e->hash = h;
		memset(&e->sig, 0, sizeof e->sig);
		e->found = resolve(cls, spelling, len, &e->sig);
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
	bool     found;
	if (raw) {
		Method m = class_getInstanceMethod(cls, sel_registerName(spelling));
		found = m && signature_of(m, &sig);
		if (!m) return no_such_selector(cls, spelling, len, true);
	} else {
		found = lookup(cls, spelling, len, &sig);
		if (!found && !sig.sel) return no_such_selector(cls, spelling, len, false);
	}
	if (!found) return clj_throw_msg("Selector %.*s on %s has a shape the bridge cannot call: a struct, a long double, too many arguments, or float and double mixed", (int)len, spelling, class_getName(cls));
	if (nargs != sig.nargs) return clj_throw_msg("Selector %.*s takes %u argument(s), got %u", (int)len, spelling, sig.nargs, nargs);

	// Off a coroutine nothing will switch, so the call keeps its own pool; on one the slice's pool is pushed
	// once and drained by clj_coro_switch_out.
	bool  own_pool = !clj_coro_in_coroutine();
	void *pool = own_pool ? objc_autoreleasePoolPush() : NULL;
	if (!own_pool && !clj_objc_pool_token) clj_objc_pool_token = objc_autoreleasePoolPush();

	long long ints[CLJ_OBJC_MAX_INT_ARGS] = {0};
	double    dbls[CLJ_OBJC_MAX_FP_ARGS] = {0};
	float     flts[CLJ_OBJC_MAX_FP_ARGS] = {0};
	unsigned  ni = 0, nf = 0;
	clj_value out = CLJ_NIL;
	for (uint32_t i = 0; i < sig.nargs && out != CLJ_THROWN; i++) {
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
		if (sig.fp_is_float) {
			switch (sig.ret) {
			case 'f': out = clj_double_new(((send_f_f)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(flts))); break;
			case 'd': out = clj_double_new(((send_d_f)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(flts))); break;
			case 'v': ((send_v_f)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(flts)), out = CLJ_NIL; break;
			default: out = from_int_return(((send_i_f)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(flts)), sig.ret, sig.owned); break;
			}
		} else {
			switch (sig.ret) {
			case 'f': out = clj_double_new(((send_f_d)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(dbls))); break;
			case 'd': out = clj_double_new(((send_d_d)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(dbls))); break;
			case 'v': ((send_v_d)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(dbls)), out = CLJ_NIL; break;
			default: out = from_int_return(((send_i_d)objc_msgSend)(self, sel, INT_ARGS(ints), FP_ARGS(dbls)), sig.ret, sig.owned); break;
			}
		}
	}
	if (own_pool) objc_autoreleasePoolPop(pool);
	return out;
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

void clj_objc_builtins_install(void) {
	clj_builtin_bind("objc-class", b_objc_class, 1, 1);
	clj_builtin_bind("objc-send", b_objc_send, 2, CLJ_ARITY_ANY);
	clj_builtin_bind("objc-object?", b_objc_object_p, 1, 1);
	clj_builtin_bind("objc-kebab*", b_objc_kebab, 1, 1);
}
