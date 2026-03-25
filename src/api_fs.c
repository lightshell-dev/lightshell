/*
 * api_fs.c - LightShell File System API (POSIX)
 *
 * Exposes lightshell.fs.{readFile, writeFile, readDir, exists, stat, mkdir, remove}
 * All operations are synchronous for v1 (return values directly, not promises).
 */

#include "api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Helper: extract path string from first argument */
static const char *get_path_arg(R8EContext *ctx, int argc, const R8EValue *argv,
                                 char *buf, size_t *len) {
    if (argc < 1 || !r8e_is_string(argv[0])) {
        r8e_throw_type_error(ctx, "fs: path must be a string");
        return NULL;
    }
    return r8e_get_cstring(argv[0], buf, len);
}

/* lightshell.fs.readFile(path, encoding) → string */
static R8EValue api_fs_read_file(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val;
    char buf[8];
    size_t len;
    const char *path = get_path_arg(ctx, argc, argv, buf, &len);
    if (!path) return R8E_UNDEFINED;

    /* Copy path since buf may be temporary */
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    FILE *f = fopen(pathbuf, "rb");
    if (!f) {
        r8e_throw_error(ctx, "fs.readFile: cannot open '%s': %s", pathbuf, strerror(errno));
        return R8E_UNDEFINED;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0 || fsize > 64 * 1024 * 1024) {
        fclose(f);
        r8e_throw_error(ctx, "fs.readFile: file too large or unreadable");
        return R8E_UNDEFINED;
    }

    char *data = malloc((size_t)fsize + 1);
    if (!data) {
        fclose(f);
        r8e_throw_error(ctx, "fs.readFile: out of memory");
        return R8E_UNDEFINED;
    }

    size_t nread = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    data[nread] = '\0';

    R8EValue result = r8e_make_string(ctx, data, nread);
    free(data);
    return result;
}

/* lightshell.fs.writeFile(path, data) → undefined */
static R8EValue api_fs_write_file(R8EContext *ctx, R8EValue this_val,
                                   int argc, const R8EValue *argv) {
    (void)this_val;
    char buf[8];
    size_t len;
    const char *path = get_path_arg(ctx, argc, argv, buf, &len);
    if (!path) return R8E_UNDEFINED;

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    if (argc < 2 || !r8e_is_string(argv[1])) {
        r8e_throw_type_error(ctx, "fs.writeFile: data must be a string");
        return R8E_UNDEFINED;
    }

    char dbuf[8];
    size_t dlen;
    const char *data = r8e_get_cstring(argv[1], dbuf, &dlen);

    FILE *f = fopen(pathbuf, "wb");
    if (!f) {
        r8e_throw_error(ctx, "fs.writeFile: cannot open '%s': %s", pathbuf, strerror(errno));
        return R8E_UNDEFINED;
    }

    fwrite(data, 1, dlen, f);
    fclose(f);
    return R8E_UNDEFINED;
}

/* lightshell.fs.readDir(path) → string[] */
static R8EValue api_fs_read_dir(R8EContext *ctx, R8EValue this_val,
                                 int argc, const R8EValue *argv) {
    (void)this_val;
    char buf[8];
    size_t len;
    const char *path = get_path_arg(ctx, argc, argv, buf, &len);
    if (!path) return R8E_UNDEFINED;

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    DIR *dir = opendir(pathbuf);
    if (!dir) {
        r8e_throw_error(ctx, "fs.readDir: cannot open '%s': %s", pathbuf, strerror(errno));
        return R8E_UNDEFINED;
    }

    R8EValue arr = r8e_make_array(ctx, 16);
    uint32_t idx = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;
        R8EValue name = r8e_make_cstring(ctx, entry->d_name);
        r8e_set_element(ctx, arr, idx++, name);
    }
    closedir(dir);
    return arr;
}

