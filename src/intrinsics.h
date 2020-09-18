/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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

#include "immintrin.h"

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
	OSAtomicIncrement32((volatile long*)x);
}

static inline bool atomic_compare_exchange(volatile i32* destination, i32 exchange, i32 comparand) {
	bool result = OSAtomicCompareAndSwap32(comparand, exchange, (volatile long*)destination);
	return result;
}

#else
//TODO: implement
#define write_barrier
#define read_barrier
#define atomic_increment(x)
#define atomic_compare_exchange(x)
#endif



