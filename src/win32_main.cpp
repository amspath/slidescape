/*
  Slideviewer, a whole-slide image viewer for digital pathology.
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

#define _WIN32_WINNT 0x0600
#define WINVER 0x0600

#include "common.h"

#define WIN32_MAIN_IMPL
#include "win32_main.h"
#include "win32_gui.h"

//#define OPENSLIDE_API_IMPL
#include "openslide_api.h"


#include "viewer.h"

//#define WIN32_LEAN_AND_MEAN
//#define VC_EXTRALEAN
#include <windows.h>
#include <xinput.h>

#include <glad/glad.h>
#include <GL/wgl.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "keycode.h"
#include "keytable.h"

#include "platform.h"
#include "stringutils.h"

#include "intrinsics.h"

#include "gui.h"
#include "remote.h"
#include "caselist.h"


// For some reason, using the Intel integrated graphics is much faster to start up (?)
// Therefore, disabled this again... also better for power consumption, probably
#if PREFER_DEDICATED_GRAPHICS
// If both dedicated GPU and integrated graphics are available -> choose dedicated
// see:
// https://stackoverflow.com/questions/6036292/select-a-graphic-device-in-windows-opengl
// https://stackoverflow.com/questions/17458803/amd-equivalent-to-nvoptimusenablement
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

HINSTANCE g_instance;
HINSTANCE g_prev_instance;
LPSTR g_cmdline;
int g_cmdshow;

i64 performance_counter_frequency;
bool32 is_sleep_granular;

bool32 show_cursor;
HCURSOR the_cursor;
WINDOWPLACEMENT window_position = { sizeof(window_position) };
surface_t backbuffer;

WNDCLASSA main_window_class;

platform_thread_info_t thread_infos[MAX_THREAD_COUNT];
HGLRC glrcs[MAX_THREAD_COUNT];


void win32_diagnostic(const char* prefix) {
    DWORD error_id = GetLastError();
    char* message_buffer;
    /*size_t size = */FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
	console_print("%s: (error code 0x%x) %s\n", prefix, (u32)error_id, message_buffer);
    LocalFree(message_buffer);
}

HANDLE win32_open_overlapped_file_handle(const char* filename) {
	HANDLE handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	                            FILE_ATTRIBUTE_NORMAL | /*FILE_FLAG_SEQUENTIAL_SCAN |*/
	                            /*FILE_FLAG_NO_BUFFERING |*/ FILE_FLAG_OVERLAPPED,
	                            NULL);
	return handle;
}

void win32_overlapped_read(thread_memory_t* thread_memory, HANDLE file_handle, void* dest, u32 read_size, i64 offset) {
	// To submit an async I/O request on Win32, we need to fill in an OVERLAPPED structure with the
	// offset in the file where we want to do the read operation
	LARGE_INTEGER offset_ = {.QuadPart = (i64)offset};
	OVERLAPPED overlapped = {};
	overlapped = (OVERLAPPED) {};
	overlapped.Offset = offset_.LowPart;
	overlapped.OffsetHigh = (DWORD)offset_.HighPart;
	overlapped.hEvent = thread_memory->async_io_events[0];
	ResetEvent(thread_memory->async_io_events[0]); // reset the event to unsignaled state

	if (!ReadFile(file_handle, dest, read_size, NULL, &overlapped)) {
		DWORD error = GetLastError();
		if (error != ERROR_IO_PENDING) {
			win32_diagnostic("ReadFile");
		}
	}

	// Wait for the result of the I/O operation (blocking, because we specify bWait=TRUE)
	DWORD bytes_read = 0;
	if (!GetOverlappedResult(file_handle, &overlapped, &bytes_read, TRUE)) {
		win32_diagnostic("GetOverlappedResult");
	}
	// This should not be strictly necessary, but do it just in case GetOverlappedResult exits early (paranoia)
	if(WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0) {
		win32_diagnostic("WaitForSingleObject");
	}
}

// TODO: Queue I/O to run, then query later?
i32 win32_add_async_io_read(thread_memory_t* thread_memory, HANDLE file_handle, void* dest, u32 read_size, i64 offset) {
	i32 io_index = thread_memory->async_io_index;
	//stub

	return io_index;
}

void load_openslide_task(int logical_thread_index, void* userdata) {
	is_openslide_available = init_openslide();
	is_openslide_loading_done = true;
}

u8* platform_alloc(size_t size) {
	u8* result = (u8*) VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!result) {
		console_print("Error: memory allocation failed!\n");
		panic();
	}
	return result;
}

// Timer-related procedures

void win32_init_timer() {
	LARGE_INTEGER perf_counter_frequency_result;
	QueryPerformanceFrequency(&perf_counter_frequency_result);
	performance_counter_frequency = perf_counter_frequency_result.QuadPart;
	// Make Sleep() more granular
	UINT desired_scheduler_granularity_ms = 1;
	is_sleep_granular = (timeBeginPeriod(desired_scheduler_granularity_ms) == TIMERR_NOERROR);
}

i64 get_clock() {
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return result.QuadPart;
}

float get_seconds_elapsed(i64 start, i64 end) {
	return (float)(end - start) / (float)performance_counter_frequency;
}

void platform_sleep(u32 ms) {
	Sleep(ms);
}

void message_box(app_state_t* app_state, const char* message) {
	MessageBoxA(app_state->main_window, message, "Slideviewer", MB_ICONERROR);
}


// Window related procecures

void set_window_title(window_handle_t window, const char* title) {
	SetWindowTextA(window, title);
}

void reset_window_title(window_handle_t window) {
	SetWindowTextA(window, "Slideviewer");
}

void win32_init_cursor() {
	the_cursor = LoadCursorA(NULL, IDC_ARROW);
	show_cursor = true;
}

