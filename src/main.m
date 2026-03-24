/* main.m - LightShell demo entry point */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "platform.h"
#include "gpu.h"
#include "r8e_display_list.h"

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

        R8EDLArena arena;
        r8e_dl_arena_init(&arena, 0);
        DisplayList dl;
        r8e_dl_init(&dl, &arena);

        bool running = true;
        while (running) {
            @autoreleasepool {
                platform_frame_begin();

                PlatformEvent event;
                while (platform_poll_event(&event)) {
                    if (event.type == PLATFORM_EVENT_CLOSE) {
                        running = false;
                    }
                }
                if (!running) break;

                r8e_dl_arena_reset(&arena);
                r8e_dl_clear(&dl);

                /* Demo: colored rectangles */
                r8e_dl_push_fill_rect(&dl, 50, 50, 200, 100, 0xFF3366FF, 8.0f);
                r8e_dl_push_fill_rect(&dl, 300, 200, 150, 150, 0xFFFF4444, 0.0f);
                r8e_dl_push_fill_rect(&dl, 100, 300, 300, 80, 0xFF44CC44, 16.0f);

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

                gpu->begin_frame();
                gpu->render(&dl);
                gpu->present();

                platform_frame_end();
            }
        }

        r8e_dl_destroy(&dl);
        r8e_dl_arena_destroy(&arena);
        gpu->destroy();
        platform_shutdown();
    }
    return 0;
}
