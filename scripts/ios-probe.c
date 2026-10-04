// @ai-generated(solo)
// The C runtime alone on iOS: no Swift, no UI. scripts/ios-sizes.sh builds, runs and measures it (design §10).
#include <stdio.h>
#include <string.h>

#include "clj/analyzer.h"
#include "clj/coro.h"
#include "clj/core.h"
#include "clj/eval.h"
#include "clj/printer.h"
#include "clj/reader.h"
#include "clj/runtime.h"
#include "clj/string.h"

void clj_host_boot(void);
void clj_host_boot(void) {}

static const char *const DEFAULT_FORMS[] = {
	"(+ 1 2)",
	"(mapv inc [1 2 3])",
	"(let [a (atom 0)] (dotimes [_ 10] (swap! a inc)) @a)",
	"(count (filter even? (range 10000)))",
	"(assoc {:a 1 :b 2} :c 3)",
	"(re-seq #\"\\d+\" \"a1b22c333\")",
	"(string? (str (random-uuid)))",
	"(require '[clojure.core.async :as a] '[clojure.string :as s])",
	// sched.c's main carrier is a CFRunLoopSource, coro.c maps the stack: a go block exercises both.
	"(a/<!! (a/go (a/<! (a/timeout 10)) :from-a-coroutine))",
	"@(future (reduce + (range 100000)))",
	// The shadow-stack bound, and guard.c's landing where a compiled frame overflows instead.
	"(try ((fn f [n] (inc (f n))) 0) (catch :default e (ex-message e)))",
	"(count (s/split (apply str (repeat 1000 \"a,\")) #\",\"))",
};

// prn would reach the output writer's own thread, out of step with this stream.
static void print_value(clj_value v) {
	clj_value text = clj_pr_str(v);
	if (text == CLJ_THROWN) {
		clj_value ex = clj_take_pending();
		printf("<unprintable>\n");
		clj_release(ex);
		return;
	}
	printf("%.*s\n", (int)clj_string_len(text), clj_string_bytes(text));
	clj_release(text);
}

static int eval_one(const char *src) {
	printf("  %s\n", src);
	clj_reader r;
	clj_reader_init(&r, src, strlen(src));
	clj_reader_use_namespaces(&r);
	clj_value last = CLJ_NIL;
	for (;;) {
		clj_value form = CLJ_NIL;
		switch (clj_read(&r, &form)) {
		case CLJ_READ_EOF:
			fputs("  => ", stdout);
			print_value(last);
			clj_release(last);
			return 0;
		case CLJ_READ_ERROR:
			printf("  !! read error: %s at %u:%u\n", clj_reader_message(&r), r.error_line, r.error_col);
			clj_release(last);
			return 1;
		default: break;
		}
		clj_env  env = {.ns = CLJ_NIL, .line = r.form_line, .col = r.form_col};
		clj_value v = clj_eval(form, &env);
		clj_release(form);
		if (v == CLJ_THROWN) {
			clj_value ex = clj_take_pending();
			fputs("  !! ", stdout);
			print_value(ex);
			clj_release(ex);
			clj_release(last);
			return 1;
		}
		clj_release(last);
		last = v;
	}
}

// A compiled core's frames reach a trace through getsectiondata on __TEXT,__cljframe (trace.c).
static void trace_check(void) {
	const char *src = "(mapv inc [1 nil 3])";
	clj_reader  r;
	clj_reader_init(&r, src, strlen(src));
	clj_reader_use_namespaces(&r);
	clj_value form = CLJ_NIL;
	if (clj_read(&r, &form) != CLJ_READ_OK) return;
	clj_env  env = {.ns = CLJ_NIL, .line = 1, .col = 1};
	clj_value v = clj_eval(form, &env);
	clj_release(form);
	if (v != CLJ_THROWN) {
		printf("trace: %s did not throw\n", src);
		clj_release(v);
		return;
	}
	clj_value ex = clj_take_pending();
	clj_value trace = clj_ex_trace(ex);
	fputs("trace of (mapv inc [1 nil 3]): ", stdout);
	print_value(trace);
	clj_release(ex);
}

int main(int argc, char **argv) {
	printf("ios-probe: core=%s\n",
#ifdef CLJ_COMPILED_CORE
	       "compiled"
#else
	       "interpreted"
#endif
	);
	size_t before = clj_debug_phys_footprint();
	clj_init();
	size_t booted = clj_debug_phys_footprint();
	printf("footprint: %zu KB before clj_init, %zu KB after boot (+%zu KB)\n",
	       before / 1024, booted / 1024, (booted - before) / 1024);
	int failures = 0;
	if (argc > 1) {
		for (int i = 1; i < argc; i++) failures += eval_one(argv[i]);
	} else {
		for (size_t i = 0; i < sizeof DEFAULT_FORMS / sizeof *DEFAULT_FORMS; i++) failures += eval_one(DEFAULT_FORMS[i]);
	}
	trace_check();
	printf("footprint: %zu KB after the forms\n", clj_debug_phys_footprint() / 1024);
	printf("ios-probe: %d of %d forms threw\n", failures, argc > 1 ? argc - 1 : (int)(sizeof DEFAULT_FORMS / sizeof *DEFAULT_FORMS));
	return failures == 0 ? 0 : 1;
}
