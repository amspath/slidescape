#include "common.h"
#include "platform.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"

#include <time.h>

#include "ImGuiFileDialog.h"


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
bool open_file_dialog_open = true;

void open_file_dialog(window_handle_t* window) {
	if (!open_file_dialog_open) {
		need_open_file_dialog = true;
	}
}

void gui_draw_open_file_dialog(app_state_t* app_state) {
	if (need_open_file_dialog) {
		ImVec2 max_size = ImVec2(app_state->client_viewport.w, (float)app_state->client_viewport.h);
		ImVec2 min_size = max_size;
		min_size.x *= 0.5f;
		min_size.y *= 0.5f;

		igfd::ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", "WSI files (*.tiff *.ptif){.tiff,.ptif},.*", "");
		need_open_file_dialog = false;
		open_file_dialog_open = true;
	}

	// display
	if (igfd::ImGuiFileDialog::Instance()->FileDialog("ChooseFileDlgKey", ImGuiWindowFlags_NoCollapse, min_size, max_size))
	{
		// action if OK
		if (igfd::ImGuiFileDialog::Instance()->IsOk == true)
		{
			std::string file_path_name = igfd::ImGuiFileDialog::Instance()->GetFilePathName();
//			std::string filePath = igfd::ImGuiFileDialog::Instance()->GetCurrentPath();
			load_generic_file(app_state, file_path_name.c_str());
		}
		// close
		igfd::ImGuiFileDialog::Instance()->CloseDialog("ChooseFileDlgKey");
		open_file_dialog_open = false;
	}
}

bool save_file_dialog(window_handle_t window, char* path_buffer, i32 path_buffer_size, const char* filter_string) {
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


