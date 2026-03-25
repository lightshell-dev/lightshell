/*
 * text.c - r8e_font text shaping implementation
 */

#include "text.h"
#include "r8e_font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static R8EFont *g_font = NULL;

int ls_text_init(const char *font_path) {
    if (font_path) {
        FILE *f = fopen(font_path, "rb");
        if (!f) {
            fprintf(stderr, "[text] Failed to open font: %s\n", font_path);
            return -1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *data = malloc((size_t)sz);
        if (!data) { fclose(f); return -1; }
        fread(data, 1, (size_t)sz, f);
        fclose(f);
        g_font = r8e_font_load(data, (uint32_t)sz);
        if (!g_font) {
            fprintf(stderr, "[text] r8e_font_load failed for: %s\n", font_path);
            free(data);
            return -1;
        }
        /* Note: data must remain valid for the lifetime of the font.
         * r8e_font_load does not copy the data. We intentionally leak it here
         * since the font lives for the duration of the process. */
        fprintf(stderr, "[text] Loaded font: %s\n", font_path);
    } else {
        g_font = r8e_font_load_default();
        if (!g_font) {
            fprintf(stderr, "[text] Failed to load bundled font\n");
            return -1;
        }
        fprintf(stderr, "[text] Font loaded (bundled)\n");
    }

    fprintf(stderr, "[text] Text subsystem initialized\n");
    return 0;
}

void ls_text_shutdown(void) {
    r8e_font_free(g_font);
    g_font = NULL;
}

R8EFont *ls_text_get_font(void) {
    return g_font;
}

float ls_text_get_scale(float font_size) {
    if (!g_font) return 0.0f;
    return r8e_font_scale(g_font, font_size);
}

/* Simple left-to-right text shaping (no HarfBuzz needed for Latin/CJK) */
int ls_text_shape(DisplayList *dl, const char *text, uint32_t len,
                  float font_size, R8EGlyphRun **out_runs, uint32_t *out_count) {
    if (!g_font || !text || len == 0) return -1;

    float scale = r8e_font_scale(g_font, font_size);

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

        uint32_t gid = r8e_font_glyph_id(g_font, cp);

        int advance, lsb;
        r8e_font_hmetrics(g_font, gid, &advance, &lsb);

        run->glyphs[gi].glyph_id = gid;
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
            uint32_t next_gid = r8e_font_glyph_id(g_font, next_cp);
            int kern = r8e_font_kern(g_font, gid, next_gid);
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
    if (!g_font || !metrics) return;
    float scale = r8e_font_scale(g_font, font_size);
    int ascent, descent, line_gap;
    r8e_font_vmetrics(g_font, &ascent, &descent, &line_gap);
    metrics->ascent = ascent * scale;
    metrics->descent = descent * scale;
    metrics->line_height = (ascent - descent + line_gap) * scale;
    metrics->advance_width = 0;
}
