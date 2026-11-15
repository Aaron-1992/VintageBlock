// tools/shim/GL/gl.h : tiny fake GL used by tools/uishot.cpp to rasterise the
// immediate-mode UI on the CPU (so the HUD can be checked without a GPU).
#pragma once
#include <cstddef>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef float GLfloat;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef double GLdouble;
typedef ptrdiff_t GLsizeiptr;
typedef char GLchar;

#define GL_QUADS 0x0007
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLE_FAN 0x0006
#define GL_POINTS 0x0000
#define GL_TEXTURE_2D 0x0DE1
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_LIGHTING 0x0B50
#define GL_FOG 0x0B60
#define GL_ALPHA_TEST 0x0BC0
#define GL_CULL_FACE 0x0B44
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_TEXTURE 0x1702
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_LESS 0x0201
#define GL_MULTISAMPLE 0x809D
#define GL_LINE_SMOOTH 0x0B20
#define GL_QUADS_FOLLOW_PROVOKING_VERTEX 0
#define GL_RESCALE_NORMAL 0x803A
#define GL_NORMALIZE 0x0BA1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_2D_MULTISAMPLE 0

void glBegin(GLenum mode);
void glEnd();
void glVertex2f(GLfloat x, GLfloat y);
void glVertex2i(GLint x, GLint y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glTexCoord2f(GLfloat u, GLfloat v);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glBlendFunc(GLenum s, GLenum d);
void glMatrixMode(GLenum m);
void glLoadIdentity();
void glPushMatrix();
void glPopMatrix();
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear(GLbitfield mask);
void glLineWidth(GLfloat w);
void glGenTextures(GLsizei n, GLuint* ids);
void glDeleteTextures(GLsizei n, const GLuint* ids);
void glBindTexture(GLenum target, GLuint id);
void glTexParameteri(GLenum target, GLenum pname, GLint v);
void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                  GLenum fmt, GLenum type, const void* data);
void glDepthMask(GLboolean on);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void glPixelStorei(GLenum p, GLint v);
