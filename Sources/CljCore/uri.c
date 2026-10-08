// @ai-generated(solo)
#include <string.h>

#include "clj/core.h"
#include "clj/uri.h"

static void uri_each_child(void *self, clj_visitor visit, void *ctx) { visit(((clj_uri *)self)->text.v, ctx); }

static uint32_t uri_hash(void *self) { return clj_hash_slow(((clj_uri *)self)->text.v); }

static bool uri_equals(void *self, clj_value other) { return clj_is_uri(other) && clj_equals(((clj_uri *)self)->text.v, clj_uri_text(other)); }

enum { K_SCHEME, K_USER_INFO, K_HOST, K_PORT, K_PATH, K_QUERY, K_FRAGMENT, K_COUNT };
static const char *const key_names[K_COUNT] = {"scheme", "user-info", "host", "port", "path", "query", "fragment"};
static clj_value         keys[K_COUNT];

static clj_value span_string(const clj_uri *u, clj_uri_span sp) {
	if (sp.len < 0) return CLJ_NIL;
	return clj_string_new(clj_string_bytes(u->text.v) + sp.from, (size_t)sp.len);
}

static clj_value uri_lookup(clj_value self, clj_value key, clj_value not_found) {
	const clj_uri *u = clj_uri_of(self);
	if (key == keys[K_SCHEME]) return span_string(u, u->scheme);
	if (key == keys[K_USER_INFO]) return span_string(u, u->user_info);
	if (key == keys[K_HOST]) return span_string(u, u->host);
	if (key == keys[K_PORT]) return u->port < 0 ? CLJ_NIL : clj_fixnum((intptr_t)u->port);
	if (key == keys[K_PATH]) return span_string(u, u->path);
	if (key == keys[K_QUERY]) return span_string(u, u->query);
	if (key == keys[K_FRAGMENT]) return span_string(u, u->fragment);
	return clj_retain(not_found);
}

const clj_type clj_uri_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "URI",
	.core_bits = CLJ_CORE_LOOKUP,
	.each_child = uri_each_child,
	.hash = uri_hash,
	.equals = uri_equals,
	.lookup = uri_lookup,
};

// ---- RFC 3986, with java.net.URI's leniency for non-ASCII "other" characters

