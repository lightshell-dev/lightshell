/* stb_image.h - v2.30 - public domain image loader
 *
 * MINIMAL STUB VERSION
 * This is a minimal stub that provides the stb_image API signatures.
 * When STB_IMAGE_IMPLEMENTATION is defined, it provides a no-op implementation
 * that always fails to load (the app uses ls_image_create_test_pattern() instead).
 *
 * To get real image loading, replace this file with the full stb_image.h from:
 *   https://github.com/nothings/stb/blob/master/stb_image.h
 *
 * LICENSE: public domain / MIT (see end of file)
 */

#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Allow selective format inclusion (ignored in stub, all formats "fail") */
#ifdef STBI_ONLY_PNG
#endif
#ifdef STBI_ONLY_JPEG
#endif
#ifdef STBI_ONLY_BMP
#endif
#ifdef STBI_ONLY_GIF
#endif

#ifndef STBIDEF
#define STBIDEF extern
#endif

STBIDEF unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF void stbi_image_free(void *retval_from_stbi_load);
STBIDEF const char *stbi_failure_reason(void);

#endif /* STBI_INCLUDE_STB_IMAGE_H */

/* ---- Implementation ---- */
#ifdef STB_IMAGE_IMPLEMENTATION

static const char *g_stbi_failure = "stb_image stub: replace with full stb_image.h for real image loading";

STBIDEF unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels) {
    (void)desired_channels;
    FILE *f = fopen(filename, "rb");
    if (!f) {
        g_stbi_failure = "can't open file";
        return NULL;
    }
    /* Read the file into memory and delegate */
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0 || len > 256 * 1024 * 1024) {
        fclose(f);
        g_stbi_failure = "file too large or empty";
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)len);
    if (!buf) { fclose(f); g_stbi_failure = "out of memory"; return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if ((long)nread != len) { free(buf); g_stbi_failure = "read error"; return NULL; }
    unsigned char *result = stbi_load_from_memory(buf, (int)len, x, y, channels_in_file, desired_channels);
    free(buf);
    return result;
}

/* Minimal BMP decoder for uncompressed 24-bit and 32-bit BMPs */
static unsigned char *stbi__bmp_load(const unsigned char *buf, int len, int *x, int *y, int *comp, int req_comp) {
    if (len < 54) return NULL;
    if (buf[0] != 'B' || buf[1] != 'M') return NULL;

    int data_offset = *(int *)(buf + 10);
    int width = *(int *)(buf + 18);
    int height = *(int *)(buf + 22);
    int bits = *(unsigned short *)(buf + 28);
    int compression = *(int *)(buf + 30);

    if (width <= 0 || height == 0) return NULL;
    if (compression != 0) return NULL; /* only uncompressed */
    if (bits != 24 && bits != 32) return NULL;

    int abs_height = height < 0 ? -height : height;
    int bpp = bits / 8;
    int row_stride = (width * bpp + 3) & ~3; /* rows are 4-byte aligned */

    int out_comp = (req_comp == 0) ? 4 : req_comp;
    unsigned char *pixels = (unsigned char *)malloc(width * abs_height * out_comp);
    if (!pixels) return NULL;

    for (int row = 0; row < abs_height; row++) {
        /* BMP is bottom-up by default (positive height) */
        int src_row = (height > 0) ? (abs_height - 1 - row) : row;
        const unsigned char *src = buf + data_offset + src_row * row_stride;
        if (data_offset + src_row * row_stride + width * bpp > len) { free(pixels); return NULL; }
        for (int col = 0; col < width; col++) {
            unsigned char b = src[col * bpp + 0];
            unsigned char g = src[col * bpp + 1];
            unsigned char r = src[col * bpp + 2];
            unsigned char a = (bpp == 4) ? src[col * bpp + 3] : 255;
            unsigned char *dst = &pixels[(row * width + col) * out_comp];
            if (out_comp >= 1) dst[0] = r;
            if (out_comp >= 2) dst[1] = g;
            if (out_comp >= 3) dst[2] = b;
            if (out_comp >= 4) dst[3] = a;
        }
    }

    *x = width;
    *y = abs_height;
    if (comp) *comp = bpp;
    return pixels;
}

/* Minimal PNG decoder - handles uncompressed (method 0) only, or returns NULL */
/* For full PNG support, replace this file with the real stb_image.h */

STBIDEF unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels) {
    if (!buffer || len < 4) {
        g_stbi_failure = "invalid input";
        return NULL;
    }

    /* Try BMP */
    if (len >= 2 && buffer[0] == 'B' && buffer[1] == 'M') {
        unsigned char *result = stbi__bmp_load(buffer, len, x, y, channels_in_file, desired_channels);
        if (result) return result;
    }

    /* Other formats not supported in stub */
    g_stbi_failure = "stb_image stub: only BMP supported. Replace with full stb_image.h for PNG/JPEG/GIF";
    return NULL;
}

STBIDEF void stbi_image_free(void *retval_from_stbi_load) {
    free(retval_from_stbi_load);
}

STBIDEF const char *stbi_failure_reason(void) {
    return g_stbi_failure;
}

#endif /* STB_IMAGE_IMPLEMENTATION */
