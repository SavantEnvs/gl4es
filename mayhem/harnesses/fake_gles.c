/* A fake GLES2 driver for gl4es: see fake_gles.h.
 *
 * Compiled with -fno-sanitize=function: unknown functions resolve to one generic
 * no-op whose C type cannot match every GLES prototype (it ignores its arguments and
 * returns 0 in the integer return register, which is what a do-nothing driver does on
 * x86-64). gl4es itself keeps every sanitizer.
 */
#include "fake_gles.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <gl4esinit.h>

static char last_shader[1 << 16];
static fake_tex_upload_t last_tex;
static unsigned int next_id = 1;

const char *fake_last_shader_source(void) { return last_shader; }
void fake_reset_shader_source(void) { last_shader[0] = 0; }
const fake_tex_upload_t *fake_last_tex_upload(void) { return &last_tex; }
void fake_reset_tex_upload(void) { memset(&last_tex, 0, sizeof(last_tex)); }

static long fake_nop(void) { return 0; }

static const GLubyte *fake_glGetString(GLenum name) {
    switch (name) {
    case GL_EXTENSIONS:
        return (const GLubyte *)"GL_OES_depth24 GL_OES_rgb8_rgba8 GL_EXT_texture_format_BGRA8888 "
                                "GL_OES_texture_npot GL_OES_standard_derivatives GL_EXT_frag_depth "
                                "GL_EXT_shader_texture_lod GL_OES_element_index_uint "
                                "GL_OES_packed_depth_stencil GL_OES_framebuffer_object";
    case GL_VERSION: return (const GLubyte *)"OpenGL ES 2.0 fake";
    case 0x8B8C /* GL_SHADING_LANGUAGE_VERSION */: return (const GLubyte *)"OpenGL ES GLSL ES 1.00";
    default: return (const GLubyte *)"fake";
    }
}

static void fake_glGetIntegerv(GLenum pname, GLint *v) {
    if (!v) return;
    switch (pname) {
    case GL_MAX_TEXTURE_SIZE: *v = 4096; return;
    case 0x8869 /* GL_MAX_VERTEX_ATTRIBS */: *v = 16; return;
    case 0x8872 /* GL_MAX_TEXTURE_IMAGE_UNITS */: *v = 16; return;
    case 0x8DFC /* GL_MAX_VARYING_VECTORS */: *v = 16; return;
    case GL_DEPTH_BITS: *v = 24; return;
    case GL_VIEWPORT: case GL_SCISSOR_BOX:
        v[0] = 0; v[1] = 0; v[2] = 256; v[3] = 256; return;
    case GL_MAX_VIEWPORT_DIMS: v[0] = 4096; v[1] = 4096; return;
    case 0x86A2 /* GL_NUM_COMPRESSED_TEXTURE_FORMATS */:
    case 0x87FE /* GL_NUM_PROGRAM_BINARY_FORMATS_OES */:
    case 0x8DF9 /* GL_NUM_SHADER_BINARY_FORMATS */:
    case 0x8CA6 /* GL_FRAMEBUFFER_BINDING */:
    case 0x8CA7 /* GL_RENDERBUFFER_BINDING */:
    case GL_TEXTURE_BINDING_2D:
    case 0x8894 /* GL_ARRAY_BUFFER_BINDING */:
    case 0x8895 /* GL_ELEMENT_ARRAY_BUFFER_BINDING */:
    case 0x8B8D /* GL_CURRENT_PROGRAM */:
        *v = 0; return;
    default: *v = 8; return;
    }
}

static void fake_glGetFloatv(GLenum pname, GLfloat *v) {
    if (!v) return;
    if (pname == 0x846E /* GL_ALIASED_LINE_WIDTH_RANGE */ || pname == 0x846D /* POINT_SIZE */) {
        v[0] = 1.0f; v[1] = 8.0f;
    } else {
        v[0] = 1.0f;
    }
}

static void fake_glGetBooleanv(GLenum pname, GLboolean *v) { (void)pname; if (v) *v = 0; }

