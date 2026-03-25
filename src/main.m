/* main.m - LightShell demo entry point */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "gpu.h"
#include "image.h"
#include "text.h"
#include "glyph_atlas.h"
#include "r8e_display_list.h"
#include "r8e_api.h"
#include "r8e_types.h"
#include "api.h"

/* Load Electron compat layer from bundled JS */
static void load_compat_layer(R8EContext *ctx, const char *exe_path) {
    const char *candidates[] = {
        "compat/compat_bundle.js",
        "../compat/compat_bundle.js",
        NULL
    };

    /* Also try relative to executable directory */
    char exe_rel[1024] = {0};
    if (exe_path) {
        /* Find last '/' in exe_path to get directory */
        const char *last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            size_t dir_len = (size_t)(last_slash - exe_path);
            if (dir_len + 30 < sizeof(exe_rel)) {
                memcpy(exe_rel, exe_path, dir_len);
                strlcpy(exe_rel + dir_len, "/../compat/compat_bundle.js",
                        sizeof(exe_rel) - dir_len);
            }
        }
    }

    FILE *f = NULL;
    const char *used_path = NULL;

    for (int i = 0; candidates[i]; i++) {
        f = fopen(candidates[i], "r");
        if (f) { used_path = candidates[i]; break; }
    }
    if (!f && exe_rel[0]) {
        f = fopen(exe_rel, "r");
        if (f) used_path = exe_rel;
    }

    if (!f) {
        fprintf(stderr, "[lightshell] Warning: compat bundle not found (tried compat/compat_bundle.js)\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc((size_t)size + 1);
    fread(source, 1, (size_t)size, f);
    source[size] = '\0';
    fclose(f);

    R8EValue result = r8e_eval(ctx, source, (size_t)size);
    (void)result;
    free(source);

    fprintf(stderr, "[lightshell] Electron compat layer loaded (%ld bytes from %s)\n", size, used_path);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    @autoreleasepool {
        PlatformWindowConfig config = {
            .title = "LightShell Demo",
            .width = 800, .height = 600,
            .min_width = 400, .min_height = 300,
            .resizable = true,
        };

        if (platform_init(&config) != 0) {
            fprintf(stderr, "Failed to initialize platform\n");
            return 1;
        }

        /* Stub GPU backend for now - just opens window */
        GPUBackend *gpu = gpu_metal_create();
        if (!gpu || gpu->init(platform_get_metal_layer()) != 0) {
            fprintf(stderr, "Failed to initialize GPU backend\n");
            return 1;
        }

        /* Create a test pattern texture for the image demo */
        LSImage *test_img = ls_image_create_test_pattern(128, 128);
        uint32_t test_tex = gpu->load_texture(test_img->pixels, test_img->width, test_img->height);
        ls_image_free(test_img);
        if (test_tex) {
            fprintf(stderr, "[lightshell] Test pattern texture created: id=%u\n", test_tex);
        }

        /* Initialize text subsystem */
        ls_glyph_atlas_init();
        if (ls_text_init(NULL) != 0) {
            fprintf(stderr, "Warning: text init failed, text won't render\n");
        }

        /* Initialize r8e JS engine */
        R8EContext *ctx = r8e_context_new();
        if (!ctx) {
            fprintf(stderr, "Failed to create r8e context\n");
            return 1;
        }

        /* Initialize native APIs */
        ls_api_fs_init(ctx);
        ls_api_sysinfo_init(ctx);
        ls_api_clipboard_init(ctx);
        ls_api_shell_init(ctx);
        ls_api_dialog_init(ctx);
        ls_api_menu_init(ctx);

        /* Load Electron compat layer (must be after native API init) */
        load_compat_layer(ctx, argv[0]);

        /* Verify native APIs */
        {
            R8EValue platform = r8e_eval(ctx, "lightshell.system.platform", 0);
            char pbuf[8]; size_t plen;
            const char *pstr = r8e_get_cstring(platform, pbuf, &plen);
            fprintf(stderr, "[lightshell] system.platform = '%.*s'\n", (int)plen, pstr);

            R8EValue exists = r8e_eval(ctx, "lightshell.fs.exists('/tmp')", 0);
            fprintf(stderr, "[lightshell] fs.exists('/tmp') = %s\n",
                    r8e_to_bool(exists) ? "true" : "false");

            /* Verify compat layer: require('electron') should return an object with .app */
            R8EValue compat_check = r8e_eval(ctx,
                "typeof require === 'function' && typeof require('electron').app === 'object' ? 'ok' : 'fail'", 0);
            char cbuf[8]; size_t clen;
            const char *cstr = r8e_get_cstring(compat_check, cbuf, &clen);
            fprintf(stderr, "[lightshell] require('electron').app = %.*s\n", (int)clen, cstr);
        }

        /* Set screen dimension globals */
        r8e_set_global(ctx, "screenWidth", r8e_from_int32(config.width));
        r8e_set_global(ctx, "screenHeight", r8e_from_int32(config.height));
        r8e_set_global(ctx, "mouseX", r8e_from_int32(0));
        r8e_set_global(ctx, "mouseY", r8e_from_int32(0));

        /* Define hover-check JS functions for each rect */
        r8e_eval(ctx,
            "function hoverBlue()  { return mouseX >= 50  && mouseX <= 250 && mouseY >= 50  && mouseY <= 150; }"
            "function hoverRed()   { return mouseX >= 300 && mouseX <= 450 && mouseY >= 200 && mouseY <= 350; }"
            "function hoverGreen() { return mouseX >= 100 && mouseX <= 400 && mouseY >= 300 && mouseY <= 380; }", 0);

        R8EDLArena arena;
        r8e_dl_arena_init(&arena, 0);
        DisplayList dl;
        r8e_dl_init(&dl, &arena);

        float mouse_x = 0, mouse_y = 0;

        bool running = true;
        while (running) {
            @autoreleasepool {
                platform_frame_begin();

                PlatformEvent event;
                while (platform_poll_event(&event)) {
                    if (event.type == PLATFORM_EVENT_CLOSE) {
                        running = false;
                    }
                    if (event.type == PLATFORM_EVENT_MOUSE_MOVE) {
                        mouse_x = event.mouse_x;
                        mouse_y = event.mouse_y;
                    }
                }
                if (!running) break;

                /* Update mouse position in JS engine */
                r8e_set_global(ctx, "mouseX", r8e_from_int32((int)mouse_x));
                r8e_set_global(ctx, "mouseY", r8e_from_int32((int)mouse_y));

                /* Query JS engine for hover states */
                R8EValue hb = r8e_eval(ctx, "hoverBlue()", 0);
                R8EValue hr = r8e_eval(ctx, "hoverRed()", 0);
                R8EValue hg = r8e_eval(ctx, "hoverGreen()", 0);

                bool blue_hover  = r8e_to_bool(hb);
                bool red_hover   = r8e_to_bool(hr);
                bool green_hover = r8e_to_bool(hg);

                /* Pick colors: lighter on hover */
                uint32_t blue_color  = blue_hover  ? 0xFF5588FF : 0xFF3366FF;
                uint32_t red_color   = red_hover   ? 0xFFFF7777 : 0xFFFF4444;
                uint32_t green_color = green_hover ? 0xFF77EE77 : 0xFF44CC44;

                r8e_dl_arena_reset(&arena);
                r8e_dl_clear(&dl);

                /* Demo: colored rectangles with JS-driven hover */
                r8e_dl_push_fill_rect(&dl, 50, 50, 200, 100, blue_color, 8.0f);
                r8e_dl_push_fill_rect(&dl, 300, 200, 150, 150, red_color, 0.0f);
                r8e_dl_push_fill_rect(&dl, 100, 300, 300, 80, green_color, 16.0f);

                /* Stroke rect */
                r8e_dl_push_stroke_rect(&dl, 500, 50, 200, 100, 0xFFFFFFFF, 3.0f, 8.0f);

                /* Clipping test */
                r8e_dl_push_clip(&dl, 100, 100, 200, 200);
                r8e_dl_push_fill_rect(&dl, 50, 50, 300, 300, 0x88FF00FF, 0.0f); /* partially clipped */
                r8e_dl_pop_clip(&dl);

                /* Opacity test */
                r8e_dl_push_opacity(&dl, 0.4f);
                r8e_dl_push_fill_rect(&dl, 400, 350, 200, 100, 0xFFFF8800, 12.0f);
                r8e_dl_pop_opacity(&dl);

                /* Image demo: draw test pattern texture */
                if (test_tex) {
                    r8e_dl_push_draw_image(&dl, 550, 180, 128, 128, test_tex);
                }

                /* Text demo: render "Hello LightShell!" */
                {
                    R8EGlyphRun *runs = NULL;
                    uint32_t run_count = 0;
                    const char *hello = "Hello LightShell!";
                    if (ls_text_shape(&dl, hello, (uint32_t)strlen(hello),
                                      32.0f, &runs, &run_count) == 0) {
                        if (runs && run_count > 0) {
                            r8e_dl_push_fill_text(&dl, 50, 500, runs, run_count,
                                                  32.0f, 0xFFFFFFFF);
                        }
                    }
                }

                gpu->begin_frame();
                gpu->render(&dl);
                gpu->present();

                platform_frame_end();
            }
        }

        r8e_dl_destroy(&dl);
        r8e_dl_arena_destroy(&arena);
        ls_text_shutdown();
        ls_glyph_atlas_destroy();
        gpu->destroy();
        r8e_context_free(ctx);
        platform_shutdown();
    }
    return 0;
}