// XInputGetState support
#define FUNC_X_INPUT_GET_STATE(function_name) DWORD WINAPI function_name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef FUNC_X_INPUT_GET_STATE(Func_XInputGetState);
FUNC_X_INPUT_GET_STATE(XInputGetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
Func_XInputGetState* XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

// XInputSetState support
#define FUNC_X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef FUNC_X_INPUT_SET_STATE(Func_XInputSetState);
FUNC_X_INPUT_SET_STATE(XInputSetStateStub) { return ERROR_DEVICE_NOT_CONNECTED; }
Func_XInputSetState* XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

void win32_init_xinput() {
	HMODULE x_library = LoadLibraryA("xinput1_4.dll");
	if (!x_library) {
		x_library = LoadLibraryA("xinput9_1_0.dll");
		if (!x_library) x_library = LoadLibraryA("xinput1_3.dll");
	}
	if (x_library != NULL) {
		XInputGetState_ = (Func_XInputGetState*) GetProcAddress(x_library, "XInputGetState");
		XInputSetState_ = (Func_XInputSetState*) GetProcAddress(x_library, "XInputSetState");
	} else {
		// TODO: diagnostic
	}
}

void win32_init_input() {
	RAWINPUTDEVICE Rid[1];

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = 0;//RIDEV_NOLEGACY;   // adds HID mouse and also ignores legacy mouse messages
	Rid[0].hwndTarget = 0;
	if (RegisterRawInputDevices(Rid, 1, sizeof(Rid[0])) == FALSE) {
		win32_diagnostic("Registering raw input devices failed");
		panic();
	}


	win32_init_xinput();

	// Flip-flop
	old_input = &inputs[0];
	curr_input = &inputs[1];
}

win32_window_dimension_t win32_get_window_dimension(HWND window) {
	RECT rect;
	GetClientRect(window, &rect);
	return (win32_window_dimension_t){rect.right - rect.left, rect.bottom - rect.top};
}

bool check_fullscreen(window_handle_t window) {
	LONG style = GetWindowLong(window, GWL_STYLE);
	bool32 result = !(style & WS_OVERLAPPEDWINDOW);
	return result;
}

void toggle_fullscreen(window_handle_t window) {
	LONG style = GetWindowLong(window, GWL_STYLE);
	if (style & WS_OVERLAPPEDWINDOW) {
		MONITORINFO monitor_info = { .cbSize = sizeof(monitor_info) };
		if (GetWindowPlacement(window, &window_position) &&
		    GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor_info))
		{
			SetWindowLong(window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
			// See: https://stackoverflow.com/questions/23145217/flickering-when-borderless-window-and-desktop-dimensions-are-the-same
			// Why????
			SetWindowPos(window, HWND_TOP, monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
			             monitor_info.rcMonitor.right - monitor_info.rcMonitor.left + 1,
			             monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
			             SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}
	} else {
		SetWindowLong(window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(window, &window_position);
		SetWindowPos(window, 0, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

void open_file_dialog(app_state_t* app_state, u32 filetype_hint) {
	// Adapted from https://docs.microsoft.com/en-us/windows/desktop/dlgbox/using-common-dialog-boxes#open_file
	OPENFILENAME ofn = {};       // common dialog box structure
	char filename[4096];       // buffer for file name
	filename[0] = '\0';

	console_print("Attempting to open a file\n");

	// Initialize OPENFILENAME
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = app_state->main_window;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = sizeof(filename);
	ofn.lpstrFilter = "All\0*.*\0Text\0*.TXT\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	// Display the Open dialog box.
	mouse_show();
	if (GetOpenFileNameA(&ofn)==TRUE) {
		load_generic_file(&global_app_state, filename, filetype_hint);
	}
}

bool save_file_dialog(app_state_t* app_state, char* path_buffer, i32 path_buffer_size, const char* filter_string) {
	OPENFILENAME ofn = {};       // common dialog box structure
	path_buffer[0] = '\0';
	ASSERT(path_buffer_size > 1);

	// Initialize OPENFILENAME
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = app_state->main_window;
	ofn.lpstrFile = path_buffer;
	ofn.nMaxFile = path_buffer_size-1;
	ofn.lpstrFilter = filter_string;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;

	// Display the Open dialog box.
	mouse_show();
	if (GetSaveFileNameA(&ofn)==TRUE) {
//		console_print("attempting to save as %s\n", path_buffer);
//  TODO: append file extension
		return true;
	} else {
#if DO_DEBUG
		DWORD error = CommDlgExtendedError();
		console_print("Save file failed with error code %d\n", error);
#endif
		return false;
	}
}

LRESULT CALLBACK main_window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
	LRESULT result = ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
//	return result;

	switch(message) {

		case WM_CREATE: {
			DragAcceptFiles(window, true);
		} break;
		case WM_DROPFILES: {
			HDROP hdrop = (HDROP) wparam;
			char buffer[2048];
			if (DragQueryFile(hdrop, 0, buffer, sizeof(buffer))) {
				u32 filetype_hint = load_next_image_as_overlay ? FILETYPE_HINT_OVERLAY : 0;
				load_generic_file(&global_app_state, buffer, filetype_hint);
			}
			DragFinish(hdrop);
			SetForegroundWindow(window); // set focus on the window (this does not happen automatically)
		} break;

#if 0
		case WM_SIZE: {
			if (is_main_window_initialized) {
				glDrawBuffer(GL_BACK);
				win32_window_dimension_t dimension = win32_get_window_dimension(global_main_window);
				viewer_update_and_render(curr_input, dimension.width, dimension.height);
				SwapBuffers(wglGetCurrentDC());
			}
			result = DefWindowProcA(window, message, wparam, lparam);
		} break;
#endif

		case WM_CLOSE: {
			// TODO: Handle this as a message to the user?
			is_program_running = false;
		} break;

#if 1
		case WM_SETCURSOR: {
			if (!gui_want_capture_mouse) {
				result = DefWindowProcA(window, message, wparam, lparam);
			}
			/*if (!result) { // only process this message if ImGui hasn't changed the cursor already
				if (show_cursor) {
//				    SetCursor(the_cursor);
					result = DefWindowProcA(window, message, wparam, lparam);
				} else {
					SetCursor(NULL); // hide the cursor when dragging
				}
			}*/

		} break;
#endif

		case WM_DESTROY: {
			// TODO: Handle this as an error - recreate window?
			is_program_running = false;
		} break;

		case WM_INPUT: {
			result = DefWindowProcA(window, message, wparam, lparam); // apparently this is needed for cleanup?
		}; break;

		case WM_CHAR:
		case WM_DEADCHAR:
		case WM_SYSCHAR:
		case WM_SYSDEADCHAR:
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP: {
//			if (gui_want_capture_keyboard) {
//				break;
//			}
//			ASSERT(!"Keyboard messages should not be dispatched!");
		} break;

#if 0
		case WM_PAINT: {
			if (is_main_window_initialized) {
				glDrawBuffer(GL_BACK);
				win32_window_dimension_t dimension = win32_get_window_dimension(global_main_window);
				viewer_update_and_render(curr_input, dimension.width, dimension.height);
				SwapBuffers(wglGetCurrentDC());
			}
			result = DefWindowProcA(window, message, wparam, lparam);
		} break;
#endif

		default: {
			result = DefWindowProcA(window, message, wparam, lparam);
		} break;

	}
	return result;
}

void win32_process_xinput_button(button_state_t* old_state, WORD xinput_state, DWORD button_bit, button_state_t* new_state) {
	new_state->down = (xinput_state & button_bit) == button_bit;
	new_state->transition_count = (old_state->down != new_state->down) ? 1 : 0;
}

void win32_process_keyboard_event(button_state_t* new_state, bool32 down_boolean) {
	down_boolean = (down_boolean != 0);
	if (new_state->down != down_boolean) {
		new_state->down = (bool8)down_boolean;
		++new_state->transition_count;
	}
}

POINT stored_mouse_pos;

void mouse_hide() {
	if (!cursor_hidden && !gui_want_capture_mouse) {
		GetCursorPos(&stored_mouse_pos);
		ShowCursor(0);
		cursor_hidden = true;
	}
}

void mouse_show() {
	if (cursor_hidden) {
		// TODO: mouse move asymptotically to the window corners?
//		SetCursorPos(stored_mouse_pos.x, stored_mouse_pos.y);
		ShowCursor(1);
		cursor_hidden = false;
	}
}

// returns true if there was an idle period, false otherwise.
bool win32_process_pending_messages(input_t* input, HWND window, bool allow_idling) {
	i64 begin = get_clock(); // profiling

	controller_input_t* keyboard_input = &input->keyboard;
	MSG message;
	i32 messages_processed = 0;

	// Just to be sure we don't get stalls, don't allow idling while we are actively using the window
	// TODO: revise, might be unnecessary power usage
	HWND foreground_window = GetForegroundWindow();
	if (foreground_window == window) {
		allow_idling = false; //
	}

	bool did_idle = false;
	WINBOOL has_message = PeekMessageA(&message, NULL, 0, 0, PM_REMOVE);
	if (!has_message) {
		if (!allow_idling) {
			return false; // don't idle waiting for messages if there e.g. animations on screen
		} else {
			did_idle = true;
			WINBOOL ret = GetMessageA(&message, NULL, 0, 0); // blocks until there is a message
			if (ret == -1) {
				win32_diagnostic("GetMessageA");
				panic();
			}
		}
	}

	do {
		++messages_processed;
		i64 last_message_clock = get_clock();

		if (message.message == WM_QUIT) {
			is_program_running = false;
		}

		switch (message.message) {
			default: {
				TranslateMessage(&message);
				DispatchMessageA(&message);
			} break;

			case WM_MOUSEWHEEL: {
				if (gui_want_capture_mouse) {
					TranslateMessage(&message);
					DispatchMessageA(&message);
				} else {
					u32 par = (u32)message.wParam;
					i32 z_delta = GET_WHEEL_DELTA_WPARAM(message.wParam);
					input->mouse_z = z_delta;
				}
			} break;

			case WM_INPUT: {
				if (gui_want_capture_mouse) {
					break;
				}
				u32 size;
				GetRawInputData((HRAWINPUT) message.lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
				RAWINPUT* raw = (RAWINPUT*) alloca(size);
				GetRawInputData((HRAWINPUT) message.lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));

				if (raw->header.dwType == RIM_TYPEMOUSE)
				{
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
						curr_input->drag_vector = (v2f){};
						curr_input->drag_start_xy = curr_input->mouse_xy;
					}

					// We want relative mouse movement
					if (!(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
						if (curr_input->mouse_buttons[0].down) {
							curr_input->drag_vector.x += (float)raw->data.mouse.lLastX;
							curr_input->drag_vector.y += (float)raw->data.mouse.lLastY;
//						    console_print("Dragging: dx=%d dy=%d\n", curr_input->delta_mouse_x, curr_input->delta_mouse_y);
						} else {
							// not dragging
							mouse_show();
						}
					}


				}

			} break;

			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP: {
				TranslateMessage(&message);
				DispatchMessageA(&message);

				//https://stackoverflow.com/questions/8737566/rolling-ones-own-keyboard-input-system-in-c-c
				u32 vk_code = (u32) message.wParam;
				u32 scancode = keycode_windows_from_lparam((u32)message.lParam);
				u32 hid_code = keycode_windows_to_hid(scancode);
				bool32 alt_down = message.lParam & (1 << 29);
				bool32 is_down = ((message.lParam & (1 << 31)) == 0);
				bool32 was_down = ((message.lParam & (1 << 30)) != 0);
				int repeat_count = message.lParam & 0xFFFF;
				i16 ctrl_state = GetKeyState(VK_CONTROL);
				bool32 ctrl_down = (ctrl_state < 0); // 'down' determined by high order bit == sign bit
				if (was_down && is_down) break; // uninteresting: repeated key

				switch(vk_code) {
					default: break;

					case VK_F4: {
						if (is_down && alt_down) {
							is_program_running = false;
						}
					} break;

					case 'O': {
						if (is_down && ctrl_down) {
							open_file_dialog(&global_app_state, 0);
						}
					} break;

					case VK_F11: {
						if (is_down && message.hwnd && !alt_down) {
							toggle_fullscreen(message.hwnd);
						}
					} break;
					case VK_RETURN: {
						if (is_down && message.hwnd && alt_down) {
							toggle_fullscreen(message.hwnd);
						}
					}
				}


				win32_process_keyboard_event(&keyboard_input->keys[hid_code], is_down);

				switch (vk_code) {
					default:
						break;
					case VK_SHIFT:
						win32_process_keyboard_event(&keyboard_input->key_shift, is_down);
						break;
					case VK_CONTROL:
						win32_process_keyboard_event(&keyboard_input->key_ctrl, is_down);
						break;
					case VK_MENU:
						win32_process_keyboard_event(&keyboard_input->key_alt, is_down);
						break;
					case VK_LWIN:
					case VK_RWIN:
						win32_process_keyboard_event(&keyboard_input->key_super, is_down);
						break;
					case VK_UP:
						win32_process_keyboard_event(&keyboard_input->action_up, is_down);
						break;
					case VK_DOWN:
						win32_process_keyboard_event(&keyboard_input->action_down, is_down);
						break;
					case VK_LEFT:
						win32_process_keyboard_event(&keyboard_input->action_left, is_down);
						break;
					case VK_RIGHT:
						win32_process_keyboard_event(&keyboard_input->action_right, is_down);
						break;

					case 'W':
						win32_process_keyboard_event(&keyboard_input->move_up, is_down);
						break;
					case 'S':
						win32_process_keyboard_event(&keyboard_input->move_down, is_down);
						break;
					case 'A':
						win32_process_keyboard_event(&keyboard_input->move_left, is_down);
						break;
					case 'D':
						win32_process_keyboard_event(&keyboard_input->move_right, is_down);
						break;

					case 'Q': {
						win32_process_keyboard_event(&keyboard_input->left_shoulder, is_down);
					}
						break;

					case 'E': {
						win32_process_keyboard_event(&keyboard_input->right_shoulder, is_down);
					}
						break;

					case VK_SPACE: {
						win32_process_keyboard_event(&keyboard_input->button_a, is_down);
					}
						break;

						/*case VK_ESCAPE: {
							is_program_running = false;
						}*/
						break;
				}


			} break;


		}

//		i64 message_process_end = get_clock();
//		float ms_elapsed = get_seconds_elapsed(last_message_clock, message_process_end) * 1000.0f;
//		if (ms_elapsed > 1.0f) {
//			console_print("Long message processing time (%g ms)! message = %x\n", ms_elapsed, message.message);
//		}

	} while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE));

//	float ms_elapsed = get_seconds_elapsed(begin, get_clock()) * 1000.0f;
//	if (ms_elapsed > 20.0f) {
//		console_print("Long input handling time! Handled %d messages.\n", messages_processed);
//	}
	return did_idle;

}

void win32_process_xinput_controllers() {
	// NOTE: performance bug in XInput! May stall if no controller is connected.
	u32 game_max_controller_count = MIN(XUSER_MAX_COUNT, COUNT(curr_input->controllers));
	// TODO: should we poll this more frequently?
	for (DWORD controller_index = 0; controller_index < game_max_controller_count; ++controller_index) {
		controller_input_t* old_controller_input = &old_input->controllers[controller_index];
		controller_input_t* new_controller_input = &curr_input->controllers[controller_index];

		XINPUT_STATE xinput_state;
		if (XInputGetState(controller_index, &xinput_state) == ERROR_SUCCESS) {
			new_controller_input->is_connected = true;
			new_controller_input->is_analog = old_controller_input->is_analog;

			// TODO: see if controller_state.dwPacketNumber increments too rapidly
			XINPUT_GAMEPAD* xinput_gamepad = &xinput_state.Gamepad;
			WORD xinput_button_state = xinput_gamepad->wButtons;

			win32_process_xinput_button(&old_controller_input->action_up, xinput_button_state, XINPUT_GAMEPAD_DPAD_UP,
			                            &new_controller_input->action_up);
			win32_process_xinput_button(&old_controller_input->action_down, xinput_button_state, XINPUT_GAMEPAD_DPAD_DOWN,
			                            &new_controller_input->action_down);
			win32_process_xinput_button(&old_controller_input->action_left, xinput_button_state, XINPUT_GAMEPAD_DPAD_LEFT,
			                            &new_controller_input->action_left);
			win32_process_xinput_button(&old_controller_input->action_right, xinput_button_state, XINPUT_GAMEPAD_DPAD_RIGHT,
			                            &new_controller_input->action_right);
			win32_process_xinput_button(&old_controller_input->left_shoulder, xinput_button_state, XINPUT_GAMEPAD_LEFT_SHOULDER,
			                            &new_controller_input->left_shoulder);
			win32_process_xinput_button(&old_controller_input->right_shoulder, xinput_button_state, XINPUT_GAMEPAD_RIGHT_SHOULDER,
			                            &new_controller_input->right_shoulder);
			win32_process_xinput_button(&old_controller_input->start, xinput_button_state, XINPUT_GAMEPAD_START,
			                            &new_controller_input->start);
			win32_process_xinput_button(&old_controller_input->back, xinput_button_state, XINPUT_GAMEPAD_BACK,
			                            &new_controller_input->back);
			win32_process_xinput_button(&old_controller_input->button_a, xinput_button_state, XINPUT_GAMEPAD_A,
			                            &new_controller_input->button_a);
			win32_process_xinput_button(&old_controller_input->button_b, xinput_button_state, XINPUT_GAMEPAD_B,
			                            &new_controller_input->button_b);
			win32_process_xinput_button(&old_controller_input->button_x, xinput_button_state, XINPUT_GAMEPAD_X,
			                            &new_controller_input->button_x);
			win32_process_xinput_button(&old_controller_input->button_y, xinput_button_state, XINPUT_GAMEPAD_Y,
			                            &new_controller_input->button_y);

			if (xinput_button_state & (XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN|
			                           XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT)
					) {
				new_controller_input->is_analog = false;
				new_controller_input->stick_end = (v2f){0};
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_UP) new_controller_input->stick_end.y += 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_DOWN) new_controller_input->stick_end.y -= 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_LEFT) new_controller_input->stick_end.x += 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_RIGHT) new_controller_input->stick_end.x -= 1.0f;
			}

			{
				i16 xinput_stick_x = xinput_gamepad->sThumbLX;
				i16 xinput_stick_y = xinput_gamepad->sThumbLY;

				v2f stick;
				if (xinput_stick_x < 0) ++xinput_stick_x;
				if (xinput_stick_y < 0) ++xinput_stick_y;
				stick.x = (float) xinput_stick_x / 32767.f;
				stick.y = (float) xinput_stick_y / 32767.f;

				float magnitude_squared = SQUARE(stick.x) + SQUARE(stick.y);
				if (magnitude_squared > SQUARE(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.f)) {
					new_controller_input->is_analog = true;
				} else {
					stick.x = 0;
					stick.y = 0;
				}

				new_controller_input->stick_start = old_controller_input->stick_start;
				new_controller_input->stick_end = stick;

				const float threshold = 0.4f;
				WORD move_up_state = (stick.y > threshold) ? 1 : 0;
				WORD move_down_state = (stick.y < -threshold) ? 1 : 0;
				WORD move_left_state = (stick.x < -threshold) ? 1 : 0;
				WORD move_right_state = (stick.x > threshold) ? 1 : 0;

				win32_process_xinput_button(&old_controller_input->move_up, move_up_state, 1,
				                            &new_controller_input->move_up);
				win32_process_xinput_button(&old_controller_input->move_down, move_down_state, 1,
				                            &new_controller_input->move_down);
				win32_process_xinput_button(&old_controller_input->move_left, move_left_state, 1,
				                            &new_controller_input->move_left);
				win32_process_xinput_button(&old_controller_input->move_right, move_right_state, 1,
				                            &new_controller_input->move_right);
			}

			if (new_controller_input->back.down) {
				is_program_running = false;
			}

#if 0
			// just to prove that it works!
			XINPUT_VIBRATION vibration;
			vibration.wLeftMotorSpeed = UINT16_MAX / 4;
			vibration.wRightMotorSpeed = UINT16_MAX / 4;
			XInputSetState(controller_index, &vibration);
#endif


		} else {
			// this controller is not available
			new_controller_input->is_connected = false;
		}
	}
}