static void fake_gen(GLsizei n, GLuint *ids) {
    for (GLsizei i = 0; ids && i < n; i++) ids[i] = next_id++;
}
static GLuint fake_create(void) { return next_id++; }
static GLuint fake_create_shader(GLenum type) { (void)type; return next_id++; }

static void fake_get_objiv(GLuint obj, GLenum pname, GLint *v) {
    (void)obj;
    if (!v) return;
    switch (pname) {
    case 0x8B81 /* GL_COMPILE_STATUS */:
    case 0x8B82 /* GL_LINK_STATUS */:
    case 0x8B83 /* GL_VALIDATE_STATUS */:
        *v = 1; return;
    /* every linked program reports one vec4 uniform, like any real (FPE) program has */
    case 0x8B86 /* GL_ACTIVE_UNIFORMS */: *v = 1; return;
    case 0x8B87 /* GL_ACTIVE_UNIFORM_MAX_LENGTH */: *v = 32; return;
    default: *v = 0; return;
    }
}

static void fake_get_log(GLuint obj, GLsizei bufsize, GLsizei *len, char *log) {
    (void)obj;
    if (len) *len = 0;
    if (log && bufsize > 0) log[0] = 0;
}

static void fake_get_active(GLuint prog, GLuint idx, GLsizei bufsize, GLsizei *len, GLint *size,
                            GLenum *type, char *name) {
    (void)prog; (void)idx;
    static const char fake_name[] = "_gl4es_fake_u";
    GLsizei n = 0;
    if (name && bufsize > 0) {
        n = (GLsizei)sizeof(fake_name) - 1 < bufsize - 1 ? (GLsizei)sizeof(fake_name) - 1 : bufsize - 1;
        memcpy(name, fake_name, (size_t)n);
        name[n] = 0;
    }
    if (len) *len = n;
    if (size) *size = 1;
    if (type) *type = 0x8B52; /* GL_FLOAT_VEC4 */
}

static GLint fake_get_location(GLuint prog, const char *name) {
    (void)prog;
    return (name && !strcmp(name, "_gl4es_fake_u")) ? 0 : -1;
}

static void fake_glGetShaderPrecisionFormat(GLenum s, GLenum p, GLint *range, GLint *precision) {
    (void)s; (void)p;
    if (range) { range[0] = 127; range[1] = 127; }
    if (precision) *precision = 23;
}

static GLenum fake_check_fb(GLenum target) { (void)target; return 0x8CD5; /* COMPLETE */ }

static void fake_glShaderSource(GLuint shader, GLsizei count, const char *const *str, const GLint *len) {
    (void)shader;
    size_t off = 0;
    last_shader[0] = 0;
    for (GLsizei i = 0; str && i < count; i++) {
        if (!str[i]) continue;
        size_t l = (len && len[i] >= 0) ? (size_t)len[i] : strlen(str[i]);
        if (l > sizeof(last_shader) - 1 - off) l = sizeof(last_shader) - 1 - off;
        memcpy(last_shader + off, str[i], l);
        off += l;
    }
    last_shader[off] = 0;
}

/* Bytes per pixel of the packed format/type pairs GLES accepts; 0 for anything else. */
static size_t bpp(GLenum format, GLenum type) {
    if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
        type == GL_UNSIGNED_SHORT_5_5_5_1)
        return 2;
    if (type != GL_UNSIGNED_BYTE) return 0;
    switch (format) {
    case GL_RGBA: case 0x80E1 /* GL_BGRA */: return 4;
    case GL_RGB: return 3;
    case GL_LUMINANCE_ALPHA: return 2;
    case GL_LUMINANCE: case GL_ALPHA: return 1;
    default: return 0;
    }
}

