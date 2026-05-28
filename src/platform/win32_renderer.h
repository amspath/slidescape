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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum win32_renderer_api_t {
	WIN32_RENDERER_API_OPENGL = 0,
} win32_renderer_api_t;

typedef struct app_state_t app_state_t;

bool win32_renderer_init_window(HWND window, win32_renderer_api_t api);
void win32_renderer_init_viewer(app_state_t* app_state);
void win32_renderer_init_imgui(app_state_t* app_state);
void win32_renderer_imgui_new_frame();
void win32_renderer_render_imgui_draw_data(struct ImDrawData* draw_data);
void win32_renderer_set_swap_interval(int interval);
int win32_renderer_get_refresh_rate();
void win32_renderer_set_viewport(i32 width, i32 height);
void win32_renderer_present();
bool win32_renderer_can_present();

#ifdef __cplusplus
}
#endif
