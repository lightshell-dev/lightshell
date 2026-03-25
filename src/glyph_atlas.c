/*
 * glyph_atlas.c - Shelf-packing glyph atlas implementation
 *
 * Rasterizes glyphs via FreeType and packs them into a grayscale texture
 * using shelf (row) packing. The Metal backend uploads this as an R8Unorm
 * texture and uses the value as alpha for text rendering.
 */

#include "glyph_atlas.h"
#include "text.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint8_t *g_atlas_data = NULL;
static uint32_t g_atlas_w = GLYPH_ATLAS_SIZE;
static uint32_t g_atlas_h = GLYPH_ATLAS_SIZE;
static int g_dirty = 0;

/* Shelf packing state */
static uint32_t g_cursor_x = 0;
static uint32_t g_cursor_y = 0;
static uint32_t g_row_height = 0;

/* Cache: glyph_id + size -> atlas entry */
#define MAX_CACHED_GLYPHS 4096

typedef struct {
    uint32_t glyph_id;
    uint32_t size_key;  /* font_size * 10 as integer for hashing */
    GlyphAtlasEntry entry;
} CachedGlyph;

static CachedGlyph g_cache[MAX_CACHED_GLYPHS];
static uint32_t g_cache_count = 0;

void ls_glyph_atlas_init(void) {
    g_atlas_data = calloc(g_atlas_w * g_atlas_h, 1);
    if (!g_atlas_data) {
        fprintf(stderr, "[glyph_atlas] Failed to allocate atlas (%ux%u)\n",
                g_atlas_w, g_atlas_h);
        return;
    }
    g_cursor_x = 1;  /* 1px padding to avoid sampling artifacts */
    g_cursor_y = 1;
    g_row_height = 0;
    g_cache_count = 0;
    g_dirty = 1;
    fprintf(stderr, "[glyph_atlas] Initialized %ux%u atlas\n", g_atlas_w, g_atlas_h);
}

void ls_glyph_atlas_destroy(void) {
    free(g_atlas_data);
    g_atlas_data = NULL;
    g_cache_count = 0;
}

GlyphAtlasEntry *ls_glyph_atlas_get(uint32_t glyph_id, float font_size) {
    if (!g_atlas_data) return NULL;

    uint32_t size_key = (uint32_t)(font_size * 10);

    /* Check cache */
    for (uint32_t i = 0; i < g_cache_count; i++) {
        if (g_cache[i].glyph_id == glyph_id && g_cache[i].size_key == size_key) {
            return &g_cache[i].entry;
        }
    }

    /* Get FreeType face from text subsystem */
    FT_Face face = (FT_Face)ls_text_get_ft_face();
    if (!face) return NULL;

    /* Set size and rasterize */
    FT_Set_Char_Size(face, 0, (FT_F26Dot6)(font_size * 64), 72, 72);
    if (FT_Load_Glyph(face, glyph_id, FT_LOAD_RENDER) != 0) {
        return NULL;
    }

    FT_Bitmap *bmp = &face->glyph->bitmap;
    uint32_t bw = bmp->width;
    uint32_t bh = bmp->rows;

    /* Handle zero-size glyphs (e.g. space) - still cache them */
    if (bw == 0 || bh == 0) {
        if (g_cache_count >= MAX_CACHED_GLYPHS) return NULL;
        CachedGlyph *cached = &g_cache[g_cache_count++];
        cached->glyph_id = glyph_id;
        cached->size_key = size_key;
        cached->entry.u0 = 0;
        cached->entry.v0 = 0;
        cached->entry.u1 = 0;
        cached->entry.v1 = 0;
        cached->entry.width = 0;
        cached->entry.height = 0;
        cached->entry.bearing_x = (float)face->glyph->bitmap_left;
        cached->entry.bearing_y = (float)face->glyph->bitmap_top;
        return &cached->entry;
    }

    /* Shelf packing: try current row */
    if (g_cursor_x + bw + 1 > g_atlas_w) {
        /* Start new row */
        g_cursor_x = 1;
        g_cursor_y += g_row_height + 1;
        g_row_height = 0;
    }
    if (g_cursor_y + bh + 1 > g_atlas_h) {
        fprintf(stderr, "[glyph_atlas] Atlas full, cannot fit glyph %u\n", glyph_id);
        return NULL;
    }

    /* Copy bitmap data into atlas */
    for (uint32_t y = 0; y < bh; y++) {
        memcpy(&g_atlas_data[(g_cursor_y + y) * g_atlas_w + g_cursor_x],
               &bmp->buffer[y * (uint32_t)bmp->pitch], bw);
    }

    /* Create cache entry */
    if (g_cache_count >= MAX_CACHED_GLYPHS) {
        fprintf(stderr, "[glyph_atlas] Cache full (%u entries)\n", MAX_CACHED_GLYPHS);
        return NULL;
    }
    CachedGlyph *cached = &g_cache[g_cache_count++];
    cached->glyph_id = glyph_id;
    cached->size_key = size_key;
    cached->entry.u0 = (float)g_cursor_x / (float)g_atlas_w;
    cached->entry.v0 = (float)g_cursor_y / (float)g_atlas_h;
    cached->entry.u1 = (float)(g_cursor_x + bw) / (float)g_atlas_w;
    cached->entry.v1 = (float)(g_cursor_y + bh) / (float)g_atlas_h;
    cached->entry.width = (float)bw;
    cached->entry.height = (float)bh;
    cached->entry.bearing_x = (float)face->glyph->bitmap_left;
    cached->entry.bearing_y = (float)face->glyph->bitmap_top;

    /* Advance cursor */
    g_cursor_x += bw + 1;
    if (bh > g_row_height) g_row_height = bh;
    g_dirty = 1;

    return &cached->entry;
}

const uint8_t *ls_glyph_atlas_data(void) { return g_atlas_data; }
uint32_t ls_glyph_atlas_width(void) { return g_atlas_w; }
uint32_t ls_glyph_atlas_height(void) { return g_atlas_h; }
int ls_glyph_atlas_dirty(void) { return g_dirty; }
void ls_glyph_atlas_clear_dirty(void) { g_dirty = 0; }