const char* wgl_extensions_string;

PFNWGLSWAPINTERVALEXTPROC       wglSwapIntervalEXT = NULL;
PFNWGLGETSWAPINTERVALEXTPROC    wglGetSwapIntervalEXT = NULL;
PFNWGLGETEXTENSIONSSTRINGEXTPROC wglGetExtensionsStringEXT = NULL;
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = NULL;
PFNSETPIXELFORMATPROC wglSetPixelFormat = NULL;
PFNDESCRIBEPIXELFORMATPROC wglDescribePixelFormat = NULL;
PFNCHOOSEPIXELFORMATPROC wglChoosePixelFormat = NULL;
PFNWGLGETPROCADDRESSPROC wglGetProcAddress_alt = NULL; // need to rename, because these are already declared in wingdi.h
PFNWGLCREATECONTEXTPROC wglCreateContext_alt = NULL;
PFNWGLMAKECURRENTPROC wglMakeCurrent_alt = NULL;
PFNWGLDELETECONTEXTPROC wglDeleteContext_alt = NULL;
PFNWGLGETCURRENTDCPROC wglGetCurrentDC_alt = NULL;
PFNSWAPBUFFERSPROC wglSwapBuffers = NULL;

// https://stackoverflow.com/questions/589064/how-to-enable-vertical-sync-in-opengl/589232#589232
bool win32_wgl_extension_supported(const char *extension_name) {
	ASSERT(wgl_extensions_string);
	bool supported = (strstr(wgl_extensions_string, extension_name) != NULL);
	return supported;
}


