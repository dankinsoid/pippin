// @ai-generated(solo)
#include <stdio.h>
#include <string.h>

#include "clj/core.h"
#include "clj/inst.h"

// Date.hashCode.
static uint32_t inst_hash(void *self) {
	uint64_t ms = (uint64_t)((clj_inst *)self)->ms;
	return (uint32_t)(ms ^ (ms >> 32));
}

static bool inst_equals(void *self, clj_value other) { return clj_is_inst(other) && ((clj_inst *)self)->ms == clj_inst_ms(other); }

const clj_type clj_inst_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "Date",
	.hash = inst_hash,
	.equals = inst_equals,
};

clj_value clj_inst_new(int64_t ms) {
	clj_inst *d = clj_alloc(&clj_inst_type, sizeof *d);
	d->ms = ms;
	return clj_from_ptr(d);
}

// Proleptic Gregorian, Howard Hinnant's algorithms; the JVM's GregorianCalendar switches to Julian before 1582.
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	int64_t yoe = y - era * 400;
	int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, int64_t *m, int64_t *d) {
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	int64_t day = doy - (153 * mp + 2) / 5 + 1;
	int64_t month = mp < 10 ? mp + 3 : mp - 9;
	d[0] = day;
	m[0] = month;
	y[0] = yoe + era * 400 + (month <= 2);
}

static bool leap_year(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int days_in_month(int64_t m, bool leap) {
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	return m == 2 && leap ? 29 : days[m - 1];
}

// Exactly `count` digits at *i, advancing past them.
static bool digits(const char *s, size_t n, size_t *i, int count, int64_t *out) {
	int64_t v = 0;
	for (int k = 0; k < count; k++) {
		if (*i >= n || s[*i] < '0' || s[*i] > '9') return false;
		v = v * 10 + (s[*i] - '0');
		(*i)++;
	}
	*out = v;
	return true;
}

static clj_value verify(bool ok, const char *test) { return ok ? CLJ_NIL : clj_throw_msg("Assert failed: %s", test); }

clj_value clj_inst_parse(const char *s, size_t n) {
	int64_t y, mo = 1, d = 1, h = 0, mi = 0, sec = 0, nanos = 0, oh = 0, om = 0;
	int     sign = 0;
	size_t  i = 0;
	bool    ok = digits(s, n, &i, 4, &y);
	if (ok && i < n && s[i] == '-') {
		i++;
		ok = digits(s, n, &i, 2, &mo);
		if (ok && i < n && s[i] == '-') {
			i++;
			ok = digits(s, n, &i, 2, &d);
			if (ok && i < n && s[i] == 'T') {
				i++;
				ok = digits(s, n, &i, 2, &h);
				if (ok && i < n && s[i] == ':') {
					i++;
					ok = digits(s, n, &i, 2, &mi);
					if (ok && i < n && s[i] == ':') {
						i++;
						ok = digits(s, n, &i, 2, &sec);
						if (ok && i < n && s[i] == '.') {
							i++;
							size_t start = i;
							// The fraction is read as nanoseconds: the first nine digits, right-padded with zeros.
							while (i < n && s[i] >= '0' && s[i] <= '9') {
								if (i - start < 9) nanos = nanos * 10 + (s[i] - '0');
								i++;
							}
							for (size_t k = i - start; k < 9; k++) nanos *= 10;
							ok = i > start;
						}
					}
				}
			}
		}
	}
	if (ok && i < n && s[i] == 'Z') {
		i++;
	} else if (ok && i < n && (s[i] == '+' || s[i] == '-')) {
		sign = s[i] == '-' ? -1 : 1;
		i++;
		ok = digits(s, n, &i, 2, &oh) && i < n && s[i] == ':';
		if (ok) {
			i++;
			ok = digits(s, n, &i, 2, &om);
		}
	}
	if (!ok || i != n) return clj_throw_msg("Unrecognized date/time syntax: %.*s", (int)n, s);
	if (verify(mo >= 1 && mo <= 12, "(<= 1 months 12)") == CLJ_THROWN) return CLJ_THROWN;
	if (verify(d >= 1 && d <= days_in_month(mo, leap_year(y)), "(<= 1 days (days-in-month months (leap-year? years)))") == CLJ_THROWN) return CLJ_THROWN;
	if (verify(h <= 23, "(<= 0 hours 23)") == CLJ_THROWN) return CLJ_THROWN;
	if (verify(mi <= 59, "(<= 0 minutes 59)") == CLJ_THROWN) return CLJ_THROWN;
	if (verify(sec <= (mi == 59 ? 60 : 59), "(<= 0 seconds (if (= minutes 59) 60 59))") == CLJ_THROWN) return CLJ_THROWN;
	if (verify(oh <= 23, "(<= -23 offset-hours 23)") == CLJ_THROWN) return CLJ_THROWN;
	if (verify(om <= 59, "(<= 0 offset-minutes 59)") == CLJ_THROWN) return CLJ_THROWN;
	int64_t ms = days_from_civil(y, mo, d) * 86400000 + h * 3600000 + mi * 60000 + sec * 1000 + nanos / 1000000;
	ms -= sign * (oh * 3600000 + om * 60000);
	return clj_inst_new(ms);
}

void clj_inst_format(clj_value inst, char *out, size_t cap) {
	int64_t ms = clj_inst_ms(inst);
	int64_t days = ms / 86400000, rem = ms % 86400000;
	if (rem < 0) {
		rem += 86400000;
		days--;
	}
	int64_t y, m, d;
	civil_from_days(days, &y, &m, &d);
	snprintf(out, cap, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lld-00:00", (long long)y, (long long)m, (long long)d, (long long)(rem / 3600000),
	         (long long)(rem / 60000 % 60), (long long)(rem / 1000 % 60), (long long)(rem % 1000));
}

// ---- builtins

static clj_value b_inst_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_inst(args[0]));
}

static clj_value b_inst_ms(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_inst(args[0])) return clj_throw_msg("inst-ms not supported on this type: %s", clj_type_name(args[0]));
	return clj_long_new(clj_inst_ms(args[0]));
}

// The #inst data reader: clojure.instant/read-instant-date.
clj_value clj_inst_read(clj_value form) {
	if (!clj_is_string(form)) return clj_throw_msg("#inst literal expects a string, got: %s", clj_type_name(form));
	return clj_inst_parse(clj_string_bytes(form), clj_string_len(form));
}

static clj_value b_read_inst(const clj_value *args, size_t n) {
	(void)n;
	return clj_inst_read(args[0]);
}

void clj_inst_builtins_install(void) {
	clj_builtin_bind("inst?", b_inst_p, 1, 1);
	clj_builtin_bind("inst-ms", b_inst_ms, 1, 1);
	clj_builtin_bind("read-inst*", b_read_inst, 1, 1);
}