/* lightshell.fs.exists(path) → bool */
static R8EValue api_fs_exists(R8EContext *ctx, R8EValue this_val,
                               int argc, const R8EValue *argv) {
    (void)this_val;
    char buf[8];
    size_t len;
    const char *path = get_path_arg(ctx, argc, argv, buf, &len);
    if (!path) return R8E_FALSE;

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    struct stat st;
    return (stat(pathbuf, &st) == 0) ? R8E_TRUE : R8E_FALSE;
}

/* lightshell.fs.stat(path) → {size, mtime, isDir} */
static R8EValue api_fs_stat(R8EContext *ctx, R8EValue this_val,
                             int argc, const R8EValue *argv) {
    (void)this_val;
    char buf[8];
    size_t len;
    const char *path = get_path_arg(ctx, argc, argv, buf, &len);
    if (!path) return R8E_UNDEFINED;

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    struct stat st;
    if (stat(pathbuf, &st) != 0) {
        r8e_throw_error(ctx, "fs.stat: cannot stat '%s': %s", pathbuf, strerror(errno));
        return R8E_UNDEFINED;
    }

    R8EValue obj = r8e_make_object(ctx);
    r8e_set_prop(ctx, obj, "size", r8e_make_number((double)st.st_size));
    r8e_set_prop(ctx, obj, "mtime", r8e_make_number((double)st.st_mtime));
    r8e_set_prop(ctx, obj, "isDir", S_ISDIR(st.st_mode) ? R8E_TRUE : R8E_FALSE);
    return obj;
}

/* lightshell.fs.mkdir(path) → undefined */
static R8EValue api_fs_mkdir(R8EContext *ctx, R8EValue this_val,
                              int argc, const R8EValue *argv) {
    (void)this_val;
    char buf[8];
    size_t len;
    const char *path = get_path_arg(ctx, argc, argv, buf, &len);
    if (!path) return R8E_UNDEFINED;

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    if (mkdir(pathbuf, 0755) != 0 && errno != EEXIST) {
        r8e_throw_error(ctx, "fs.mkdir: cannot create '%s': %s", pathbuf, strerror(errno));
    }
    return R8E_UNDEFINED;
}

/* lightshell.fs.remove(path) → undefined */
static R8EValue api_fs_remove(R8EContext *ctx, R8EValue this_val,
                               int argc, const R8EValue *argv) {
    (void)this_val;
    char buf[8];
    size_t len;
    const char *path = get_path_arg(ctx, argc, argv, buf, &len);
    if (!path) return R8E_UNDEFINED;

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    struct stat st;
    if (stat(pathbuf, &st) != 0) {
        r8e_throw_error(ctx, "fs.remove: '%s' not found: %s", pathbuf, strerror(errno));
        return R8E_UNDEFINED;
    }

    int rc;
    if (S_ISDIR(st.st_mode)) {
        rc = rmdir(pathbuf);
    } else {
        rc = unlink(pathbuf);
    }

    if (rc != 0) {
        r8e_throw_error(ctx, "fs.remove: cannot remove '%s': %s", pathbuf, strerror(errno));
    }
    return R8E_UNDEFINED;
}

void ls_api_fs_init(R8EContext *ctx) {
    R8EValue fs = r8e_make_object(ctx);

    r8e_set_prop(ctx, fs, "readFile",
        r8e_make_native_func(ctx, api_fs_read_file, "readFile", 2));
    r8e_set_prop(ctx, fs, "writeFile",
        r8e_make_native_func(ctx, api_fs_write_file, "writeFile", 2));
    r8e_set_prop(ctx, fs, "readDir",
        r8e_make_native_func(ctx, api_fs_read_dir, "readDir", 1));
    r8e_set_prop(ctx, fs, "exists",
        r8e_make_native_func(ctx, api_fs_exists, "exists", 1));
    r8e_set_prop(ctx, fs, "stat",
        r8e_make_native_func(ctx, api_fs_stat, "stat", 1));
    r8e_set_prop(ctx, fs, "mkdir",
        r8e_make_native_func(ctx, api_fs_mkdir, "mkdir", 1));
    r8e_set_prop(ctx, fs, "remove",
        r8e_make_native_func(ctx, api_fs_remove, "remove", 1));

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "fs", fs);
}
