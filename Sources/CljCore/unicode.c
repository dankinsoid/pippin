// @ai-generated(solo)
// Unicode case mapping and character properties, answered as JDK 21 answers them (NOTES "Unicode data").
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"
#include "unicode_internal.h"

#include "unicode_data.inc"

const char clj_uc_version[] = UC_VERSION;

const clj_uc_record *clj_uc_record_of(uint32_t cp) {
	if (cp > 0x10FFFF) cp = 0x10FFFF; // a noncharacter: unassigned, no case, no property
	uint32_t mid = (uint32_t)uc_top[cp >> (UC_LEAF_BITS + UC_MID_BITS)] << UC_MID_BITS;
	uint32_t leaf = (uint32_t)uc_mid[mid | ((cp >> UC_LEAF_BITS) & ((1u << UC_MID_BITS) - 1))] << UC_LEAF_BITS;
	return &uc_records[uc_leaf[leaf | (cp & ((1u << UC_LEAF_BITS) - 1))]];
}

// ---- UTF-8

static inline bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

// The width of the well-formed scalar at pos, or 0 when the bytes there are not one.
static size_t decode(const unsigned char *p, size_t len, size_t pos, uint32_t *out) {
	unsigned char c = p[pos];
	size_t        n;
	uint32_t      cp, min;
	if (c < 0x80) {
		*out = c;
		return 1;
	}
	if (c >= 0xC2 && c <= 0xDF) n = 2, cp = c & 0x1F, min = 0x80;
	else if (c >= 0xE0 && c <= 0xEF) n = 3, cp = c & 0x0F, min = 0x800;
	else if (c >= 0xF0 && c <= 0xF4) n = 4, cp = c & 0x07, min = 0x10000;
	else return 0;
	if (pos + n > len) return 0;
	for (size_t i = 1; i < n; i++) {
		if (!is_cont(p[pos + i])) return 0;
		cp = cp << 6 | (p[pos + i] & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
	*out = cp;
	return n;
}

// The bytes from pos to the next lead byte: what a malformed sequence is copied as.
static size_t raw_unit(const unsigned char *p, size_t len, size_t pos) {
	size_t end = pos + 1;
	while (end < len && is_cont(p[end])) end++;
	return end - pos;
}

static size_t encode(uint32_t cp, char *out) {
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | cp >> 6);
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | cp >> 12);
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | cp >> 18);
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

// The scalar ending at pos, decoded from the lead byte before it; *start is that lead's offset. U+FFFD when malformed.
static uint32_t decode_before(const unsigned char *p, size_t len, size_t pos, size_t *start) {
	size_t s = pos - 1;
	while (s > 0 && is_cont(p[s]) && pos - s < 4) s--;
	uint32_t cp;
	*start = s;
	return decode(p, len, s, &cp) == pos - s ? cp : 0xFFFD;
}

static uint32_t decode_at(const unsigned char *p, size_t len, size_t pos, size_t *next) {
	uint32_t cp;
	size_t   w = decode(p, len, pos, &cp);
	*next = pos + (w ? w : raw_unit(p, len, pos));
	return w ? cp : 0xFFFD;
}

// ---- case mapping

typedef struct {
	char  *data;
	size_t len, cap;
} out_buf;

static void reserve(out_buf *b, size_t n) {
	if (b->len + n <= b->cap) return;
	size_t cap = b->cap * 2 > b->len + n ? b->cap * 2 : b->len + n;
	b->data = realloc(b->data, cap);
	if (!b->data) clj_fatal("out of memory");
	b->cap = cap;
}

static void put_bytes(out_buf *b, const void *s, size_t n) {
	reserve(b, n);
	memcpy(b->data + b->len, s, n);
	b->len += n;
}

static void put_cp(out_buf *b, uint32_t cp) {
	reserve(b, 4);
	b->len += encode(cp, b->data + b->len);
}

// The output starts at the first byte that changes, with the unchanged prefix copied once.
static void start(out_buf *b, const unsigned char *p, size_t len, size_t prefix) {
	b->cap = len + len / 2 + 8;
	b->data = malloc(b->cap);
	if (!b->data) clj_fatal("out of memory");
	memcpy(b->data, p, prefix);
	b->len = prefix;
}

static char *finish(out_buf *b, size_t *out_len) {
	reserve(b, 1);
	b->data[b->len] = '\0';
	*out_len = b->len;
	return b->data;
}

static const uint16_t *special_upper(uint32_t cp) {
	size_t lo = 0, hi = sizeof uc_special_upper / sizeof *uc_special_upper;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (uc_special_upper[mid].cp < cp) lo = mid + 1;
		else hi = mid;
	}
	if (lo == sizeof uc_special_upper / sizeof *uc_special_upper || uc_special_upper[lo].cp != cp)
		clj_fatal("unicode: a record flags an upper-case expansion its table lacks");
	return uc_special_upper[lo].up;
}

