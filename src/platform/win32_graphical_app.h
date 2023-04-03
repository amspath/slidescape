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

#ifndef WIN32_MAIN
#define WIN32_MAIN

#include <windows.h>
#include "platform.h"
#include "win32_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BYTES_PER_PIXEL 4

typedef struct {
	int width;
	int height;
} win32_window_dimension_t;

LPSTR* WINAPI CommandLineToArgvA(LPSTR lpCmdline, int* numargs);

// globals
#if defined(WIN32_GRAPHICAL_APP_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

//extern HWND global_main_window;
extern HCURSOR global_cursor_arrow;
extern HCURSOR global_cursor_crosshair;
extern HCURSOR global_current_cursor;
extern bool global_is_using_software_renderer;

#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif


#endif
