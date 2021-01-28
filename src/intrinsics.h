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

static inline void atomic_increment(volatile i32* x) {
	InterlockedIncrement((volatile long*)x);
}

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
	i32 read_value = InterlockedCompareExchange((volatile long*)destination, exchange, comparand);
	return (read_value == comparand);
}

#elif APPLE
#define OSATOMIC_USE_INLINED 1
#include <libkern/OSAtomic.h>

#define write_barrier
#define read_barrier

static inline void atomic_increment(volatile i32* x) {
	OSAtomicIncrement32(x);
}

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
	bool result = OSAtomicCompareAndSwap32(comparand, exchange, destination);
	return result;
}

#else
//TODO: implement
#define write_barrier
#define read_barrier

static inline void atomic_increment(volatile i32* x) {
    __sync_fetch_and_add(x, 1);
}

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
    i32 read_value = __sync_val_compare_and_swap(destination, comparand, exchange);
    return (read_value == comparand);
}

#endif



