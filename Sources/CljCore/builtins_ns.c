// @ai-generated(guided)
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "clj/hosttype.h"

static clj_value ns_symbol_arg(const char *what, clj_value v) {
	if (clj_is_symbol(v) && clj_is_nil(clj_symbol_ns(v))) return v;
	return clj_throw_msg("%s expects an unqualified symbol, got: %s", what, clj_type_name(v));
}

// A namespace from a namespace or its symbol; throws "No namespace: x found".
static clj_value the_ns(clj_value v) {
	if (clj_is_ns(v)) return v;
	if (!clj_is_symbol(v)) return clj_throw_msg("%s cannot be cast to a namespace", clj_type_name(v));
	clj_value ns = clj_ns_find(v);
	if (clj_is_nil(ns)) return clj_throw_msg("No namespace: %s found", clj_string_bytes(clj_symbol_name(v)));
	return ns;
}

static clj_value b_in_ns(const clj_value *args, size_t n) {
	(void)n;
	if (ns_symbol_arg("in-ns", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	clj_value ns = clj_ns_find_or_create(args[0]);
	clj_ns_set_current(ns);
	return ns;
}

static clj_value b_create_ns(const clj_value *args, size_t n) {
	(void)n;
	if (ns_symbol_arg("create-ns", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	return clj_ns_find_or_create(args[0]);
}

static clj_value b_find_ns(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_symbol(args[0])) return clj_throw_msg("find-ns expects a symbol, got: %s", clj_type_name(args[0]));
	return clj_ns_find(args[0]);
}

static clj_value b_the_ns(const clj_value *args, size_t n) {
	(void)n;
	return the_ns(args[0]);
}

static clj_value b_ns_name(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	return ns == CLJ_THROWN ? CLJ_THROWN : clj_retain(clj_ns_name(ns));
}

static clj_value b_all_ns(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_ns_all();
}

typedef struct {
	clj_value out;
	bool      public_only;
} collect_ctx;

static bool collect_public(clj_value key, clj_value val, void *ctx) {
	collect_ctx *c = ctx;
	if (!c->public_only || !clj_var_is_private(val)) c->out = clj_map_assoc(c->out, key, val);
	return true;
}

static clj_value ns_map_of(clj_value ns_arg, bool with_refers, bool public_only) {
	clj_value ns = the_ns(ns_arg);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	collect_ctx c = {clj_map_empty(), public_only};
	if (with_refers) clj_map_each(clj_ns_refers(ns), collect_public, &c);
	clj_map_each(clj_ns_mappings(ns), collect_public, &c);
	return c.out;
}

static bool collect_import(clj_value key, clj_value var, void *ctx);

// An import maps to its type, as the JVM's ns-map maps a class name to the class.
static clj_value b_ns_map(const clj_value *args, size_t n) {
	(void)n;
	clj_value m = ns_map_of(args[0], true, false);
	if (m != CLJ_THROWN) clj_map_each(clj_ns_imports(the_ns(args[0])), collect_import, &m);
	return m;
}

static clj_value b_ns_publics(const clj_value *args, size_t n) {
	(void)n;
	return ns_map_of(args[0], false, true);
}

static clj_value b_ns_interns(const clj_value *args, size_t n) {
	(void)n;
	return ns_map_of(args[0], false, false);
}

static clj_value b_ns_refers(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	return ns == CLJ_THROWN ? CLJ_THROWN : clj_retain(clj_ns_refers(ns));
}

static clj_value b_ns_aliases(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	return ns == CLJ_THROWN ? CLJ_THROWN : clj_retain(clj_ns_aliases(ns));
}

static clj_value b_alias(const clj_value *args, size_t n) {
	(void)n;
	if (ns_symbol_arg("alias", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	clj_value target = the_ns(args[1]);
	if (target == CLJ_THROWN) return CLJ_THROWN;
	clj_value ns = clj_ns_current();
	clj_value existing = clj_map_get(clj_ns_aliases(ns), args[0], CLJ_NIL);
	if (!clj_is_nil(existing) && existing != target) {
		return clj_throw_msg("Alias %s already exists in namespace %s, aliasing %s", clj_string_bytes(clj_symbol_name(args[0])),
		                     clj_string_bytes(clj_symbol_name(clj_ns_name(ns))), clj_string_bytes(clj_symbol_name(clj_ns_name(existing))));
	}
	clj_ns_alias(ns, args[0], target);
	return CLJ_NIL;
}

static clj_value b_remove_ns(const clj_value *args, size_t n) {
	(void)n;
	if (ns_symbol_arg("remove-ns", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	if (clj_equals(args[0], clj_ns_name(clj_ns_core()))) return clj_throw_msg("Cannot remove clojure namespace");
	return clj_ns_remove(args[0]);
}

static clj_value b_ns_unalias(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_symbol(args[1])) return clj_throw_msg("ns-unalias expects a symbol, got: %s", clj_type_name(args[1]));
	clj_ns_unalias(ns, args[1]);
	return CLJ_NIL;
}

static clj_value b_ns_unmap(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	if (ns_symbol_arg("ns-unmap", args[1]) == CLJ_THROWN) return CLJ_THROWN;
	clj_ns_unmap(ns, args[1]);
	return CLJ_NIL;
}

// The env argument is a macro's &env: a name it binds is a local, and a local shadows the var.
static bool shadowed_by_env(clj_value env, clj_value sym) {
	if (clj_is_nil(env)) return false;
	clj_value v = clj_get(env, sym, CLJ_UNBOUND);
	if (v == CLJ_THROWN) return false;
	bool found = v != CLJ_UNBOUND;
	clj_release(v);
	return found;
}

static clj_value b_ns_resolve(const clj_value *args, size_t n) {
	clj_value ns = the_ns(args[0]);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	clj_value sym = args[n - 1];
	if (!clj_is_symbol(sym)) return clj_throw_msg("ns-resolve expects a symbol, got: %s", clj_type_name(sym));
	if (n == 3 && shadowed_by_env(args[1], sym)) return CLJ_NIL;
	return clj_ns_resolve(ns, sym);
}

static clj_value b_resolve(const clj_value *args, size_t n) {
	clj_value sym = args[n - 1];
	if (!clj_is_symbol(sym)) return clj_throw_msg("resolve expects a symbol, got: %s", clj_type_name(sym));
	if (n == 2 && shadowed_by_env(args[0], sym)) return CLJ_NIL;
	return clj_ns_resolve(clj_ns_current(), sym);
}

static clj_value b_ns_refer_star(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	if (ns_symbol_arg("refer", args[1]) == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_var(args[2])) return clj_throw_msg("refer expects a var, got: %s", clj_type_name(args[2]));
	clj_ns_refer(ns, args[1], args[2]);
	return CLJ_NIL;
}

static clj_value b_ns_exclude_star(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_nil(args[1]) && !clj_is_set(args[1])) return clj_throw_msg("ns-exclude* expects a set, got: %s", clj_type_name(args[1]));
	clj_ns_set_excludes(ns, args[1]);
	return CLJ_NIL;
}

static clj_value b_intern(const clj_value *args, size_t n) {
	clj_value ns = the_ns(args[0]);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	if (ns_symbol_arg("intern", args[1]) == CLJ_THROWN) return CLJ_THROWN;
	clj_value bare = clj_symbol_new(CLJ_NIL, clj_symbol_name(args[1]));
	clj_value var = clj_ns_intern(ns, bare);
	clj_release(bare);
	if (n == 3) clj_var_bind_root(var, args[2]);
	clj_value m = clj_meta(args[1]);
	if (clj_is_nil(m)) m = clj_map_empty();
	m = clj_map_assoc(m, clj_keyword_from_cstr("ns"), clj_var_ns(var));
	m = clj_map_assoc(m, clj_keyword_from_cstr("name"), clj_var_name(var));
	clj_var_set_meta(var, m);
	clj_release(m);
	return clj_retain(var);
}

static clj_value var_arg(const char *what, clj_value v) {
	if (clj_is_var(v)) return v;
	return clj_throw_msg("%s expects a var, got: %s", what, clj_type_name(v));
}

static clj_value b_var_get(const clj_value *args, size_t n) {
	(void)n;
	if (var_arg("var-get", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	return clj_var_deref(args[0]);
}

static clj_value b_var_set(const clj_value *args, size_t n) {
	(void)n;
	if (var_arg("var-set", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	return clj_var_set(args[0], args[1]);
}

static clj_value b_push_thread_bindings(const clj_value *args, size_t n) {
	(void)n;
	return clj_var_push_bindings(args[0]);
}

static clj_value b_pop_thread_bindings(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_var_pop_bindings();
}

static clj_value b_get_thread_bindings(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_var_get_thread_bindings();
}

static clj_value b_bound_p(const clj_value *args, size_t n) {
	for (size_t i = 0; i < n; i++) {
		if (var_arg("bound?", args[i]) == CLJ_THROWN) return CLJ_THROWN;
		if (!clj_var_is_bound(args[i]) && !clj_var_is_thread_bound(args[i])) return CLJ_FALSE;
	}
	return CLJ_TRUE;
}

static clj_value b_thread_bound_p(const clj_value *args, size_t n) {
	for (size_t i = 0; i < n; i++) {
		if (var_arg("thread-bound?", args[i]) == CLJ_THROWN) return CLJ_THROWN;
		if (!clj_var_is_thread_bound(args[i])) return CLJ_FALSE;
	}
	return CLJ_TRUE;
}

static clj_value b_var_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_var(args[0]));
}

static clj_value b_ns_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_ns(args[0]));
}

// The root, never a thread binding: a rebind of the root under a binding must not write the bound value back.
static clj_value var_root_arg(const char *who, clj_value v) {
	if (var_arg(who, v) == CLJ_THROWN) return CLJ_THROWN;
	clj_value root = clj_var_root(v);
	if (root == CLJ_UNBOUND) {
		clj_value ns = clj_symbol_name(clj_var_ns(v)), name = clj_symbol_name(clj_var_name(v));
		return clj_throw_msg("Unbound var: #'%s/%s", clj_string_bytes(ns), clj_string_bytes(name));
	}
	return clj_retain(root);
}

static clj_value b_var_root(const clj_value *args, size_t n) {
	(void)n;
	return var_root_arg("var-root*", args[0]);
}

static clj_value b_alter_var_root(const clj_value *args, size_t n) {
	clj_value root = var_root_arg("alter-var-root", args[0]);
	if (root == CLJ_THROWN) return CLJ_THROWN;
	clj_value *all = calloc(n - 1, sizeof *all);
	if (!all) clj_fatal("out of memory");
	all[0] = root;
	memcpy(all + 1, args + 2, (n - 2) * sizeof *all);
	clj_value v = clj_invoke(args[1], all, n - 1);
	free(all);
	clj_release(root);
	if (v == CLJ_THROWN) return CLJ_THROWN;
	clj_var_bind_root(args[0], v);
	return v;
}

static clj_value b_load_file(const clj_value *args, size_t n) {
	(void)n;
	return clj_load_file(args[0]);
}

static clj_value b_load_resource_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("load-resource* expects a string, got: %s", clj_type_name(args[0]));
	return clj_load_resource_path(clj_string_bytes(args[0]));
}

// a.b-c → "a/b_c"
static clj_value b_lib_path_star(const clj_value *args, size_t n) {
	(void)n;
	clj_value name;
	if (clj_is_symbol(args[0])) name = clj_symbol_name(args[0]);
	else if (clj_is_string(args[0])) name = args[0];
	else return clj_throw_msg("lib-path* expects a symbol or string, got: %s", clj_type_name(args[0]));
	uint32_t len = clj_string_len(name);
	char    *text = malloc(len + 1);
	if (!text) clj_fatal("out of memory");
	memcpy(text, clj_string_bytes(name), len + 1);
	for (uint32_t i = 0; i < len; i++) {
		if (text[i] == '.') text[i] = '/';
		else if (text[i] == '-') text[i] = '_';
	}
	clj_value s = clj_string_new(text, len);
	free(text);
	return s;
}

static clj_value b_read_string(const clj_value *args, size_t n) {
	clj_value s = args[n - 1];
	if (clj_is_nil(s)) return clj_throw_msg("read-string of nil");
	if (!clj_is_string(s)) return clj_throw_msg("read-string expects a string, got: %s", clj_type_name(s));
	clj_reader r;
	clj_reader_init(&r, clj_string_bytes(s), clj_string_len(s));
	clj_reader_use_namespaces(&r);
	clj_value       form;
	clj_read_status st = clj_read(&r, &form);
	if (st == CLJ_READ_EOF) return clj_throw_msg("EOF while reading");
	if (st == CLJ_READ_ERROR) return clj_throw_msg("%s", clj_reader_message(&r));
	return form;
}

static clj_value incomplete_kw;

static bool is_trim_byte(char c) { return (unsigned char)c <= ' '; }

// [form rest consumed], nil for no form, :incomplete when s ends inside one; consumed trims as String.trim does.
static clj_value b_read_prefix_star(const clj_value *args, size_t n) {
	(void)n;
	clj_value s = args[0];
	if (!clj_is_string(s)) return clj_throw_msg("read-prefix* expects a string, got: %s", clj_type_name(s));
	const char *bytes = clj_string_bytes(s);
	size_t      len = clj_string_len(s);
	clj_reader  r;
	clj_reader_init(&r, bytes, len);
	clj_reader_use_namespaces(&r);
	clj_value       form;
	clj_read_status st = clj_read(&r, &form);
	if (st == CLJ_READ_EOF) return CLJ_NIL;
	if (st == CLJ_READ_ERROR) {
		const char *msg = clj_reader_message(&r);
		if (strncmp(msg, "EOF while reading", 17) == 0) return incomplete_kw;
		return clj_throw_msg("%s", msg);
	}
	size_t from = 0, to = r.pos;
	while (from < to && is_trim_byte(bytes[from])) from++;
	while (to > from && is_trim_byte(bytes[to - 1])) to--;
	clj_value items[3] = {form, clj_string_new(bytes + r.pos, len - r.pos), clj_string_new(bytes + from, to - from)};
	clj_value out = clj_vector_from_array(items, 3);
	for (int i = 0; i < 3; i++) clj_release(items[i]);
	return out;
}

static clj_value b_mangle_star(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("mangle* expects a string, got: %s", clj_type_name(args[0]));
	char     *m = clj_mangle(NULL, clj_string_bytes(args[0]));
	clj_value out = clj_string_from_cstr(m);
	free(m);
	return out;
}

static clj_value b_eval(const clj_value *args, size_t n) {
	(void)n;
	return clj_eval(args[0], NULL);
}

static clj_value b_load_string(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("load-string expects a string, got: %s", clj_type_name(args[0]));
	return clj_load_source(clj_string_bytes(args[0]), clj_string_len(args[0]), CLJ_NIL);
}

static clj_value b_special_symbol_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_symbol(args[0]) && clj_is_special_symbol(args[0]));
}

static clj_value b_out_capture_push(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	clj_output_push_capture();
	return CLJ_NIL;
}

static clj_value b_out_capture_pop(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_output_pop_capture();
}

// ---- import: a short name for a type a qualified name reaches (design §3 «"Наш хост" — это C-ядро»)

static bool is_type_value(clj_value v) { return clj_is_type(v) || clj_is_protocol(v) || clj_is_host_type(v); }

// A JVM class has no counterpart here; a bridge answering java.lang.Object would be ObjC's root class by coincidence.
static bool is_jvm_package(const char *pkg, size_t len) {
	static const char *const roots[] = {"java", "javax", "jdk", "sun", "clojure.lang"};
	for (size_t i = 0; i < sizeof roots / sizeof *roots; i++) {
		size_t n = strlen(roots[i]);
		if (len >= n && memcmp(pkg, roots[i], n) == 0 && (len == n || pkg[n] == '.')) return true;
	}
	return false;
}

// The JVM spells a record of namespace my-app.core as my_app.core.Name.
static clj_value program_type_var(clj_value pkg, clj_value name) {
	clj_value ns = clj_ns_find(pkg);
	if (clj_is_nil(ns)) {
		clj_value   text = clj_symbol_name(pkg);
		uint32_t    len = clj_string_len(text);
		char       *dashed = malloc(len + 1);
		if (!dashed) clj_fatal("out of memory");
		memcpy(dashed, clj_string_bytes(text), len + 1);
		for (uint32_t i = 0; i < len; i++) {
			if (dashed[i] == '_') dashed[i] = '-';
		}
		clj_value sym = clj_symbol_from_cstr(dashed);
		free(dashed);
		ns = clj_ns_find(sym);
		clj_release(sym);
		if (clj_is_nil(ns)) return CLJ_NIL;
	}
	clj_value var = clj_map_get(clj_ns_mappings(ns), name, CLJ_NIL);
	return !clj_is_nil(var) && is_type_value(clj_var_root(var)) ? var : CLJ_NIL;
}

// Owned type, or nil when this bridge does not reach pkg/name.
typedef clj_value (*type_bridge)(const char *pkg, size_t plen, const char *name, size_t nlen);

// The host's resolver (Swift): Pkg/Name means here what the qualified symbol and a catch clause mean.
static clj_value host_resolver_bridge(const char *pkg, size_t plen, const char *name, size_t nlen) {
	char *text = malloc(plen + nlen + 2);
	if (!text) clj_fatal("out of memory");
	memcpy(text, pkg, plen);
	text[plen] = '/';
	memcpy(text + plen + 1, name, nlen + 1);
	clj_value t = clj_host_type_named(text, plen + 1 + nlen);
	free(text);
	return t;
}

// The ObjC runtime by class name: its classes share one namespace, so the package does not narrow it.
static clj_value objc_runtime_bridge(const char *pkg, size_t plen, const char *name, size_t nlen) {
	(void)pkg, (void)plen, (void)nlen;
	return clj_objc_class(name);
}

// In order; C has no type values yet (design §5 «C — уровень 0»: a struct by value is refused), so no C bridge.
static const type_bridge bridges[] = {host_resolver_bridge, objc_runtime_bridge};

// The var holding the type pkg.name names, interning a bridge's answer as pkg/name; nil when nothing reaches it.
static clj_value import_type_var(clj_value pkg, clj_value name) {
	clj_value var = program_type_var(pkg, name);
	if (!clj_is_nil(var)) return var;
	clj_value   ptext = clj_symbol_name(pkg), ntext = clj_symbol_name(name);
	const char *p = clj_string_bytes(ptext);
	size_t      plen = clj_string_len(ptext);
	if (is_jvm_package(p, plen)) return CLJ_NIL;
	clj_value t = CLJ_NIL;
	for (size_t i = 0; i < sizeof bridges / sizeof *bridges && clj_is_nil(t); i++) t = bridges[i](p, plen, clj_string_bytes(ntext), clj_string_len(ntext));
	if (clj_is_nil(t)) return CLJ_NIL;
	var = clj_ns_intern(clj_ns_find_or_create(pkg), name);
	clj_value root = clj_var_root(var);
	if (root != CLJ_UNBOUND && !is_type_value(root) && !clj_is_objc_object(root)) {
		clj_release(t);
		return clj_throw_msg("Cannot import %s.%s: %s/%s is a var holding a %s", p, clj_string_bytes(ntext), p, clj_string_bytes(ntext), clj_type_name(root));
	}
	clj_var_bind_root(var, t);
	clj_release(t);
	return var;
}

static clj_value import_into(clj_value ns, clj_value pkg, clj_value name, clj_value *last) {
	if (!clj_is_symbol(name) || !clj_is_nil(clj_symbol_ns(name))) return clj_throw_msg("import expects an unqualified class name, got: %s", clj_type_name(name));
	clj_value var = import_type_var(pkg, name);
	if (var == CLJ_THROWN) return CLJ_THROWN;
	// A name nothing reaches is left unmapped: its use is the error, where it is used (design §3).
	if (clj_is_nil(var)) return CLJ_NIL;
	clj_value prior = clj_map_get(clj_ns_mappings(ns), name, CLJ_NIL);
	if (clj_is_nil(prior)) prior = clj_map_get(clj_ns_refers(ns), name, CLJ_NIL);
	if (clj_is_nil(prior)) prior = clj_map_get(clj_ns_imports(ns), name, CLJ_NIL);
	// The core's own fallback is not a mapping: an import shadows it, since the JVM's core has no such var.
	if (!clj_is_nil(prior) && prior != var) {
		clj_value text = clj_pr_str(prior);
		if (text == CLJ_THROWN) return CLJ_THROWN;
		clj_value r = clj_throw_msg("%s already refers to: %s in namespace: %s", clj_string_bytes(clj_symbol_name(name)), clj_string_bytes(text),
		                            clj_string_bytes(clj_symbol_name(clj_ns_name(ns))));
		clj_release(text);
		return r;
	}
	clj_ns_import(ns, name, var);
	*last = clj_var_root(var);
	return CLJ_NIL;
}

// (import* specs): each spec a symbol pkg.Name or a sequential (pkg Name*). The last type imported, or nil.
static clj_value b_import_star(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = clj_ns_current(), last = CLJ_NIL;
	clj_value specs = clj_seq(args[0]);
	if (specs == CLJ_THROWN) return CLJ_THROWN;
	for (clj_value s = specs; !clj_is_nil(s);) {
		clj_value spec = clj_first(s), r = CLJ_NIL;
		if (clj_is_symbol(spec) && clj_is_nil(clj_symbol_ns(spec))) {
			clj_value   text = clj_symbol_name(spec);
			const char *bytes = clj_string_bytes(text);
			const char *dot = strrchr(bytes, '.');
			// A name with no package reaches no namespace, and every bridge takes a package.
			if (dot && dot != bytes && dot[1]) {
				char *pkg = malloc((size_t)(dot - bytes) + 1);
				if (!pkg) clj_fatal("out of memory");
				memcpy(pkg, bytes, (size_t)(dot - bytes));
				pkg[dot - bytes] = '\0';
				clj_value psym = clj_symbol_from_cstr(pkg), nsym = clj_symbol_from_cstr(dot + 1);
				free(pkg);
				r = import_into(ns, psym, nsym, &last);
				clj_release(psym);
				clj_release(nsym);
			}
		} else if (clj_is_vector(spec) || clj_is_seq(spec)) {
			clj_value items = clj_seq(spec);
			clj_value pkg = clj_is_nil(items) || items == CLJ_THROWN ? CLJ_NIL : clj_first(items);
			if (items == CLJ_THROWN || pkg == CLJ_THROWN) r = CLJ_THROWN;
			else if (!clj_is_symbol(pkg) || !clj_is_nil(clj_symbol_ns(pkg))) r = clj_throw_msg("import expects a package symbol first, got: %s", clj_type_name(pkg));
			else {
				clj_value t = clj_next(items);
				while (!clj_is_nil(t) && t != CLJ_THROWN && r != CLJ_THROWN) {
					clj_value name = clj_first(t);
					r = name == CLJ_THROWN ? CLJ_THROWN : import_into(ns, pkg, name, &last);
					clj_release(name);
					clj_value nx = r == CLJ_THROWN ? CLJ_NIL : clj_next(t);
					clj_release(t);
					t = nx;
				}
				if (t == CLJ_THROWN) r = CLJ_THROWN;
			}
			clj_release(pkg);
			clj_release(items);
		} else {
			r = clj_throw_msg("import expects a symbol or a list (package Name*), got: %s", clj_type_name(spec));
		}
		clj_release(spec);
		if (r == CLJ_THROWN) {
			clj_release(s);
			return CLJ_THROWN;
		}
		clj_value nx = clj_next(s);
		clj_release(s);
		s = nx;
		if (s == CLJ_THROWN) return CLJ_THROWN;
	}
	return clj_retain(last);
}

static bool collect_import(clj_value key, clj_value var, void *ctx) {
	clj_value *out = ctx;
	*out = clj_map_assoc(*out, key, clj_var_root(var));
	return true;
}

static clj_value b_ns_imports(const clj_value *args, size_t n) {
	(void)n;
	clj_value ns = the_ns(args[0]);
	if (ns == CLJ_THROWN) return CLJ_THROWN;
	clj_value out = clj_map_empty();
	clj_map_each(clj_ns_imports(ns), collect_import, &out);
	return out;
}

#define ANY CLJ_ARITY_ANY

static const struct {
	const char   *name;
	clj_native_fn fn;
	uint32_t      min, max;
} entries[] = {
	{"in-ns", b_in_ns, 1, 1},           {"create-ns", b_create_ns, 1, 1},   {"find-ns", b_find_ns, 1, 1},     {"the-ns", b_the_ns, 1, 1},
	{"ns-name", b_ns_name, 1, 1},       {"all-ns", b_all_ns, 0, 0},         {"ns-map", b_ns_map, 1, 1},       {"ns-publics", b_ns_publics, 1, 1},
	{"ns-interns", b_ns_interns, 1, 1}, {"ns-refers", b_ns_refers, 1, 1},   {"ns-aliases", b_ns_aliases, 1, 1}, {"alias", b_alias, 2, 2},
	{"remove-ns", b_remove_ns, 1, 1},   {"ns-unalias", b_ns_unalias, 2, 2},
	{"ns-unmap", b_ns_unmap, 2, 2},     {"ns-resolve", b_ns_resolve, 2, 3}, {"resolve", b_resolve, 1, 2},     {"ns-refer*", b_ns_refer_star, 3, 3},
	{"ns-exclude*", b_ns_exclude_star, 2, 2}, {"intern", b_intern, 2, 3},   {"var-get", b_var_get, 1, 1},     {"var-set", b_var_set, 2, 2},
	{"push-thread-bindings", b_push_thread_bindings, 1, 1}, {"pop-thread-bindings", b_pop_thread_bindings, 0, 0},
	{"get-thread-bindings", b_get_thread_bindings, 0, 0}, {"bound?", b_bound_p, 0, ANY}, {"thread-bound?", b_thread_bound_p, 0, ANY},
	{"var-root*", b_var_root, 1, 1}, {"var?", b_var_p, 1, 1},            {"ns?", b_ns_p, 1, 1},              {"alter-var-root", b_alter_var_root, 2, ANY},
	{"load-file", b_load_file, 1, 1},   {"load-resource*", b_load_resource_star, 1, 1}, {"lib-path*", b_lib_path_star, 1, 1},
	{"read-string", b_read_string, 1, 2}, {"eval", b_eval, 1, 1},           {"load-string", b_load_string, 1, 1},
	{"special-symbol?", b_special_symbol_p, 1, 1}, {"out-capture-push*", b_out_capture_push, 0, 0}, {"out-capture-pop*", b_out_capture_pop, 0, 0},
	{"import*", b_import_star, 1, 1},  {"ns-imports", b_ns_imports, 1, 1},  {"read-prefix*", b_read_prefix_star, 1, 1},
	{"mangle*", b_mangle_star, 1, 1},
};

void clj_ns_builtins_install(void) {
	incomplete_kw = clj_keyword_from_cstr("incomplete");
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) clj_builtin_bind(entries[i].name, entries[i].fn, entries[i].min, entries[i].max);
	clj_core_mark_extension("ns?"); // the one name above that the JVM's clojure.core has not
}
