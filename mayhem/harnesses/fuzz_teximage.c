/* Fuzz gl4es' pixel format conversion (pixel.c: pixel_convert, scaling, swizzles) and the
 * texture upload/readback paths (texture.c, texture_read.c, texture_params.c) through
 * glTexImage2D / glTexSubImage2D / glGetTexImage with app-chosen format, type, internal
 * format and unpack alignment.
 *
 * Input: [type][format][internalformat][align][w][h][flags][sub x][sub y][sub w][sub h]
 * then pixel bytes.  The buffer handed to gl4es has exactly the size the GL spec requires
 * for that format/type/alignment (pixfmt.h), filled from the input (zero past its end). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pixfmt.h"
#include "fake_gles.h"

static const GLint internals[] = {
    0 /* = format */, GL_RGBA, GL_RGB, GL_RGBA8, GL_RGB8, GL_RGB5, GL_RGBA4, GL_RGB5_A1,
    GL_LUMINANCE, GL_LUMINANCE8, GL_LUMINANCE_ALPHA, GL_ALPHA, GL_ALPHA8, GL_INTENSITY,
    GL_COMPRESSED_RGBA, GL_COMPRESSED_RGB, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
    GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT16, GL_R8, GL_RG8, GL_RGBA16F_ARB, GL_RGB32F_ARB,
    GL_SRGB8, GL_SRGB8_ALPHA8, GL_BGRA, 1, 2, 3, 4,
};

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    fake_gles_init();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 11) return -1;
    fake_gles_init();
    GLenum format, type;
    unsigned esize;
    int packed;
    int comps = pix_pick(data[0], data[1], &format, &type, &esize, &packed);
    GLint internal = internals[data[2] % (sizeof(internals) / sizeof(internals[0]))];
    if (!internal) internal = (GLint)format;
    static const int aligns[] = {1, 2, 4, 8};
    int align = aligns[data[3] & 3];
    int w = 1 + data[4] % 96, h = 1 + data[5] % 96;
    uint8_t flags = data[6];
    int sx = data[7] % w, sy = data[8] % h;
    int sw = 1 + data[9] % (w - sx), sh = 1 + data[10] % (h - sy);
    const uint8_t *p = data + 11;
    size_t left = size - 11;

    size_t need = pix_image_size(w, h, comps, esize, packed, align);
    uint8_t *img = malloc(need);
    pix_take(&p, &left, img, need);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    if (flags & 1) glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    if (flags & 2) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    GLint level = (flags >> 2) & 1;
    glTexImage2D(GL_TEXTURE_2D, level, internal, w, h, 0, format, type, img);

    if (flags & 8) {
        size_t sneed = pix_image_size(sw, sh, comps, esize, packed, align);
        uint8_t *sub = malloc(sneed);
        pix_take(&p, &left, sub, sneed);
        glTexSubImage2D(GL_TEXTURE_2D, level, sx, sy, sw, sh, format, type, sub);
        free(sub);
    }
    if (flags & 16) glGenerateMipmap(GL_TEXTURE_2D);
    if (flags & 32) {
        /* Read back as RGBA/UNSIGNED_BYTE with pack alignment 1: exactly w*h*4 bytes. */
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        uint8_t *out = malloc((size_t)w * (size_t)h * 4);
        glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, out);
        free(out);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
    free(img);
    return 0;
}
