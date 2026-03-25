#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"
#include "image.h"
#include <stdlib.h>
#include <string.h>

LSImage *ls_image_load(const char *path) {
    int w, h, channels;
    uint8_t *data = stbi_load(path, &w, &h, &channels, 4); /* force RGBA */
    if (!data) return NULL;

    LSImage *img = malloc(sizeof(LSImage));
    img->pixels = data;
    img->width = (uint32_t)w;
    img->height = (uint32_t)h;
    return img;
}

LSImage *ls_image_load_mem(const uint8_t *data, uint32_t len) {
    int w, h, channels;
    uint8_t *pixels = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    if (!pixels) return NULL;

    LSImage *img = malloc(sizeof(LSImage));
    img->pixels = pixels;
    img->width = (uint32_t)w;
    img->height = (uint32_t)h;
    return img;
}

void ls_image_free(LSImage *img) {
    if (!img) return;
    stbi_image_free(img->pixels);
    free(img);
}

LSImage *ls_image_create_test_pattern(uint32_t width, uint32_t height) {
    LSImage *img = malloc(sizeof(LSImage));
    if (!img) return NULL;
    img->width = width;
    img->height = height;
    img->pixels = malloc(width * height * 4);
    if (!img->pixels) { free(img); return NULL; }
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t *p = &img->pixels[(y * width + x) * 4];
            p[0] = (uint8_t)((x * 255) / width);      /* R: gradient left-right */
            p[1] = (uint8_t)((y * 255) / height);      /* G: gradient top-bottom */
            p[2] = 128;                                  /* B: constant */
            p[3] = 255;                                  /* A: opaque */
        }
    }
    return img;
}

uint32_t ls_image_load_texture(GPUBackend *gpu, const char *path) {
    LSImage *img = ls_image_load(path);
    if (!img) return 0;
    uint32_t tex_id = gpu->load_texture(img->pixels, img->width, img->height);
    ls_image_free(img);
    return tex_id;
}

/* Simple texture cache: array of path->texture_id pairs */
#define MAX_CACHED_TEXTURES 256
static struct { char path[256]; uint32_t tex_id; } g_tex_cache[MAX_CACHED_TEXTURES];
static uint32_t g_tex_cache_count = 0;

uint32_t ls_texture_cache_get(GPUBackend *gpu, const char *path) {
    for (uint32_t i = 0; i < g_tex_cache_count; i++) {
        if (strcmp(g_tex_cache[i].path, path) == 0) return g_tex_cache[i].tex_id;
    }
    uint32_t tex_id = ls_image_load_texture(gpu, path);
    if (tex_id && g_tex_cache_count < MAX_CACHED_TEXTURES) {
        strncpy(g_tex_cache[g_tex_cache_count].path, path, 255);
        g_tex_cache[g_tex_cache_count].path[255] = '\0';
        g_tex_cache[g_tex_cache_count].tex_id = tex_id;
        g_tex_cache_count++;
    }
    return tex_id;
}