void set_swap_interval(int interval) {
	if (wglSwapIntervalEXT) {
		wglSwapIntervalEXT(interval);
	}
}



HMODULE opengl32_dll_handle;

void* gl_get_proc_address(const char *name) {
	void* proc = (void*) GetProcAddress(opengl32_dll_handle, name);
	if (!proc) {
		proc = (void*) wglGetProcAddress_alt(name);
		if (!proc) {
			console_print("Error initalizing OpenGL: could not load proc '%s'.\n", name);
		}
	}
	return proc;
}

#if USE_OPENGL_DEBUG_CONTEXT

#define skip_if_already_encountered(x) { \
	static bool id_##x##_already_encountered;\
    if (id == x) { if (id_##x##_already_encountered) return; else  id_##x##_already_encountered = true; } }

void GLAPIENTRY opengl_debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                              const char* message, const void* user_param)
{
	skip_if_already_encountered(131154); // (NVIDIA) Pixel-path performance warning: Pixel transfer is synchronized with 3D rendering.

	char severity_unknown_string[32];
	const char* severity_string = NULL;
	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH: severity_string =  "HIGH"; break;
		case GL_DEBUG_SEVERITY_MEDIUM: severity_string = "MEDIUM"; break;
		case GL_DEBUG_SEVERITY_LOW: severity_string = "LOW"; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: severity_string = "NOTIFICATION"; break;
		default: {
			snprintf(severity_unknown_string, sizeof(severity_unknown_string), "0x%x", severity);
			severity_string = severity_unknown_string;
		} break;
	}

	char type_unknown_string[32];
	const char* type_string = NULL;
	switch (type) {
		case GL_DEBUG_TYPE_ERROR: type_string = "ERROR"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_string = "DEPRECATED_BEHAVIOR"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: type_string = "UNDEFINED_BEHAVIOR"; break;
		case GL_DEBUG_TYPE_PORTABILITY: type_string = "PORTABILITY"; break;
		case GL_DEBUG_TYPE_PERFORMANCE: type_string = "PERFORMANCE"; break;
		case GL_DEBUG_TYPE_OTHER: type_string = "OTHER"; break;
		case GL_DEBUG_TYPE_MARKER: type_string = "MARKER"; break;
		case GL_DEBUG_TYPE_PUSH_GROUP: type_string = "PUSH_GROUP"; break;
		case GL_DEBUG_TYPE_POP_GROUP: type_string = "POP_GROUP"; break;
		default: {
			snprintf(type_unknown_string, sizeof(type_unknown_string), "0x%x", type);
			type_string = type_unknown_string;
		} break;
	}

	console_print("GL CALLBACK: type = %s, id = %d, severity = %s,\n    MESSAGE: %s\n", type_string, id, severity_string, message);
}
#endif

