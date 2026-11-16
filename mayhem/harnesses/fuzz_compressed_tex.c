/* Fuzz gl4es' software S3TC (DXT1/3/5) decoder and the texture upload path behind
 * glCompressedTexImage2D / glCompressedTexSubImage2D (texture_compressed.c, decompress.c,
 * pixel.c).  The fake driver advertises no S3TC, so gl4es decodes every block on the CPU.
 *
 * Input: [fmt][level][w lo][w hi][h lo][h hi][flags] then block data.
 * The GL spec (EXT_texture_compression_s3tc) fixes imageSize to
 * ceil(w/4)*ceil(h/4)*(8 for DXT1, 16 for DXT3/5); an application must pass exactly that
 * many bytes, so shorter inputs are rejected and gl4es gets an exact-size heap copy. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "fake_gles.h"

static const GLenum formats[] = {
    GL_COMPRESSED_RGB_S3TC_DXT1_EXT,       GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
    GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,      GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
    GL_COMPRESSED_SRGB_S3TC_DXT1_EXT,      GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT,
    GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT,
};

static size_t dxt_size(GLenum f, int w, int h) {
    size_t block = (f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
                    f == GL_COMPRESSED_SRGB_S3TC_DXT1_EXT || f == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT)
                       ? 8 : 16;
    return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * block;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    fake_gles_init();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 7) return -1;
    fake_gles_init();
    GLenum fmt = formats[data[0] % (sizeof(formats) / sizeof(formats[0]))];
    GLint level = data[1] % 4;
    int w = 1 + ((data[2] | (data[3] << 8)) % 64);
    int h = 1 + ((data[4] | (data[5] << 8)) % 64);
    uint8_t flags = data[6];
    const uint8_t *blocks = data + 7;
    size_t avail = size - 7;
    size_t need = dxt_size(fmt, w, h);
    if (avail < need) return -1;
    uint8_t *img = malloc(need ? need : 1);
    memcpy(img, blocks, need);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (flags & 1) glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    if (flags & 2) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glCompressedTexImage2D(GL_TEXTURE_2D, level, fmt, w, h, 0, (GLsizei)need, img);

    /* A sub-rectangle update at a 4-aligned offset (the S3TC rule), with the rest of the data. */
    if ((flags & 4) && w >= 4 && h >= 4) {
        int sw = 4 * (1 + (flags >> 4) % (w / 4));
        int sh = 4 * (1 + (flags >> 6) % (h / 4));
        int sx = 0, sy = 0;
        if (w - sw >= 4) sx = 4 * ((flags >> 3 & 1) * ((w - sw) / 4));
        if (h - sh >= 4) sy = 4 * ((flags >> 5 & 1) * ((h - sh) / 4));
        size_t sneed = dxt_size(fmt, sw, sh);
        if (avail - need >= sneed) {
            uint8_t *sub = malloc(sneed);
            memcpy(sub, blocks + need, sneed);
            glCompressedTexSubImage2D(GL_TEXTURE_2D, level, sx, sy, sw, sh, fmt, (GLsizei)sneed, sub);
            free(sub);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
    free(img);
    return 0;
}
