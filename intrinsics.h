#pragma once

#include "common.h"

#include "intrin.h"

#if WINDOWS
#define write_barrier do { _WriteBarrier(); _mm_sfence(); } while (0)
#define read_barrier _ReadBarrier()

#define interlocked_increment(x) InterlockedIncrement((volatile long*)(x))
#define interlocked_compare_exchange(destination, exchange, comparand) \
  InterlockedCompareExchange((volatile long*)(destination), (exchange), (comparand))

#else
//TODO: implement
#define write_barrier
#define read_barrier
#define interlocked_increment(x)
#define interlocked_compare_exchange(x)
#endif



