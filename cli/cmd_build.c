#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <libgen.h>
#include <mach-o/dyld.h>

/* Read file into malloc'd buffer */
static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

/* Collect all files from src/ recursively */
#define MAX_FILES 256
static struct { char path[256]; } g_files[MAX_FILES];
static int g_file_count = 0;

static void collect_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) && g_file_count < MAX_FILES) {
        if (entry->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { collect_files(path); continue; }
        strncpy(g_files[g_file_count++].path, path, 255);
    }
    closedir(d);
}

/* Minimal JSON string value extraction (no dependency needed) */
static int json_get_string(const char *json, const char *key, char *out, size_t out_sz) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), '"');
    if (!p) return -1;
    p++; /* skip opening quote */
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

static int json_get_int(const char *json, const char *key, int *out) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    /* skip to colon then digits */
    while (*p && *p != ':') p++;
    if (!*p) return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    *out = atoi(p);
    return 0;
}

static int json_get_bool(const char *json, const char *key, int *out) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p && *p != ':') p++;
    if (!*p) return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    *out = (strncmp(p, "true", 4) == 0) ? 1 : 0;
    return 0;
}

/* Get lightshell root directory from the binary's location.
 * The binary is at <root>/build/lightshell, so root = dirname(dirname(binary)). */
static int get_lightshell_root(char *out, size_t out_sz) {
    char exe_path[1024];
    uint32_t sz = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &sz) != 0) return -1;

    /* Resolve symlinks */
    char resolved[1024];
    if (!realpath(exe_path, resolved)) return -1;

    /* Go up twice: binary -> build/ -> lightshell_root/ */
    char *dir1 = dirname(resolved);  /* build/ */
    char *dir2 = dirname(dir1);      /* lightshell_root/ */
    snprintf(out, out_sz, "%s", dir2);
    return 0;
}