char *clj_uc_upper_utf8(const char *bytes, size_t len, size_t *out_len) {
	const unsigned char *p = (const unsigned char *)bytes;
	out_buf              b = {0};
	for (size_t pos = 0; pos < len;) {
		unsigned char c = p[pos];
		if (c < 0x80) {
			if (c >= 'a' && c <= 'z') {
				if (!b.data) start(&b, p, len, pos);
				c -= 32;
			}
			if (b.data) put_bytes(&b, &c, 1);
			pos++;
			continue;
		}
		uint32_t cp;
		size_t   w = decode(p, len, pos, &cp);
		const clj_uc_record *r = w ? clj_uc_record_of(cp) : NULL;
		if (!r || (!r->upper && !(r->flags & CLJ_UC_SPECIAL_UPPER))) {
			if (!w) w = raw_unit(p, len, pos);
			if (b.data) put_bytes(&b, p + pos, w);
			pos += w;
			continue;
		}
		if (!b.data) start(&b, p, len, pos);
		if (r->flags & CLJ_UC_SPECIAL_UPPER) {
			const uint16_t *up = special_upper(cp);
			for (int i = 0; i < 3 && up[i]; i++) put_cp(&b, up[i]);
		} else put_cp(&b, (uint32_t)((int32_t)cp + r->upper));
		pos += w;
	}
	return b.data ? finish(&b, out_len) : NULL;
}

// ConditionalSpecialCasing.isCased: the categories plus JDK's own list of Other_Lowercase and Other_Uppercase.
static bool java_cased(uint32_t cp) {
	unsigned g = clj_uc_gc(cp);
	if (g == CLJ_GC_Lu || g == CLJ_GC_Ll || g == CLJ_GC_Lt) return true;
	return (cp >= 0x02B0 && cp <= 0x02B8) || cp == 0x02C0 || cp == 0x02C1 || (cp >= 0x02E0 && cp <= 0x02E4) ||
	       cp == 0x0345 || cp == 0x037A || (cp >= 0x1D2C && cp <= 0x1D61) || (cp >= 0x2160 && cp <= 0x217F) ||
	       (cp >= 0x24B6 && cp <= 0x24E9);
}

// What a character is to the JDK's word BreakIterator (sun.text.resources.BreakIteratorRules, "WordBreakRules").
enum { W_IGNORE, W_MARK, W_LET, W_DGT, W_MIDWORD, W_MIDNUM, W_MIDBOTH, W_OTHER };

static bool cjk_apart(uint32_t cp) {
	return cp == 0x3005 || (cp >= 0x4E00 && cp <= 0x9FA5) || (cp >= 0xF900 && cp <= 0xFA2D) || (cp >= 0x30A1 && cp <= 0x30FA) ||
	       cp == 0x30FD || cp == 0x30FE || (cp >= 0x3041 && cp <= 0x3094) || cp == 0x309D || cp == 0x309E ||
	       (cp >= 0x3099 && cp <= 0x309C) || cp == 0x30FB || cp == 0x30FC;
}

static int word_class(uint32_t cp) {
	if (cp == '.' || cp == '\'' || cp == '"') return W_MIDBOTH;
	if (cp == ',' || cp == 0x066B) return W_MIDNUM;
	unsigned g = clj_uc_gc(cp);
	if (g == CLJ_GC_Cf) return W_IGNORE;
	if (g == CLJ_GC_Mn || g == CLJ_GC_Me) return W_MARK;
	if (((CLJ_GC(Lu) | CLJ_GC(Ll) | CLJ_GC(Lt) | CLJ_GC(Lm) | CLJ_GC(Lo) | CLJ_GC(Mc)) >> g) & 1) return cjk_apart(cp) ? W_OTHER : W_LET;
	if (((CLJ_GC(Nd) | CLJ_GC(Nl) | CLJ_GC(No)) >> g) & 1) return W_DGT;
	if (g == CLJ_GC_Pd || g == CLJ_GC_Pc || cp == 0x2027) return W_MIDWORD;
	return W_OTHER;
}

static bool joins(int punct, int left, int right) {
	if (left != right) return false;
	if (left == W_LET) return punct == W_MIDWORD || punct == W_MIDBOTH;
	return left == W_DGT && (punct == W_MIDNUM || punct == W_MIDBOTH);
}

// The class of the base before pos, past marks and ignorables; *cased if any of them or the base is cased.
static int unit_before(const unsigned char *p, size_t len, size_t pos, bool *cased, size_t *start) {
	bool marks = false;
	while (pos > 0) {
		uint32_t cp = decode_before(p, len, pos, &pos);
		int      k = word_class(cp);
		if (java_cased(cp)) *cased = true;
		if (k == W_IGNORE) continue;
		if (k == W_MARK) {
			marks = true;
			continue;
		}
		*start = pos;
		// A mark after a punctuation or a space belongs to that character's token.
		return marks && k != W_LET && k != W_DGT ? W_OTHER : k;
	}
	*start = 0;
	return W_OTHER;
}

