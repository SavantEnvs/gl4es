/* Fuzz gl4es' fixed-function emulation as a GL 1.x application drives it: immediate mode
 * (glBegin/glEnd), display lists (list.c, listdraw.c, listrl.c), client vertex arrays and
 * glDrawArrays/glDrawElements/glMultiDrawArrays (drawing.c, array.c, render.c), and the
 * state that feeds the generated fixed-function shaders (fpe.c, fpe_shader.c, light.c,
 * fog, texgen.c, texenv, eval.c, matrix.c, stack.c).
 *
 * Input: a sequence of opcodes, each one public GL call whose arguments come from the
 * following bytes (enums from small tables, coordinates bounded to +-512).  Client arrays
 * point into a fixed buffer and every vertex index/count the harness passes stays inside
 * it, so gl4es is only handed arrays a conforming application could pass; every enabled array
 * has a pointer, and glBegin/glEnd and glNewList/glEndList are never misnested. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "fake_gles.h"

#define MAXV 256          /* vertices addressable through client arrays */
#define VSTRIDE 64        /* largest stride (bytes) the harness uses */
#define MAXIDX 512

static float vbuf[MAXV * VSTRIDE / sizeof(float)];
static GLubyte idx8[MAXIDX];
static GLushort idx16[MAXIDX];
static GLuint idx32[MAXIDX];
static float ctrl[3 * 8 * 8];
static GLuint textures[2];

typedef struct { const uint8_t *p; size_t left; } cur_t;
static uint8_t u8(cur_t *c) {
    if (!c->left) return 0;
    c->left--;
    return *c->p++;
}
static int s16(cur_t *c) { int lo = u8(c); int hi = u8(c); return (int16_t)(lo | hi << 8); }
static float fl(cur_t *c) { return (float)(s16(c) % 32768) / 64.0f; } /* -512 .. 512 */
static float unit(cur_t *c) { return u8(c) / 255.0f; }
static void fl4(cur_t *c, float *v) { for (int i = 0; i < 4; i++) v[i] = fl(c); }
#define PICK(c, tab) (tab[u8(c) % (sizeof(tab) / sizeof(tab[0]))])

static const GLenum prims[] = {GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP, GL_TRIANGLES,
                               GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUADS, GL_QUAD_STRIP, GL_POLYGON};
static const GLenum caps[] = {
    GL_LIGHTING, GL_LIGHT0, GL_LIGHT1, GL_FOG, GL_TEXTURE_2D, GL_TEXTURE_1D, GL_TEXTURE_GEN_S,
    GL_TEXTURE_GEN_T, GL_TEXTURE_GEN_R, GL_TEXTURE_GEN_Q, GL_BLEND, GL_ALPHA_TEST, GL_CULL_FACE,
    GL_COLOR_MATERIAL, GL_NORMALIZE, GL_RESCALE_NORMAL, GL_POLYGON_STIPPLE, GL_LINE_STIPPLE,
    GL_CLIP_PLANE0, GL_CLIP_PLANE1, GL_POINT_SPRITE, GL_DEPTH_TEST, GL_POLYGON_OFFSET_FILL,
    GL_MAP1_VERTEX_3, GL_MAP2_VERTEX_3, GL_AUTO_NORMAL, GL_COLOR_SUM, GL_LINE_SMOOTH,
    GL_POINT_SMOOTH, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_RECTANGLE_ARB,
    GL_VERTEX_PROGRAM_POINT_SIZE, GL_COLOR_LOGIC_OP, GL_DITHER,
};
static const GLenum client_states[] = {GL_VERTEX_ARRAY, GL_COLOR_ARRAY, GL_NORMAL_ARRAY,
                                       GL_TEXTURE_COORD_ARRAY, GL_SECONDARY_COLOR_ARRAY,
                                       GL_FOG_COORD_ARRAY};
static const GLenum light_pnames[] = {GL_AMBIENT, GL_DIFFUSE, GL_SPECULAR, GL_POSITION,
                                      GL_SPOT_DIRECTION, GL_SPOT_EXPONENT, GL_SPOT_CUTOFF,
                                      GL_CONSTANT_ATTENUATION, GL_LINEAR_ATTENUATION,
                                      GL_QUADRATIC_ATTENUATION};
static const GLenum mat_pnames[] = {GL_AMBIENT, GL_DIFFUSE, GL_SPECULAR, GL_EMISSION, GL_SHININESS,
                                    GL_AMBIENT_AND_DIFFUSE};
