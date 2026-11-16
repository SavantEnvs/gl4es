/* Fuzz gl4es' GLSL 1.10/1.20 -> GLSL ES converter (shaderconv.c, preproc.c, shader_hacks.c)
 * through the public GL shader API, as an application would hand it shader text.
 * Input: byte 0 bit 0 picks vertex/fragment shader; the rest is the shader source. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "fake_gles.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    fake_gles_init();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    fake_gles_init();
    GLenum type = (data[0] & 1) ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER;
    /* glShaderSource with a NULL length array takes NUL-terminated strings. */
    char *src = malloc(size);
    memcpy(src, data + 1, size - 1);
    src[size - 1] = 0;
    const GLchar *strs[1] = {src};

    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, strs, NULL);
    glCompileShader(sh);
    GLint len = 0;
    glGetShaderiv(sh, GL_SHADER_SOURCE_LENGTH, &len);
    char back[256];
    GLsizei got = 0;
    glGetShaderSource(sh, sizeof(back), &got, back);
    glDeleteShader(sh);
    free(src);
    return 0;
}