// ConditionalSpecialCasing.isFinalCased: a cased letter before the sigma at [at, end) and none after, in its word as
// the JDK's word BreakIterator draws it (letters and digits, one joining punctuation between two of a kind).
static bool final_sigma(const unsigned char *p, size_t len, size_t at, size_t end) {
	bool   before = false;
	int    right = W_LET;
	size_t pos = at;
	for (;;) {
		bool   cased = false, ignored = false;
		size_t base, ignored_base;
		int    k = unit_before(p, len, pos, &cased, &base);
		if (k == W_LET || k == W_DGT) {
			if (cased) {
				before = true;
				break;
			}
			right = k;
			pos = base;
			continue;
		}
		if ((k == W_MIDWORD || k == W_MIDNUM || k == W_MIDBOTH) && joins(k, unit_before(p, len, base, &ignored, &ignored_base), right)) {
			pos = base;
			continue;
		}
		break;
	}
	if (!before) return false;
	int left = W_LET;
	for (pos = end; pos < len;) {
		size_t   next;
		uint32_t cp = decode_at(p, len, pos, &next);
		int      k = word_class(cp);
		if (k == W_IGNORE || k == W_MARK) {
			if (java_cased(cp)) return false;
			pos = next;
			continue;
		}
		if (k == W_LET || k == W_DGT) {
			if (java_cased(cp)) return false;
			left = k;
			pos = next;
			continue;
		}
		if (k == W_MIDWORD || k == W_MIDNUM || k == W_MIDBOTH) {
			size_t after = next;
			int    right_k = W_OTHER;
			while (after < len) {
				size_t   n2;
				uint32_t c2 = decode_at(p, len, after, &n2);
				int      k2 = word_class(c2);
				if (k2 == W_IGNORE) {
					after = n2;
					continue;
				}
				right_k = k2;
				break;
			}
			if (joins(k, left, right_k)) {
				pos = next;
				continue;
			}
		}
		break;
	}
	return true;
}

char *clj_uc_lower_utf8(const char *bytes, size_t len, size_t *out_len) {
	const unsigned char *p = (const unsigned char *)bytes;
	out_buf              b = {0};
	for (size_t pos = 0; pos < len;) {
		unsigned char c = p[pos];
		if (c < 0x80) {
			if (c >= 'A' && c <= 'Z') {
				if (!b.data) start(&b, p, len, pos);
				c += 32;
			}
			if (b.data) put_bytes(&b, &c, 1);
			pos++;
			continue;
		}
		uint32_t cp;
		size_t   w = decode(p, len, pos, &cp);
		const clj_uc_record *r = w ? clj_uc_record_of(cp) : NULL;
		if (!r || !r->lower) {
			if (!w) w = raw_unit(p, len, pos);
			if (b.data) put_bytes(&b, p + pos, w);
			pos += w;
			continue;
		}
		if (!b.data) start(&b, p, len, pos);
		if (cp == 0x0130) {
			put_cp(&b, 0x0069);
			put_cp(&b, 0x0307);
		} else if (cp == 0x03A3) put_cp(&b, final_sigma(p, len, pos, pos + w) ? 0x03C2 : 0x03C3);
		else put_cp(&b, (uint32_t)((int32_t)cp + r->lower));
		pos += w;
	}
	return b.data ? finish(&b, out_len) : NULL;
}

// ---- java.util.regex predicates

static bool ascii_type(uint8_t fn, uint32_t cp) {
	if (cp >= 0x80) return false;
	bool upper = cp >= 'A' && cp <= 'Z', lower = cp >= 'a' && cp <= 'z', digit = cp >= '0' && cp <= '9';
	bool punct = (cp >= '!' && cp <= '/') || (cp >= ':' && cp <= '@') || (cp >= '[' && cp <= '`') || (cp >= '{' && cp <= '~');
	switch (fn) {
	case CLJ_UC_FN_ASCII_ALNUM: return upper || lower || digit;
	case CLJ_UC_FN_ASCII_ALPHA: return upper || lower;
	case CLJ_UC_FN_ASCII_BLANK: return cp == ' ' || cp == '\t';
	case CLJ_UC_FN_ASCII_CNTRL: return cp < 0x20 || cp == 0x7F;
	case CLJ_UC_FN_ASCII_GRAPH: return upper || lower || digit || punct;
	case CLJ_UC_FN_ASCII_PUNCT: return punct;
	case CLJ_UC_FN_ASCII_SPACE: return (cp >= '\t' && cp <= '\r') || cp == ' ';
	case CLJ_UC_FN_ASCII_XDIGIT: return digit || (cp >= 'A' && cp <= 'F') || (cp >= 'a' && cp <= 'f');
	default: return upper || lower || digit || cp == '_';
	}
}

static bool identifier_ignorable(uint32_t cp, unsigned g) {
	return cp <= 8 || (cp >= 0x0E && cp <= 0x1B) || (cp >= 0x7F && cp <= 0x9F) || g == CLJ_GC_Cf;
}

