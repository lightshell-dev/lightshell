#ifndef LIGHTSHELL_IMAGE_H
#define LIGHTSHELL_IMAGE_H

#include <stdint.h>
#include "gpu.h"

/* Decoded image data */
typedef struct {
    uint8_t *pixels;    /* RGBA8 pixel data */
    uint32_t width;
    uint32_t height;
} LSImage;

/* Load image from file path. Returns NULL on error. Caller must free with ls_image_free(). */
LSImage *ls_image_load(const char *path);

/* Load image from memory buffer */
LSImage *ls_image_load_mem(const uint8_t *data, uint32_t len);

/* Free decoded image */
void ls_image_free(LSImage *img);

/* Create a test pattern image (RGB gradient). Caller must free with ls_image_free(). */
LSImage *ls_image_create_test_pattern(uint32_t width, uint32_t height);

/* Load image and upload to GPU as texture. Returns texture_id (0 = error). */
uint32_t ls_image_load_texture(GPUBackend *gpu, const char *path);

/* Texture cache: load or reuse existing texture for path */
uint32_t ls_texture_cache_get(GPUBackend *gpu, const char *path);

#endif
