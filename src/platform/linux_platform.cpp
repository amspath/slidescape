/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2022  Pieter Valkema

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
#include "platform.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl.h"

#include <time.h>
#include <pwd.h>
#include <dirent.h>

#include "ImGuiFileDialog.h"

#include "viewer.h"


SDL_Window* g_window;

i64 get_clock() {
    struct timespec t = {};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_nsec + 1e9 * t.tv_sec;
}

float get_seconds_elapsed(i64 start, i64 end) {
    i64 elapsed_nanoseconds = end - start;
    float elapsed_seconds = ((float)elapsed_nanoseconds) / 1e9f;
    return elapsed_seconds;
}

void platform_sleep(u32 ms) {
    struct timespec tim = {}, tim2 = {};
    tim.tv_sec = 0;
    tim.tv_nsec = ms * 1000000;
    nanosleep(&tim, &tim2);
}

void platform_sleep_ns(i64 ns) {
	struct timespec tim = {}, tim2 = {};
	tim.tv_sec = 0;
	tim.tv_nsec = ns;
	nanosleep(&tim, &tim2);
}

void message_box(app_state_t *message, const char *string) {
//	NSRunAlertPanel(@"Title", @"This is your message.", @"OK", nil, nil);
	console_print("[message box] %s\n", message);
    console_print_error("unimplemented: message_box()\n");
}

void set_window_title(window_handle_t window, const char* title) {
	SDL_SetWindowTitle(window, title);
}

void reset_window_title(window_handle_t window) {
	SDL_SetWindowTitle(window, APP_TITLE);
}

void set_swap_interval(int interval) {
    SDL_GL_SetSwapInterval(interval);
}

u8* platform_alloc(size_t size) {
    u8* result = (u8*) malloc(size);
    if (!result) {
        printf("Error: memory allocation failed!\n");
        panic();
    }
    return result;
}

// On Linux, hiding/showing the cursor is buggy and unpredictable.
// SDL_ShowCursor() doesn't work at all.
// SDL_SetRelativeMouseMode() MIGHT work, but might also cause buggy behavior, see:
// https://stackoverflow.com/questions/25576438/sdl-getrelativemousestate-strange-behaviour
// This seems to occur at least under Ubuntu + SDL 2.0.10
// Manjaro + SDL 2.0.16 seems to be fine.
// TODO: how to detect if SDL_SetRelativeMouseMode will work properly or not?
// Do we simply disable by default, and add an option to re-enable cursor hiding?

void mouse_show() {
    if (cursor_hidden) {
        cursor_hidden = false;
#if 0
	    SDL_SetRelativeMouseMode(SDL_FALSE);
#endif
    }
}

void mouse_hide() {
    if (!cursor_hidden) {
        cursor_hidden = true;
		// TODO: fix mouse hiding on macOS while panning
#if 0
	    SDL_SetRelativeMouseMode(SDL_TRUE);
#endif
    }
}

void update_cursor() {
	// TODO:
}

void set_cursor_default() {
	// TODO: SetCursor(global_cursor_arrow);
}

void set_cursor_crosshair() {
	// TODO: SetCursor(global_cursor_arrow);
}

bool need_open_file_dialog = false;
u32 open_file_filetype_hint;
bool open_file_dialog_open = false;

void open_file_dialog(app_state_t* app_state, u32 filetype_hint) {
	if (!open_file_dialog_open) {
		need_open_file_dialog = true;
		open_file_filetype_hint = filetype_hint;
	}
}

const char* get_default_save_directory() {
	struct passwd* pwd = getpwuid(getuid());
	if (pwd)
		return pwd->pw_dir;
	else
		return "";
};

extern "C"
void gui_draw_open_file_dialog(app_state_t* app_state) {
	ImVec2 max_size = ImVec2(app_state->client_viewport.w, (float)app_state->client_viewport.h);
	max_size.x *= app_state->display_points_per_pixel * 0.9f;
	max_size.y *= app_state->display_points_per_pixel * 0.9f;
	ImVec2 min_size = max_size;
	min_size.x *= 0.5f;
	min_size.y *= 0.5f;

	if (need_open_file_dialog) {
		const char* filters = ".*,WSI files (*.tiff *.ptif){.tiff,.ptif}";
		IGFD::FileDialog::Instance()->OpenDialog((const std::string &) "ChooseFileDlgKey",
		                                         (const std::string &) "Choose File", filters,
		                                         (const std::string &) get_active_directory(app_state),
		                                         (const std::string &) "", 1, nullptr, 0);
		need_open_file_dialog = false;
		open_file_dialog_open = true;
	}

	// display
	if (IGFD::FileDialog::Instance()->Display((const std::string &) "ChooseFileDlgKey", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
		// action if OK
		if (IGFD::FileDialog::Instance()->IsOk() == true) {
			auto selection = IGFD::FileDialog::Instance()->GetSelection();
			auto it = selection.begin();
			for (auto element : selection) {
				std::string file_path_name = element.second;
				load_generic_file(app_state, file_path_name.c_str(), open_file_filetype_hint);
				break;
			}
		}
		// close
		IGFD::FileDialog::Instance()->Close();
		open_file_dialog_open = false;
	}
}

bool need_save_file_dialog = false;

bool save_file_dialog(app_state_t* app_state, char* path_buffer, i32 path_buffer_size, const char* filter_string, const char* filename_hint) {
	if (!save_file_dialog_open) {
		need_save_file_dialog = true;
	}
//	console_print_error("Not implemented: save_file_dialog\n");
	ImVec2 max_size = ImVec2(app_state->client_viewport.w, (float)app_state->client_viewport.h);
	max_size.x *= app_state->display_points_per_pixel * 0.9f;
	max_size.y *= app_state->display_points_per_pixel * 0.9f;
	ImVec2 min_size = max_size;
	min_size.x *= 0.5f;
	min_size.y *= 0.5f;

	if (need_save_file_dialog) {
		IGFD::FileDialog::Instance()->OpenModal((const std::string &) "SaveFileDlgKey",
		                                        (const std::string &) "Save as...", "WSI files (*.tiff *.ptif){.tiff,.ptif},.*",
		                                        (const std::string &) get_active_directory(app_state),
		                                        (const std::string &) "", 1, nullptr, 0);
		need_save_file_dialog = false;
		save_file_dialog_open = true;
	}

	// display
	if (IGFD::FileDialog::Instance()->Display((const std::string &) "SaveFileDlgKey", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
		// action if OK
		if (IGFD::FileDialog::Instance()->IsOk() == true) {
			std::string file_path_name = IGFD::FileDialog::Instance()->GetFilePathName();
			const char* filename_c = file_path_name.c_str();
			strncpy(global_export_save_as_filename, filename_c, sizeof(global_export_save_as_filename)-1);
		}
		// close
		IGFD::FileDialog::Instance()->Close();
		save_file_dialog_open = false;
		return true;
	}
	return false;
}

void toggle_fullscreen(window_handle_t window) {
//    printf("Not implemented: toggle_fullscreen\n");
    bool fullscreen = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

bool check_fullscreen(window_handle_t window) {
//    printf("Not implemented: check_fullscreen\n");
    bool fullscreen = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
    return fullscreen;
}


