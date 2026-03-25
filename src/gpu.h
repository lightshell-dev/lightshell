#ifndef LIGHTSHELL_GPU_H
#define LIGHTSHELL_GPU_H

#include "r8e_display_list.h"
#include <stdint.h>

typedef struct GPUBackend {
    int  (*init)(void *metal_layer);
    void (*begin_frame)(void);
    void (*render)(DisplayList *dl);
    void (*present)(void);
    void (*resize)(uint32_t width, uint32_t height);
    void (*destroy)(void);
    /* Future: texture management */
    uint32_t (*load_texture)(const uint8_t *data, uint32_t w, uint32_t h);
    void (*free_texture)(uint32_t texture_id);
    void (*update_glyph_atlas)(const uint8_t *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
} GPUBackend;

/* Metal backend (macOS) */
GPUBackend *gpu_metal_create(void);

/* Vulkan backend (Linux) */
GPUBackend *gpu_vulkan_create(void);

#endif
