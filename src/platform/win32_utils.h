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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "platform.h"

#include <windows.h>

// Windows-specific procedures
wchar_t* win32_string_widen(const char* s, size_t len, wchar_t* buffer);
char* win32_string_narrow(wchar_t* s, char* buffer, size_t buffer_size);
void win32_diagnostic(const char* prefix);
void win32_diagnostic_verbose(const char* prefix);
HANDLE win32_open_overlapped_file_handle(const char* filename);
size_t win32_overlapped_read(thread_memory_t* thread_memory, HANDLE file_handle, void* dest, u32 read_size, i64 offset);


#ifdef __cplusplus
}
#endif
