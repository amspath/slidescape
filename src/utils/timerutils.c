/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2023  Pieter Valkema

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

// Timer-related procedures

#include "common.h"
#include "timerutils.h"

#if WINDOWS

#include <windows.h>

i64 performance_counter_frequency;
bool is_sleep_granular;

void win32_init_timer() {
	LARGE_INTEGER perf_counter_frequency_result;
	QueryPerformanceFrequency(&perf_counter_frequency_result);
	performance_counter_frequency = perf_counter_frequency_result.QuadPart;
	// Make Sleep() more granular
	UINT desired_scheduler_granularity_ms = 1;
	is_sleep_granular = (timeBeginPeriod(desired_scheduler_granularity_ms) == TIMERR_NOERROR);
}

i64 get_clock() {
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

