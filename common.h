#ifndef COMMON_H
#define COMMON_H


#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

// Platform detection
#ifdef _WIN32
#define WINDOWS 1
#define _WIN32_WINNT 0x0600
#define WINVER 0x0600
#else
#define WINDOWS 0
#endif

// Compiler detection
#ifdef _MSC_VER
#define COMPILER_MSVC 1
#else
#define COMPILER_MSVC 0
#endif

#ifdef __GNUC__
#define COMPILER_GCC 1
#else
#define COMPILER_GCC 0
#endif

// IDE detection (for dealing with pesky preprocessor highlighting issues)
#if defined(__JETBRAINS_IDE__)
#define CODE_EDITOR 1
#else
#define CODE_EDITOR 0
#endif

// Use 64-bit file offsets for fopen, etc.
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <stdint.h>
#include <stdbool.h>

// Typedef choices for numerical types
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int32_t bool32;
typedef int8_t bool8;

// Convenience macros
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define LERP(t,a,b)               ( (a) + (t) * (float) ((b)-(a)) )
#define UNLERP(t,a,b)             ( ((t) - (a)) / (float) ((b) - (a)) )

#define CLAMP(x,xmin,xmax)  ((x) < (xmin) ? (xmin) : (x) > (xmax) ? (xmax) : (x))


#define SQUARE(x) ((x)*(x))

#define memset_zero(x) memset((x), 0, sizeof(*x))


#if defined(SOURCE_PATH_SIZE) && !defined(__FILENAME__)
#define __FILENAME__ (__FILE__ + SOURCE_PATH_SIZE)
#elif !defined(__FILENAME__)
#define __FILENAME__ __FILE__
#endif

static inline void panic() {
#if COMPILER_GCC
	__builtin_trap();
#else
	abort();
#endif
}

#ifndef NDEBUG
#define ASSERT(expr) if (!(expr)) panic();
#else
#define ASSERT(expr)
#endif


#endif
