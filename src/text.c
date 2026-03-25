/*
 * text.c - stb_truetype text shaping implementation
 */

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "text.h"
#include "glyph_atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static stbtt_fontinfo g_font;
static unsigned char *g_font_data = NULL;

int ls_text_init(const char *font_path) {
    if (!font_path) {
        /* Try common macOS system fonts */
        const char *candidates[] = {
            "/System/Library/Fonts/SFNS.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/Library/Fonts/Arial.ttf",
            "/System/Library/Fonts/Geneva.ttf",
            NULL
        };
        for (int i = 0; candidates[i]; i++) {
            FILE *f = fopen(candidates[i], "rb");
            if (f) {
                font_path = candidates[i];
                fclose(f);
                break;
            }
        }
        if (!font_path) {
            fprintf(stderr, "[text] No suitable system font found\n");
            return -1;
        }
    }

    /* Read font file */
    FILE *f = fopen(font_path, "rb");
    if (!f) {
        fprintf(stderr, "[text] Failed to open font: %s\n", font_path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_font_data = malloc(size);
    if (!g_font_data) {
        fclose(f);
        return -1;
    }
    fread(g_font_data, 1, size, f);
    fclose(f);

    /* For .ttc files, use font index 0 */
    int offset = stbtt_GetFontOffsetForIndex(g_font_data, 0);
    if (!stbtt_InitFont(&g_font, g_font_data, offset)) {
        fprintf(stderr, "[text] stbtt_InitFont failed for: %s\n", font_path);
        free(g_font_data);
        g_font_data = NULL;
        return -1;
    }

    fprintf(stderr, "[text] Loaded font: %s\n", font_path);
    fprintf(stderr, "[text] Text subsystem initialized\n");
    return 0;
}

void ls_text_shutdown(void) {
    free(g_font_data);
    g_font_data = NULL;
}

/* Simple left-to-right text shaping (no HarfBuzz needed for Latin/CJK) */
int ls_text_shape(DisplayList *dl, const char *text, uint32_t len,
                  float font_size, R8EGlyphRun **out_runs, uint32_t *out_count) {
    if (!g_font_data || !text || len == 0) return -1;

    float scale = stbtt_ScaleForPixelHeight(&g_font, font_size);

    /* Count characters (skip UTF-8 continuation bytes) */
    uint32_t glyph_count = 0;
    for (uint32_t i = 0; i < len; i++) {
        if ((text[i] & 0xC0) != 0x80) glyph_count++;
    }

    R8EGlyphRun *run = r8e_dl_arena_alloc_glyph_run(dl, glyph_count, 0);
    if (!run) return -1;

    uint32_t gi = 0;
    for (uint32_t i = 0; i < len && gi < glyph_count; ) {
        /* Decode UTF-8 codepoint */
        uint32_t cp;
        if ((text[i] & 0x80) == 0) {
            cp = text[i]; i += 1;
        } else if ((text[i] & 0xE0) == 0xC0) {
            cp = ((uint32_t)(text[i] & 0x1F) << 6) |
                  (uint32_t)(text[i+1] & 0x3F);
            i += 2;
        } else if ((text[i] & 0xF0) == 0xE0) {
            cp = ((uint32_t)(text[i] & 0x0F) << 12) |
                 ((uint32_t)(text[i+1] & 0x3F) << 6) |
                  (uint32_t)(text[i+2] & 0x3F);
            i += 3;
        } else {
            cp = ((uint32_t)(text[i] & 0x07) << 18) |
                 ((uint32_t)(text[i+1] & 0x3F) << 12) |
                 ((uint32_t)(text[i+2] & 0x3F) << 6) |
                  (uint32_t)(text[i+3] & 0x3F);
            i += 4;
        }

        int glyph_index = stbtt_FindGlyphIndex(&g_font, cp);

        int advance, lsb;
        stbtt_GetGlyphHMetrics(&g_font, glyph_index, &advance, &lsb);

        run->glyphs[gi].glyph_id = glyph_index;
        run->glyphs[gi].x_offset = 0;
        run->glyphs[gi].y_offset = 0;
        run->glyphs[gi].x_advance = advance * scale;

        /* Kerning with next character */
        if (i < len) {
            uint32_t next_cp;
            if ((text[i] & 0x80) == 0) {
                next_cp = text[i];
            } else if ((text[i] & 0xE0) == 0xC0 && i + 1 < len) {
                next_cp = ((uint32_t)(text[i] & 0x1F) << 6) |
                           (uint32_t)(text[i+1] & 0x3F);
            } else if ((text[i] & 0xF0) == 0xE0 && i + 2 < len) {
                next_cp = ((uint32_t)(text[i] & 0x0F) << 12) |
                          ((uint32_t)(text[i+1] & 0x3F) << 6) |
                           (uint32_t)(text[i+2] & 0x3F);
            } else {
                next_cp = '?';
            }
            int next_gi = stbtt_FindGlyphIndex(&g_font, next_cp);
            int kern = stbtt_GetGlyphKernAdvance(&g_font, glyph_index, next_gi);
            run->glyphs[gi].x_advance += kern * scale;
        }

        gi++;
    }
    run->count = gi;

    *out_runs = run;
    *out_count = 1;
    return 0;
}

void ls_text_metrics(float font_size, LSTextMetrics *metrics) {
    if (!g_font_data || !metrics) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font, font_size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);
    metrics->ascent = ascent * scale;
    metrics->descent = descent * scale;
    metrics->line_height = (ascent - descent + line_gap) * scale;
    metrics->advance_width = 0;
}

/* Export font info for glyph atlas */
stbtt_fontinfo *ls_text_get_font(void) {
    return g_font_data ? &g_font : NULL;
}

float ls_text_get_scale(float font_size) {
    if (!g_font_data) return 0.0f;
    return stbtt_ScaleForPixelHeight(&g_font, font_size);
}
