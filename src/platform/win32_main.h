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


void win32_diagnostic(const char* prefix);
void win32_diagnostic_verbose(const char* prefix);
HANDLE win32_open_overlapped_file_handle(const char* filename);
size_t win32_overlapped_read(thread_memory_t* thread_memory, HANDLE file_handle, void* dest, u32 read_size, i64 offset);

// globals
#if defined(WIN32_MAIN_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

//extern HWND global_main_window;


#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif


#endif
