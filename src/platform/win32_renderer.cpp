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
#include "win32_renderer_backend.h"

typedef struct win32_renderer_t {
	win32_renderer_api_t api;
	const win32_renderer_backend_t* backend;
	void* present_handle;
	bool initialized;
} win32_renderer_t;

static win32_renderer_t win32_renderer;

static const win32_renderer_backend_t* win32_renderer_get_backend(win32_renderer_api_t api) {
	const win32_renderer_backend_t* result = NULL;
	switch (api) {
		case WIN32_RENDERER_API_OPENGL: {
			result = win32_renderer_opengl_get_backend();
		} break;
	}
	return result;
}

bool win32_renderer_init_window(HWND window, HINSTANCE instance, const char* window_class_name, win32_renderer_api_t api) {
	win32_renderer.api = api;
	win32_renderer.backend = win32_renderer_get_backend(api);
	win32_renderer.present_handle = NULL;
	win32_renderer.initialized = false;

	ASSERT(win32_renderer.backend);
	win32_renderer.initialized = win32_renderer.backend->init_window(window, instance, window_class_name, &win32_renderer.present_handle);

	return win32_renderer.initialized;
}

void win32_renderer_init_viewer(app_state_t* app_state) {
	win32_renderer.backend->init_viewer(app_state);
}

void win32_renderer_init_imgui(app_state_t* app_state) {
	win32_renderer.backend->init_imgui(app_state);
}

void win32_renderer_imgui_new_frame() {
	win32_renderer.backend->imgui_new_frame();
}

void win32_renderer_render_imgui_draw_data(ImDrawData* draw_data) {
	win32_renderer.backend->render_imgui_draw_data(draw_data);
}

void win32_renderer_set_swap_interval(int interval) {
	win32_renderer.backend->set_swap_interval(interval);
}

int win32_renderer_get_refresh_rate() {
	return win32_renderer.backend->get_refresh_rate(win32_renderer.present_handle);
}

void win32_renderer_set_viewport(i32 width, i32 height) {
	win32_renderer.backend->set_viewport(width, height);
}

void win32_renderer_present() {
	win32_renderer.backend->present(win32_renderer.present_handle);
}

bool win32_renderer_can_present() {
	return win32_renderer.initialized && win32_renderer.present_handle;
}
