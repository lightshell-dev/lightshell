/*
 * glyph_atlas.h - Glyph atlas texture manager
 *
 * Manages a single grayscale (R8) texture atlas for rasterized glyph bitmaps.
 * Uses shelf-packing: glyphs are placed left-to-right in rows.
 */

#ifndef LIGHTSHELL_GLYPH_ATLAS_H
#define LIGHTSHELL_GLYPH_ATLAS_H

#include <stdint.h>

#define GLYPH_ATLAS_SIZE 1024  /* 1024x1024 initial atlas */

typedef struct {
    float u0, v0, u1, v1;      /* UV coordinates in atlas */
    float width, height;         /* glyph bitmap dimensions in pixels */
    float bearing_x, bearing_y;  /* glyph bearing (offset from pen position) */
} GlyphAtlasEntry;

/* Initialize the glyph atlas */
void ls_glyph_atlas_init(void);
void ls_glyph_atlas_destroy(void);

/* Get or rasterize a glyph into the atlas. Returns entry with UV coordinates,
 * or NULL if the glyph could not be rasterized or the atlas is full. */
GlyphAtlasEntry *ls_glyph_atlas_get(uint32_t glyph_id, float font_size);

/* Get the raw atlas texture data (single-channel grayscale) */
const uint8_t *ls_glyph_atlas_data(void);
uint32_t ls_glyph_atlas_width(void);
uint32_t ls_glyph_atlas_height(void);

/* Check if atlas was modified since last GPU upload */
int ls_glyph_atlas_dirty(void);
void ls_glyph_atlas_clear_dirty(void);

#endif /* LIGHTSHELL_GLYPH_ATLAS_H */
