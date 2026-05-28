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

#include "common.h"
#include "win32_renderer.h"

#include "imgui.h"

typedef struct win32_renderer_t {
	win32_renderer_api_t api;
	void* present_handle;
	bool initialized;
} win32_renderer_t;

static win32_renderer_t win32_renderer;

bool win32_renderer_opengl_init_window(HWND window, HINSTANCE instance, const char* window_class_name, HDC* out_glrc_hdc);
void win32_renderer_opengl_init_viewer(app_state_t* app_state);
void win32_renderer_opengl_init_imgui(app_state_t* app_state);
void win32_renderer_opengl_imgui_new_frame();
void win32_renderer_opengl_render_imgui_draw_data(ImDrawData* draw_data);
void win32_renderer_opengl_set_swap_interval(int interval);
int win32_renderer_opengl_get_refresh_rate(HDC glrc_hdc);
void win32_renderer_opengl_set_viewport(i32 width, i32 height);
void win32_renderer_opengl_present(HDC glrc_hdc);

bool win32_renderer_init_window(HWND window, HINSTANCE instance, const char* window_class_name, win32_renderer_api_t api) {
	win32_renderer.api = api;
	win32_renderer.present_handle = NULL;
	win32_renderer.initialized = false;

	bool result = false;
	switch (api) {
		case WIN32_RENDERER_API_OPENGL: {
			HDC glrc_hdc = NULL;
			result = win32_renderer_opengl_init_window(window, instance, window_class_name, &glrc_hdc);
			win32_renderer.present_handle = glrc_hdc;
		} break;
	}

	win32_renderer.initialized = result;
	return win32_renderer.initialized;
}

void win32_renderer_init_viewer(app_state_t* app_state) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			win32_renderer_opengl_init_viewer(app_state);
		} break;
	}
}

void win32_renderer_init_imgui(app_state_t* app_state) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			win32_renderer_opengl_init_imgui(app_state);
		} break;
	}
}

void win32_renderer_imgui_new_frame() {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			win32_renderer_opengl_imgui_new_frame();
		} break;
	}
}

void win32_renderer_render_imgui_draw_data(ImDrawData* draw_data) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			win32_renderer_opengl_render_imgui_draw_data(draw_data);
		} break;
	}
}

void win32_renderer_set_swap_interval(int interval) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			win32_renderer_opengl_set_swap_interval(interval);
		} break;
	}
}

int win32_renderer_get_refresh_rate() {
	int result = 0;
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			result = win32_renderer_opengl_get_refresh_rate((HDC)win32_renderer.present_handle);
		} break;
	}
	return result;
}

void win32_renderer_set_viewport(i32 width, i32 height) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			win32_renderer_opengl_set_viewport(width, height);
		} break;
	}
}

void win32_renderer_present() {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			win32_renderer_opengl_present((HDC)win32_renderer.present_handle);
		} break;
	}
}

bool win32_renderer_can_present() {
	return win32_renderer.initialized && win32_renderer.present_handle;
}
