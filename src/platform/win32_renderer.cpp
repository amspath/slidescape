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

#include <glad/glad.h>
#include <GL/wgl.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

typedef struct win32_renderer_t {
	win32_renderer_api_t api;
	HDC glrc_hdc;
	bool initialized;
} win32_renderer_t;

static win32_renderer_t win32_renderer;

extern bool global_is_using_software_renderer;
extern PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
extern PFNWGLGETCURRENTDCPROC wglGetCurrentDC_alt;
extern PFNSWAPBUFFERSPROC wglSwapBuffers;

bool win32_init_opengl(HWND window, bool use_software_renderer);
extern "C" void init_opengl_stuff(app_state_t* app_state);

bool win32_renderer_init_window(HWND window, win32_renderer_api_t api) {
	win32_renderer.api = api;
	win32_renderer.glrc_hdc = NULL;
	win32_renderer.initialized = false;

	bool result = false;
	switch (api) {
		case WIN32_RENDERER_API_OPENGL: {
			result = win32_init_opengl(window, false);
			if (result) {
				win32_renderer.glrc_hdc = wglGetCurrentDC_alt();
				win32_renderer.initialized = (win32_renderer.glrc_hdc != NULL);
			}
		} break;
	}

	return result && win32_renderer.initialized;
}

void win32_renderer_init_viewer(app_state_t* app_state) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			init_opengl_stuff(app_state);
		} break;
	}
}

void win32_renderer_init_imgui(app_state_t* app_state) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			ImGui_ImplOpenGL3_Init(NULL, global_is_using_software_renderer ? "opengl32software.dll" : "opengl32.dll");
		} break;
	}
}

void win32_renderer_imgui_new_frame() {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			ImGui_ImplOpenGL3_NewFrame();
		} break;
	}
}

void win32_renderer_render_imgui_draw_data(ImDrawData* draw_data) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			ImGui_ImplOpenGL3_RenderDrawData(draw_data);
		} break;
	}
}

void win32_renderer_set_swap_interval(int interval) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			if (wglSwapIntervalEXT) {
				wglSwapIntervalEXT(interval);
			}
		} break;
	}
}

int win32_renderer_get_refresh_rate() {
	int refresh_rate = 0;
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			if (win32_renderer.glrc_hdc) {
				refresh_rate = GetDeviceCaps(win32_renderer.glrc_hdc, VREFRESH);
			}
		} break;
	}
	return refresh_rate;
}

void win32_renderer_set_viewport(i32 width, i32 height) {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			glViewport(0, 0, width, height);
		} break;
	}
}

void win32_renderer_present() {
	switch (win32_renderer.api) {
		case WIN32_RENDERER_API_OPENGL: {
			if (win32_renderer.glrc_hdc) {
				wglSwapBuffers(win32_renderer.glrc_hdc);
			}
		} break;
	}
}

bool win32_renderer_can_present() {
	return win32_renderer.initialized && win32_renderer.glrc_hdc;
}
