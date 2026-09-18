// @ai-generated(guided)
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clj/keyword.h"
#include "clj/lock.h"
#include "clj/map.h"
#include "clj/vector.h"
#include "profile_internal.h"

uint8_t clj_instrument;

static void set_instrument(uint8_t bit, bool on) {
	if (on) clj_instrument |= bit;
	else clj_instrument &= (uint8_t)~bit;
}

// ---- signposts

#ifdef __APPLE__
os_log_t clj_signposts_log;

static pthread_once_t log_once = PTHREAD_ONCE_INIT;

static void make_log(void) { clj_signposts_log = os_log_create("clj", OS_LOG_CATEGORY_POINTS_OF_INTEREST); }

void clj_signposts_enable(bool on) {
	pthread_once(&log_once, make_log);
	set_instrument(CLJ_INSTRUMENT_SIGNPOSTS, on);
}

bool clj_signposts_enabled(void) { return (clj_instrument & CLJ_INSTRUMENT_SIGNPOSTS) != 0; }
#else
void clj_signposts_enable(bool on) { (void)on; }
bool clj_signposts_enabled(void) { return false; }
#endif

// ---- fn profiler

uint64_t clj_profile_now(void) {
#ifdef __APPLE__
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

typedef struct {
	const clj_node *node; // retained: the report reads its name and position after the tree may have died
	uint64_t        calls, ns;
} profile_entry;

static clj_lock lock = CLJ_LOCK_INIT;
static profile_entry  *table;
static size_t          cap, used;

static size_t slot_of(const clj_node *node, size_t mask) { return (((uintptr_t)node >> 4) * 0x9E3779B97F4A7C15u) & mask; }

static void grow(void) {
	size_t         ncap = cap ? cap * 2 : 64;
	profile_entry *ntable = calloc(ncap, sizeof *ntable);
	if (!ntable) clj_fatal("out of memory");
	for (size_t i = 0; i < cap; i++) {
		if (!table[i].node) continue;
		size_t j = slot_of(table[i].node, ncap - 1);
		while (ntable[j].node) j = (j + 1) & (ncap - 1);
		ntable[j] = table[i];
	}
	free(table);
	table = ntable;
	cap = ncap;
}

static profile_entry *entry_for(const clj_node *node) {
	if (used * 2 >= cap) grow();
	size_t i = slot_of(node, cap - 1);
	while (table[i].node && table[i].node != node) i = (i + 1) & (cap - 1);
	if (!table[i].node) {
		table[i].node = node;
		clj_retain(clj_from_ptr((void *)node));
		used++;
	}
	return &table[i];
}

void clj_profile_record(const clj_node *fn_node, uint64_t ns) {
	clj_lock_lock(&lock);
	if (clj_profile_running()) {
		profile_entry *e = entry_for(fn_node);
		e->calls++;
		e->ns += ns;
	}
	clj_lock_unlock(&lock);
}

static void clear(void) {
	for (size_t i = 0; i < cap; i++) {
		if (table[i].node) clj_release(clj_from_ptr((void *)table[i].node));
	}
	free(table);
	table = NULL;
	cap = used = 0;
}

void clj_profile_start(void) {
	clj_lock_lock(&lock);
	clear();
	set_instrument(CLJ_INSTRUMENT_PROFILE, true);
	clj_lock_unlock(&lock);
}

bool clj_profile_running(void) { return (clj_instrument & CLJ_INSTRUMENT_PROFILE) != 0; }

static int by_ns_desc(const void *a, const void *b) {
	const profile_entry *x = a, *y = b;
	return x->ns < y->ns ? 1 : x->ns > y->ns ? -1 : 0;
}

static pthread_once_t keywords_once = PTHREAD_ONCE_INIT;
static clj_value      kw_fns, kw_name, kw_line, kw_column, kw_calls, kw_ns;

static void intern_keywords(void) {
	kw_fns = clj_keyword_from_cstr("fns");
	kw_name = clj_keyword_from_cstr("name");
	kw_line = clj_keyword_from_cstr("line");
	kw_column = clj_keyword_from_cstr("column");
	kw_calls = clj_keyword_from_cstr("calls");
	kw_ns = clj_keyword_from_cstr("ns");
}

void clj_profile_intern_keywords(void) { pthread_once(&keywords_once, intern_keywords); }

clj_value clj_profile_stop(void) {
	pthread_once(&keywords_once, intern_keywords);
	clj_lock_lock(&lock);
	set_instrument(CLJ_INSTRUMENT_PROFILE, false);
	profile_entry *entries = calloc(used ? used : 1, sizeof *entries);
	if (!entries) clj_fatal("out of memory");
	size_t n = 0;
	for (size_t i = 0; i < cap; i++) {
		if (table[i].node) entries[n++] = table[i];
	}
	qsort(entries, n, sizeof *entries, by_ns_desc);
	clj_value fns = clj_vector_empty();
	for (size_t i = 0; i < n; i++) {
		const clj_node *node = entries[i].node;
		clj_value       m = clj_map_assoc(clj_map_empty(), kw_name, node->u.fn.name);
		m = clj_map_assoc(m, kw_line, clj_fixnum(node->line));
		m = clj_map_assoc(m, kw_column, clj_fixnum(node->col));
		m = clj_map_assoc(m, kw_calls, clj_fixnum((intptr_t)entries[i].calls));
		m = clj_map_assoc(m, kw_ns, clj_fixnum((intptr_t)entries[i].ns));
		fns = clj_vector_conj(fns, m);
		clj_release(m);
	}
	free(entries);
	clear();
	clj_lock_unlock(&lock);
	clj_value data = clj_map_assoc(clj_map_empty(), kw_fns, fns);
	clj_release(fns);
	return data;
}
