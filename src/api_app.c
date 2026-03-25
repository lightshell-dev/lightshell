/*
 * api_app.c - LightShell App Lifecycle API
 *
 * Exposes lightshell.app.{quit(), version, dataDir}
 */

#include "api.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LIGHTSHELL_VERSION "0.1.0"

/* lightshell.app.quit() */
static R8EValue api_app_quit(R8EContext *ctx, R8EValue this_val,
                              int argc, const R8EValue *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    exit(0);
    return R8E_UNDEFINED;  /* unreachable */
}

/* getter: lightshell.app.version → "0.1.0" */
static R8EValue api_app_version(R8EContext *ctx, R8EValue this_val,
                                 int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return r8e_make_cstring(ctx, LIGHTSHELL_VERSION);
}

/* getter: lightshell.app.dataDir → platform-specific data directory */
static R8EValue api_app_data_dir(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    char path[1024];
#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (!home) home = "/";
    snprintf(path, sizeof(path), "%s/Library/Application Support/LightShell", home);
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
        snprintf(path, sizeof(path), "%s/lightshell", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/";
        snprintf(path, sizeof(path), "%s/.local/share/lightshell", home);
    }
#endif
    return r8e_make_cstring(ctx, path);
}

void ls_api_app_init(R8EContext *ctx) {
    R8EValue app = r8e_make_object(ctx);

    r8e_set_prop(ctx, app, "quit",
        r8e_make_native_func(ctx, api_app_quit, "quit", 0));

    /* version and dataDir are accessor properties (read as values, not function calls) */
    r8e_define_accessor(ctx, app, "version",
        r8e_make_native_func(ctx, api_app_version, "get version", 0), R8E_UNDEFINED);
    r8e_define_accessor(ctx, app, "dataDir",
        r8e_make_native_func(ctx, api_app_data_dir, "get dataDir", 0), R8E_UNDEFINED);

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "app", app);
}
