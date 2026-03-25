/*
 * text.h - Text shaping and rasterization API using stb_truetype
 *
 * Provides simple left-to-right text shaping to produce glyph runs for the
 * display list. Glyph rasterization is handled by stb_truetype via the
 * glyph atlas.
 */

#ifndef LIGHTSHELL_TEXT_H
#define LIGHTSHELL_TEXT_H

#include <stdint.h>
#include "r8e_display_list.h"

/* Forward declaration - defined in stb_truetype.h */
typedef struct stbtt_fontinfo stbtt_fontinfo;

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

/* Accessor for the stb_truetype font info (used by glyph_atlas.c) */
stbtt_fontinfo *ls_text_get_font(void);

/* Get the scale factor for a given pixel height */
float ls_text_get_scale(float font_size);

#endif /* LIGHTSHELL_TEXT_H */
