#include "gpu.h"
static int stub_init(void *layer) { (void)layer; return 0; }
static void stub_noop(void) {}
static void stub_render(DisplayList *dl) { (void)dl; }
static void stub_resize(uint32_t w, uint32_t h) { (void)w; (void)h; }
static uint32_t stub_load(const uint8_t *d, uint32_t w, uint32_t h) { (void)d;(void)w;(void)h; return 0; }
static void stub_free_tex(uint32_t id) { (void)id; }
static void stub_update_atlas(const uint8_t *d, uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)d;(void)x;(void)y;(void)w;(void)h; }

static GPUBackend g_stub = {
    stub_init, stub_noop, stub_render, stub_noop, stub_resize, stub_noop,
    stub_load, stub_free_tex, stub_update_atlas
};
GPUBackend *gpu_metal_create(void) { return &g_stub; }
