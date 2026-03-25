/*
 * api_window.c - LightShell Window Management API
 *
 * Exposes lightshell.window.{setTitle, setSize, getSize, minimize, maximize, close}
 */

#include "api.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

/* lightshell.window.setTitle(title) */
static R8EValue api_window_set_title(R8EContext *ctx, R8EValue this_val,
                                      int argc, const R8EValue *argv) {
    (void)this_val;
    if (argc < 1 || !r8e_is_string(argv[0])) {
        r8e_throw_type_error(ctx, "window.setTitle: title must be a string");
        return R8E_UNDEFINED;
    }
    char buf[8];
    size_t len;
    const char *str = r8e_get_cstring(argv[0], buf, &len);
    char titlebuf[1024];
    snprintf(titlebuf, sizeof(titlebuf), "%.*s", (int)len, str);
    platform_set_title(titlebuf);
    return R8E_UNDEFINED;
}

/* lightshell.window.setSize(width, height) */
static R8EValue api_window_set_size(R8EContext *ctx, R8EValue this_val,
                                     int argc, const R8EValue *argv) {
    (void)this_val;
    if (argc < 2 || !r8e_is_number(argv[0]) || !r8e_is_number(argv[1])) {
        r8e_throw_type_error(ctx, "window.setSize: width and height must be numbers");
        return R8E_UNDEFINED;
    }
    int w = r8e_to_int32(argv[0]);
    int h = r8e_to_int32(argv[1]);
    platform_set_size(w, h);
    return R8E_UNDEFINED;
}

/* lightshell.window.getSize() → {width, height} */
static R8EValue api_window_get_size(R8EContext *ctx, R8EValue this_val,
                                     int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    int w, h;
    platform_get_size(&w, &h);
    R8EValue obj = r8e_make_object(ctx);
    r8e_set_prop(ctx, obj, "width", r8e_from_int32(w));
    r8e_set_prop(ctx, obj, "height", r8e_from_int32(h));
    return obj;
}

/* lightshell.window.minimize() */
static R8EValue api_window_minimize(R8EContext *ctx, R8EValue this_val,
                                     int argc, const R8EValue *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    platform_minimize();
    return R8E_UNDEFINED;
}

/* lightshell.window.maximize() */
static R8EValue api_window_maximize(R8EContext *ctx, R8EValue this_val,
                                     int argc, const R8EValue *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    platform_maximize();
    return R8E_UNDEFINED;
}

/* lightshell.window.close() */
static R8EValue api_window_close(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    platform_close();
    return R8E_UNDEFINED;
}

void ls_api_window_init(R8EContext *ctx) {
    R8EValue win = r8e_make_object(ctx);

    r8e_set_prop(ctx, win, "setTitle",
        r8e_make_native_func(ctx, api_window_set_title, "setTitle", 1));
    r8e_set_prop(ctx, win, "setSize",
        r8e_make_native_func(ctx, api_window_set_size, "setSize", 2));
    r8e_set_prop(ctx, win, "getSize",
        r8e_make_native_func(ctx, api_window_get_size, "getSize", 0));
    r8e_set_prop(ctx, win, "minimize",
        r8e_make_native_func(ctx, api_window_minimize, "minimize", 0));
    r8e_set_prop(ctx, win, "maximize",
        r8e_make_native_func(ctx, api_window_maximize, "maximize", 0));
    r8e_set_prop(ctx, win, "close",
        r8e_make_native_func(ctx, api_window_close, "close", 0));

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "window", win);
}
