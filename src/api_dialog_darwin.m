/*
 * api_dialog_darwin.m - LightShell Dialog API (macOS)
 *
 * Exposes lightshell.dialog.{open, save, message}
 */

#import <Cocoa/Cocoa.h>
#include "api.h"
#include <string.h>

/* Helper: extract a string property from a JS object */
static const char *get_string_prop(R8EContext *ctx, R8EValue obj, const char *name,
                                    char *buf, size_t *len) {
    R8EValue val = r8e_get_prop(ctx, obj, name);
    if (r8e_is_undefined(val) || r8e_is_null(val)) return NULL;
    if (!r8e_is_string(val)) return NULL;
    return r8e_get_cstring(val, buf, len);
}

/* Helper: extract a bool property from a JS object */
static bool get_bool_prop(R8EContext *ctx, R8EValue obj, const char *name) {
    R8EValue val = r8e_get_prop(ctx, obj, name);
    return r8e_to_bool(val);
}

/*
 * lightshell.dialog.open({filters, multiple, directory}) → string[]
 *
 * Opens a native file-open dialog. Returns an array of selected paths,
 * or an empty array if cancelled.
 */
static R8EValue api_dialog_open(R8EContext *ctx, R8EValue this_val,
                                 int argc, const R8EValue *argv) {
    (void)this_val;

    bool directory = false;
    bool multiple = false;

    if (argc >= 1 && r8e_is_object(argv[0])) {
        directory = get_bool_prop(ctx, argv[0], "directory");
        multiple = get_bool_prop(ctx, argv[0], "multiple");
    }

    __block R8EValue result = r8e_make_array(ctx, 4);

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:!directory];
    [panel setCanChooseDirectories:directory];
    [panel setAllowsMultipleSelection:multiple];

    if ([panel runModal] == NSModalResponseOK) {
        uint32_t idx = 0;
        for (NSURL *url in [panel URLs]) {
            const char *path = [[url path] UTF8String];
            r8e_set_element(ctx, result, idx++, r8e_make_cstring(ctx, path));
        }
    }

    return result;
}

/*
 * lightshell.dialog.save({filters, defaultPath}) → string
 *
 * Opens a native file-save dialog. Returns the selected path,
 * or empty string if cancelled.
 */
static R8EValue api_dialog_save(R8EContext *ctx, R8EValue this_val,
                                 int argc, const R8EValue *argv) {
    (void)this_val;

    NSSavePanel *panel = [NSSavePanel savePanel];

    if (argc >= 1 && r8e_is_object(argv[0])) {
        char buf[8];
        size_t len;
        const char *defPath = get_string_prop(ctx, argv[0], "defaultPath", buf, &len);
        if (defPath) {
            char pathbuf[4096];
            snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)len, defPath);
            NSString *path = [NSString stringWithUTF8String:pathbuf];
            [panel setDirectoryURL:[NSURL fileURLWithPath:[path stringByDeletingLastPathComponent]]];
            [panel setNameFieldStringValue:[path lastPathComponent]];
        }
    }

    if ([panel runModal] == NSModalResponseOK) {
        const char *path = [[[panel URL] path] UTF8String];
        return r8e_make_cstring(ctx, path);
    }

    return r8e_make_cstring(ctx, "");
}

/*
 * lightshell.dialog.message(text, {type, title}) → undefined
 *
 * Shows a native message dialog.
 */
static R8EValue api_dialog_message(R8EContext *ctx, R8EValue this_val,
                                    int argc, const R8EValue *argv) {
    (void)this_val;

    if (argc < 1 || !r8e_is_string(argv[0])) {
        r8e_throw_type_error(ctx, "dialog.message: text must be a string");
        return R8E_UNDEFINED;
    }

    char buf[8];
    size_t len;
    const char *text = r8e_get_cstring(argv[0], buf, &len);
    char textbuf[4096];
    snprintf(textbuf, sizeof(textbuf), "%.*s", (int)len, text);

    NSString *title = @"Message";

    if (argc >= 2 && r8e_is_object(argv[1])) {
        char tbuf[8];
        size_t tlen;
        const char *t = get_string_prop(ctx, argv[1], "title", tbuf, &tlen);
        if (t) {
            char titlebuf[256];
            snprintf(titlebuf, sizeof(titlebuf), "%.*s", (int)tlen, t);
            title = [NSString stringWithUTF8String:titlebuf];
        }
    }

    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:title];
    [alert setInformativeText:[NSString stringWithUTF8String:textbuf]];
    [alert setAlertStyle:NSAlertStyleInformational];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];

    return R8E_UNDEFINED;
}

void ls_api_dialog_init(R8EContext *ctx) {
    R8EValue dialog = r8e_make_object(ctx);

    r8e_set_prop(ctx, dialog, "open",
        r8e_make_native_func(ctx, api_dialog_open, "open", 1));
    r8e_set_prop(ctx, dialog, "save",
        r8e_make_native_func(ctx, api_dialog_save, "save", 1));
    r8e_set_prop(ctx, dialog, "message",
        r8e_make_native_func(ctx, api_dialog_message, "message", 2));

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "dialog", dialog);
}
