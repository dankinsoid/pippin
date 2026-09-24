// @ai-generated(guided)
// Loads one .clj file through the C core and nothing else. Built under ASan it answers a crash with a real
// native stack, where the swift-testing run prints "<empty stack>" (NOTES.md, "Guard").
#include <stdio.h>
#include <stdlib.h>

#include "clj/core.h"
#include "clj/printer.h"
#include "clj/runtime.h"

// The weak boot hook of a Swift host; a C-only host comes up without one (design §5).
void clj_host_boot(void);
void clj_host_boot(void) {}

static char *read_all(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long size = ftell(f);
	rewind(f);
	char *buf = size < 0 ? NULL : malloc((size_t)size + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	*len = fread(buf, 1, (size_t)size, f);
	buf[*len] = '\0';
	fclose(f);
	return buf;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: clj-load <file.clj>\n");
		return 2;
	}
	size_t len = 0;
	char  *src = read_all(argv[1], &len);
	if (!src) {
		fprintf(stderr, "clj-load: cannot read %s\n", argv[1]);
		return 2;
	}
	clj_init();
	clj_value path = clj_string_from_cstr(argv[1]);
	clj_value r = clj_load_source(src, len, path);
	clj_release(path);
	free(src);
	if (r != CLJ_THROWN) return 0;
	clj_value ex = clj_take_pending();
	clj_value text = clj_pr_str(ex);
	// A throw while printing the throw: the first one is still what the caller needs.
	if (text != CLJ_THROWN) {
		fprintf(stderr, "%.*s\n", (int)clj_string_len(text), clj_string_bytes(text));
		clj_release(text);
	}
	clj_release(ex);
	return 1;
}
