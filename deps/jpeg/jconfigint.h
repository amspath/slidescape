/* libjpeg-turbo build number */
#define BUILD  "@BUILD@"

/* Compiler's inline keyword */
#undef inline

/* How to obtain function inlining. */
#ifdef _MSC_VER
#define INLINE __forceinline
#else
#define INLINE inline __attribute__((always_inline))
#endif

/* How to obtain thread-local storage */
#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL __thread
#endif

/* Define to the full name of this package. */
#define PACKAGE_NAME  "@CMAKE_PROJECT_NAME@"

/* Version number of package */
#define VERSION  "2.0.90"

/* The size of `size_t', as computed by sizeof. */
#ifdef _MSC_VER
#define SIZEOF_SIZE_T 8 // TODO: what is the equivalent for MSVC?
#else
#define SIZEOF_SIZE_T __SIZEOF_POINTER__
#endif

/* Define if your compiler has __builtin_ctzl() and sizeof(unsigned long) == sizeof(size_t). */
#ifndef _MSC_VER
#define HAVE_BUILTIN_CTZL
#endif

/* Define to 1 if you have the <intrin.h> header file. */
#if !defined(__APPLE__) && defined(__has_include)
#if __has_include(<intrin.h>)
#define HAVE_INTRIN_H
#endif
#endif

#if defined(_MSC_VER) && defined(HAVE_INTRIN_H)
#if (SIZEOF_SIZE_T == 8)
//#define HAVE_BITSCANFORWARD64
#elif (SIZEOF_SIZE_T == 4)
//#define HAVE_BITSCANFORWARD
#endif
#endif