static bool test_fn(uint8_t fn, uint32_t cp) {
	const clj_uc_record *r = clj_uc_record_of(cp);
	uint32_t             g = r->gc, bit = 1u << g;
	switch (fn) {
	case CLJ_UC_FN_ALL: return true;
	case CLJ_UC_FN_WHITE_SPACE:
		return (bit & (CLJ_GC(Zs) | CLJ_GC(Zl) | CLJ_GC(Zp))) || (cp >= 0x09 && cp <= 0x0D) || cp == 0x85;
	case CLJ_UC_FN_HEX_DIGIT:
		return g == CLJ_GC_Nd || (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'F') || (cp >= 'a' && cp <= 'f') ||
		       (cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0xFF21 && cp <= 0xFF26) || (cp >= 0xFF41 && cp <= 0xFF46);
	case CLJ_UC_FN_NONCHARACTER: return (cp & 0xFFFE) == 0xFFFE || (cp >= 0xFDD0 && cp <= 0xFDEF);
	case CLJ_UC_FN_BLANK: return g == CLJ_GC_Zs || cp == 0x09;
	case CLJ_UC_FN_PRINT: {
		bool graph = !(bit & (CLJ_GC(Zs) | CLJ_GC(Zl) | CLJ_GC(Zp) | CLJ_GC(Cc) | CLJ_GC(Cs) | CLJ_GC(Cn)));
		return (graph || g == CLJ_GC_Zs || cp == 0x09) && g != CLJ_GC_Cc;
	}
	case CLJ_UC_FN_JOIN_CONTROL: return cp == 0x200C || cp == 0x200D;
	case CLJ_UC_FN_WORD:
		return (r->flags & CLJ_UC_ALPHABETIC) || (bit & (CLJ_GC(Mn) | CLJ_GC(Me) | CLJ_GC(Mc) | CLJ_GC(Nd) | CLJ_GC(Pc))) ||
		       cp == 0x200C || cp == 0x200D;
	case CLJ_UC_FN_JAVA_IDENTIFIER_PART:
		return (bit & (CLJ_GC(Lu) | CLJ_GC(Ll) | CLJ_GC(Lt) | CLJ_GC(Lm) | CLJ_GC(Lo) | CLJ_GC(Nl) | CLJ_GC(Sc) | CLJ_GC(Pc) |
		               CLJ_GC(Nd) | CLJ_GC(Mn) | CLJ_GC(Mc))) ||
		       identifier_ignorable(cp, g);
	case CLJ_UC_FN_UNICODE_IDENTIFIER_START: return (r->flags & CLJ_UC_ID_START) || cp == 0x2E2F;
	case CLJ_UC_FN_UNICODE_IDENTIFIER_PART: return (r->flags & CLJ_UC_ID_CONTINUE) || identifier_ignorable(cp, g) || cp == 0x2E2F;
	case CLJ_UC_FN_IDENTIFIER_IGNORABLE: return identifier_ignorable(cp, g);
	case CLJ_UC_FN_JAVA_WHITESPACE:
		return ((bit & (CLJ_GC(Zs) | CLJ_GC(Zl) | CLJ_GC(Zp))) && cp != 0xA0 && cp != 0x2007 && cp != 0x202F) ||
		       (cp >= 0x09 && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x1F);
	case CLJ_UC_FN_ISO_CONTROL: return cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F);
	case CLJ_UC_FN_HORIZ_WS:
		return cp == 0x09 || cp == 0x20 || cp == 0xA0 || cp == 0x1680 || cp == 0x180E || (cp >= 0x2000 && cp <= 0x200A) ||
		       cp == 0x202F || cp == 0x205F || cp == 0x3000;
	case CLJ_UC_FN_VERT_WS: return (cp >= 0x0A && cp <= 0x0D) || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
	default: return ascii_type(fn, cp);
	}
}

static int script_of(uint32_t cp) {
	size_t lo = 0, hi = sizeof uc_script_runs / sizeof *uc_script_runs;
	while (hi - lo > 1) {
		size_t mid = lo + (hi - lo) / 2;
		if ((uc_script_runs[mid] >> 8) <= cp) lo = mid;
		else hi = mid;
	}
	return (int)(uc_script_runs[lo] & 0xFF);
}

static int block_of(uint32_t cp) {
	size_t lo = 0, hi = UC_BLOCKS;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (uc_block_ranges[mid][1] < cp) lo = mid + 1;
		else hi = mid;
	}
	return lo < UC_BLOCKS && uc_block_ranges[lo][0] <= cp ? (int)lo : -1;
}

bool clj_uc_pred_test(const clj_uc_pred *p, uint32_t cp) {
	switch (p->kind) {
	case CLJ_UC_PRED_PROP: {
		const clj_uc_record *r = clj_uc_record_of(cp);
		return ((p->gc_mask >> r->gc) & 1) || (r->flags & p->flags);
	}
	case CLJ_UC_PRED_FN: return test_fn(p->fn, cp);
	case CLJ_UC_PRED_SCRIPT: return cp <= 0x10FFFF && script_of(cp) == p->arg;
	case CLJ_UC_PRED_BLOCK: return p->arg >= 0 && cp <= 0x10FFFF && block_of(cp) == p->arg;
	default: return cp >= (uint32_t)(p->arg >> 8) && cp <= (uint32_t)(p->arg & 0xFF);
	}
}

// ---- property names

// The name upper-cased as String.toUpperCase does, into buf; false when it does not fit.
static bool name_case(const char *name, size_t len, bool upper, char *buf, size_t cap) {
	size_t n = len;
	char  *mapped = upper ? clj_uc_upper_utf8(name, len, &n) : clj_uc_lower_utf8(name, len, &n);
	bool   fits = n < cap;
	if (fits) {
		memcpy(buf, mapped ? mapped : name, n);
		buf[n] = '\0';
	}
	free(mapped);
	return fits;
}

#define L_LETTER (CLJ_GC(Lu) | CLJ_GC(Ll) | CLJ_GC(Lt) | CLJ_GC(Lm) | CLJ_GC(Lo))
#define L_CASED  (CLJ_GC(Lu) | CLJ_GC(Ll) | CLJ_GC(Lt))

static clj_uc_pred prop(uint32_t mask, uint16_t flags) { return (clj_uc_pred){CLJ_UC_PRED_PROP, 0, flags, mask, 0}; }
static clj_uc_pred range(uint32_t lo, uint32_t hi) { return (clj_uc_pred){CLJ_UC_PRED_RANGE, 0, 0, 0, (int32_t)(lo << 8 | hi)}; }

