#pragma once
#include "common.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif


void win32_init_gui(HWND hwnd);
void do_gui(i32 client_width, i32 client_height);

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

extern bool is_fullscreen;
extern bool is_program_running;
extern bool show_demo_window;
extern bool show_image_adjustments_window INIT(= false);
extern bool show_display_options_window;
extern bool gui_want_capture_mouse;
extern bool gui_want_capture_keyboard;

#undef INIT
#undef extern

#ifdef __cplusplus
};
#endif