/* Generate build/app_main.m — the ObjC entry point that loads embedded files */
static int generate_app_main(const char *title, int width, int height,
                              int min_width, int min_height, int resizable,
                              const char *entry_file) {
    FILE *f = fopen("build/app_main.m", "w");
    if (!f) {
        fprintf(stderr, "Error: cannot create build/app_main.m\n");
        return -1;
    }

    fprintf(f,
        "/* Auto-generated — LightShell app entry point */\n"
        "#import <Foundation/Foundation.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include \"platform.h\"\n"
        "#include \"gpu.h\"\n"
        "#include \"image.h\"\n"
        "#include \"text.h\"\n"
        "#include \"glyph_atlas.h\"\n"
        "#include \"r8e_display_list.h\"\n"
        "#include \"r8e_api.h\"\n"
        "#include \"r8e_types.h\"\n"
        "#include \"api.h\"\n"
        "\n"
        "/* Embedded file data */\n"
        "#include \"embed.c\"\n"
        "\n"
        "static const EmbeddedFile *find_file(const char *name) {\n"
        "    for (int i = 0; i < embedded_file_count; i++) {\n"
        "        if (strstr(embedded_files[i].name, name)) return &embedded_files[i];\n"
        "    }\n"
        "    return NULL;\n"
        "}\n"
        "\n"
        "/* Extract <script src=\"...\"> paths from HTML */\n"
        "#define MAX_SCRIPTS 32\n"
        "static int extract_scripts(const char *html, size_t len, char scripts[][256], int max) {\n"
        "    int count = 0;\n"
        "    const char *p = html;\n"
        "    const char *end = html + len;\n"
        "    while (p < end && count < max) {\n"
        "        const char *tag = strstr(p, \"<script\");\n"
        "        if (!tag || tag >= end) break;\n"
        "        const char *src = strstr(tag, \"src=\");\n"
        "        const char *close = strchr(tag, '>');\n"
        "        if (src && close && src < close) {\n"
        "            char q = src[4]; /* quote char */\n"
        "            if (q == '\"' || q == '\\'') {\n"
        "                const char *s = src + 5;\n"
        "                const char *e = strchr(s, q);\n"
        "                if (e && (size_t)(e - s) < 255) {\n"
        "                    memcpy(scripts[count], s, (size_t)(e - s));\n"
        "                    scripts[count][(size_t)(e - s)] = '\\0';\n"
        "                    count++;\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        p = (close ? close + 1 : tag + 7);\n"
        "    }\n"
        "    return count;\n"
        "}\n"
        "\n"
        "int main(int argc, char **argv) {\n"
        "    (void)argc; (void)argv;\n"
        "\n"
        "    @autoreleasepool {\n"
        "        PlatformWindowConfig config = {\n"
        "            .title = \"%s\",\n"
        "            .width = %d, .height = %d,\n"
        "            .min_width = %d, .min_height = %d,\n"
        "            .resizable = %s,\n"
        "        };\n"
        "\n"
        "        if (platform_init(&config) != 0) {\n"
        "            fprintf(stderr, \"Failed to initialize platform\\n\");\n"
        "            return 1;\n"
        "        }\n"
        "\n"
        "        GPUBackend *gpu = gpu_metal_create();\n"
        "        if (!gpu || gpu->init(platform_get_metal_layer()) != 0) {\n"
        "            fprintf(stderr, \"Failed to initialize GPU backend\\n\");\n"
        "            return 1;\n"
        "        }\n"
        "\n"
        "        ls_glyph_atlas_init();\n"
        "        if (ls_text_init(NULL) != 0) {\n"
        "            fprintf(stderr, \"Warning: text init failed\\n\");\n"
        "        }\n"
        "\n"
        "        /* Create r8e JS engine */\n"
        "        R8EContext *ctx = r8e_context_new();\n"
        "        if (!ctx) {\n"
        "            fprintf(stderr, \"Failed to create r8e context\\n\");\n"
        "            return 1;\n"
        "        }\n"
        "\n"
        "        /* Register native APIs */\n"
        "        ls_api_fs_init(ctx);\n"
        "        ls_api_sysinfo_init(ctx);\n"
        "        ls_api_clipboard_init(ctx);\n"
        "        ls_api_shell_init(ctx);\n"
        "        ls_api_dialog_init(ctx);\n"
        "        ls_api_menu_init(ctx);\n"
        "\n"
        "        /* Set screen dimension globals */\n"
        "        r8e_set_global(ctx, \"screenWidth\", r8e_from_int32(config.width));\n"
        "        r8e_set_global(ctx, \"screenHeight\", r8e_from_int32(config.height));\n"
        "        r8e_set_global(ctx, \"mouseX\", r8e_from_int32(0));\n"
        "        r8e_set_global(ctx, \"mouseY\", r8e_from_int32(0));\n"
        "\n"
        "        /* Load entry HTML and extract script tags */\n"
        "        const EmbeddedFile *entry = find_file(\"%s\");\n"
        "        if (entry) {\n"
        "            char scripts[MAX_SCRIPTS][256];\n"
        "            int nscripts = extract_scripts((const char *)entry->data, entry->size, scripts, MAX_SCRIPTS);\n"
        "            fprintf(stderr, \"[lightshell] Found %%d script(s) in entry HTML\\n\", nscripts);\n"
        "            for (int i = 0; i < nscripts; i++) {\n"
        "                const EmbeddedFile *js = find_file(scripts[i]);\n"
        "                if (js) {\n"
        "                    fprintf(stderr, \"[lightshell] Evaluating: %%s\\n\", scripts[i]);\n"
        "                    r8e_eval(ctx, (const char *)js->data, js->size);\n"
        "                } else {\n"
        "                    fprintf(stderr, \"[lightshell] Warning: script not found: %%s\\n\", scripts[i]);\n"
        "                }\n"
        "            }\n"
        "        } else {\n"
        "            fprintf(stderr, \"[lightshell] Warning: entry file '%s' not found\\n\");\n"
        "        }\n"
        "\n"
        "        R8EDLArena arena;\n"
        "        r8e_dl_arena_init(&arena, 0);\n"
        "        DisplayList dl;\n"
        "        r8e_dl_init(&dl, &arena);\n"
        "\n"
        "        float mouse_x = 0, mouse_y = 0;\n"
        "\n"
        "        /* Check if app defines a render() function */\n"
        "        bool running = true;\n"
        "        while (running) {\n"
        "            @autoreleasepool {\n"
        "                platform_frame_begin();\n"
        "\n"
        "                PlatformEvent event;\n"
        "                while (platform_poll_event(&event)) {\n"
        "                    if (event.type == PLATFORM_EVENT_CLOSE) running = false;\n"
        "                    if (event.type == PLATFORM_EVENT_MOUSE_MOVE) {\n"
        "                        mouse_x = event.mouse_x;\n"
        "                        mouse_y = event.mouse_y;\n"
        "                    }\n"
        "                }\n"
        "                if (!running) break;\n"
        "\n"
        "                r8e_set_global(ctx, \"mouseX\", r8e_from_int32((int)mouse_x));\n"
        "                r8e_set_global(ctx, \"mouseY\", r8e_from_int32((int)mouse_y));\n"
        "\n"
        "                r8e_dl_arena_reset(&arena);\n"
        "                r8e_dl_clear(&dl);\n"
        "\n"
        "                /* Call render() if defined by the app */\n"
        "                r8e_eval(ctx, \"if (typeof render === 'function') render()\", 0);\n"
        "\n"
        "                gpu->begin_frame();\n"
        "                gpu->render(&dl);\n"
        "                gpu->present();\n"
        "\n"
        "                platform_frame_end();\n"
        "            }\n"
        "        }\n"
        "\n"
        "        r8e_dl_destroy(&dl);\n"
        "        r8e_dl_arena_destroy(&arena);\n"
        "        ls_text_shutdown();\n"
        "        ls_glyph_atlas_destroy();\n"
        "        gpu->destroy();\n"
        "        r8e_context_free(ctx);\n"
        "        platform_shutdown();\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        title, width, height, min_width, min_height,
        resizable ? "true" : "false",
        entry_file, entry_file
    );

    fclose(f);
    return 0;
}

