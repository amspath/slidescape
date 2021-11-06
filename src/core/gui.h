/*
  Slidescape, a whole-slide image viewer for digital pathology.
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

#pragma once
#include "common.h"
#include "viewer.h"
#include "platform.h"

#ifdef __cplusplus
#include "imgui.h"
#include "imgui_internal.h"
extern "C" {
#endif

#if WINDOWS
void win32_init_gui(app_state_t* app_state);
// from imgui_impl_win32.cpp
LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

void imgui_create_context();
void gui_draw_open_file_dialog(app_state_t* app_state);
void menu_close_file(app_state_t* app_state);
void gui_draw_polygon_outline(v2f* points, i32 count, rgba_t rgba, float thickness);
void gui_draw_polygon_outline_in_scene(v2f* points, i32 count, rgba_t color, float thickness, scene_t* scene);
void gui_draw_bounds_in_scene(bounds2f bounds, rgba_t color, float thickness, scene_t* scene);
void gui_draw(app_state_t* app_state, input_t* input, i32 client_width, i32 client_height);
void gui_do_modal_popups();
void gui_add_modal_popup(const char* title, const char* message, ...);
void draw_console_window(app_state_t* app_state, const char* window_title, bool* p_open);




// globals
#if defined(GUI_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif


extern bool show_demo_window;
extern bool show_image_options_window;
extern bool show_open_remote_window;
extern bool show_slide_list_window;
extern bool show_layers_window;
extern bool show_annotations_window;
extern bool show_annotation_group_assignment_window;
extern bool show_general_options_window;
extern bool show_about_window;
extern bool show_export_region_dialog;
extern bool show_console_window INIT(= DO_DEBUG);
extern bool show_menu_bar INIT(= true);
extern bool load_next_image_as_overlay;
extern bool gui_want_capture_mouse;
extern bool gui_want_capture_keyboard;
extern char remote_hostname[64] INIT(= "localhost");
extern char remote_port[64] INIT(= "2000");
extern char remote_filename[128] INIT(= "sample.tiff");
#ifdef __cplusplus
extern ImFont* global_main_font;
extern ImFont* global_fixed_width_font;
extern ImFont* global_icon_font;
#endif

extern i32 viewer_min_level INIT(= -1);
extern i32 viewer_max_level INIT(= 10);
extern float layer_t INIT(= 1.0f);
extern float target_layer_t INIT(= 1.0f);

// annotation.cpp
extern bool auto_assign_last_group;
extern bool annotation_show_polygon_nodes_outside_edit_mode INIT(= false);
extern float annotation_normal_line_thickness INIT(= 2.0f);
extern float annotation_selected_line_thickness INIT(= 4.0f);
extern float annotation_node_size INIT(= 5.0f);
extern float annotation_opacity INIT(= 1.0f);
extern float annotation_hover_distance INIT(= 10.0f);
extern float annotation_insert_hover_distance INIT(= 25.0f);
extern bool show_delete_annotation_prompt;
extern bool dont_ask_to_delete_annotations;
extern bool show_annotation_palette_window INIT(=true);

// gui_coco.cpp
extern bool show_coco_editor_window;

extern i32 desired_region_export_format;
extern u16 tiff_export_desired_color_space INIT(= TIFF_PHOTOMETRIC_YCBCR);//TIFF_PHOTOMETRIC_RGB;
extern i32 tiff_export_jpeg_quality INIT(= 80);

#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif
