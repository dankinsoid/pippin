// @ai-generated(solo)
// The iOS bundle's shell; the screen itself is screen.clj (docs/notes/ios.md).
#include <dispatch/dispatch.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/core.h"

// The weak boot hook of a Swift host; this shell is C-only and comes up without one (design §5).
void clj_host_boot(void);
void clj_host_boot(void) {}

// UIKit declares this in an Objective-C header, and the shell is C.
// NULL for the delegate class: UIKit allocates that class itself, which objc-reify cannot be (docs/notes/ios.md).
extern int UIApplicationMain(int argc, char *argv[], const void *principal_class, const void *delegate_class);

static void report(const char *what) {
	printf("pippin-app: footprint %zu KB %s\n", clj_debug_phys_footprint() / 1024, what);
	fflush(stdout);
}

// The run loop has turned by now, so this is the screen standing, not the boot (CLJ_APP_SETTLE_MS).
static void settled(void *ctx) {
	(void)ctx;
	report("with the screen up");
	if (getenv("CLJ_APP_EXIT")) exit(0);
}

static int load_screen(const char *exe) {
	char  *copy = strdup(exe);
	char   path[4096];
	size_t n = (size_t)snprintf(path, sizeof path, "%s/screen.clj", dirname(copy));
	free(copy);
	if (n >= sizeof path) return 1;
	clj_value name = clj_string_from_cstr(path);
	clj_value r = clj_load_file(name);
	clj_release(name);
	if (r != CLJ_THROWN) return 0;
	clj_value ex = clj_take_pending();
	clj_value text = clj_pr_str(ex);
	if (text != CLJ_THROWN) {
		printf("pippin-app: screen.clj threw: %.*s\n", (int)clj_string_len(text), clj_string_bytes(text));
		clj_release(text);
	}
	clj_release(ex);
	return 1;
}

int main(int argc, char **argv) {
	printf("pippin-app: core=%s\n",
#ifdef CLJ_COMPILED_CORE
	       "compiled"
#else
	       "interpreted"
#endif
	);
	size_t before = clj_debug_phys_footprint();
	clj_init();
	size_t booted = clj_debug_phys_footprint();
	printf("pippin-app: footprint %zu KB before clj_init, %zu KB after boot (+%zu KB)\n",
	       before / 1024, booted / 1024, (booted - before) / 1024);
	// UIKit drives the screen from the main thread, and an atom with :affinity :main needs the carrier there.
	clj_sched_main_install();
	int failed = load_screen(argv[0]);
	report("after screen.clj");
	const char *ms = getenv("CLJ_APP_SETTLE_MS");
	int64_t     settle = ms ? atoll(ms) : 3000;
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, settle * NSEC_PER_MSEC), dispatch_get_main_queue(), NULL, settled);
	fflush(stdout);
	if (failed) return 1;
	return UIApplicationMain(argc, argv, NULL, NULL);
}
