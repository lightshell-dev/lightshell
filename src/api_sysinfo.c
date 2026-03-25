/*
 * api_sysinfo.c - LightShell System Info API (cross-platform)
 *
 * Exposes lightshell.system.{platform, arch, homeDir, tempDir, hostname}
 * All properties are read-only string getters.
 */

#include "api.h"
#include <sys/utsname.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Getter: lightshell.system.platform → 'darwin' | 'linux' */
static R8EValue api_sys_platform(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    struct utsname uts;
    if (uname(&uts) == 0) {
        /* Lowercase the sysname */
        if (strcasecmp(uts.sysname, "Darwin") == 0)
            return r8e_make_cstring(ctx, "darwin");
        if (strcasecmp(uts.sysname, "Linux") == 0)
            return r8e_make_cstring(ctx, "linux");
        return r8e_make_cstring(ctx, uts.sysname);
    }
#ifdef __APPLE__
    return r8e_make_cstring(ctx, "darwin");
#else
    return r8e_make_cstring(ctx, "linux");
#endif
}

/* Getter: lightshell.system.arch → 'arm64' | 'x64' */
static R8EValue api_sys_arch(R8EContext *ctx, R8EValue this_val,
                              int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    struct utsname uts;
    if (uname(&uts) == 0) {
        if (strcmp(uts.machine, "arm64") == 0 || strcmp(uts.machine, "aarch64") == 0)
            return r8e_make_cstring(ctx, "arm64");
        if (strcmp(uts.machine, "x86_64") == 0)
            return r8e_make_cstring(ctx, "x64");
        return r8e_make_cstring(ctx, uts.machine);
    }
#ifdef __aarch64__
    return r8e_make_cstring(ctx, "arm64");
#else
    return r8e_make_cstring(ctx, "x64");
#endif
}

/* Getter: lightshell.system.homeDir → string */
static R8EValue api_sys_home_dir(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    const char *home = getenv("HOME");
    if (!home) home = "/";
    return r8e_make_cstring(ctx, home);
}

/* Getter: lightshell.system.tempDir → string */
static R8EValue api_sys_temp_dir(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    const char *tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    return r8e_make_cstring(ctx, tmp);
}

/* Getter: lightshell.system.hostname → string */
static R8EValue api_sys_hostname(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        return r8e_make_cstring(ctx, hostname);
    }
    return r8e_make_cstring(ctx, "unknown");
}

void ls_api_sysinfo_init(R8EContext *ctx) {
    R8EValue sys = r8e_make_object(ctx);

    /* Use accessor properties so JS reads them as lightshell.system.platform (not function calls) */
    r8e_define_accessor(ctx, sys, "platform",
        r8e_make_native_func(ctx, api_sys_platform, "get platform", 0), R8E_UNDEFINED);
    r8e_define_accessor(ctx, sys, "arch",
        r8e_make_native_func(ctx, api_sys_arch, "get arch", 0), R8E_UNDEFINED);
    r8e_define_accessor(ctx, sys, "homeDir",
        r8e_make_native_func(ctx, api_sys_home_dir, "get homeDir", 0), R8E_UNDEFINED);
    r8e_define_accessor(ctx, sys, "tempDir",
        r8e_make_native_func(ctx, api_sys_temp_dir, "get tempDir", 0), R8E_UNDEFINED);
    r8e_define_accessor(ctx, sys, "hostname",
        r8e_make_native_func(ctx, api_sys_hostname, "get hostname", 0), R8E_UNDEFINED);

    R8EValue ls = ls_get_namespace(ctx);
    r8e_set_prop(ctx, ls, "system", sys);
}