static void record_upload(GLint internalformat, GLsizei w, GLsizei h, GLenum format, GLenum type,
                          const void *data) {
    last_tex.calls++;
    last_tex.internalformat = (unsigned)internalformat;
    last_tex.format = format;
    last_tex.type = type;
    last_tex.width = w;
    last_tex.height = h;
    last_tex.size = 0;
    size_t b = bpp(format, type);
    if (!data || !b || w <= 0 || h <= 0) return;
    size_t n = (size_t)w * (size_t)h * b;
    if (n > sizeof(last_tex.data)) n = sizeof(last_tex.data);
    memcpy(last_tex.data, data, n);
    last_tex.size = n;
}

static void fake_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h,
                              GLint border, GLenum format, GLenum type, const void *data) {
    (void)target; (void)level; (void)border;
    record_upload(internalformat, w, h, format, type, data);
}

static void fake_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                 GLenum format, GLenum type, const void *data) {
    (void)target; (void)level; (void)x; (void)y;
    record_upload((GLint)format, w, h, format, type, data);
}

static void fake_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                              void *data) {
    (void)x; (void)y;
    size_t b = bpp(format, type);
    if (data && b && w > 0 && h > 0) memset(data, 0x40, (size_t)w * (size_t)h * b);
}

static void *APIENTRY_GL4ES fake_getprocaddress(const char *name) {
    static const struct { const char *name; void *fn; } table[] = {
        {"glGetString", (void *)fake_glGetString},
        {"glGetIntegerv", (void *)fake_glGetIntegerv},
        {"glGetFloatv", (void *)fake_glGetFloatv},
        {"glGetBooleanv", (void *)fake_glGetBooleanv},
        {"glGenTextures", (void *)fake_gen},
        {"glGenBuffers", (void *)fake_gen},
        {"glGenFramebuffers", (void *)fake_gen},
        {"glGenFramebuffersOES", (void *)fake_gen},
        {"glGenRenderbuffers", (void *)fake_gen},
        {"glGenRenderbuffersOES", (void *)fake_gen},
        {"glGenQueries", (void *)fake_gen},
        {"glGenQueriesEXT", (void *)fake_gen},
        {"glGenSamplers", (void *)fake_gen},
        {"glGenVertexArrays", (void *)fake_gen},
        {"glGenVertexArraysOES", (void *)fake_gen},
        {"glCreateProgram", (void *)fake_create},
        {"glCreateShader", (void *)fake_create_shader},
        {"glGetShaderiv", (void *)fake_get_objiv},
        {"glGetProgramiv", (void *)fake_get_objiv},
        {"glGetShaderInfoLog", (void *)fake_get_log},
        {"glGetProgramInfoLog", (void *)fake_get_log},
        {"glGetActiveUniform", (void *)fake_get_active},
        {"glGetActiveAttrib", (void *)fake_get_active},
        {"glGetUniformLocation", (void *)fake_get_location},
        {"glGetAttribLocation", (void *)fake_get_location},
        {"glGetShaderPrecisionFormat", (void *)fake_glGetShaderPrecisionFormat},
        {"glCheckFramebufferStatus", (void *)fake_check_fb},
        {"glCheckFramebufferStatusOES", (void *)fake_check_fb},
        {"glShaderSource", (void *)fake_glShaderSource},
        {"glTexImage2D", (void *)fake_glTexImage2D},
        {"glTexSubImage2D", (void *)fake_glTexSubImage2D},
        {"glReadPixels", (void *)fake_glReadPixels},
        /* Leave buffer mapping unavailable so gl4es keeps its own client copies. */
        {"glMapBufferOES", NULL},
        {"glMapBufferRange", NULL},
        {"glMapBufferRangeEXT", NULL},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (!strcmp(name, table[i].name)) return table[i].fn;
    return (void *)fake_nop;
}

static void APIENTRY_GL4ES fake_getmainfbsize(int *w, int *h) {
    if (w) *w = 256;
    if (h) *h = 256;
}

void fake_gles_init(void) {
    static int done;
    if (done) return;
    done = 1;
    setenv("LIBGL_NOBANNER", "1", 1);
    setenv("LIBGL_SILENTSTUB", "1", 1);
    set_getprocaddress(fake_getprocaddress);
    set_getmainfbsize(fake_getmainfbsize);
    initialize_gl4es();
}
