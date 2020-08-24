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
#include "viewer.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif


void win32_init_gui(HWND hwnd);
void gui_new_frame();
void menu_close_file(app_state_t* app_state);
void gui_draw_polygon_outline(v2f* points, i32 count, rgba_t rgba, float thickness);
void gui_draw(app_state_t* app_state, input_t* input, i32 client_width, i32 client_height);

// from imgui_impl_win32.cpp
LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);



// globals
#if defined(GUI_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif


extern bool show_demo_window;
extern bool show_image_adjustments_window INIT(= false);
extern bool show_open_remote_window;
extern bool show_slide_list_window;
extern bool show_annotations_window;
extern bool show_annotation_group_assignment_window;
extern bool show_display_options_window;
extern bool show_about_window;
extern bool gui_want_capture_mouse;
extern bool gui_want_capture_keyboard;
extern char remote_hostname[64] INIT(= "localhost");
extern char remote_port[64] INIT(= "2000");
extern char remote_filename[128] INIT(= "sample.tiff");


#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif
