/* Fuzz gl4es' raster path (raster.c: glBitmap clipping into the CPU bitmap buffer,
 * glDrawPixels conversion and zoom, raster position; blit.c) with a short sequence of
 * raster-state and drawing calls, as an application issues them.
 *
 * Input: a sequence of 1-byte opcodes, each followed by its fixed-size argument bytes.
 * Bitmap and pixel buffers get exactly the size the GL spec requires for their
 * dimensions and the current unpack alignment (pixfmt.h). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pixfmt.h"
#include "fake_gles.h"

typedef struct { const uint8_t *p; size_t left; } cur_t;

static uint8_t u8(cur_t *c) { uint8_t v = 0; pix_take(&c->p, &c->left, &v, 1); return v; }
static int16_t s16(cur_t *c) { uint8_t b[2]; pix_take(&c->p, &c->left, b, 2); return (int16_t)(b[0] | b[1] << 8); }
/* a coordinate-sized float: -512 .. 512 in 1/4 steps */
static float crd(cur_t *c) { return (float)(s16(c) % 2048) / 4.0f; }

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    fake_gles_init();
    return 0;
}

/* Puts every piece of GL state the opcodes (or gl4es' own raster bookkeeping) can change back
 * to its default, so an input's result does not depend on the previous input. */
static void reset_state(void) {
    static const GLenum store[] = {GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS,
                                   GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST, GL_PACK_ROW_LENGTH,
                                   GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS, GL_PACK_SWAP_BYTES,
                                   GL_PACK_LSB_FIRST};
    for (size_t i = 0; i < sizeof(store) / sizeof(store[0]); i++) glPixelStorei(store[i], 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    static const GLenum scale[] = {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE};
    static const GLenum bias[] = {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS};
    for (int i = 0; i < 4; i++) { glPixelTransferf(scale[i], 1.0f); glPixelTransferf(bias[i], 0.0f); }
    glPixelTransferf(GL_INDEX_SHIFT, 0);
    glPixelTransferf(GL_INDEX_OFFSET, 0);
    glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
    glPixelZoom(1.0f, 1.0f);
    glViewport(0, 0, 256, 256);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 256, 0, 256, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(1, 1, 1);
    glRasterPos2f(0, 0);
    glWindowPos2i(0, 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    fake_gles_init();
    cur_t c = {data, size};
    int align = 4;
    reset_state();
    for (int n = 0; n < 48 && c.left > 0; n++) {
        switch (u8(&c) % 12) {
        case 0: { /* viewport */
            int x = s16(&c) % 512, y = s16(&c) % 512;
            int w = u8(&c) + 1, h = u8(&c) + 1;
            glViewport(x, y, w, h);
            break;
        }
        case 1: { float x = crd(&c), y = crd(&c); glRasterPos2f(x, y); break; }
        case 2: { int x = s16(&c) % 1024, y = s16(&c) % 1024; glWindowPos2i(x, y); break; }
        case 3: { float x = crd(&c), y = crd(&c), z = crd(&c) / 512.0f; glRasterPos3f(x, y, z); break; }
        case 4: { /* pixel zoom: -8 .. 8 */
            float zx = (float)((int8_t)u8(&c)) / 16.0f, zy = (float)((int8_t)u8(&c)) / 16.0f;
            glPixelZoom(zx, zy);
            break;
        }
        case 5: { static const int al[] = {1, 2, 4, 8}; align = al[u8(&c) & 3]; glPixelStorei(GL_UNPACK_ALIGNMENT, align); break; }
        case 6: { /* glBitmap */
            int w = u8(&c) % 97, h = u8(&c) % 97;
            float xo = crd(&c), yo = crd(&c), xm = crd(&c), ym = crd(&c);
            size_t need = pix_bitmap_size(w, h, align);
            uint8_t *bm = malloc(need ? need : 1);
            pix_take(&c.p, &c.left, bm, need);
            glBitmap(w, h, xo, yo, xm, ym, bm);
            free(bm);
            break;
        }
        case 7: { /* glDrawPixels */
            GLenum format, type; unsigned esize; int packed;
            uint8_t ts = u8(&c), fs = u8(&c);
            int comps = pix_pick(ts, fs, &format, &type, &esize, &packed);
            int w = 1 + u8(&c) % 64, h = 1 + u8(&c) % 64;
            size_t need = pix_image_size(w, h, comps, esize, packed, align);
            uint8_t *px = malloc(need);
            pix_take(&c.p, &c.left, px, need);
            glDrawPixels(w, h, format, type, px);
            free(px);
            break;
        }
        case 8: { float r = u8(&c) / 255.0f, g = u8(&c) / 255.0f, b = u8(&c) / 255.0f; glColor3f(r, g, b); break; }
        case 9: glFlush(); break;
        case 10: { /* glCopyPixels from the (fake) framebuffer */
            int x = s16(&c) % 512, y = s16(&c) % 512, w = u8(&c) % 64, h = u8(&c) % 64;
            glCopyPixels(x, y, w, h, GL_COLOR);
            break;
        }
        case 11: { /* glReadPixels into an exact RGBA8 buffer, pack alignment 1 */
            int x = s16(&c) % 512, y = s16(&c) % 512, w = 1 + u8(&c) % 64, h = 1 + u8(&c) % 64;
            uint8_t *out = malloc((size_t)w * (size_t)h * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            free(out);
            break;
        }
        }
    }
    glFinish();
    reset_state();
    return 0;
}
