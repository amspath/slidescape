/*
  BSD 2-Clause License

  Copyright (c) 2019-2023, Pieter Valkema

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include "common.h"

// https://stackoverflow.com/questions/11228855/header-files-for-x86-simd-intrinsics
#if defined(_MSC_VER)
/* Microsoft C/C++-compatible compiler */
     #include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
/* GCC-compatible compiler, targeting x86/x86-64 */
#include <x86intrin.h>
#elif defined(__GNUC__) && defined(__ARM_NEON__)
/* GCC-compatible compiler, targeting ARM with NEON */
     #include <arm_neon.h>
#elif defined(__GNUC__) && defined(__IWMMXT__)
     /* GCC-compatible compiler, targeting ARM with WMMX */
     #include <mmintrin.h>
#elif (defined(__GNUC__) || defined(__xlC__)) && (defined(__VEC__) || defined(__ALTIVEC__))
     /* XLC or GCC-compatible compiler, targeting PowerPC with VMX/VSX */
     #include <altivec.h>
#elif defined(__GNUC__) && defined(__SPE__)
     /* GCC-compatible compiler, targeting PowerPC with SPE */
     #include <spe.h>
#endif

#if WINDOWS
#define write_barrier do { _WriteBarrier(); _mm_sfence(); } while (0)
#define read_barrier _ReadBarrier()

static inline i32 atomic_increment(volatile i32* x) {
	return InterlockedIncrement((volatile long*)x);
}

static inline i32 atomic_decrement(volatile i32* x) {
	return InterlockedDecrement((volatile long*)x);
}

static inline i32 atomic_add(volatile i32* x, i32 amount) {
	return InterlockedAdd((volatile long*)x, (long)amount);
}

static inline i32 atomic_subtract(volatile i32* x, i32 amount) {
	return InterlockedAdd((volatile long*)x, (long)(-amount));
}

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
	i32 read_value = InterlockedCompareExchange((volatile long*)destination, exchange, comparand);
	return (read_value == comparand);
}

static inline u32 bit_scan_forward(u32 x) {
	unsigned long first_bit = 0;
	_BitScanForward(&first_bit, x);
	return (u32) first_bit;
}

#elif APPLE
#define OSATOMIC_USE_INLINED 1
#include <libkern/OSAtomic.h>

#define write_barrier
#define read_barrier

static inline i32 atomic_increment(volatile i32* x) {
	return OSAtomicIncrement32(x);
}

static inline i32 atomic_decrement(volatile i32* x) {
	return OSAtomicDecrement32(x);
}

static inline i32 atomic_add(volatile i32* x, i32 amount) {
    return OSAtomicAdd32(amount, x);
}

static inline i32 atomic_subtract(volatile i32* x, i32 amount) {
    return OSAtomicAdd32(-amount, x);
}

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
	bool result = OSAtomicCompareAndSwap32(comparand, exchange, destination);
	return result;
}

static inline u32 bit_scan_forward(u32 x) {
	return __builtin_ctz(x);
}

#else
//TODO: implement
#define write_barrier
#define read_barrier

static inline i32 atomic_increment(volatile i32* x) {
    return __sync_add_and_fetch(x, 1);
}

static inline i32 atomic_decrement(volatile i32* x) {
    return __sync_sub_and_fetch(x, 1);
}

static inline i32 atomic_add(volatile i32* x, i32 amount) {
    return __sync_add_and_fetch(x, amount);
}

static inline i32 atomic_subtract(volatile i32* x, i32 amount) {
    return __sync_sub_and_fetch(x, amount);
}

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
    i32 read_value = __sync_val_compare_and_swap(destination, comparand, exchange);
    return (read_value == comparand);
}

static inline u32 atomic_or(volatile u32* x, u32 mask) {
	return __sync_or_and_fetch(x, mask);
}

static inline u32 bit_scan_forward(u32 x) {
	return _bit_scan_forward(x);
}

#endif

// see:
// https://stackoverflow.com/questions/41770887/cross-platform-definition-of-byteswap-uint64-and-byteswap-ulong
// byte swap operations adapted from this code:
// https://github.com/google/cityhash/blob/8af9b8c2b889d80c22d6bc26ba0df1afb79a30db/src/city.cc#L50
// License information copied in below:

// Copyright (c) 2011 Google, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// CityHash, by Geoff Pike and Jyrki Alakuijala

#ifdef __GNUC__

#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)

#elif _MSC_VER

#include <stdlib.h>
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)

#elif defined(__APPLE__)

// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#elif defined(__sun) || defined(sun)

#include <sys/byteorder.h>
#define bswap_16(x) BSWAP_16(x)
#define bswap_32(x) BSWAP_32(x)
#define bswap_64(x) BSWAP_64(x)

#elif defined(__FreeBSD__)

#include <sys/endian.h>
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)

#elif defined(__OpenBSD__)

#include <sys/types.h>
#define bswap_32(x) swap16(x)
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)

#elif defined(__NetBSD__)

#include <sys/types.h>
#include <machine/bswap.h>
#if defined(__BSWAP_RENAME) && !defined(__bswap_32)
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#endif

#else

#include <byteswap.h>

#endif

static inline u16 maybe_swap_16(u16 x, bool is_big_endian) {
	return is_big_endian ? bswap_16(x) : x;
}

static inline u32 maybe_swap_32(u32 x, bool is_big_endian) {
	return is_big_endian ? bswap_32(x) : x;
}

static inline u64 maybe_swap_64(u64 x, bool is_big_endian) {
	return is_big_endian ? bswap_64(x) : x;
}

#if APPLE_ARM
static inline i32 popcount(u32 x) {
    return __builtin_popcount(x);
}
#elif COMPILER_MSVC
static inline i32 popcount(u32 x) {
	return __popcnt(x);
}
#else
static inline i32 popcount(u32 x) {
    return _popcnt32(x);
}
#endif
