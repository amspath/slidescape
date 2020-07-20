// OpenGL loaders: use 4.5 for debugging, 3.1 for release (no OpenGL debug)
// TODO: use 3.1 base version?

#ifndef USE_OPENGL_DEBUG_CONTEXT
#error "please define USE_OPENGL_DEBUG_CONTEXT"
#endif

#if USE_OPENGL_DEBUG_CONTEXT
#include <glad/glad45.h>
#else
#include <glad/glad31.h>
#endif