/* Valid (format, type) pairs and the client buffer size the GL spec requires for them
 * (OpenGL 2.1 spec, section 3.6.4 "Unpacking"), so harnesses hand gl4es exactly as many
 * bytes as a conforming application must.  Shared by the pixel/raster harnesses. */
#ifndef MAYHEM_PIXFMT_H
#define MAYHEM_PIXFMT_H
#include <stddef.h>
#include <stdint.h>
#include <GL/gl.h>
#include <GL/glext.h>

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif

typedef struct { GLenum type; unsigned size; int packed_comps; /* 0 = not packed */ } pix_type_t;

static const pix_type_t pix_types[] = {
    {GL_UNSIGNED_BYTE, 1, 0}, {GL_BYTE, 1, 0}, {GL_UNSIGNED_SHORT, 2, 0}, {GL_SHORT, 2, 0},
    {GL_UNSIGNED_INT, 4, 0}, {GL_INT, 4, 0}, {GL_FLOAT, 4, 0}, {GL_HALF_FLOAT, 2, 0},
    {GL_UNSIGNED_SHORT_5_6_5, 2, 3}, {GL_UNSIGNED_SHORT_5_6_5_REV, 2, 3},
    {GL_UNSIGNED_BYTE_3_3_2, 1, 3}, {GL_UNSIGNED_BYTE_2_3_3_REV, 1, 3},
    {GL_UNSIGNED_SHORT_4_4_4_4, 2, 4}, {GL_UNSIGNED_SHORT_4_4_4_4_REV, 2, 4},
    {GL_UNSIGNED_SHORT_5_5_5_1, 2, 4}, {GL_UNSIGNED_SHORT_1_5_5_5_REV, 2, 4},
    {GL_UNSIGNED_INT_8_8_8_8, 4, 4}, {GL_UNSIGNED_INT_8_8_8_8_REV, 4, 4},
    {GL_UNSIGNED_INT_2_10_10_10_REV, 4, 4},
};
#define PIX_NTYPES (sizeof(pix_types) / sizeof(pix_types[0]))

typedef struct { GLenum format; int comps; } pix_format_t;
static const pix_format_t pix_formats[] = {
    {GL_RGBA, 4}, {GL_BGRA, 4}, {GL_RGB, 3}, {GL_BGR, 3}, {GL_LUMINANCE, 1},
    {GL_LUMINANCE_ALPHA, 2}, {GL_ALPHA, 1}, {GL_RED, 1}, {GL_GREEN, 1}, {GL_BLUE, 1},
    {GL_RG, 2}, {GL_INTENSITY, 1}, {GL_DEPTH_COMPONENT, 1}, {GL_COLOR_INDEX, 1},
    {GL_STENCIL_INDEX, 1},
};
#define PIX_NFORMATS (sizeof(pix_formats) / sizeof(pix_formats[0]))

/* Picks a valid pair from two selector bytes; returns the pixel components. */
static inline int pix_pick(uint8_t tsel, uint8_t fsel, GLenum *format, GLenum *type, unsigned *esize,
                           int *packed) {
    const pix_type_t *t = &pix_types[tsel % PIX_NTYPES];
    *type = t->type;
    *esize = t->size;
    *packed = t->packed_comps != 0;
    if (t->packed_comps == 3) {             /* packed RGB types need GL_RGB/GL_BGR */
        static const GLenum f3[] = {GL_RGB, GL_BGR};
        *format = f3[fsel & 1];
        return 3;
    }
    if (t->packed_comps == 4) {             /* packed RGBA types need GL_RGBA/GL_BGRA */
        static const GLenum f4[] = {GL_RGBA, GL_BGRA};
        *format = f4[fsel & 1];
        return 4;
    }
    const pix_format_t *f = &pix_formats[fsel % PIX_NFORMATS];
    *format = f->format;
    return f->comps;
}

/* Bytes the GL reads for a w x h image with the given unpack alignment, row length 0, no
 * skips.  Rows are padded to the alignment when the element is smaller than it; this
 * rounds the last row up too (a conforming driver may read the padded row). */
static inline size_t pix_image_size(int w, int h, int comps, unsigned esize, int packed, int align) {
    size_t row = packed ? (size_t)w * esize : (size_t)w * (size_t)comps * esize;
    size_t stride = row;
    if (esize < (unsigned)align) stride = (row + (size_t)align - 1) / (size_t)align * (size_t)align;
    return stride * (size_t)h;
}

/* GL_BITMAP data: one bit per pixel, rows padded to the unpack alignment. */
static inline size_t pix_bitmap_size(int w, int h, int align) {
    size_t row = ((size_t)w + 7) / 8;
    return (row + (size_t)align - 1) / (size_t)align * (size_t)align * (size_t)h;
}

/* Reads up to n bytes from the input cursor, zero-padding past its end. */
static inline void pix_take(const uint8_t **p, size_t *left, void *dst, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    size_t k = n < *left ? n : *left;
    for (size_t i = 0; i < k; i++) d[i] = (*p)[i];
    for (size_t i = k; i < n; i++) d[i] = 0;
    *p += k;
    *left -= k;
}
#endif