static const GLenum faces[] = {GL_FRONT, GL_BACK, GL_FRONT_AND_BACK};
static const GLenum texgen_modes[] = {GL_OBJECT_LINEAR, GL_EYE_LINEAR, GL_SPHERE_MAP, GL_NORMAL_MAP,
                                      GL_REFLECTION_MAP};
static const GLenum texgen_coords[] = {GL_S, GL_T, GL_R, GL_Q};
static const GLenum fog_modes[] = {GL_LINEAR, GL_EXP, GL_EXP2};
static const GLenum env_modes[] = {GL_MODULATE, GL_DECAL, GL_BLEND, GL_REPLACE, GL_ADD, GL_COMBINE};
static const GLenum env_pnames[] = {GL_COMBINE_RGB, GL_COMBINE_ALPHA, GL_SRC0_RGB, GL_SRC1_RGB,
                                    GL_SRC2_RGB, GL_SRC0_ALPHA, GL_OPERAND0_RGB, GL_OPERAND1_RGB,
                                    GL_OPERAND0_ALPHA, GL_RGB_SCALE, GL_ALPHA_SCALE};
static const GLint env_values[] = {GL_REPLACE, GL_MODULATE, GL_ADD, GL_ADD_SIGNED, GL_INTERPOLATE,
                                   GL_SUBTRACT, GL_DOT3_RGB, GL_DOT3_RGBA, GL_TEXTURE, GL_CONSTANT,
                                   GL_PRIMARY_COLOR, GL_PREVIOUS, GL_TEXTURE0, GL_TEXTURE1,
                                   GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
                                   GL_ONE_MINUS_SRC_ALPHA, 1, 2, 4};
static const GLenum matrix_modes[] = {GL_MODELVIEW, GL_PROJECTION, GL_TEXTURE};
static const GLenum blend_factors[] = {GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                       GL_DST_COLOR, GL_SRC_COLOR, GL_CONSTANT_COLOR};
static const GLenum funcs[] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL,
                               GL_GEQUAL, GL_ALWAYS};
