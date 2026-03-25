/*
 * text.c - HarfBuzz + FreeType text shaping implementation
 */

#include "text.h"
#include "glyph_atlas.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static FT_Library g_ft_lib = NULL;
static FT_Face g_ft_face = NULL;
static hb_font_t *g_hb_font = NULL;

int ls_text_init(const char *font_path) {
    if (FT_Init_FreeType(&g_ft_lib) != 0) {
        fprintf(stderr, "[text] FT_Init_FreeType failed\n");
        return -1;
    }

    /* Find a default font if none specified */
    if (!font_path) {
        const char *candidates[] = {
            "/System/Library/Fonts/SFPro.ttf",
            "/System/Library/Fonts/SFNS.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/Library/Fonts/Arial.ttf",
            "/System/Library/Fonts/Geneva.ttf",
            NULL
        };
        for (int i = 0; candidates[i]; i++) {
            if (FT_New_Face(g_ft_lib, candidates[i], 0, &g_ft_face) == 0) {
                fprintf(stderr, "[text] Loaded font: %s\n", candidates[i]);
                break;
            }
        }
        if (!g_ft_face) {
            fprintf(stderr, "[text] No suitable system font found\n");
            FT_Done_FreeType(g_ft_lib);
            g_ft_lib = NULL;
            return -1;
        }
    } else {
        if (FT_New_Face(g_ft_lib, font_path, 0, &g_ft_face) != 0) {
            fprintf(stderr, "[text] Failed to load font: %s\n", font_path);
            FT_Done_FreeType(g_ft_lib);
            g_ft_lib = NULL;
            return -1;
        }
        fprintf(stderr, "[text] Loaded font: %s\n", font_path);
    }

    g_hb_font = hb_ft_font_create(g_ft_face, NULL);
    if (!g_hb_font) {
        fprintf(stderr, "[text] hb_ft_font_create failed\n");
        FT_Done_Face(g_ft_face);
        g_ft_face = NULL;
        FT_Done_FreeType(g_ft_lib);
        g_ft_lib = NULL;
        return -1;
    }

    fprintf(stderr, "[text] Text subsystem initialized\n");
    return 0;
}

void ls_text_shutdown(void) {
    if (g_hb_font) { hb_font_destroy(g_hb_font); g_hb_font = NULL; }
    if (g_ft_face) { FT_Done_Face(g_ft_face); g_ft_face = NULL; }
    if (g_ft_lib)  { FT_Done_FreeType(g_ft_lib); g_ft_lib = NULL; }
}

void *ls_text_get_ft_face(void) {
    return g_ft_face;
}

int ls_text_shape(DisplayList *dl, const char *text, uint32_t len,
                  float font_size, R8EGlyphRun **out_runs, uint32_t *out_count) {
    if (!g_hb_font || !text || len == 0) return -1;

    /* Set font size */
    FT_Set_Char_Size(g_ft_face, 0, (FT_F26Dot6)(font_size * 64), 72, 72);
    hb_ft_font_changed(g_hb_font);

    /* Create HarfBuzz buffer and shape */
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text, (int)len, 0, (int)len);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(g_hb_font, buf, NULL, 0);

    /* Extract glyph info */
    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

    /* Allocate glyph run from display list arena */
    R8EGlyphRun *run = r8e_dl_arena_alloc_glyph_run(dl, glyph_count, 0);
    if (!run) {
        hb_buffer_destroy(buf);
        return -1;
    }

    for (unsigned int i = 0; i < glyph_count; i++) {
        run->glyphs[i].glyph_id = glyph_info[i].codepoint;
        run->glyphs[i].x_offset = glyph_pos[i].x_offset / 64.0f;
        run->glyphs[i].y_offset = glyph_pos[i].y_offset / 64.0f;
        run->glyphs[i].x_advance = glyph_pos[i].x_advance / 64.0f;
    }

    hb_buffer_destroy(buf);

    *out_runs = run;
    *out_count = 1;
    return 0;
}

void ls_text_metrics(float font_size, LSTextMetrics *metrics) {
    if (!g_ft_face || !metrics) return;
    FT_Set_Char_Size(g_ft_face, 0, (FT_F26Dot6)(font_size * 64), 72, 72);
    metrics->ascent = g_ft_face->size->metrics.ascender / 64.0f;
    metrics->descent = g_ft_face->size->metrics.descender / 64.0f;
    metrics->line_height = (g_ft_face->size->metrics.ascender
                          - g_ft_face->size->metrics.descender) / 64.0f;
    metrics->advance_width = 0; /* depends on actual text */
}
