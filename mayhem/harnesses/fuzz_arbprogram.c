/* Fuzz gl4es' ARB_vertex_program / ARB_fragment_program assembler -> GLSL converter
 * (arbparser.c, arbgenerator.c, arbconverter.c, oldprogram.c) through glProgramStringARB,
 * the way an application hands program text to the GL.
 * Input: byte 0 bit 0 picks GL_VERTEX_PROGRAM_ARB / GL_FRAGMENT_PROGRAM_ARB; the rest is the
 * program string (passed with its exact length, as the ARB spec allows non-terminated text). */
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
    if (size < 1 || size - 1 > 0x7fffffff) return -1;
    fake_gles_init();
    GLenum target = (data[0] & 1) ? GL_FRAGMENT_PROGRAM_ARB : GL_VERTEX_PROGRAM_ARB;
    GLsizei len = (GLsizei)(size - 1);
    /* exact-size copy so reads past the declared length are caught */
    char *txt = malloc(len ? (size_t)len : 1);
    memcpy(txt, data + 1, (size_t)len);

    GLuint prog = 0;
    glGenProgramsARB(1, &prog);
    glBindProgramARB(target, prog);
    glProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, len, txt);
    const GLubyte *err = glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    (void)err;
    GLint errpos = 0;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errpos);
    glBindProgramARB(target, 0);
    glDeleteProgramsARB(1, &prog);
    free(txt);
    return 0;
}
