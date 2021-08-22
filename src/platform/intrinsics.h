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

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
	bool result = OSAtomicCompareAndSwap32(comparand, exchange, destination);
	return result;
}

static inline u32 bit_scan_forward(u32 x) {
	return _bit_scan_forward(x);
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

static inline u16 maybe_swap_16(u16 x, bool32 is_big_endian) {
	return is_big_endian ? bswap_16(x) : x;
}

static inline u32 maybe_swap_32(u32 x, bool32 is_big_endian) {
	return is_big_endian ? bswap_32(x) : x;
}

static inline u64 maybe_swap_64(u64 x, bool32 is_big_endian) {
	return is_big_endian ? bswap_64(x) : x;
}

