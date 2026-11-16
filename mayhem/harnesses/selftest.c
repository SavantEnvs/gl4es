/* gl4es known-answer self-test (run by mayhem/test.sh).
 *
 * Upstream's own tests replay apitrace captures against a real GLES display, which a
 * container does not have.  This drives the same public GL entry points the fuzz targets
 * use, on top of the fake GLES driver, and checks what gl4es hands the driver: the GLSL ES
 * produced from desktop GLSL and from ARB programs, and the pixels produced by the software
 * DXT decoder and the pixel converters.  Prints "ok <n> <name>" / "not ok <n> <name>" lines
 * and a final "summary passed=<p> failed=<f>". */
#include <stdio.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "fake_gles.h"

static int passed, failed, num;

static void check(const char *name, int cond, const char *detail) {
    num++;
    if (cond) {
        passed++;
        printf("ok %d %s\n", num, name);
    } else {
        failed++;
        printf("not ok %d %s\n", num, name);
        if (detail) printf("# got:\n%s\n", detail);
    }
}

static const char *compile(GLenum type, const char *src) {
    fake_reset_shader_source();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glDeleteShader(sh);
    return fake_last_shader_source();
}

static const char *arb(GLenum target, const char *src) {
    fake_reset_shader_source();
    GLuint p = 0;
    glGenProgramsARB(1, &p);
    glBindProgramARB(target, p);
    glProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(src), src);
    return fake_last_shader_source();
}

static int pixels_are(const fake_tex_upload_t *u, int first, int count, const unsigned char rgba[4]) {
    for (int i = first; i < first + count; i++)
        if ((size_t)(i * 4 + 4) > u->size || memcmp(u->data + i * 4, rgba, 4)) return 0;
    return 1;
}

int main(void) {
    fake_gles_init();
    const char *out;

    out = compile(GL_VERTEX_SHADER,
                  "#version 120\nvoid main(){ gl_Position = ftransform(); gl_FrontColor = gl_Color; }\n");
    check("glsl_vertex_version", !strncmp(out, "#version 100\n", 13), out);
    check("glsl_vertex_ftransform",
          strstr(out, "return _gl4es_ModelViewProjectionMatrix * _gl4es_Vertex;") != NULL &&
              strstr(out, "attribute highp vec4 _gl4es_Vertex;") != NULL, out);
    check("glsl_vertex_builtins",
          strstr(out, "_gl4es_FrontColor = _gl4es_Color;") != NULL && !strstr(out, "gl_FrontColor"), out);

    out = compile(GL_FRAGMENT_SHADER,
                  "#version 120\nuniform sampler2D tex;\n"
                  "void main(){ gl_FragColor = texture2D(tex, gl_TexCoord[0].xy) * gl_Color; }\n");
    check("glsl_fragment_builtins",
          !strncmp(out, "#version 100\n", 13) && !strstr(out, "gl_TexCoord[") && !strstr(out, " gl_Color") &&
              strstr(out, "uniform sampler2D tex;") != NULL, out);

    out = arb(GL_VERTEX_PROGRAM_ARB,
              "!!ARBvp1.0\nMOV result.position, vertex.position;\nMOV result.color, vertex.color;\nEND\n");
    check("arb_vertex_mov",
          strstr(out, "gl_Position = _gl4es_Vertex;") != NULL &&
              strstr(out, "_gl4es_FrontColor = _gl4es_Color;") != NULL, out);

    out = arb(GL_FRAGMENT_PROGRAM_ARB, "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n");
    check("arb_fragment_mov", strstr(out, "gl_FragColor") != NULL && strstr(out, "void main()") != NULL, out);

    out = arb(GL_VERTEX_PROGRAM_ARB, "!!ARBvp1.0\nFOO result.position;\nEND\n");
    GLint pos = -5;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
    const char *err = (const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    check("arb_error_report", err && !strcmp(err, "Unknown operand") && pos == 14, err ? err : "(null)");

    /* DXT1 block: color0 = 0xF800 (red), color1 = 0x001F (blue); row 0 uses index 1. */
    static const unsigned char red[4] = {0xff, 0, 0, 0xff}, blue[4] = {0, 0, 0xff, 0xff};
    unsigned char blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0x55, 0x00, 0x00, 0x00};
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    fake_reset_tex_upload();
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 8, blk);
    const fake_tex_upload_t *u = fake_last_tex_upload();
    check("dxt1_decode_format", u->calls == 1 && u->format == GL_RGBA && u->type == GL_UNSIGNED_BYTE &&
                                    u->width == 4 && u->height == 4, NULL);
    check("dxt1_decode_pixels", pixels_are(u, 0, 4, blue) && pixels_are(u, 4, 12, red), NULL);

    float fpx[3] = {1.0f, 0.5f, 0.0f};
    fake_reset_tex_upload();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_FLOAT, fpx);
    check("float_rgb_to_ubyte", u->format == GL_RGB && u->type == GL_UNSIGNED_BYTE && u->size == 3 &&
                                    u->data[0] == 0xff && u->data[1] == 0x7f && u->data[2] == 0, NULL);

    unsigned char bgra[4] = {10, 20, 30, 40};
    fake_reset_tex_upload();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    check("bgra_upload", u->format == GL_BGRA && u->size == 4 && !memcmp(u->data, bgra, 4), NULL);

    unsigned char lum[2] = {7, 9};
    fake_reset_tex_upload();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum);
    check("luminance_upload", u->format == GL_LUMINANCE && u->size == 2 && u->data[0] == 7 && u->data[1] == 9,
          NULL);

    printf("summary passed=%d failed=%d\n", passed, failed);
    return failed ? 1 : 0;
}
