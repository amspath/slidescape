#include "common.h"
#include "platform.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"

#include <time.h>

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

void message_box(const char* message) {
//	NSRunAlertPanel(@"Title", @"This is your message.", @"OK", nil, nil);
	console_print("[message box] %s\n", message);
    console_print_error("unimplemented: message_box()\n");
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

bool cursor_hidden;
void mouse_show() {
    if (cursor_hidden) {
        cursor_hidden = false;
	    SDL_ShowCursor(1);
    }
}

void mouse_hide() {
    if (!cursor_hidden) {
        cursor_hidden = true;
        SDL_ShowCursor(0);
    }
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

extern "C"
void gui_draw_open_file_dialog(app_state_t* app_state) {
	ImVec2 max_size = ImVec2(app_state->client_viewport.w, (float)app_state->client_viewport.h);
	ImVec2 min_size = max_size;
	min_size.x *= 0.5f;
	min_size.y *= 0.5f;

	if (need_open_file_dialog) {
		IGFD::FileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", "WSI files (*.tiff *.ptif){.tiff,.ptif},.*", "");
		need_open_file_dialog = false;
		open_file_dialog_open = true;
	}

	// display
	if (IGFD::FileDialog::Instance()->Display("ChooseFileDlgKey", ImGuiWindowFlags_NoCollapse, min_size, max_size))
	{
		// action if OK
		if (IGFD::FileDialog::Instance()->IsOk() == true)
		{
			std::string file_path_name = IGFD::FileDialog::Instance()->GetFilePathName();
//			std::string filePath = igfd::ImGuiFileDialog::Instance()->GetCurrentPath();
			load_generic_file(app_state, file_path_name.c_str(), open_file_filetype_hint);
		}
		// close
		IGFD::FileDialog::Instance()->Close();
		open_file_dialog_open = false;
	}
}

bool need_save_file_dialog = false;
bool save_file_dialog_open = false;

bool save_file_dialog(app_state_t* app_state, char* path_buffer, i32 path_buffer_size, const char* filter_string) {
	if (!save_file_dialog_open) {
		need_save_file_dialog = true;
	}
	console_print_error("Not implemented: save_file_dialog\n");
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