static bool is_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static bool is_hex(unsigned char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// Unreserved, sub-delims, a percent escape, a non-ASCII byte, or one of extra.
static bool valid_span(const unsigned char *s, int32_t from, int32_t len, const char *extra) {
	for (int32_t i = from; i < from + len; i++) {
		unsigned char c = s[i];
		if (c >= 0x80 || is_alpha(c) || is_digit(c)) continue;
		if (c <= ' ' || c == 0x7f) return false;
		if (strchr("-._~!$&'()*+,;=", c) || strchr(extra, c)) continue;
		if (c == '%' && i + 2 < from + len && is_hex(s[i + 1]) && is_hex(s[i + 2])) {
			i += 2;
			continue;
		}
		return false;
	}
	return true;
}

static int32_t find(const unsigned char *s, int32_t from, int32_t to, unsigned char c) {
	for (int32_t i = from; i < to; i++) {
		if (s[i] == c) return i;
	}
	return -1;
}

static int32_t find_last(const unsigned char *s, int32_t from, int32_t to, unsigned char c) {
	for (int32_t i = to - 1; i >= from; i--) {
		if (s[i] == c) return i;
	}
	return -1;
}

static const clj_uri_span absent = {0, -1};

// host[:port] or [ip-literal][:port] over [from, to).
static bool parse_host_port(const unsigned char *s, int32_t from, int32_t to, clj_uri *u) {
	int32_t port_at = -1;
	if (from < to && s[from] == '[') {
		int32_t close = find(s, from, to, ']');
		if (close < 0 || close == from + 1 || !valid_span(s, from + 1, close - from - 1, ":")) return false;
		u->host = (clj_uri_span){from, close + 1 - from};
		if (close + 1 < to) {
			if (s[close + 1] != ':') return false;
			port_at = close + 2;
		}
	} else {
		int32_t colon = find_last(s, from, to, ':');
		int32_t end = colon < 0 ? to : colon;
		if (!valid_span(s, from, end - from, "")) return false;
		// An empty host is absent, as java.net.URI answers null for file:///x.
		u->host = end > from ? (clj_uri_span){from, end - from} : absent;
		if (colon >= 0) port_at = colon + 1;
	}
	if (port_at >= 0 && port_at < to) {
		int64_t port = 0;
		for (int32_t i = port_at; i < to; i++) {
			if (!is_digit(s[i])) return false;
			port = port * 10 + (s[i] - '0');
			if (port > INT32_MAX) return false;
		}
		u->port = port;
	}
	return true;
}

static bool parse(const unsigned char *s, int32_t n, clj_uri *u) {
	u->scheme = u->user_info = u->host = u->query = u->fragment = absent;
	u->port = -1;
	int32_t i = 0;
	if (n > 0 && is_alpha(s[0])) {
		int32_t j = 1;
		while (j < n && (is_alpha(s[j]) || is_digit(s[j]) || s[j] == '+' || s[j] == '-' || s[j] == '.')) j++;
		if (j < n && s[j] == ':') {
			u->scheme = (clj_uri_span){0, j};
			i = j + 1;
		}
	}
	int32_t end = n;
	int32_t hash = find(s, i, end, '#');
	if (hash >= 0) {
		u->fragment = (clj_uri_span){hash + 1, end - hash - 1};
		end = hash;
	}
	int32_t q = find(s, i, end, '?');
	if (q >= 0) {
		u->query = (clj_uri_span){q + 1, end - q - 1};
		end = q;
	}
	if (end - i >= 2 && s[i] == '/' && s[i + 1] == '/') {
		int32_t a = i + 2;
		int32_t slash = find(s, a, end, '/');
		int32_t aend = slash < 0 ? end : slash;
		int32_t at = find(s, a, aend, '@');
		int32_t hp = a;
		if (at >= 0) {
			if (!valid_span(s, a, at - a, ":")) return false;
			u->user_info = (clj_uri_span){a, at - a};
			hp = at + 1;
		}
		if (!parse_host_port(s, hp, aend, u)) return false;
		u->path = (clj_uri_span){aend, end - aend};
	} else {
		u->path = (clj_uri_span){i, end - i};
		// A relative reference's first segment takes no colon, or it would read as a scheme (RFC 3986 §4.2).
		if (u->scheme.len < 0) {
			int32_t slash = find(s, i, end, '/');
			if (find(s, i, slash < 0 ? end : slash, ':') >= 0) return false;
		}
	}
	return valid_span(s, u->path.from, u->path.len, ":@/") && (u->query.len < 0 || valid_span(s, u->query.from, u->query.len, ":@/?")) &&
	       (u->fragment.len < 0 || valid_span(s, u->fragment.from, u->fragment.len, ":@/?"));
}

clj_value clj_uri_parse(clj_value text) {
	CLJ_ASSERT(clj_is_string(text), "a URI parses from a string");
	size_t len = clj_string_len(text);
	if (len > INT32_MAX) return CLJ_NIL;
	clj_uri parsed;
	if (!parse((const unsigned char *)clj_string_bytes(text), (int32_t)len, &parsed)) return CLJ_NIL;
	clj_uri *u = clj_alloc(&clj_uri_type, sizeof *u);
	u->scheme = parsed.scheme;
	u->user_info = parsed.user_info;
	u->host = parsed.host;
	u->path = parsed.path;
	u->query = parsed.query;
	u->fragment = parsed.fragment;
	u->port = parsed.port;
	clj_slot_init(&u->h, &u->text, clj_retain(text));
	return clj_from_ptr(u);
}

// ---- builtins

static clj_value b_uri_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_uri(args[0]));
}

static clj_value b_parse_uri(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_string(args[0])) return clj_throw_msg("%s cannot be cast to a string", clj_type_name(args[0]));
	return clj_uri_parse(args[0]);
}

void clj_uri_builtins_install(void) {
	for (int i = 0; i < K_COUNT; i++) keys[i] = clj_keyword_from_cstr(key_names[i]);
	clj_builtin_bind("uri?", b_uri_p, 1, 1);
	clj_builtin_bind_extension("parse-uri", b_parse_uri, 1, 1);
}