void win32_init_opengl(HWND window) {
	i64 debug_start = get_clock();

	// Set environment variable needed for Mesa3D software driver support
	// See:
	// https://github.com/pal1000/mesa-dist-win (section 'OpenGL context configuration override')
	// https://gitlab.freedesktop.org/mesa/mesa/-/blob/master/src/mesa/main/version.c
	// https://stackoverflow.com/questions/4788398/changes-via-setenvironmentvariable-do-not-take-effect-in-library-that-uses-geten
	_putenv_s("MESA_GL_VERSION_OVERRIDE", "4.3FC");

	opengl32_dll_handle = LoadLibraryA("opengl32.dll");
	if (!opengl32_dll_handle) {
		win32_diagnostic("LoadLibraryA");
		console_print("Error initializing OpenGL: failed to load opengl32.dll.\n");
	}

	// Initializing OpenGL on Windows is somewhat tricky.
	// https://mariuszbartosik.com/opengl-4-x-initialization-in-windows-without-a-framework/

	// Dynamically import all of the functions we need from opengl32.dll and all the wgl functions
	wglGetProcAddress_alt = (PFNWGLGETPROCADDRESSPROC) GetProcAddress(opengl32_dll_handle, "wglGetProcAddress");
	if (!wglGetProcAddress_alt) {
		console_print("Error initalizing OpenGL: could not load proc 'wglGetProcAddress'.\n");
		panic();
	}
	wglCreateContext_alt = (PFNWGLCREATECONTEXTPROC) gl_get_proc_address("wglCreateContext");
	if (!wglCreateContext_alt) { panic(); }
	wglMakeCurrent_alt = (PFNWGLMAKECURRENTPROC) gl_get_proc_address("wglMakeCurrent");
	if (!wglMakeCurrent_alt) { panic(); }
	wglDeleteContext_alt = (PFNWGLDELETECONTEXTPROC) gl_get_proc_address("wglDeleteContext");
	if (!wglDeleteContext_alt) { panic(); }
	wglGetCurrentDC_alt = (PFNWGLGETCURRENTDCPROC) gl_get_proc_address("wglGetCurrentDC");
	if (!wglGetCurrentDC_alt) { panic(); }
	wglSetPixelFormat = (PFNSETPIXELFORMATPROC) gl_get_proc_address("wglSetPixelFormat");
	if (!wglSetPixelFormat) { panic(); }
	wglDescribePixelFormat = (PFNDESCRIBEPIXELFORMATPROC) gl_get_proc_address("wglDescribePixelFormat");
	if (!wglDescribePixelFormat) { panic(); }
	wglChoosePixelFormat = (PFNCHOOSEPIXELFORMATPROC) gl_get_proc_address("wglChoosePixelFormat");
	if (!wglChoosePixelFormat) { panic(); }
	wglSwapBuffers = (PFNSWAPBUFFERSPROC) gl_get_proc_address("wglSwapBuffers");
	if (!wglSwapBuffers) { panic(); }

	// We want to create an OpenGL context using wglCreateContextAttribsARB, instead of the regular wglCreateContext.
	// Unfortunately, that's considered an OpenGL extension. Therefore, we first need to create a "dummy" context
	// (and destroy it again) solely for the purpose of creating the actual OpenGL context that we want.
	// (Why bother? Mostly because we want to have worker threads for loading textures in the background. For this
	// to work properly, we want multiple OpenGL contexts (one per thread), and we want all the contexts
	// to be able share their resources, which requires creating them with wglCreateContextAttribsARB.)

	// Set up a 'dummy' window, because Win32 requires a device context (DC) coupled to a window for creating
	// OpenGL contexts.
	HWND dummy_window = CreateWindowExA(0, main_window_class.lpszClassName, "dummy window",
			                            0/*WS_DISABLED*/, 0, 0, 640, 480, NULL, NULL, g_instance, 0);
	HDC dummy_dc = GetDC(dummy_window);

	PIXELFORMATDESCRIPTOR desired_pixel_format = (PIXELFORMATDESCRIPTOR){};
	desired_pixel_format.nSize = sizeof(desired_pixel_format);
	desired_pixel_format.nVersion = 1;
	desired_pixel_format.iPixelType = PFD_TYPE_RGBA;
	desired_pixel_format.dwFlags = PFD_SUPPORT_OPENGL|PFD_DRAW_TO_WINDOW|PFD_DOUBLEBUFFER;
	desired_pixel_format.cColorBits = 32;
	desired_pixel_format.cAlphaBits = 8;
	desired_pixel_format.cStencilBits = 8;
	desired_pixel_format.iLayerType = PFD_MAIN_PLANE;

	int suggested_pixel_format_index = wglChoosePixelFormat(dummy_dc, &desired_pixel_format);
	PIXELFORMATDESCRIPTOR suggested_pixel_format;
	wglDescribePixelFormat(dummy_dc, suggested_pixel_format_index, sizeof(suggested_pixel_format), &suggested_pixel_format);
	if (!SetPixelFormat(dummy_dc, suggested_pixel_format_index, &suggested_pixel_format)) {
		win32_diagnostic("wglSetPixelFormat");
		panic();
	}

	// Create the OpenGL context for the main thread.
	HGLRC dummy_glrc = wglCreateContext_alt(dummy_dc);
	if (!dummy_glrc) {
		win32_diagnostic("wglCreateContext");
		panic();
	}

	if (!wglMakeCurrent_alt(dummy_dc, dummy_glrc)) {
		win32_diagnostic("wglMakeCurrent");
		panic();
	}

	// Before we go any further, try to report the supported OpenGL version from openg32.dll
	PFNGLGETSTRINGPROC temp_glGetString = (PFNGLGETSTRINGPROC) gl_get_proc_address("glGetString");
	if (temp_glGetString == NULL) {
		panic();
	}

	char* version_string = (char*)temp_glGetString(GL_VERSION);
	console_print("OpenGL supported version: %s\n", version_string);

	bool is_opengl_version_supported = false;
	i32 major_required = 3;
	i32 minor_required = 3;
	if (strlen(version_string) >= 3) {
		i32 major_version = version_string[0] - '0';
		i32 minor_version = version_string[2] - '0';
		if (major_version > major_required || (major_version == major_required && minor_version >= minor_required)) {
			is_opengl_version_supported = true;
		}
	}

	if (!is_opengl_version_supported) {
		char buf[4096];
		snprintf(buf, sizeof(buf), "Error: OpenGL version is insufficient.\n"
							        "Required: %d.%d\n\n"
		                           "Available on this system:\n%s", major_required, minor_required, version_string);
		message_box(nullptr, buf);
		panic();
	}

	if (strstr(version_string, "NVIDIA")) {
	    is_nvidia_gpu = true;
	}

	// Now try to load the extensions we will need.

#define GET_WGL_PROC(proc, type) do { proc = (type) wglGetProcAddress_alt(#proc); } while(0)

	GET_WGL_PROC(wglGetExtensionsStringEXT, PFNWGLGETEXTENSIONSSTRINGEXTPROC);
	if (!wglGetExtensionsStringEXT) {
		console_print("Error: wglGetExtensionsStringEXT is unavailable\n");
		panic();
	}
	wgl_extensions_string = wglGetExtensionsStringEXT();
//	puts(wgl_extensions_string);

	if (win32_wgl_extension_supported("WGL_EXT_swap_control")) {
		GET_WGL_PROC(wglSwapIntervalEXT, PFNWGLSWAPINTERVALEXTPROC);
		GET_WGL_PROC(wglGetSwapIntervalEXT, PFNWGLGETSWAPINTERVALEXTPROC);
	} else {
		console_print("Error: WGL_EXT_swap_control is unavailable\n");
		panic();
	}

	if (win32_wgl_extension_supported("WGL_ARB_create_context")) {
		GET_WGL_PROC(wglCreateContextAttribsARB, PFNWGLCREATECONTEXTATTRIBSARBPROC);
	} else {
		console_print("Error: WGL_ARB_create_context is unavailable\n");
		panic();
	}

	if (win32_wgl_extension_supported("WGL_ARB_pixel_format")) {
		GET_WGL_PROC(wglChoosePixelFormatARB, PFNWGLCHOOSEPIXELFORMATARBPROC);
	} else {
		console_print("Error: WGL_ARB_pixel_format is unavailable\n");
		panic();
	}

#undef GET_WGL_PROC

	// Now we're finally ready to create the real context.
	// https://mariuszbartosik.com/opengl-4-x-initialization-in-windows-without-a-framework/

	const int pixel_attribs[] = {
			WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
			WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
			WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
			WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
			WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
			WGL_COLOR_BITS_ARB, 32,
			WGL_ALPHA_BITS_ARB, 8,
			WGL_DEPTH_BITS_ARB, 24,
			WGL_STENCIL_BITS_ARB, 8,
			WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
			WGL_SAMPLES_ARB, 4,
			0
	};

	HDC dc = GetDC(window);

	u32 num_formats = 0;
	suggested_pixel_format_index = 0;
	memset_zero(&suggested_pixel_format);
	bool32 status = wglChoosePixelFormatARB(dc, pixel_attribs, NULL, 1, &suggested_pixel_format_index, &num_formats);
	if (!status || num_formats == 0) {
		console_print("wglChoosePixelFormatARB() failed.");
		panic();
	}
	wglDescribePixelFormat(dc, suggested_pixel_format_index, sizeof(suggested_pixel_format), &suggested_pixel_format);
	if (!wglSetPixelFormat(dc, suggested_pixel_format_index, &suggested_pixel_format)) {
		win32_diagnostic("wglSetPixelFormat");
	}

#if USE_OPENGL_DEBUG_CONTEXT
	int context_attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
			WGL_CONTEXT_MINOR_VERSION_ARB, 3,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB, // Ask for a debug context
			0
	};
