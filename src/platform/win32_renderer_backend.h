/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2026  Pieter Valkema

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

#include <windows.h>

typedef struct app_state_t app_state_t;
struct ImDrawData;

typedef struct win32_renderer_backend_t {
	bool (*init_window)(HWND window, HINSTANCE instance, const char* window_class_name, void** out_present_handle);
	void (*init_viewer)(app_state_t* app_state);
	void (*init_imgui)(app_state_t* app_state);
	void (*imgui_new_frame)();
	void (*render_imgui_draw_data)(ImDrawData* draw_data);
	void (*set_swap_interval)(int interval);
	int (*get_refresh_rate)(void* present_handle);
	void (*set_viewport)(i32 width, i32 height);
	void (*present)(void* present_handle);
} win32_renderer_backend_t;

const win32_renderer_backend_t* win32_renderer_opengl_get_backend();
