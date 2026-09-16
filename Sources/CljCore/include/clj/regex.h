// @ai-generated(solo)
#ifndef CLJ_REGEX_H
#define CLJ_REGEX_H

#include "object.h"

// The pattern text is the identity: `=`, `hash` and the printer all read it.
typedef struct {
	clj_header h;
	clj_value  pattern; // string, verbatim as written
	clj_value  names;   // name -> group number, nil when no group is named
	void      *prog;    // re_prog, freed by the finalizer
	uint32_t   ngroups; // not counting group 0
} clj_regex;

extern const clj_type clj_regex_type;

// ex-info with :pattern and :offset, as PatternSyntaxException reports.
clj_value clj_regex_new(clj_value pattern);

static inline bool       clj_is_regex(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_regex_type; }
static inline clj_regex *clj_regex_of(clj_value v) { return (clj_regex *)clj_to_ptr(v); }
// Borrowed: valid while re is.
static inline clj_value clj_regex_pattern(clj_value re) { return clj_regex_of(re)->pattern; }
static inline uint32_t  clj_regex_group_count(clj_value re) { return clj_regex_of(re)->ngroups; }

// java.util.regex.Matcher: a scan position and the last match, mutable.
typedef struct {
	clj_header h;
	clj_value  re;
	clj_value  input;  // string
	void      *text;   // re_text: the input decoded once, so a scan stays linear
	int32_t   *slots;  // 2 * (ngroups + 1) code point bounds, -1 when absent
	uint32_t   from;   // where the next search starts
	bool       matched;
} clj_matcher;

extern const clj_type clj_matcher_type;

clj_value clj_matcher_new(clj_value re, clj_value input);

static inline bool         clj_is_matcher(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_matcher_type; }
static inline clj_matcher *clj_matcher_of(clj_value v) { return (clj_matcher *)clj_to_ptr(v); }

// nil, the matched string without groups, or a vector of group 0 and every group.
clj_value clj_re_find(clj_value re, clj_value s);
clj_value clj_re_matches(clj_value re, clj_value s);
// Advances the matcher past the match it reports.
clj_value clj_matcher_find(clj_value m);
clj_value clj_matcher_groups(clj_value m);
// Throws past the last group and before the first find, as Matcher.group does.
clj_value clj_matcher_group(clj_value m, intptr_t n);

// limit as Java's String.split: 0 drops the trailing empty strings.
clj_value clj_regex_split(clj_value re, clj_value s, intptr_t limit);
// $0..$n and ${name} name groups in replacement; \$ and \\ are literal.
clj_value clj_regex_replace(clj_value re, clj_value s, clj_value replacement, bool first_only);
// f takes re-find's result for each match and must answer a string.
clj_value clj_regex_replace_by(clj_value re, clj_value s, clj_value f, bool first_only);
clj_value clj_regex_quote_replacement(clj_value s);

void clj_regex_builtins_install(void);

#endif