#else
	int context_attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
			WGL_CONTEXT_MINOR_VERSION_ARB, 3,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
			0
	};
#endif

	glrcs[0] = wglCreateContextAttribsARB(dc, NULL, context_attribs);
	if (glrcs[0] == NULL) {
		console_print("wglCreateContextAttribsARB() failed.");
		panic();
	}


	// Delete the dummy context and start using the real one.
	wglMakeCurrent_alt(NULL, NULL);
	wglDeleteContext_alt(dummy_glrc);
	ReleaseDC(dummy_window, dummy_dc);
	DestroyWindow(dummy_window);
	if (!wglMakeCurrent_alt(dc, glrcs[0])) {
		win32_diagnostic("wglMakeCurrent");
		panic();
	}
	ReleaseDC(window, dc);

	// Now, get the OpenGL proc addresses using GLAD.
	if (!gladLoadGLLoader((GLADloadproc) gl_get_proc_address)) {
		console_print("Error initializing OpenGL: failed to initialize GLAD.\n");
		panic();
	}

	// Hack: Enabling synchronous debug output (on NVIDIA drivers) apparently disables OpenGL driver multithreading.
	// We badly want to disable this, because we are already heavily using threads in the program!
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);



	// Create separate OpenGL contexts for each worker thread, so that they can load textures (etc.) on the fly
#if USE_MULTIPLE_OPENGL_CONTEXTS
	ASSERT(logical_cpu_count > 0);
	for (i32 thread_index = 1; thread_index < total_thread_count; ++thread_index) {
		HGLRC glrc = wglCreateContextAttribsARB(dc, glrcs[0], context_attribs);
		if (!glrc) {
			console_print("Thread %d: wglCreateContextAttribsARB() failed.", thread_index);
			panic();
		}
/*		if (!wglShareLists(glrcs[0], glrc)) {
			console_print("Thread %d: ", thread_index);
			win32_diagnostic("wglShareLists");
			panic();
		}*/
		glrcs[thread_index] = glrc;
	}
#endif

	// Try to enable debug output on the main thread.
#if USE_OPENGL_DEBUG_CONTEXT
	i32 gl_context_flags;
	glGetIntegerv(GL_CONTEXT_FLAGS, &gl_context_flags);
	if (gl_context_flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
//		console_print("enabling debug output for thread %d...\n", 0);
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(opengl_debug_message_callback, 0);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, false);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW, 0, NULL, true);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_MEDIUM, 0, NULL, true);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_HIGH, 0, NULL, true);
	}
	glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER, 0,
	                     GL_DEBUG_SEVERITY_HIGH, -1, "OpenGL debugging enabled");
#endif

	// debug
	console_print("Initialized OpenGL in %g seconds.\n", get_seconds_elapsed(debug_start, get_clock()));

	glDrawBuffer(GL_BACK);

}


