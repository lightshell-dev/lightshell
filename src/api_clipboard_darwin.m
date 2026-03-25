/*
 * api_clipboard_darwin.m - LightShell Clipboard API (macOS)
 *
 * Exposes lightshell.clipboard.{read, write}
 */

#import <Cocoa/Cocoa.h>
#include "api.h"

/* lightshell.clipboard.read() → string */
static R8EValue api_clipboard_read(R8EContext *ctx, R8EValue this_val,
                                    int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    NSString *text = [pb stringForType:NSPasteboardTypeString];
    if (text == nil) {
        return r8e_make_cstring(ctx, "");
    }
    return r8e_make_cstring(ctx, [text UTF8String]);
}

/* lightshell.clipboard.write(text) → undefined */
static R8EValue api_clipboard_write(R8EContext *ctx, R8EValue this_val,
                                     int argc, const R8EValue *argv) {
    (void)this_val;
    if (argc < 1 || !r8e_is_string(argv[0])) {
        r8e_throw_type_error(ctx, "clipboard.write: text must be a string");
        return R8E_UNDEFINED;
    }

    char buf[8];
    size_t len;
    const char *text = r8e_get_cstring(argv[0], buf, &len);

    /* Need null-terminated copy since buf may not be */
    char *str = malloc(len + 1);
    memcpy(str, text, len);
    str[len] = '\0';

    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:[NSString stringWithUTF8String:str] forType:NSPasteboardTypeString];

    free(str);
    return R8E_UNDEFINED;
}

void ls_api_clipboard_init(R8EContext *ctx) {
    R8EValue clipboard = r8e_make_object(ctx);

    r8e_set_prop(ctx, clipboard, "read",
        r8e_make_native_func(ctx, api_clipboard_read, "read", 0));
    r8e_set_prop(ctx, clipboard, "write",
        r8e_make_native_func(ctx, api_clipboard_write, "write", 1));

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "clipboard", clipboard);
}
