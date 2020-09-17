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

#define interlocked_increment(x) InterlockedIncrement((volatile long*)(x))
#define interlocked_compare_exchange(destination, exchange, comparand) \
  (InterlockedCompareExchange((volatile long*)(destination), (exchange), (comparand)) == (comparand))

#elif APPLE
#define OSATOMIC_USE_INLINED 1
#include <libkern/OSAtomic.h>

#define write_barrier
#define read_barrier
#define interlocked_increment(x) OSAtomicIncrement32((x))
#define interlocked_compare_exchange(destination, exchange, comparand) OSAtomicCompareAndSwap32((comparand), (exchange), (destination))

#else
//TODO: implement
#define write_barrier
#define read_barrier
#define interlocked_increment(x)
#define interlocked_compare_exchange(x)
#endif