bool win32_process_input(app_state_t* app_state) {

	i64 last_section = get_clock(); // profiling

	// Swap
	input_t* temp = old_input;
	old_input = curr_input;
	curr_input = temp;

	memcpy(curr_input, old_input, sizeof(input_t));

	for (u32 i = 0; i < COUNT(curr_input->mouse_buttons); ++i) {
		curr_input->mouse_buttons[i].transition_count = 0;
	}
	curr_input->mouse_z_start = curr_input->mouse_z;


	for (u32 controller_index = 0; controller_index < COUNT(curr_input->abstract_controllers); ++controller_index) {
		controller_input_t* controller = curr_input->abstract_controllers + controller_index;
		controller->stick_start = controller->stick_end;
		// Reset transition counts.
		for (u32 i = 0; i < COUNT(controller->buttons); ++i) {
			controller->buttons[i].transition_count = 0;
		}
	}

	memset_zero(&curr_input->mouse_buttons);
	for (u32 i = 0; i < COUNT(curr_input->mouse_buttons); ++i) {
		curr_input->mouse_buttons[i].down = old_input->mouse_buttons[i].down;
	}

	curr_input->drag_start_xy = old_input->drag_start_xy;
	curr_input->drag_vector = old_input->drag_vector;


	// TODO: retrieve subpixel mouse coordinates?
	// https://stackoverflow.com/questions/11179572/why-is-it-not-possible-to-obtain-mouse-coordinates-as-floating-point-values
	// https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmousemovepointsex?redirectedfrom=MSDN
	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	ScreenToClient(app_state->main_window, &cursor_pos);
	curr_input->mouse_xy = (v2f){ (float)cursor_pos.x, (float)cursor_pos.y };
	curr_input->mouse_z = 0;

	// NOTE: should we call GetAsyncKeyState or GetKeyState?
	win32_process_keyboard_event(&curr_input->mouse_buttons[0], GetAsyncKeyState(VK_LBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[1], GetAsyncKeyState(VK_RBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[2], GetAsyncKeyState(VK_MBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[3], GetAsyncKeyState(VK_XBUTTON1) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[4], GetAsyncKeyState(VK_XBUTTON2) & (1<<15));

	last_section = profiler_end_section(last_section, "input: (1)", 5.0f);


	bool did_idle = win32_process_pending_messages(curr_input, app_state->main_window, app_state->allow_idling_next_frame);
//	last_section = profiler_end_section(last_section, "input: (2) process pending messages", 20.0f);

//	win32_process_xinput_controllers();
//	last_section = profiler_end_section(last_section, "input: (3) xinput", 5.0f);

	// Check if at least one button/key is pressed at all (if no buttons are pressed,
	// we might be allowed to idle waiting for input (skipping frames!) as long as nothing is animating on the screen).
	curr_input->are_any_buttons_down = false;
	for (u32 i = 0; i < COUNT(curr_input->keyboard.buttons); ++i) {
		curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->keyboard.buttons[i].down;
	}
	for (u32 i = 0; i < COUNT(curr_input->keyboard.keys); ++i) {
		curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->keyboard.keys[i].down;
	}
	for (u32 i = 0; i < COUNT(curr_input->mouse_buttons); ++i) {
		curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->mouse_buttons[i].down;
	}

	return did_idle;
}


DWORD WINAPI thread_proc(void* parameter) {
	platform_thread_info_t* thread_info = (platform_thread_info_t*) parameter;
	i64 init_start_time = get_clock();

	atomic_increment(&global_worker_thread_idle_count);

	init_thread_memory(thread_info->logical_thread_index);
	thread_memory_t* thread_memory = local_thread_memory;

	for (i32 i = 0; i < MAX_ASYNC_IO_EVENTS; ++i) {
		thread_memory->async_io_events[i] = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (!thread_memory->async_io_events[i]) {
			win32_diagnostic("CreateEvent");
		}
	}

	// Create a dedicated OpenGL context for this thread, to be used for on-the-fly texture loading
#if USE_MULTIPLE_OPENGL_CONTEXTS
	ASSERT(global_app_state.main_window);
	HDC dc = 0;
	for (;;) {
		dc = GetDC(global_app_state.main_window);
		if (dc) break; else {
			Sleep(1);
		}
	}
	HGLRC glrc = glrcs[thread_info->logical_thread_index];
	ASSERT(glrc);
	while (!wglMakeCurrent_alt(dc, glrc)) {
		DWORD error_id = GetLastError();
		if (error_id == 0) {
			Sleep(1000);
			continue; // for some reason, this can fail, but with error code 0, retrying seems harmless??
		} else {
			win32_diagnostic("wglMakeCurrent");
			panic();
		}
	}
	ReleaseDC(global_app_state.main_window, dc);

	// Hack: make sure the OpenGL driver doesn't spawn any more threads
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

#if USE_OPENGL_DEBUG_CONTEXT
	i32 gl_context_flags;
	glGetIntegerv(GL_CONTEXT_FLAGS, &gl_context_flags);
	if (gl_context_flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
//		console_print("enabling debug output for thread %d...\n", thread_info->logical_thread_index);
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(opengl_debug_message_callback, 0);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, false);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW, 0, NULL, true);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_MEDIUM, 0, NULL, true);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_HIGH, 0, NULL, true);
	}
#endif
#endif//USE_MULTIPLE_OPENGL_CONTEXTS

//	console_print("Thread %d reporting for duty (init took %.3f seconds)\n", thread_info->logical_thread_index, get_seconds_elapsed(init_start_time, get_clock()));

	for (;;) {
		if (!is_queue_work_in_progress(thread_info->queue)) {
			Sleep(1);
			WaitForSingleObjectEx(thread_info->queue->semaphore, 1, FALSE);
		}
		do_worker_work(thread_info->queue, thread_info->logical_thread_index);
	}
}

void init_work_queue(work_queue_t* queue) {
	i32 semaphore_initial_count = 0;
	queue->semaphore = CreateSemaphoreExA(0, semaphore_initial_count, worker_thread_count, 0, 0, SEMAPHORE_ALL_ACCESS);
}

void win32_init_multithreading() {
	init_thread_memory(0);

	i32 semaphore_initial_count = 0;
	worker_thread_count = total_thread_count - 1;

	init_work_queue(&global_work_queue); // Queue for newly submitted tasks
	init_work_queue(&global_completion_queue); // Message queue for completed tasks

	// NOTE: the main thread is considered thread 0.
	for (i32 i = 1; i < total_thread_count; ++i) {
		thread_infos[i] = (platform_thread_info_t){ .logical_thread_index = i, .queue = &global_work_queue};

		DWORD thread_id;
		HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, thread_infos + i, 0, &thread_id);
		CloseHandle(thread_handle);

	}

	test_multithreading_work_queue();

}

void win32_init_main_window(app_state_t* app_state) {
	main_window_class = (WNDCLASSA){};
	main_window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	main_window_class.lpfnWndProc = main_window_callback;
	main_window_class.hInstance = g_instance;
	main_window_class.hCursor = the_cursor;
	main_window_class.hIcon = LoadIconA(g_instance, MAKEINTRESOURCE(101));
	main_window_class.lpszClassName = "SlideviewerMainWindow";
	main_window_class.hbrBackground = NULL;

	if (!RegisterClassA(&main_window_class)) {
		win32_diagnostic("RegisterClassA");
		panic();
	};

	RECT desired_window_rect = {};
	desired_window_rect.right = desired_window_width;
	desired_window_rect.bottom = desired_window_height;
	DWORD window_style = WS_OVERLAPPEDWINDOW|WS_EX_ACCEPTFILES;
	if (window_start_maximized) {
		window_style |= WS_MAXIMIZE;
	}
	AdjustWindowRect(&desired_window_rect, window_style, 0);
	int initial_window_width = desired_window_rect.right - desired_window_rect.left;
	int initial_window_height = desired_window_rect.bottom - desired_window_rect.top;

	app_state->main_window = CreateWindowExA(0,//WS_EX_TOPMOST|WS_EX_LAYERED,
	                              main_window_class.lpszClassName, "Slideviewer",
	                                     window_style,
	                                     0/*CW_USEDEFAULT*/, 0/*CW_USEDEFAULT*/, initial_window_width, initial_window_height,
	                                     0, 0, g_instance, 0);
	if (!app_state->main_window) {
		win32_diagnostic("CreateWindowExA");
		panic();
	}

	win32_init_opengl(app_state->main_window);

	ShowWindow(app_state->main_window, window_start_maximized ? SW_MAXIMIZE : SW_SHOW);

}

