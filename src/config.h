#ifndef NDEBUG
#define DO_DEBUG 1
#else
#define DO_DEBUG 0
#endif

#if !IS_SERVER
#define USE_CONSOLE_GUI 1
#endif

#define PANIC_DONT_INLINE 1

#define NO_TIMER_INITIALIZED_RUNTIME_CHECK

#ifdef _WIN32
#define USE_LTALLOC_INSTEAD_OF_MALLOC
#endif

#define USE_OPENGL_DEBUG_CONTEXT DO_DEBUG

#define USE_MULTIPLE_OPENGL_CONTEXTS 0
#define PREFER_DEDICATED_GRAPHICS 0

#define REMOTE_CLIENT_VERBOSE 0

#define APP_TITLE "Slidescape"
#define APP_VERSION "0.46"