int cmd_build(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Reset file collector state (matters when called multiple times from cmd_dev) */
    g_file_count = 0;

    if (access("lightshell.json", F_OK) != 0) {
        fprintf(stderr, "Error: lightshell.json not found\n");
        return 1;
    }

    /* Read config */
    char *config = read_file_contents("lightshell.json", NULL);
    if (!config) {
        fprintf(stderr, "Error: cannot read lightshell.json\n");
        return 1;
    }
    printf("[build] Read lightshell.json\n");

    /* Parse config values */
    char app_name[128] = "LightShell App";
    char title[128] = "LightShell App";
    char entry[256] = "src/index.html";
    int width = 1024, height = 768;
    int min_width = 400, min_height = 300;
    int resizable = 1;

    json_get_string(config, "name", app_name, sizeof(app_name));
    json_get_string(config, "entry", entry, sizeof(entry));
    json_get_string(config, "title", title, sizeof(title));
    /* If title wasn't set explicitly, use app name */
    if (strcmp(title, "LightShell App") == 0 && strcmp(app_name, "LightShell App") != 0) {
        strncpy(title, app_name, sizeof(title) - 1);
    }
    json_get_int(config, "width", &width);
    json_get_int(config, "height", &height);
    json_get_int(config, "minWidth", &min_width);
    json_get_int(config, "minHeight", &min_height);
    json_get_bool(config, "resizable", &resizable);
    free(config);

    printf("[build] App: %s (%dx%d)\n", title, width, height);

    /* Collect source files */
    collect_files("src");
    printf("[build] Found %d source files\n", g_file_count);

    /* Generate embed.c — C file with all source files as byte arrays */
    mkdir("build", 0755);
    FILE *embed = fopen("build/embed.c", "w");
    if (!embed) {
        fprintf(stderr, "Error: cannot create build/embed.c\n");
        return 1;
    }

    fprintf(embed, "/* Auto-generated — embedded app source files */\n");
    fprintf(embed, "#include <stdint.h>\n#include <stddef.h>\n\n");
    fprintf(embed, "typedef struct { const char *name; const uint8_t *data; size_t size; } EmbeddedFile;\n\n");

    for (int i = 0; i < g_file_count; i++) {
        size_t len;
        char *data = read_file_contents(g_files[i].path, &len);
        if (!data) continue;

        /* Sanitize name for C identifier */
        char varname[128];
        snprintf(varname, sizeof(varname), "file_%d", i);

        fprintf(embed, "static const uint8_t %s[] = {", varname);
        for (size_t j = 0; j < len; j++) {
            if (j % 16 == 0) fprintf(embed, "\n    ");
            fprintf(embed, "0x%02x,", (unsigned char)data[j]);
        }
        fprintf(embed, "\n};\n\n");
        free(data);
    }

    fprintf(embed, "const EmbeddedFile embedded_files[] = {\n");
    for (int i = 0; i < g_file_count; i++) {
        fprintf(embed, "    { \"%s\", file_%d, sizeof(file_%d) },\n",
                g_files[i].path, i, i);
    }
    fprintf(embed, "};\n");
    fprintf(embed, "const int embedded_file_count = %d;\n", g_file_count);
    fclose(embed);

    printf("[build] Generated build/embed.c\n");

    /* Generate app_main.m */
    if (generate_app_main(title, width, height, min_width, min_height, resizable, entry) != 0) {
        return 1;
    }
    printf("[build] Generated build/app_main.m\n");

    /* Find LightShell root (where src/ and engine/ live) */
    char ls_root[1024];
    if (get_lightshell_root(ls_root, sizeof(ls_root)) != 0) {
        fprintf(stderr, "Error: cannot determine LightShell installation path\n");
        return 1;
    }
    printf("[build] LightShell root: %s\n", ls_root);

    /* Verify key files exist */
    char check_path[1280];
    snprintf(check_path, sizeof(check_path), "%s/src/platform.h", ls_root);
    if (access(check_path, F_OK) != 0) {
        fprintf(stderr, "Error: LightShell source not found at %s/src/\n", ls_root);
        fprintf(stderr, "  Expected platform.h at: %s\n", check_path);
        return 1;
    }

    snprintf(check_path, sizeof(check_path), "%s/engine/r8e/build/libr8e.a", ls_root);
    if (access(check_path, F_OK) != 0) {
        fprintf(stderr, "Error: libr8e.a not found. Build the engine first:\n");
        fprintf(stderr, "  cd %s/engine/r8e && make release\n", ls_root);
        return 1;
    }

    /* Compile: app_main.m + platform layer + libr8e.a */
    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "cc -O2 -std=c11 -fobjc-arc "
        "-I%s/engine/r8e/src/gpu -I%s/engine/r8e/src -I%s/engine/r8e/include "
        "-I%s/src "
        "build/app_main.m "
        "%s/src/platform_darwin.m "
        "%s/src/gpu_metal.m "
        "%s/src/image.c "
        "%s/src/text.c "
        "%s/src/glyph_atlas.c "
        "%s/src/api_fs.c "
        "%s/src/api_sysinfo.c "
        "%s/src/api_clipboard_darwin.m "
        "%s/src/api_shell_darwin.m "
        "%s/src/api_dialog_darwin.m "
        "%s/src/api_menu_darwin.m "
        "%s/engine/r8e/src/gpu/r8e_display_list.c "
        "-framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore "
        "%s/engine/r8e/build/libr8e.a "
        "-o build/app",
        ls_root, ls_root, ls_root,
        ls_root,
        ls_root, ls_root, ls_root, ls_root, ls_root,
        ls_root, ls_root, ls_root, ls_root, ls_root,
        ls_root, ls_root, ls_root
    );

    printf("[build] Compiling...\n");
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[build] Compilation failed (exit code %d)\n", ret);
        return 1;
    }

    printf("[build] Built: build/app\n");
    return 0;
}
