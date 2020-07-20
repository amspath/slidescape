// OpenGL loaders: use 4.5 for debugging, 3.1 for release (no OpenGL debug)
// TODO: use 3.1 base version?

#ifdef NDEBUG
#include "glad31.c"
#else
#include "glad45.c"
#endif
