/*
 * api.h - LightShell Native API declarations
 *
 * Each API module registers JS functions on the lightshell.* namespace.
 */

#ifndef LIGHTSHELL_API_H
#define LIGHTSHELL_API_H

#include "r8e_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* File system API (POSIX) */
void ls_api_fs_init(R8EContext *ctx);

/* System info API (cross-platform) */
void ls_api_sysinfo_init(R8EContext *ctx);

/* Clipboard API (macOS) */
void ls_api_clipboard_init(R8EContext *ctx);

/* Shell API (macOS) */
void ls_api_shell_init(R8EContext *ctx);

/* Dialog API (macOS) */
void ls_api_dialog_init(R8EContext *ctx);

/* Menu API (macOS) */
void ls_api_menu_init(R8EContext *ctx);

/* Window management API */
void ls_api_window_init(R8EContext *ctx);

/* App lifecycle API */
void ls_api_app_init(R8EContext *ctx);

/* Console API (console.log/error/warn) */
void ls_api_console_init(R8EContext *ctx);

/* Timer API (setTimeout/setInterval/clear*) */
void ls_api_timers_init(R8EContext *ctx);

/* Tick timers — call each frame from event loop */
void ls_timers_tick(R8EContext *ctx);

/* Helper: get or create the lightshell global namespace */
static inline R8EValue ls_get_namespace(R8EContext *ctx) {
    R8EValue ls = r8e_get_global(ctx, "lightshell");
    if (r8e_is_undefined(ls) || r8e_is_null(ls)) {
        ls = r8e_make_object(ctx);
        r8e_set_global(ctx, "lightshell", ls);
    }
    return ls;
}

#ifdef __cplusplus
}
#endif

#endif /* LIGHTSHELL_API_H */
