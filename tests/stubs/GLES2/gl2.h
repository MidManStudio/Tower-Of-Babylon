// Stand-in for the OpenGL ES 2.0 header, for tests/platform_check.sh only: the few names the game calls directly.
#ifndef STUB_GL2_H
#define STUB_GL2_H
typedef int GLint;
typedef unsigned int GLbitfield;
typedef unsigned int GLenum;
#define GL_DEPTH_BITS 0x0D56
#define GL_SAMPLES 0x80A9
#define GL_DEPTH_BUFFER_BIT 0x00000100
void glClear(GLbitfield mask);
void glGetIntegerv(GLenum pname, GLint *data);
#endif
