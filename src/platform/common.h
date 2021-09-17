/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2021  Pieter Valkema

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once
#ifndef COMMON_H
#define COMMON_H

#include "config.h"

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
#define OPENGL_H <glad/glad.h>
#else
#define WINDOWS 0
#endif

#ifdef __APPLE__
#define APPLE 1
#define OPENGL_H <OpenGL/gl3.h>
#else
#define APPLE 0
#endif

#if LINUX
#include <features.h>
#endif
#if !WINDOWS
#include <unistd.h> // for access(), F_OK
#include <stdlib.h>
//#define _aligned_malloc(size, alignment) aligned_alloc(alignment, size)
//#define _aligned_free(ptr) free(ptr)
#if __SIZEOF_POINTER__==8
#define fseeko64 fseek
#define fopen64 fopen
#define fgetpos64 fgetpos
#define fsetpos64 fsetpos
#endif
#endif

#if defined(__linux__) || (!defined(__APPLE__) && (defined(__unix__) || defined(_POSIX_VERSION)))
#define LINUX 1
#define OPENGL_H <GL/glew.h>
#else
#define LINUX 0
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
#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef THREAD_LOCAL
#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL __thread
#endif
#endif

#if LINUX
#include <fcntl.h>
#endif

// Faster malloc(), realloc(), free()
#include <ltalloc.h>
#if WINDOWS
#define malloc ltmalloc
#define calloc ltcalloc
#define free ltfree
#define realloc ltrealloc
#endif

// Typesafe dynamic array and hash tables for C
// https://github.com/nothings/stb/blob/master/stb_ds.h
// NOTE: need to define STB_DS_IMPLEMENTATION in one source file
#define STBDS_REALLOC(context,ptr,size) ltrealloc((ptr),(size))
#define STBDS_FREE(context,ptr) ltfree((ptr))
#include <stb_ds.h>
#define arrlastptr(a) ((a)+(stbds_header(a)->length-1))

// NOTE: need to define STB_SPRINTF_IMPLEMENTATION in one source file
#include <stb_sprintf.h>
#undef sprintf
#undef snprintf
#undef vsprintf
#undef vnsprintf
#define sprintf   stbsp_sprintf
#define snprintf  stbsp_snprintf
#define vsprintf  stbsp_vsprintf
#define vnsprintf stbsp_vnsprintf

// Typedef choices for numerical types
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef long long i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef unsigned long long u64;

typedef int32_t bool32;
typedef int8_t bool8;

// Convenience macros
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#define ATLEAST(a, b) MAX(a, b)
#define ATMOST(a, b) MIN(a, b)
#define ABS(x) ((x) < 0 ? -(x) : (x))

#define LERP(t,a,b)               ( (a) + (t) * (float) ((b)-(a)) )
#define UNLERP(t,a,b)             ( ((t) - (a)) / (float) ((b) - (a)) )

#define CLAMP(x,xmin,xmax)  ((x) < (xmin) ? (xmin) : (x) > (xmax) ? (xmax) : (x))

#define MACRO_VAR(name) concat(name, __LINE__)
#define defer(start, end) for (int MACRO_VAR(_i_) = (start, 0); !MACRO_VAR(_i_); (MACRO_VAR(_i_)+=1,end))


#define SQUARE(x) ((x)*(x))

#define memset_zero(x) memset((x), 0, sizeof(*x))

#define KILOBYTES(n) (1024LL*(n))
#define MEGABYTES(n) (1024LL*KILOBYTES(n))
#define GIGABYTES(n) (1024LL*MEGABYTES(n))
#define TERABYTES(n) (1024LL*GIGABYTES(n))


#if defined(SOURCE_PATH_SIZE) && !defined(__FILENAME__)
#define __FILENAME__ (__FILE__ + SOURCE_PATH_SIZE)
#elif !defined(__FILENAME__)
#define __FILENAME__ __FILE__
#endif

#define panic() _panic(__FILE__, __LINE__, __func__)
static inline void _panic(const char* source_filename, i32 line, const char* func) {
	fprintf(stderr, "%s(): %s:%d\n", func, source_filename, line);
	fprintf(stderr, "A fatal error occurred (aborting).\n");
#if DO_DEBUG
#if COMPILER_GCC
	__builtin_trap();
#endif
#endif
	abort();
}

#ifndef NDEBUG
#define DO_DEBUG 1
#define ASSERT(expr) do {if (!(expr)) panic();} while(0)
#else
#define DO_DEBUG 0
#define ASSERT(expr)
#endif

//http://www.pixelbeat.org/programming/gcc/static_assert.html
#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
/* These can't be used after statements in c89. */
#ifdef __COUNTER__
#define STATIC_ASSERT(e) \
    ;enum { ASSERT_CONCAT(ASSERT_CONCAT(static_assert_, __COUNTER__), __LINE__) = 1/(int)(!!(e)) }
#else
/* This can't be used twice on the same line so ensure if using in headers
   * that the headers are not included twice (by wrapping in #ifndef...#endif)
   * Note it doesn't cause an issue when used on same line of separate modules
   * compiled with gcc -combine -fwhole-program.  */
  #define STATIC_ASSERT(e) \
    ;enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(int)(!!(e)) }
#endif

#define DUMMY_STATEMENT do { int __x = 5; } while(0)

static inline u64 next_pow2(u64 x) {
	ASSERT(x > 1);
	x -= 1;
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	x |= (x >> 32);
	return x + 1;
}

static inline i32 div_floor(i32 a, i32 b) {
	return a / b - (a % b < 0);
}




#endif
