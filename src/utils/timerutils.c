/*
  BSD 2-Clause License

  Copyright (c) 2019-2024, Pieter Valkema

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

// Timer-related procedures

#include "common.h"
#include "timerutils.h"

#if WINDOWS

#include <windows.h>

static i64 performance_counter_frequency;
static bool is_sleep_granular;
static bool is_timer_initialized;

void win32_init_timer() {
	LARGE_INTEGER perf_counter_frequency_result;
	QueryPerformanceFrequency(&perf_counter_frequency_result);
	performance_counter_frequency = perf_counter_frequency_result.QuadPart;
	// Make Sleep() more granular
	UINT desired_scheduler_granularity_ms = 1;
	is_sleep_granular = (timeBeginPeriod(desired_scheduler_granularity_ms) == TIMERR_NOERROR);
	is_timer_initialized = true;
}

i64 get_clock() {
#ifndef NO_TIMER_INITIALIZED_RUNTIME_CHECK
	if (!is_timer_initialized) win32_init_timer();
#else
#ifndef NDEBUG
	if (!is_timer_initialized) {
		fatal_error("get_clock(): timer not initialized; on Windows, call win32_init_timer() once before calling get_clock()");
	}
#endif
#endif
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return result.QuadPart;
}

float get_seconds_elapsed(i64 start, i64 end) {
	return (float)(end - start) / (float)performance_counter_frequency;
}

void platform_sleep(u32 ms) {
	Sleep(ms);
}

void platform_sleep_ns(i64 ns) {
	Sleep(ns / 1000000);
}

#else

#include <time.h>

i64 get_clock() {
	struct timespec t = {};
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_nsec + 1e9 * t.tv_sec;
}

float get_seconds_elapsed(i64 start, i64 end) {
	i64 elapsed_nanoseconds = end - start;
	float elapsed_seconds = ((float)elapsed_nanoseconds) / 1e9f;
	return elapsed_seconds;
}

void platform_sleep(u32 ms) {
	struct timespec tim = {}, tim2 = {};
	tim.tv_sec = 0;
	tim.tv_nsec = ms * 1000000;
	nanosleep(&tim, &tim2);
}

void platform_sleep_ns(i64 ns) {
	struct timespec tim = {}, tim2 = {};
	tim.tv_sec = 0;
	tim.tv_nsec = ns;
	nanosleep(&tim, &tim2);
}
#endif

