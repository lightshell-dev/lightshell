/*
 * api_menu_darwin.m - LightShell Menu API (macOS)
 *
 * Exposes lightshell.menu.set(template)
 * Template format: [{label, submenu: [{label, accelerator, click}]}]
 */

#import <Cocoa/Cocoa.h>
#include "api.h"
#include <string.h>

/* Helper: extract a string prop, returning a temporary NSString or nil */
static NSString *get_ns_string_prop(R8EContext *ctx, R8EValue obj, const char *name) {
    R8EValue val = r8e_get_prop(ctx, obj, name);
    if (r8e_is_undefined(val) || r8e_is_null(val) || !r8e_is_string(val))
        return nil;
    char buf[8];
    size_t len;
    const char *str = r8e_get_cstring(val, buf, &len);
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%.*s", (int)len, str);
    return [NSString stringWithUTF8String:tmp];
}

/* Convert accelerator string like "CmdOrCtrl+Q" to NSEvent key equivalent */
static NSString *parse_accelerator(NSString *accel, NSUInteger *modMask) {
    *modMask = 0;
    if (!accel || [accel length] == 0) return @"";

    NSArray *parts = [accel componentsSeparatedByString:@"+"];
    NSString *key = @"";
    for (NSString *part in parts) {
        NSString *lower = [part lowercaseString];
        if ([lower isEqualToString:@"cmd"] || [lower isEqualToString:@"cmdorctrl"] ||
            [lower isEqualToString:@"command"]) {
            *modMask |= NSEventModifierFlagCommand;
        } else if ([lower isEqualToString:@"shift"]) {
            *modMask |= NSEventModifierFlagShift;
        } else if ([lower isEqualToString:@"alt"] || [lower isEqualToString:@"option"]) {
            *modMask |= NSEventModifierFlagOption;
        } else if ([lower isEqualToString:@"ctrl"] || [lower isEqualToString:@"control"]) {
            *modMask |= NSEventModifierFlagControl;
        } else {
            key = lower;
        }
    }
    return key;
}

/*
 * lightshell.menu.set(template) → void
 *
 * template: array of {label, submenu: [{label, accelerator}]}
 * Sets the macOS application menu bar.
 */
static R8EValue api_menu_set(R8EContext *ctx, R8EValue this_val,
                              int argc, const R8EValue *argv) {
    (void)this_val;

    if (argc < 1 || !r8e_is_array(argv[0])) {
        r8e_throw_type_error(ctx, "menu.set: template must be an array");
        return R8E_UNDEFINED;
    }

    R8EValue template = argv[0];
    int32_t menuCount = r8e_get_length(ctx, template);

    NSMenu *mainMenu = [[NSMenu alloc] init];

    for (int32_t i = 0; i < menuCount; i++) {
        R8EValue menuItem = r8e_get_element(ctx, template, (uint32_t)i);
        if (r8e_is_undefined(menuItem)) continue;

        NSString *label = get_ns_string_prop(ctx, menuItem, "label");
        if (!label) label = @"";

        NSMenuItem *topItem = [[NSMenuItem alloc] init];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:label];

        R8EValue submenuArr = r8e_get_prop(ctx, menuItem, "submenu");
        if (r8e_is_array(submenuArr)) {
            int32_t subCount = r8e_get_length(ctx, submenuArr);
            for (int32_t j = 0; j < subCount; j++) {
                R8EValue subItem = r8e_get_element(ctx, submenuArr, (uint32_t)j);
                if (r8e_is_undefined(subItem)) continue;

                NSString *subLabel = get_ns_string_prop(ctx, subItem, "label");
                if (!subLabel) {
                    /* Separator */
                    [submenu addItem:[NSMenuItem separatorItem]];
                    continue;
                }

                NSString *accel = get_ns_string_prop(ctx, subItem, "accelerator");
                NSUInteger modMask = 0;
                NSString *key = parse_accelerator(accel, &modMask);

                /* Map known labels to standard actions */
                SEL action = NULL;
                NSString *lowerLabel = [subLabel lowercaseString];
                if ([lowerLabel isEqualToString:@"quit"] || [lowerLabel isEqualToString:@"exit"]) {
                    action = @selector(terminate:);
                } else if ([lowerLabel isEqualToString:@"copy"]) {
                    action = @selector(copy:);
                } else if ([lowerLabel isEqualToString:@"paste"]) {
                    action = @selector(paste:);
                } else if ([lowerLabel isEqualToString:@"cut"]) {
                    action = @selector(cut:);
                } else if ([lowerLabel isEqualToString:@"undo"]) {
                    action = @selector(undo:);
                } else if ([lowerLabel isEqualToString:@"redo"]) {
                    action = @selector(redo:);
                } else if ([lowerLabel isEqualToString:@"select all"]) {
                    action = @selector(selectAll:);
                }

                NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:subLabel
                                                              action:action
                                                       keyEquivalent:key];
                if (modMask) {
                    [item setKeyEquivalentModifierMask:modMask];
                }
                [submenu addItem:item];
            }
        }

        [topItem setSubmenu:submenu];
        [mainMenu addItem:topItem];
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp setMainMenu:mainMenu];
    });

    return R8E_UNDEFINED;
}

void ls_api_menu_init(R8EContext *ctx) {
    R8EValue menu = r8e_make_object(ctx);

    r8e_set_prop(ctx, menu, "set",
        r8e_make_native_func(ctx, api_menu_set, "set", 1));

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "menu", menu);
}