#define CREATE_ICO 0
#if CREATE_ICO

#pragma pack(push, 1)
typedef struct icondirentry_t {
	u8 width;
	u8 height;
	u8 num_colors; // 0 means 256
	u8 reserved;
	u16 color_planes;
	u16 bits_per_pixel;
	u32 data_size;
	u32 data_offset;
} icondirentry_t;
#pragma pack(pop)

void create_ico() {
	mem_t* icon16 = platform_read_entire_file("resources/icon/icon16.png");
	mem_t* icon24 = platform_read_entire_file("resources/icon/icon24.png");
	mem_t* icon32 = platform_read_entire_file("resources/icon/icon32.png");
	mem_t* icon64 = platform_read_entire_file("resources/icon/icon64.png");
	mem_t* icon128 = platform_read_entire_file("resources/icon/icon128.png");
	mem_t* icon256 = platform_read_entire_file("resources/icon/icon256.png");

	u32 entries_offset = 6 + sizeof(icondirentry_t) * 6;
	u32 data_offset = entries_offset;
	icondirentry_t entry16 = {16, 16, 0, 0, 0, 32, icon16->len, data_offset};
	data_offset += icon16->len;
	icondirentry_t entry24 = {24, 24, 0, 0, 0, 32, icon24->len, data_offset};
	data_offset += icon24->len;
	icondirentry_t entry32 = {32, 32, 0, 0, 0, 32, icon32->len, data_offset};
	data_offset += icon32->len;
	icondirentry_t entry64 = {64, 64, 0, 0, 0, 32, icon64->len, data_offset};
	data_offset += icon64->len;
	icondirentry_t entry128 = {128, 128, 0, 0, 0, 32, icon128->len, data_offset};
	data_offset += icon128->len;
	icondirentry_t entry256 = {0, 0, 0, 0, 0, 32, icon256->len, data_offset};
	data_offset += icon256->len;

	FILE* fp = fopen("icon.ico", "wb");
	u16 reserved = 0;
	u16 image_type = 1; // 1 for icon (ICO), 2 for cursor (CUR)
	u16 image_count = 6;
	fwrite(&reserved, sizeof(u16), 1, fp);
	fwrite(&image_type, sizeof(u16), 1, fp);
	fwrite(&image_count, sizeof(u16), 1, fp);
	fwrite(&entry16, sizeof(icondirentry_t), 1, fp);
	fwrite(&entry24, sizeof(icondirentry_t), 1, fp);
	fwrite(&entry32, sizeof(icondirentry_t), 1, fp);
	fwrite(&entry64, sizeof(icondirentry_t), 1, fp);
	fwrite(&entry128, sizeof(icondirentry_t), 1, fp);
	fwrite(&entry256, sizeof(icondirentry_t), 1, fp);
	fwrite(&icon16->data, icon16->len, 1, fp);
	fwrite(&icon24->data, icon24->len, 1, fp);
	fwrite(&icon32->data, icon32->len, 1, fp);
	fwrite(&icon64->data, icon64->len, 1, fp);
	fwrite(&icon128->data, icon128->len, 1, fp);
	fwrite(&icon256->data, icon256->len, 1, fp);

	fclose(fp);

}

#endif


int main(int argc, const char** argv) {
	g_instance = GetModuleHandle(NULL);
	g_cmdline = GetCommandLine();
	g_argc = argc;
	g_argv = argv;

#if CREATE_ICO
	create_ico();
#endif

	console_printer_benaphore = benaphore_create();
	console_print("Starting up...\n");

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	get_system_info();

	app_state_t* app_state = &global_app_state;
	init_app_state(app_state);

	viewer_init_options(app_state);

	win32_init_timer();
	win32_init_cursor();
	win32_init_main_window(app_state);
	win32_init_multithreading();
	// Load OpenSlide in the background, we might not need it immediately.
#if 1
	add_work_queue_entry(&global_work_queue, load_openslide_task, NULL, 0);
#else
    load_openslide_task(0, NULL);
#endif
	win32_init_input();
//	do_remote_connection_test();

	is_program_running = true;

	win32_init_gui(app_state);

    init_opengl_stuff(app_state);

    // Load a slide from the command line or through the OS (double-click / drag on executable, etc.)
	if (g_argc > 1) {
		const char* filename = g_argv[1];
		load_generic_file(app_state, filename, 0);
	}

	HDC glrc_hdc = wglGetCurrentDC_alt();

	is_vsync_enabled = false;
	set_swap_interval(is_vsync_enabled ? 1 : 0);

	i64 last_clock = get_clock();
	while (is_program_running) {
		i64 current_clock = get_clock();
		app_state->last_frame_start = current_clock;

		int refresh_rate = GetDeviceCaps(glrc_hdc, VREFRESH);
		if (refresh_rate <= 1) {
			refresh_rate = 60; // guess
		}
		float predicted_frame_ms = 1000.0f / (float)refresh_rate;
		if (!is_vsync_enabled) predicted_frame_ms *= 0.5f; // try to hit twice the refresh rate

		float delta_t = (float)(current_clock - last_clock) / (float)performance_counter_frequency;
		last_clock = current_clock;
		delta_t = ATMOST(2.0f / 60.0f, delta_t); // prevent physics overshoot at lag spikes

		bool did_idle = win32_process_input(app_state);
		if (did_idle) {
			last_clock = get_clock();
		}
		i64 section_end = profiler_end_section(last_clock, "input", 20.0f);

		win32_gui_new_frame();

		// Update and render our application
		win32_window_dimension_t dimension = win32_get_window_dimension(app_state->main_window);
		viewer_update_and_render(app_state, curr_input, dimension.width, dimension.height, delta_t);

		// Render the UI
		ImGui::Render();
		glViewport(0, 0, dimension.width, dimension.height);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		section_end = profiler_end_section(section_end, "viewer update and render", 20.0f);

		float frame_ms = get_seconds_elapsed(app_state->last_frame_start, get_clock()) * 1000.0f;
		float ms_left = predicted_frame_ms - frame_ms;
		float time_margin = is_vsync_enabled ? 2.0f : 0.0f;
		float sleep_time = ms_left - time_margin;
		if (sleep_time >= 1.0f) {
			// Sleep seems to cause Vsync stutter on some NVIDIA gpus. (?)
			// Also, Intel integrated graphics seems to perform worse when Sleep is used (unsure)
			if (!(is_vsync_enabled)) {
				Sleep((DWORD)sleep_time);
			}
		}

		wglSwapBuffers(glrc_hdc);
		section_end = profiler_end_section(section_end, "end frame", 100.0f);

	}

	autosave(app_state, true); // save any unsaved changes

	return 0;
}
