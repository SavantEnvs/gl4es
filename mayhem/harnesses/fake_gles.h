/* A fake GLES2 driver for running gl4es without a GPU (harness + self-test support).
 *
 * gl4es translates desktop GL into GLES calls that it resolves through the
 * getprocaddress hook of include/gl4esinit.h.  This driver answers those lookups:
 * a handful of functions return sane state (strings, limits, object ids, status),
 * every other one is a no-op returning 0.  The functions that receive the converted
 * output of gl4es (glShaderSource, glTexImage2D, glTexSubImage2D) record what they
 * got, so the self-test can check gl4es' results.
 */
#ifndef MAYHEM_FAKE_GLES_H
#define MAYHEM_FAKE_GLES_H
#include <stddef.h>

/* Install the driver and initialize gl4es once (idempotent). */
void fake_gles_init(void);

/* Last shader source passed by gl4es to the driver's glShaderSource (NUL-terminated,
 * "" if none). */
const char *fake_last_shader_source(void);
void fake_reset_shader_source(void);

/* Last pixel upload passed by gl4es to the driver's glTexImage2D/glTexSubImage2D. */
typedef struct {
    int calls;
    unsigned int internalformat, format, type;
    int width, height;
    unsigned char data[64 * 64 * 4]; /* first bytes of the upload (RGBA8 up to 64x64) */
    size_t size;                     /* bytes copied into data */
} fake_tex_upload_t;
const fake_tex_upload_t *fake_last_tex_upload(void);
void fake_reset_tex_upload(void);

#endif
