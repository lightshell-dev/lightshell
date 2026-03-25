/*
 * api_console.c - console.log / console.error / console.warn
 *
 * Registered as globals (not on lightshell namespace).
 * Each argument is converted to string and printed space-separated.
 */

#include "api.h"
#include <stdio.h>
#include <string.h>

/* Helper: print all arguments space-separated to the given stream */
static void print_args(R8EContext *ctx, FILE *stream, const char *prefix,
                       int argc, const R8EValue *argv) {
    if (prefix) {
        fputs(prefix, stream);
    }
    for (int i = 0; i < argc; i++) {
        if (i > 0) fputc(' ', stream);
        R8EValue str_val = r8e_to_string(ctx, argv[i]);
        char buf[8];
        size_t len;
        const char *s = r8e_get_cstring(str_val, buf, &len);
        fwrite(s, 1, len, stream);
    }
    fputc('\n', stream);
    fflush(stream);
}

/* console.log(...) → fprintf(stdout, ...) */
static R8EValue api_console_log(R8EContext *ctx, R8EValue this_val,
                                 int argc, const R8EValue *argv) {
    (void)this_val;
    print_args(ctx, stdout, NULL, argc, argv);
    return R8E_UNDEFINED;
}

/* console.error(...) → fprintf(stderr, ...) */
static R8EValue api_console_error(R8EContext *ctx, R8EValue this_val,
                                   int argc, const R8EValue *argv) {
    (void)this_val;
    print_args(ctx, stderr, NULL, argc, argv);
    return R8E_UNDEFINED;
}

/* console.warn(...) → fprintf(stderr, "[WARN] ...") */
static R8EValue api_console_warn(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val;
    print_args(ctx, stderr, "[WARN] ", argc, argv);
    return R8E_UNDEFINED;
}

void ls_api_console_init(R8EContext *ctx) {
    R8EValue console = r8e_make_object(ctx);

    r8e_set_prop(ctx, console, "log",
        r8e_make_native_func(ctx, api_console_log, "log", -1));
    r8e_set_prop(ctx, console, "error",
        r8e_make_native_func(ctx, api_console_error, "error", -1));
    r8e_set_prop(ctx, console, "warn",
        r8e_make_native_func(ctx, api_console_warn, "warn", -1));

    r8e_set_global(ctx, "console", console);
}