// CharPredicates.getUnicodePredicate, then getPosixPredicate: an upper-cased name.
static bool unicode_predicate(const char *n, bool ci, bool posix_only, clj_uc_pred *out) {
	static const struct {
		const char *name;
		uint16_t    flags;
	} flag_props[] = {
		{"ALPHABETIC", CLJ_UC_ALPHABETIC},
		{"EMOJI", CLJ_UC_EMOJI},
		{"EMOJI_PRESENTATION", CLJ_UC_EMOJI_PRESENTATION},
		{"EMOJI_MODIFIER", CLJ_UC_EMOJI_MODIFIER},
		{"EMOJI_MODIFIER_BASE", CLJ_UC_EMOJI_MODIFIER_BASE},
		{"EMOJI_COMPONENT", CLJ_UC_EMOJI_COMPONENT},
		{"EXTENDED_PICTOGRAPHIC", CLJ_UC_EXTENDED_PICTOGRAPHIC},
		{"IDEOGRAPHIC", CLJ_UC_IDEOGRAPHIC},
	};
	const uint16_t cased = CLJ_UC_LOWERCASE | CLJ_UC_UPPERCASE;
	if (!posix_only) {
		for (size_t i = 0; i < sizeof flag_props / sizeof *flag_props; i++) {
			if (strcmp(n, flag_props[i].name) == 0) return *out = prop(0, flag_props[i].flags), true;
		}
		if (!strcmp(n, "ASSIGNED")) return *out = prop(~CLJ_GC(Cn), 0), true;
		if (!strcmp(n, "CONTROL")) return *out = prop(CLJ_GC(Cc), 0), true;
		if (!strcmp(n, "HEXDIGIT") || !strcmp(n, "HEX_DIGIT")) return *out = clj_uc_pred_fn(CLJ_UC_FN_HEX_DIGIT), true;
		if (!strcmp(n, "JOINCONTROL") || !strcmp(n, "JOIN_CONTROL")) return *out = clj_uc_pred_fn(CLJ_UC_FN_JOIN_CONTROL), true;
		if (!strcmp(n, "LETTER")) return *out = prop(L_LETTER, 0), true;
		if (!strcmp(n, "LOWERCASE")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(0, CLJ_UC_LOWERCASE), true;
		if (!strcmp(n, "NONCHARACTERCODEPOINT") || !strcmp(n, "NONCHARACTER_CODE_POINT"))
			return *out = clj_uc_pred_fn(CLJ_UC_FN_NONCHARACTER), true;
		if (!strcmp(n, "TITLECASE")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(CLJ_GC(Lt), 0), true;
		if (!strcmp(n, "PUNCTUATION"))
			return *out = prop(CLJ_GC(Pd) | CLJ_GC(Ps) | CLJ_GC(Pe) | CLJ_GC(Pc) | CLJ_GC(Po) | CLJ_GC(Pi) | CLJ_GC(Pf), 0), true;
		if (!strcmp(n, "UPPERCASE")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(0, CLJ_UC_UPPERCASE), true;
		if (!strcmp(n, "WHITESPACE") || !strcmp(n, "WHITE_SPACE")) return *out = clj_uc_pred_fn(CLJ_UC_FN_WHITE_SPACE), true;
		if (!strcmp(n, "WORD")) return *out = clj_uc_pred_fn(CLJ_UC_FN_WORD), true;
	}
	if (!strcmp(n, "ALPHA")) return *out = prop(0, CLJ_UC_ALPHABETIC), true;
	if (!strcmp(n, "LOWER")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(0, CLJ_UC_LOWERCASE), true;
	if (!strcmp(n, "UPPER")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(0, CLJ_UC_UPPERCASE), true;
	if (!strcmp(n, "SPACE")) return *out = clj_uc_pred_fn(CLJ_UC_FN_WHITE_SPACE), true;
	if (!strcmp(n, "PUNCT"))
		return *out = prop(CLJ_GC(Pd) | CLJ_GC(Ps) | CLJ_GC(Pe) | CLJ_GC(Pc) | CLJ_GC(Po) | CLJ_GC(Pi) | CLJ_GC(Pf), 0), true;
	if (!strcmp(n, "XDIGIT")) return *out = clj_uc_pred_fn(CLJ_UC_FN_HEX_DIGIT), true;
	if (!strcmp(n, "ALNUM")) return *out = prop(CLJ_GC(Nd), CLJ_UC_ALPHABETIC), true;
	if (!strcmp(n, "CNTRL")) return *out = prop(CLJ_GC(Cc), 0), true;
	if (!strcmp(n, "DIGIT")) return *out = prop(CLJ_GC(Nd), 0), true;
	if (!strcmp(n, "BLANK")) return *out = clj_uc_pred_fn(CLJ_UC_FN_BLANK), true;
	if (!strcmp(n, "GRAPH"))
		return *out = prop(~(CLJ_GC(Zs) | CLJ_GC(Zl) | CLJ_GC(Zp) | CLJ_GC(Cc) | CLJ_GC(Cs) | CLJ_GC(Cn)), 0), true;
	if (!strcmp(n, "PRINT")) return *out = clj_uc_pred_fn(CLJ_UC_FN_PRINT), true;
	return false;
}

// CharPredicates.forProperty: names compared as written.
static bool java_property(const char *n, bool ci, clj_uc_pred *out) {
	static const struct {
		const char *name;
		uint32_t    mask;
	} categories[] = {
		{"Cn", CLJ_GC(Cn)}, {"Lm", CLJ_GC(Lm)}, {"Lo", CLJ_GC(Lo)}, {"Mn", CLJ_GC(Mn)}, {"Me", CLJ_GC(Me)},
		{"Mc", CLJ_GC(Mc)}, {"Nd", CLJ_GC(Nd)}, {"Nl", CLJ_GC(Nl)}, {"No", CLJ_GC(No)}, {"Zs", CLJ_GC(Zs)},
		{"Zl", CLJ_GC(Zl)}, {"Zp", CLJ_GC(Zp)}, {"Cc", CLJ_GC(Cc)}, {"Cf", CLJ_GC(Cf)}, {"Co", CLJ_GC(Co)},
		{"Cs", CLJ_GC(Cs)}, {"Pd", CLJ_GC(Pd)}, {"Ps", CLJ_GC(Ps)}, {"Pe", CLJ_GC(Pe)}, {"Pc", CLJ_GC(Pc)},
		{"Po", CLJ_GC(Po)}, {"Sm", CLJ_GC(Sm)}, {"Sc", CLJ_GC(Sc)}, {"Sk", CLJ_GC(Sk)}, {"So", CLJ_GC(So)},
		{"Pi", CLJ_GC(Pi)}, {"Pf", CLJ_GC(Pf)}, {"L", L_LETTER}, {"M", CLJ_GC(Mn) | CLJ_GC(Me) | CLJ_GC(Mc)},
		{"N", CLJ_GC(Nd) | CLJ_GC(Nl) | CLJ_GC(No)}, {"Z", CLJ_GC(Zs) | CLJ_GC(Zl) | CLJ_GC(Zp)},
		{"C", CLJ_GC(Cc) | CLJ_GC(Cf) | CLJ_GC(Co) | CLJ_GC(Cs) | CLJ_GC(Cn)},
		{"P", CLJ_GC(Pd) | CLJ_GC(Ps) | CLJ_GC(Pe) | CLJ_GC(Pc) | CLJ_GC(Po) | CLJ_GC(Pi) | CLJ_GC(Pf)},
		{"S", CLJ_GC(Sm) | CLJ_GC(Sc) | CLJ_GC(Sk) | CLJ_GC(So)}, {"LC", L_CASED}, {"LD", L_LETTER | CLJ_GC(Nd)},
		{"javaDigit", CLJ_GC(Nd)}, {"javaDefined", ~CLJ_GC(Cn)}, {"javaLetter", L_LETTER},
		{"javaLetterOrDigit", L_LETTER | CLJ_GC(Nd)},
		{"javaJavaIdentifierStart", L_LETTER | CLJ_GC(Nl) | CLJ_GC(Sc) | CLJ_GC(Pc)},
		{"javaSpaceChar", CLJ_GC(Zs) | CLJ_GC(Zl) | CLJ_GC(Zp)},
	};
	static const struct {
		const char *name;
		uint8_t     fn;
	} fns[] = {
		{"all", CLJ_UC_FN_ALL},
		{"Alnum", CLJ_UC_FN_ASCII_ALNUM},
		{"Alpha", CLJ_UC_FN_ASCII_ALPHA},
		{"Blank", CLJ_UC_FN_ASCII_BLANK},
		{"Cntrl", CLJ_UC_FN_ASCII_CNTRL},
		{"Graph", CLJ_UC_FN_ASCII_GRAPH},
		{"Punct", CLJ_UC_FN_ASCII_PUNCT},
		{"Space", CLJ_UC_FN_ASCII_SPACE},
		{"XDigit", CLJ_UC_FN_ASCII_XDIGIT},
		{"javaJavaIdentifierPart", CLJ_UC_FN_JAVA_IDENTIFIER_PART},
		{"javaUnicodeIdentifierStart", CLJ_UC_FN_UNICODE_IDENTIFIER_START},
		{"javaUnicodeIdentifierPart", CLJ_UC_FN_UNICODE_IDENTIFIER_PART},
		{"javaIdentifierIgnorable", CLJ_UC_FN_IDENTIFIER_IGNORABLE},
		{"javaWhitespace", CLJ_UC_FN_JAVA_WHITESPACE},
		{"javaISOControl", CLJ_UC_FN_ISO_CONTROL},
	};
	const uint16_t cased = CLJ_UC_LOWERCASE | CLJ_UC_UPPERCASE;
	for (size_t i = 0; i < sizeof categories / sizeof *categories; i++) {
		if (!strcmp(n, categories[i].name)) return *out = prop(categories[i].mask, 0), true;
	}
	for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
		if (!strcmp(n, fns[i].name)) return *out = clj_uc_pred_fn(fns[i].fn), true;
	}
	if (!strcmp(n, "Lu")) return *out = prop(ci ? L_CASED : CLJ_GC(Lu), 0), true;
	if (!strcmp(n, "Ll")) return *out = prop(ci ? L_CASED : CLJ_GC(Ll), 0), true;
	if (!strcmp(n, "Lt")) return *out = prop(ci ? L_CASED : CLJ_GC(Lt), 0), true;
	if (!strcmp(n, "L1")) return *out = range(0x00, 0xFF), true;
	if (!strcmp(n, "ASCII")) return *out = range(0x00, 0x7F), true;
	if (!strcmp(n, "Digit")) return *out = range('0', '9'), true;
	if (!strcmp(n, "Print")) return *out = range(0x20, 0x7E), true;
	if (!strcmp(n, "Lower")) return *out = ci ? clj_uc_pred_fn(CLJ_UC_FN_ASCII_ALPHA) : range('a', 'z'), true;
	if (!strcmp(n, "Upper")) return *out = ci ? clj_uc_pred_fn(CLJ_UC_FN_ASCII_ALPHA) : range('A', 'Z'), true;
	if (!strcmp(n, "javaLowerCase")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(0, CLJ_UC_LOWERCASE), true;
	if (!strcmp(n, "javaUpperCase")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(0, CLJ_UC_UPPERCASE), true;
	if (!strcmp(n, "javaTitleCase")) return *out = ci ? prop(CLJ_GC(Lt), cased) : prop(CLJ_GC(Lt), 0), true;
	if (!strcmp(n, "javaAlphabetic")) return *out = prop(0, CLJ_UC_ALPHABETIC), true;
	if (!strcmp(n, "javaIdeographic")) return *out = prop(0, CLJ_UC_IDEOGRAPHIC), true;
	if (!strcmp(n, "javaMirrored")) return *out = prop(0, CLJ_UC_MIRRORED), true;
	return false;
}

// Character.UnicodeScript.forName: an upper-cased name or ISO 15924 code.
static bool script_named(const char *n, clj_uc_pred *out) {
	const char *s = uc_script_names;
	for (int i = 0; i < 2 * UC_SCRIPTS; i++, s += strlen(s) + 1) {
		if (!strcmp(n, s)) return *out = (clj_uc_pred){CLJ_UC_PRED_SCRIPT, 0, 0, 0, i / 2}, true;
	}
	return false;
}

// Whether the upper-cased name n is the canonical name c upper-cased, with spaces dropped (drop), or with spaces and
// hyphens as underscores (ident).
static bool block_form(const char *n, const char *c, bool drop, bool ident) {
	for (;; c++) {
		char ch = *c;
		if (ch == ' ' && drop) continue;
		if (ident && (ch == ' ' || ch == '-')) ch = '_';
		if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
		if (*n != ch) return false;
		if (!ch) return true;
		n++;
	}
}

// Character.UnicodeBlock.forName: an upper-cased name.
static bool block_named(const char *n, clj_uc_pred *out) {
	for (size_t i = 0; i < sizeof uc_block_extra / sizeof *uc_block_extra; i++) {
		if (!strcmp(n, uc_block_extra[i].name)) return *out = (clj_uc_pred){CLJ_UC_PRED_BLOCK, 0, 0, 0, uc_block_extra[i].block}, true;
	}
	bool refused = false;
	for (size_t i = 0; i < sizeof uc_block_refused / sizeof *uc_block_refused; i++) refused |= !strcmp(n, uc_block_refused[i]);
	const char *c = uc_block_names;
	for (int i = 0; i < UC_BLOCKS; i++, c += strlen(c) + 1) {
		if (block_form(n, c, false, false) || block_form(n, c, true, false) || (!refused && block_form(n, c, false, true)))
			return *out = (clj_uc_pred){CLJ_UC_PRED_BLOCK, 0, 0, 0, i}, true;
	}
	return false;
}

bool clj_uc_family(const char *name, size_t len, bool ci, bool unicode_class, clj_uc_pred *out) {
	char        key[96], value[96], upper[96];
	const char *eq = memchr(name, '=', len);
	if (memchr(name, '\0', len)) return false;
	if (eq) {
		size_t klen = (size_t)(eq - name), vlen = len - klen - 1;
		if (!name_case(name, klen, false, key, sizeof key) || vlen >= sizeof value) return false;
		memcpy(value, eq + 1, vlen);
		value[vlen] = '\0';
		if (!strcmp(key, "sc") || !strcmp(key, "script"))
			return name_case(value, vlen, true, upper, sizeof upper) && script_named(upper, out);
		if (!strcmp(key, "blk") || !strcmp(key, "block"))
			return name_case(value, vlen, true, upper, sizeof upper) && block_named(upper, out);
		if (!strcmp(key, "gc") || !strcmp(key, "general_category")) return java_property(value, ci, out);
		return false;
	}
	if (len >= sizeof value) return false;
	memcpy(value, name, len);
	value[len] = '\0';
	if (len >= 2 && name[0] == 'I' && name[1] == 'n')
		return name_case(name + 2, len - 2, true, upper, sizeof upper) && block_named(upper, out);
	if (len >= 2 && name[0] == 'I' && name[1] == 's') {
		if (!name_case(name + 2, len - 2, true, upper, sizeof upper)) return false;
		return unicode_predicate(upper, ci, false, out) || java_property(value + 2, ci, out) || script_named(upper, out);
	}
	if (unicode_class && name_case(name, len, true, upper, sizeof upper) && unicode_predicate(upper, ci, true, out)) return true;
	return java_property(value, ci, out);
}

// ---- grapheme clusters (jdk.internal.util.regex.Grapheme)

enum { G_OTHER, G_CR, G_LF, G_CONTROL, G_EXTEND, G_ZWJ, G_RI, G_PREPEND, G_SPACINGMARK, G_L, G_V, G_T, G_LV, G_LVT, G_EXT_PICT };

static int grapheme_type(uint32_t cp) {
	if (cp < 0x7F) return cp >= 32 ? G_OTHER : cp == '\r' ? G_CR : cp == '\n' ? G_LF : G_CONTROL;
	const clj_uc_record *r = clj_uc_record_of(cp);
	if (r->flags & CLJ_UC_EXTENDED_PICTOGRAPHIC) return G_EXT_PICT;
	switch (r->gc) {
	case CLJ_GC_Cn:
		// GraphemeBreakTest.txt lists U+0378 as Other, which the JDK follows.
		if (cp == 0x0378) return G_OTHER;
		return G_CONTROL;
	case CLJ_GC_Cc:
	case CLJ_GC_Zl:
	case CLJ_GC_Zp:
	case CLJ_GC_Cs: return G_CONTROL;
	case CLJ_GC_Cf:
		if (cp == 0x200C || (cp >= 0xE0020 && cp <= 0xE007F)) return G_EXTEND;
		if (cp == 0x200D) return G_ZWJ;
		if ((cp >= 0x0600 && cp <= 0x0605) || cp == 0x06DD || cp == 0x070F || cp == 0x0890 || cp == 0x0891 || cp == 0x08E2 ||
		    cp == 0x110BD || cp == 0x110CD)
			return G_PREPEND;
		return G_CONTROL;
	case CLJ_GC_Mn:
	case CLJ_GC_Me: return G_EXTEND;
	case CLJ_GC_Mc:
		if (cp == 0x102B || cp == 0x102C || cp == 0x1038 || (cp >= 0x1062 && cp <= 0x1064) || (cp >= 0x1067 && cp <= 0x106D) ||
		    cp == 0x1083 || (cp >= 0x1087 && cp <= 0x108C) || cp == 0x108F || (cp >= 0x109A && cp <= 0x109C) || cp == 0x1A61 ||
		    cp == 0x1A63 || cp == 0x1A64 || cp == 0xAA7B || cp == 0xAA7D)
			return G_OTHER;
		return G_SPACINGMARK;
	case CLJ_GC_So: return cp >= 0x1F1E6 && cp <= 0x1F1FF ? G_RI : G_OTHER;
	case CLJ_GC_Lm:
	case CLJ_GC_Sk: return cp == 0xFF9E || cp == 0xFF9F || (cp >= 0x1F3FB && cp <= 0x1F3FF) ? G_EXTEND : G_OTHER;
	case CLJ_GC_Lo: {
		if (cp == 0x0E33 || cp == 0x0EB3) return G_SPACINGMARK;
		if (cp >= 0x1100 && cp <= 0x11FF) return cp <= 0x115F ? G_L : cp <= 0x11A7 ? G_V : G_T;
		if (cp >= 0xAC00 && cp < 0xAC00 + 11172) return (cp - 0xAC00) % 28 == 0 ? G_LV : G_LVT;
		if (cp >= 0xA960 && cp <= 0xA97C) return G_L;
		if (cp >= 0xD7B0 && cp <= 0xD7C6) return G_V;
		if (cp >= 0xD7CB && cp <= 0xD7FB) return G_T;
		static const uint32_t prepend[] = {0x0D4E,  0x111C2, 0x111C3, 0x1193F, 0x11941, 0x11A3A, 0x11A84,
		                                   0x11A85, 0x11A86, 0x11A87, 0x11A88, 0x11A89, 0x11D46, 0x11F02};
		for (size_t i = 0; i < sizeof prepend / sizeof *prepend; i++) {
			if (cp == prepend[i]) return G_PREPEND;
		}
		return G_OTHER;
	}
	default: return G_OTHER;
	}
}

// Grapheme's rules table: whether there is a boundary between a t0 and a t1.
static bool grapheme_break(int t0, int t1) {
	if (t0 == G_CR && t1 == G_LF) return false;
	if (t0 == G_CR || t0 == G_LF || t0 == G_CONTROL || t1 == G_CR || t1 == G_LF || t1 == G_CONTROL) return true;
	if (t0 == G_L && (t1 == G_L || t1 == G_V || t1 == G_LV || t1 == G_LVT)) return false;
	if ((t0 == G_LV || t0 == G_V) && (t1 == G_V || t1 == G_T)) return false;
	if ((t0 == G_LVT || t0 == G_T) && t1 == G_T) return false;
	if (t1 == G_EXTEND || t1 == G_ZWJ || t1 == G_SPACINGMARK || t0 == G_PREPEND) return false;
	return true;
}

uint32_t clj_uc_grapheme_end(const uint32_t *cp, uint32_t from, uint32_t n) {
	int      t0 = grapheme_type(cp[from]);
	uint32_t ri = t0 == G_RI, end = from + 1;
	bool     gb11 = t0 == G_EXT_PICT;
	for (; end < n; end++) {
		int t1 = grapheme_type(cp[end]);
		if (gb11 && t0 == G_ZWJ && t1 == G_EXT_PICT) {
		} else if (ri % 2 == 1 && t0 == G_RI && t1 == G_RI) {
		} else if (grapheme_break(t0, t1)) break;
		ri += t1 == G_RI;
		t0 = t1;
	}
	return end;
}
