/*
 * api_shell_darwin.m - LightShell Shell API (macOS)
 *
 * Exposes lightshell.shell.open(url)
 */

#import <Cocoa/Cocoa.h>
#include "api.h"

/* lightshell.shell.open(url) → void */
static R8EValue api_shell_open(R8EContext *ctx, R8EValue this_val,
                                int argc, const R8EValue *argv) {
    (void)this_val;
    if (argc < 1 || !r8e_is_string(argv[0])) {
        r8e_throw_type_error(ctx, "shell.open: url must be a string");
        return R8E_UNDEFINED;
    }

    char buf[8];
    size_t len;
    const char *url = r8e_get_cstring(argv[0], buf, &len);

    char *str = malloc(len + 1);
    memcpy(str, url, len);
    str[len] = '\0';

    NSString *nsURL = [NSString stringWithUTF8String:str];
    NSURL *nsurl = [NSURL URLWithString:nsURL];
    if (nsurl) {
        [[NSWorkspace sharedWorkspace] openURL:nsurl];
    }

    free(str);
    return R8E_UNDEFINED;
}

void ls_api_shell_init(R8EContext *ctx) {
    R8EValue shell = r8e_make_object(ctx);

    r8e_set_prop(ctx, shell, "open",
        r8e_make_native_func(ctx, api_shell_open, "open", 1));

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "shell", shell);
}
