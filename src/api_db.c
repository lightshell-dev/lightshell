#include "api.h"
#include "stow.h"
#include <string.h>
#include <stdio.h>

static StowDB *g_db = NULL;

/* lightshell.db.open(path) — open/create database */
static R8EValue api_db_open(R8EContext *ctx, R8EValue this_val,
                              int argc, const R8EValue *argv) {
    (void)this_val;
    if (argc < 1) return R8E_UNDEFINED;
    char buf[256]; size_t len;
    const char *path = r8e_get_cstring(argv[0], buf, &len);
    char pathbuf[512];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, path);

    if (g_db) stow_close(g_db);
    g_db = stow_open(pathbuf);
    return g_db ? R8E_TRUE : R8E_FALSE;
}

/* lightshell.db.set(key, value) */
static R8EValue api_db_set(R8EContext *ctx, R8EValue this_val,
                             int argc, const R8EValue *argv) {
    (void)this_val;
    if (!g_db || argc < 2) return R8E_FALSE;
    char kbuf[256]; size_t klen;
    const char *key = r8e_get_cstring(argv[0], kbuf, &klen);
    char vbuf[1024]; size_t vlen;
    const char *val = r8e_get_cstring(argv[1], vbuf, &vlen);

    char keybuf[512];
    snprintf(keybuf, sizeof(keybuf), "%.*s", (int)klen, key);

    int r = stow_set(g_db, keybuf, val, (uint32_t)vlen);
    return r == STOW_OK ? R8E_TRUE : R8E_FALSE;
}

/* lightshell.db.get(key) */
static R8EValue api_db_get(R8EContext *ctx, R8EValue this_val,
                             int argc, const R8EValue *argv) {
    (void)this_val;
    if (!g_db || argc < 1) return R8E_UNDEFINED;
    char kbuf[256]; size_t klen;
    const char *key = r8e_get_cstring(argv[0], kbuf, &klen);
    char keybuf[512];
    snprintf(keybuf, sizeof(keybuf), "%.*s", (int)klen, key);

    void *val; uint32_t vlen;
    int r = stow_get(g_db, keybuf, &val, &vlen);
    if (r != STOW_OK) return R8E_UNDEFINED;

    return r8e_make_string(ctx, (const char *)val, vlen);
}

/* lightshell.db.delete(key) */
static R8EValue api_db_delete(R8EContext *ctx, R8EValue this_val,
                                int argc, const R8EValue *argv) {
    (void)this_val;
    if (!g_db || argc < 1) return R8E_FALSE;
    char kbuf[256]; size_t klen;
    const char *key = r8e_get_cstring(argv[0], kbuf, &klen);
    char keybuf[512];
    snprintf(keybuf, sizeof(keybuf), "%.*s", (int)klen, key);

    int r = stow_delete(g_db, keybuf);
    return r == STOW_OK ? R8E_TRUE : R8E_FALSE;
}

/* lightshell.db.close() */
static R8EValue api_db_close(R8EContext *ctx, R8EValue this_val,
                               int argc, const R8EValue *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    if (g_db) { stow_close(g_db); g_db = NULL; }
    return R8E_UNDEFINED;
}

/* Registration */
void ls_api_db_init(R8EContext *ctx) {
    R8EValue db = r8e_make_object(ctx);
    r8e_set_prop(ctx, db, "open", r8e_make_native_func(ctx, api_db_open, "open", 1));
    r8e_set_prop(ctx, db, "set", r8e_make_native_func(ctx, api_db_set, "set", 2));
    r8e_set_prop(ctx, db, "get", r8e_make_native_func(ctx, api_db_get, "get", 1));
    r8e_set_prop(ctx, db, "delete", r8e_make_native_func(ctx, api_db_delete, "delete", 1));
    r8e_set_prop(ctx, db, "close", r8e_make_native_func(ctx, api_db_close, "close", 0));

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "db", db);
}
