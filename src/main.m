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

                gpu->begin_frame();
                gpu->render(&dl);
                gpu->present();
            }
        }

        r8e_dl_destroy(&dl);
        r8e_dl_arena_destroy(&arena);
        gpu->destroy();
        platform_shutdown();
    }
    return 0;
}