static const GLenum interleaved[] = {GL_V2F, GL_V3F, GL_C4UB_V2F, GL_C4UB_V3F, GL_C3F_V3F,
                                     GL_N3F_V3F, GL_C4F_N3F_V3F, GL_T2F_V3F, GL_T4F_V4F,
                                     GL_T2F_C4UB_V3F, GL_T2F_C3F_V3F, GL_T2F_N3F_V3F,
                                     GL_T2F_C4F_N3F_V3F, GL_T4F_C4F_N3F_V4F};

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    fake_gles_init();
    static const GLubyte px[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
    glGenTextures(2, textures);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

/* Pops the current matrix stack down to its base entry (never below it: popping an
 * empty stack is a separate gl4es bug the opcodes can reach). */
static void pop_matrices(GLenum depth_query) {
    GLint depth = 1;
    glGetIntegerv(depth_query, &depth);
    for (GLint i = 1; i < depth && i < 1024; i++) glPopMatrix();
    glLoadIdentity();
}

/* Gives every client array a valid pointer into vbuf (a tightly packed 4-float layout, in range
 * for all MAXV vertices), on all four texture units. The GL spec leaves an enabled array without
 * a pointer undefined, so every array the opcodes can enable has one before the first opcode. */
static void set_arrays(void) {
    for (int t = 0; t < 4; t++) {
        glClientActiveTexture(GL_TEXTURE0 + t);
        glTexCoordPointer(4, GL_FLOAT, 0, vbuf);
    }
    glClientActiveTexture(GL_TEXTURE0);
    glVertexPointer(4, GL_FLOAT, 0, vbuf);
    glColorPointer(4, GL_FLOAT, 0, vbuf);
    glNormalPointer(GL_FLOAT, 0, vbuf);
    glSecondaryColorPointer(3, GL_FLOAT, 0, vbuf);
    glFogCoordPointer(GL_FLOAT, 0, vbuf);
}

/* Keeps the call sequence one a conforming application issues. The GL spec makes the following
 * application errors, so the harness does not issue them:
 *  - between glBegin and glEnd only glVertex, glColor, glNormal, glTexCoord, glMultiTexCoord,
 *    glMaterial and glArrayElement (opcodes 2-7, 10, 39) are allowed; opcode 43 calls glBegin
 *    itself, so it is left out too;
 *  - glVertex and glArrayElement (which sends vertex calls) only between glBegin and glEnd;
 *  - no glBegin inside glBegin, no glEnd without glBegin;
 *  - no glNewList inside glNewList, no glEndList without glNewList. */
static int call_is_valid(int op, int in_begin, int in_list) {
    if (in_begin) return (op >= 1 && op <= 7) || op == 10 || op == 39;
    if (op == 1 || op == 2 || op == 3 || op == 39) return 0;
    if (op == 30) return !in_list;
    if (op == 31) return in_list;
    return 1;
}

/* Restores the GL state an input could change, so each input starts from the same state. */
static void reset_state(int in_begin, int in_list) {
    if (in_begin) glEnd();
    if (in_list) glEndList();
    glFlush();
    while (glGetError() != GL_NO_ERROR) {}
    for (int t = 0; t < 4; t++) {
        glActiveTexture(GL_TEXTURE0 + t);
        glClientActiveTexture(GL_TEXTURE0 + t);
        for (size_t i = 0; i < sizeof(client_states) / sizeof(client_states[0]); i++)
            glDisableClientState(client_states[i]);
        glMatrixMode(GL_TEXTURE);
        pop_matrices(GL_TEXTURE_STACK_DEPTH);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    for (int m = 0; m < 2; m++) {
        glMatrixMode(m ? GL_PROJECTION : GL_MODELVIEW);
        pop_matrices(m ? GL_PROJECTION_STACK_DEPTH : GL_MODELVIEW_STACK_DEPTH);
    }
    /* pop to (and including) the attribute frames pushed at the start of the input */
    for (int i = 0; i < 64; i++) { glPopClientAttrib(); if (glGetError() != GL_NO_ERROR) break; }
    for (int i = 0; i < 64; i++) { glPopAttrib(); if (glGetError() != GL_NO_ERROR) break; }
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) glDisable(caps[i]);
    glDeleteLists(1, 8);
    glColor4f(1, 1, 1, 1);
    glNormal3f(0, 0, 1);
    glTexCoord4f(0, 0, 0, 1);
    while (glGetError() != GL_NO_ERROR) {}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    fake_gles_init();
    cur_t c = {data, size};
    /* array contents: the first part of the input, as bounded floats */
    for (size_t i = 0; i < sizeof(vbuf) / sizeof(vbuf[0]); i++)
        vbuf[i] = (i < size) ? (float)((int8_t)data[i]) / 16.0f : 0.5f;
    for (int i = 0; i < MAXIDX; i++) {
        uint8_t b = data[i % size];
        idx8[i] = b; idx16[i] = b; idx32[i] = b; /* all < MAXV */
    }
    set_arrays();
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    set_arrays();

    int in_begin = 0, in_list = 0;
    for (int n = 0; n < 256 && c.left > 0; n++) {
        float v[4];
        int op = u8(&c) % 46;
        if (!call_is_valid(op, in_begin, in_list)) continue;
        switch (op) {
        case 0: glBegin(PICK(&c, prims)); in_begin = 1; break;
        case 1: glEnd(); in_begin = 0; break;
        case 2: { float x = fl(&c), y = fl(&c), z = fl(&c); glVertex3f(x, y, z); break; }
        case 3: { float x = fl(&c), y = fl(&c); glVertex2f(x, y); break; }
        case 4: { float r = unit(&c), g = unit(&c), b = unit(&c), a = unit(&c); glColor4f(r, g, b, a); break; }
        case 5: { float x = fl(&c), y = fl(&c), z = fl(&c); glNormal3f(x, y, z); break; }
        case 6: { float s = fl(&c), t = fl(&c); glTexCoord2f(s, t); break; }
        case 7: { GLenum u = GL_TEXTURE0 + u8(&c) % 4; float s = fl(&c), t = fl(&c), r = fl(&c), q = fl(&c); glMultiTexCoord4f(u, s, t, r, q); break; }
        case 8: { int cap = u8(&c); if (cap & 0x80) glDisable(caps[cap % (sizeof(caps) / sizeof(caps[0]))]); else glEnable(caps[cap % (sizeof(caps) / sizeof(caps[0]))]); break; }
        case 9: { GLenum l = GL_LIGHT0 + u8(&c) % 8; GLenum pn = PICK(&c, light_pnames); fl4(&c, v); glLightfv(l, pn, v); break; }
        case 10: { GLenum f = PICK(&c, faces); GLenum pn = PICK(&c, mat_pnames); fl4(&c, v); if (pn == GL_SHININESS) v[0] = unit(&c) * 128; glMaterialfv(f, pn, v); break; }
        case 11: { GLenum f = PICK(&c, faces); GLenum pn = PICK(&c, mat_pnames); if (pn == GL_SHININESS) pn = GL_DIFFUSE; glColorMaterial(f, pn); break; }
        case 12: { glFogi(GL_FOG_MODE, PICK(&c, fog_modes)); float d = unit(&c), s = fl(&c), e = fl(&c); glFogf(GL_FOG_DENSITY, d); glFogf(GL_FOG_START, s); glFogf(GL_FOG_END, e); break; }
        case 13: { v[0] = unit(&c); v[1] = unit(&c); v[2] = unit(&c); v[3] = unit(&c); glFogfv(GL_FOG_COLOR, v); break; }
        case 14: { GLenum co = PICK(&c, texgen_coords); GLenum m = PICK(&c, texgen_modes); glTexGeni(co, GL_TEXTURE_GEN_MODE, m); break; }
        case 15: { GLenum co = PICK(&c, texgen_coords); GLenum pl = (u8(&c) & 1) ? GL_EYE_PLANE : GL_OBJECT_PLANE; fl4(&c, v); glTexGenfv(co, pl, v); break; }
        case 16: glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, PICK(&c, env_modes)); break;
        case 17: { GLenum pn = PICK(&c, env_pnames); glTexEnvi(GL_TEXTURE_ENV, pn, PICK(&c, env_values)); break; }
        case 18: { GLenum u = GL_TEXTURE0 + u8(&c) % 4; glActiveTexture(u); glClientActiveTexture(u); break; }
        case 19: { int t = u8(&c) % 3; glBindTexture(GL_TEXTURE_2D, t ? textures[t - 1] : 0); break; }
        case 20: glMatrixMode(PICK(&c, matrix_modes)); break;
        case 21: glPushMatrix(); break;
        case 22: glPopMatrix(); break;
        case 23: glLoadIdentity(); break;
        case 24: { float x = fl(&c), y = fl(&c), z = fl(&c); glTranslatef(x, y, z); break; }
        case 25: { float a = fl(&c), x = fl(&c), y = fl(&c), z = fl(&c); glRotatef(a, x, y, z); break; }
        case 26: { float x = fl(&c), y = fl(&c), z = fl(&c); glScalef(x, y, z); break; }
        case 27: { float l = fl(&c), r = fl(&c), b = fl(&c), t = fl(&c), ne = fl(&c), fa = fl(&c);
                   if (u8(&c) & 1) glOrtho(l, r, b, t, ne, fa); else glFrustum(l, r, b, t, ne, fa); break; }
        case 28: glPushAttrib(GL_ALL_ATTRIB_BITS); break;
        case 29: glPopAttrib(); break;
        case 30: { /* glNewList */ GLuint id = 1 + u8(&c) % 8; glNewList(id, (u8(&c) & 1) ? GL_COMPILE_AND_EXECUTE : GL_COMPILE); in_list = 1; break; }
        case 31: glEndList(); in_list = 0; break;
        case 32: glCallList(1 + u8(&c) % 8); break;
        case 33: { int cs = u8(&c); GLenum st = client_states[cs % (sizeof(client_states) / sizeof(client_states[0]))];
                   if (cs & 0x80) glDisableClientState(st); else glEnableClientState(st); break; }
        case 34: { /* array pointers into vbuf; stride 0 or up to VSTRIDE */
            int which = u8(&c) % 5, sz = 2 + u8(&c) % 3, st = u8(&c) % 5;
            GLsizei stride = st ? (GLsizei)(4 * 4 * st) : 0; /* 16..64 bytes */
            if (stride && stride < 4 * sz) stride = 4 * sz;
            const void *ptr = vbuf + (u8(&c) % 4);
            switch (which) {
            case 0: glVertexPointer(sz, GL_FLOAT, stride, ptr); break;
            case 1: if (u8(&c) & 1) glColorPointer(4, GL_UNSIGNED_BYTE, stride, ptr); else glColorPointer(sz < 3 ? 3 : sz, GL_FLOAT, stride, ptr); break;
            case 2: glNormalPointer(GL_FLOAT, stride && stride < 12 ? 12 : stride, ptr); break;
            case 3: glTexCoordPointer(sz, GL_FLOAT, stride, ptr); break;
            case 4: glSecondaryColorPointer(3, GL_FLOAT, stride && stride < 12 ? 12 : stride, ptr); break;
            }
            break;
        }
        case 35: { GLenum m = PICK(&c, prims); int first = u8(&c), count = u8(&c) % (MAXV - first + 1); glDrawArrays(m, first, count); break; }
        case 36: { GLenum m = PICK(&c, prims); int count = (u8(&c) | u8(&c) << 8) % (MAXIDX + 1); int t = u8(&c) % 3;
                   if (t == 0) glDrawElements(m, count, GL_UNSIGNED_BYTE, idx8);
                   else if (t == 1) glDrawElements(m, count, GL_UNSIGNED_SHORT, idx16);
                   else glDrawElements(m, count, GL_UNSIGNED_INT, idx32);
                   break; }
        case 37: { /* glMultiDrawArrays, counts/firsts inside the arrays */
            GLenum m = PICK(&c, prims); int k = u8(&c) % 9;
            GLint firsts[8]; GLsizei counts[8];
            for (int i = 0; i < k; i++) { firsts[i] = u8(&c); counts[i] = u8(&c) % (MAXV - firsts[i] + 1); }
            glMultiDrawArrays(m, firsts, counts, k);
            break;
        }
        case 38: { /* glMultiDrawElements over slices of the index arrays */
            GLenum m = PICK(&c, prims); int k = u8(&c) % 5;
            const GLvoid *ptrs[4]; GLsizei counts[4];
            for (int i = 0; i < k; i++) { int off = u8(&c) % 128; ptrs[i] = idx16 + off; counts[i] = u8(&c) % (MAXIDX - off + 1); }
            glMultiDrawElements(m, counts, GL_UNSIGNED_SHORT, ptrs, k);
            break;
        }
        case 39: glArrayElement(u8(&c) % MAXV); break;
        case 40: { float a = fl(&c), b = fl(&c), cc = fl(&c), d = fl(&c); glRectf(a, b, cc, d); break; }
        case 41: { GLenum f = PICK(&c, faces); static const GLenum pm[] = {GL_POINT, GL_LINE, GL_FILL}; glPolygonMode(f, PICK(&c, pm)); break; }
        case 42: { int fac = 1 + u8(&c) % 8; int pat = u8(&c) | u8(&c) << 8; glLineStipple(fac, (GLushort)pat);
                   GLubyte st[128]; for (int i = 0; i < 128; i++) st[i] = u8(&c); glPolygonStipple(st); break; }
        case 43: { /* evaluators: control points sized for the order */
            int uo = 1 + u8(&c) % 8, vo = 1 + u8(&c) % 8;
            for (int i = 0; i < 3 * uo * vo; i++) ctrl[i] = fl(&c);
            float u1 = fl(&c), u2 = fl(&c);
            if (u8(&c) & 1) {
                glMap1f(GL_MAP1_VERTEX_3, u1, u2, 3, uo, ctrl);
                int nseg = 1 + u8(&c) % 32;
                glMapGrid1f(nseg, u1, u2);
                glEvalMesh1(GL_LINE, 0, nseg);
                glBegin(GL_LINE_STRIP); glEvalCoord1f(fl(&c)); glEvalCoord1f(fl(&c)); glEnd();
            } else {
                glMap2f(GL_MAP2_VERTEX_3, u1, u2, 3, uo, 0, 1, 3 * uo, vo, ctrl);
                int nu = 1 + u8(&c) % 16, nv = 1 + u8(&c) % 16;
                glMapGrid2f(nu, u1, u2, nv, 0, 1);
                glEvalMesh2((u8(&c) & 1) ? GL_FILL : GL_LINE, 0, nu, 0, nv);
            }
            break;
        }
        case 44: { GLenum p = GL_CLIP_PLANE0 + u8(&c) % 6; double eq[4]; for (int i = 0; i < 4; i++) eq[i] = fl(&c); glClipPlane(p, eq); break; }
        case 45: { /* misc raster-state */
            switch (u8(&c) % 7) {
            case 0: glShadeModel((u8(&c) & 1) ? GL_FLAT : GL_SMOOTH); break;
            case 1: glAlphaFunc(PICK(&c, funcs), unit(&c)); break;
            case 2: { GLenum s = PICK(&c, blend_factors), d = PICK(&c, blend_factors); glBlendFunc(s, d); break; }
            case 3: glPointSize(1 + u8(&c) % 64); break;
            case 4: glLineWidth(1 + u8(&c) % 16); break;
            case 5: glInterleavedArrays(PICK(&c, interleaved), 0, vbuf); break;
            case 6: glDepthFunc(PICK(&c, funcs)); break;
            }
            break;
        }
        }
    }
    reset_state(in_begin, in_list);
    return 0;
}
