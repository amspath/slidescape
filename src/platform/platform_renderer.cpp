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
#include "platform_renderer.h"
#include "platform_renderer_backend.h"
#include "renderer.h"

typedef struct platform_renderer_t {
	platform_renderer_api_t api;
	const platform_renderer_backend_t* backend;
	window_handle_t window;
	void* present_handle;
	bool initialized;
} platform_renderer_t;

static platform_renderer_t platform_renderer;

static const platform_renderer_backend_t* platform_renderer_get_backend(platform_renderer_api_t api) {
	const platform_renderer_backend_t* result = NULL;
	switch (api) {
		case PLATFORM_RENDERER_API_OPENGL: {
			result = platform_renderer_opengl_get_backend();
		} break;
	}
	return result;
}

bool platform_renderer_init_window(const platform_renderer_window_desc_t* desc, platform_renderer_api_t api) {
	platform_renderer.api = api;
	platform_renderer.backend = platform_renderer_get_backend(api);
	platform_renderer.window = NULL;
	platform_renderer.present_handle = NULL;
	platform_renderer.initialized = false;

	ASSERT(platform_renderer.backend);
	platform_renderer.initialized = platform_renderer.backend->init_window(desc, &platform_renderer.window, &platform_renderer.present_handle);

	return platform_renderer.initialized;
}

window_handle_t platform_renderer_get_window() {
	return platform_renderer.window;
}

void platform_renderer_init_viewer(app_state_t* app_state) {
	renderer_init(app_state);
}

void platform_renderer_init_imgui(app_state_t* app_state) {
	platform_renderer.backend->init_imgui(app_state, platform_renderer.window);
}

void platform_renderer_imgui_new_frame() {
	platform_renderer.backend->imgui_new_frame();
}

void platform_renderer_render_imgui_draw_data(ImDrawData* draw_data) {
	platform_renderer.backend->render_imgui_draw_data(draw_data);
}

void platform_renderer_set_swap_interval(int interval) {
	platform_renderer.backend->set_swap_interval(interval);
}

int platform_renderer_get_refresh_rate() {
	return platform_renderer.backend->get_refresh_rate(platform_renderer.window, platform_renderer.present_handle);
}

void platform_renderer_set_viewport(i32 width, i32 height) {
	renderer_set_viewport(width, height);
}

void platform_renderer_get_drawable_size(i32* out_width, i32* out_height) {
	platform_renderer.backend->get_drawable_size(platform_renderer.window, out_width, out_height);
}

void platform_renderer_present() {
	platform_renderer.backend->present(platform_renderer.window, platform_renderer.present_handle);
}

void platform_renderer_shutdown() {
	if (platform_renderer.initialized) {
		platform_renderer.backend->shutdown(platform_renderer.window, platform_renderer.present_handle);
	}
	platform_renderer.window = NULL;
	platform_renderer.present_handle = NULL;
	platform_renderer.backend = NULL;
	platform_renderer.initialized = false;
}

bool platform_renderer_can_present() {
	return platform_renderer.initialized && platform_renderer.window;
}
