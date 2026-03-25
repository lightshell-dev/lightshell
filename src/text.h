/*
 * text.h - Text shaping and rasterization API using HarfBuzz + FreeType
 *
 * Provides text shaping (HarfBuzz) to produce glyph runs for the display list.
 * Glyph rasterization is handled by FreeType via the glyph atlas.
 */

#ifndef LIGHTSHELL_TEXT_H
#define LIGHTSHELL_TEXT_H

#include <stdint.h>
#include "r8e_display_list.h"

/* Initialize text subsystem (loads default font).
 * Pass NULL for font_path to auto-detect a system font. */
int ls_text_init(const char *font_path);
void ls_text_shutdown(void);

/* Shape text and produce glyph runs for the display list.
 * Returns 0 on success, -1 on error.
 * Glyph runs are allocated from the display list's arena. */
int ls_text_shape(DisplayList *dl, const char *text, uint32_t len,
                  float font_size, R8EGlyphRun **out_runs, uint32_t *out_count);

/* Get font metrics at a given size */
typedef struct {
    float advance_width;
    float line_height;
    float ascent;
    float descent;
} LSTextMetrics;

void ls_text_metrics(float font_size, LSTextMetrics *metrics);

/* Accessor for the FreeType face (used by glyph_atlas.c) */
void *ls_text_get_ft_face(void);

#endif /* LIGHTSHELL_TEXT_H */
