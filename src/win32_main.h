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

#ifndef WIN32_MAIN
#define WIN32_MAIN

// todo: fix this (make opaque structure?)
#include <windows.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BYTES_PER_PIXEL 4

typedef struct {
	union {
		struct {
			BITMAPINFO bitmapinfo;
		} win32;
	};
	void* memory;
	size_t memory_size;
	int width;
	int height;
	int pitch;
} surface_t;

typedef struct {
	int width;
	int height;
} win32_window_dimension_t;


typedef struct work_queue_t {
	HANDLE semaphore_handle;
	i32 volatile next_entry_to_submit;
	i32 volatile next_entry_to_execute;
	i32 volatile completion_count;
	i32 volatile completion_goal;
	work_queue_entry_t entries[256];
} work_queue_t;


typedef struct win32_thread_info_t {
	i32 logical_thread_index;
	work_queue_t* queue;
} win32_thread_info_t;

typedef struct {
	HANDLE async_io_event;
	OVERLAPPED overlapped;
	u64 thread_memory_raw_size;
	u64 thread_memory_usable_size; // free space from aligned_rest_of_thread_memory onward
	void* aligned_rest_of_thread_memory;
} thread_memory_t;


void win32_open_file_dialog(HWND window);
void win32_toggle_fullscreen(HWND window);
bool32 win32_is_fullscreen(HWND window);
void win32_diagnostic(const char* prefix);

// globals
#if defined(WIN32_MAIN_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern HWND main_window;
extern SYSTEM_INFO system_info;
extern u32 os_page_size;
extern i32 total_thread_count;
extern i32 logical_cpu_count;
extern work_queue_t work_queue;


#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif


#endif
