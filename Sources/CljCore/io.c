// @ai-generated(solo)
// slurp, spit and file-seq's IO: on the blocking pool, as the loader's reads, so no carrier waits on the disk.
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "clj/core.h"
#include "clj/runtime.h"
#include "coro_internal.h"

// The JVM's FileNotFoundException text: "path (reason)".
static clj_value io_error(const char *path, int err) { return clj_throw_msg("%s (%s)", path, strerror(err)); }

static clj_value path_arg(const char *what, clj_value v) {
	if (clj_is_string(v)) return v;
	return clj_throw_msg("%s expects a path string, got: %s", what, clj_type_name(v));
}

typedef struct {
	const char *path;
	char       *bytes;
	size_t      len;
	int         err;
} slurp_job;

static void slurp_run(void *ctx) {
	slurp_job *j = ctx;
	FILE      *f = fopen(j->path, "rb");
	if (!f) {
		j->err = errno;
		return;
	}
	size_t cap = 0;
	for (;;) {
		if (j->len == cap) {
			cap = cap ? cap * 2 : 1 << 12;
			j->bytes = realloc(j->bytes, cap);
			if (!j->bytes) clj_fatal("out of memory");
		}
		size_t n = fread(j->bytes + j->len, 1, cap - j->len, f);
		if (n == 0) break;
		j->len += n;
	}
	if (ferror(f)) j->err = errno ? errno : EIO;
	fclose(f);
}

static clj_value b_slurp(const clj_value *args, size_t n) {
	(void)n;
	if (path_arg("slurp", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	slurp_job job = {clj_string_bytes(args[0]), NULL, 0, 0};
	clj_blocking(slurp_run, &job, sizeof job);
	if (job.err) {
		free(job.bytes);
		return io_error(clj_string_bytes(args[0]), job.err);
	}
	clj_value s = clj_string_new(job.bytes ? job.bytes : "", job.len);
	free(job.bytes);
	return s;
}

typedef struct {
	const char *path, *bytes;
	size_t      len;
	bool        append;
	int         err;
} spit_job;

static void spit_run(void *ctx) {
	spit_job *j = ctx;
	FILE     *f = fopen(j->path, j->append ? "ab" : "wb");
	if (!f) {
		j->err = errno;
		return;
	}
	if (fwrite(j->bytes, 1, j->len, f) != j->len) j->err = errno ? errno : EIO;
	if (fclose(f) != 0 && !j->err) j->err = errno;
}

static clj_value b_spit(const clj_value *args, size_t n) {
	(void)n;
	if (path_arg("spit", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_is_string(args[1])) return clj_throw_msg("spit* expects the text as a string, got: %s", clj_type_name(args[1]));
	spit_job job = {clj_string_bytes(args[0]), clj_string_bytes(args[1]), clj_string_len(args[1]), clj_truthy(args[2]), 0};
	clj_blocking(spit_run, &job, sizeof job);
	return job.err ? io_error(clj_string_bytes(args[0]), job.err) : CLJ_NIL;
}

typedef struct {
	const char *path;
	char      **names;
	size_t      n;
	bool        dir;
} list_job;

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void list_run(void *ctx) {
	list_job *j = ctx;
	DIR      *d = opendir(j->path);
	if (!d) return;
	j->dir = true;
	size_t cap = 0;
	for (struct dirent *e; (e = readdir(d));) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
		if (j->n == cap) {
			cap = cap ? cap * 2 : 16;
			j->names = realloc(j->names, cap * sizeof *j->names);
			if (!j->names) clj_fatal("out of memory");
		}
		j->names[j->n] = strdup(e->d_name);
		if (!j->names[j->n]) clj_fatal("out of memory");
		j->n++;
	}
	closedir(d);
	// readdir's order is the file system's; sorted, a walk answers the same on every machine.
	qsort(j->names, j->n, sizeof *j->names, by_name);
}

// The children of a directory as paths, sorted by name; nil when path is no directory.
static clj_value b_dir_children(const clj_value *args, size_t n) {
	(void)n;
	if (path_arg("file-seq", args[0]) == CLJ_THROWN) return CLJ_THROWN;
	list_job job = {clj_string_bytes(args[0]), NULL, 0, false};
	clj_blocking(list_run, &job, sizeof job);
	if (!job.dir) return CLJ_NIL;
	const char *dir = clj_string_bytes(args[0]);
	size_t      dlen = clj_string_len(args[0]);
	bool        slash = dlen && dir[dlen - 1] == '/';
	clj_value   v = clj_vector_empty();
	for (size_t i = 0; i < job.n; i++) {
		size_t nlen = strlen(job.names[i]), len = dlen + (slash ? 0 : 1) + nlen;
		char  *p = malloc(len + 1);
		if (!p) clj_fatal("out of memory");
		memcpy(p, dir, dlen);
		if (!slash) p[dlen] = '/';
		memcpy(p + len - nlen, job.names[i], nlen + 1);
		clj_value s = clj_string_new(p, len);
		v = clj_vector_conj(v, s);
		clj_release(s);
		free(p);
		free(job.names[i]);
	}
	free(job.names);
	return v;
}

void clj_io_builtins_install(void) {
	clj_builtin_bind("slurp*", b_slurp, 1, 1);
	clj_builtin_bind("spit*", b_spit, 3, 3);
	clj_builtin_bind("dir-children*", b_dir_children, 1, 1);
}
