#define _WIN32_WINNT 0x0600
#define WINVER 0x0600

#include "common.h"
#include "openslide_api.h"
#include "viewer.h"

#include <stdio.h>
#include <sys/stat.h>

//#define WIN32_LEAN_AND_MEAN
//#define VC_EXTRALEAN
#include <windows.h>
#include <xinput.h>

#include <glad/glad.h>
#include <GL/wglext.h>


#include "platform.h"

#include "win32_main.h"
#include "intrinsics.h"

int g_argc;
char** g_argv;

HINSTANCE g_instance;
HINSTANCE g_prev_instance;
LPSTR g_cmdline;
int g_cmdshow;

i64 performance_counter_frequency;
bool32 is_sleep_granular;

bool32 is_program_running;
bool32 show_cursor;
HCURSOR the_cursor;
WINDOWPLACEMENT window_position = { sizeof(window_position) };
surface_t backbuffer;

WNDCLASSA main_window_class;
HWND main_window;
bool32 is_main_window_initialized;

work_queue_t work_queue;
win32_thread_info_t thread_infos[MAX_THREAD_COUNT];
HGLRC glrcs[MAX_THREAD_COUNT];

i32 total_thread_count;
i32 logical_cpu_count;

input_t inputs[2];
input_t *old_input;
input_t *curr_input;

// for software renderer only; remove this??
static GLuint global_blit_texture_handle;

openslide_api openslide;

void win32_diagnostic(const char* prefix) {
    DWORD error_id = GetLastError();
    char* message_buffer;
    /*size_t size = */FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
    printf("%s: (error code 0x%x) %s\n", prefix, (u32)error_id, message_buffer);
    LocalFree(message_buffer);
}


bool32 win32_init_openslide() {
	i64 debug_start = get_clock();
	SetDllDirectoryA("openslide");
	HINSTANCE dll_handle = LoadLibraryA("libopenslide-0.dll");
	if (dll_handle) {

#define GET_PROC(proc) if (!(openslide.proc = (void*) GetProcAddress(dll_handle, #proc))) goto failed;
		GET_PROC(openslide_detect_vendor);
		GET_PROC(openslide_open);
		GET_PROC(openslide_get_level_count);
		GET_PROC(openslide_get_level0_dimensions);
		GET_PROC(openslide_get_level_dimensions);
		GET_PROC(openslide_get_level_downsample);
		GET_PROC(openslide_get_best_level_for_downsample);
		GET_PROC(openslide_read_region);
		GET_PROC(openslide_close);
		GET_PROC(openslide_get_error);
		GET_PROC(openslide_get_property_names);
		GET_PROC(openslide_get_property_value);
		GET_PROC(openslide_get_associated_image_names);
		GET_PROC(openslide_get_associated_image_dimensions);
		GET_PROC(openslide_read_associated_image);
		GET_PROC(openslide_get_version);
#undef GET_PROC

		printf("Initialized OpenSlide in %g seconds.\n", get_seconds_elapsed(debug_start, get_clock()));
		return true;

	} else failed: {
		win32_diagnostic("LoadLibraryA");
		printf("Could not load libopenslide-0.dll\n");
		return false;
	}

}

bool32 is_openslide_available;
bool32 is_openslide_loading_done;

void load_openslide_task(int logical_thread_index, void* userdata) {
	is_openslide_available = win32_init_openslide();
	is_openslide_loading_done = true;
}



u8* platform_alloc(size_t size) {
	u8* result = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!result) {
		printf("Error: memory allocation failed!\n");
		panic();
	}
}

file_mem_t* platform_read_entire_file(const char* filename) {
	file_mem_t* result = NULL;
	FILE* fp = fopen(filename, "rb");
	if (fp) {
		struct stat st;
		if (fstat(fileno(fp), &st) == 0) {
			i64 filesize = st.st_size;
			if (filesize > 0) {
				size_t allocation_size = filesize + sizeof(result->len) + 1;
				result = malloc(allocation_size);
				if (result) {
					((u8*)result)[allocation_size-1] = '\0';
					result->len = filesize;
					size_t bytes_read = fread(result->data, 1, filesize, fp);
					if (bytes_read != filesize) {
						panic();
					}
				}
			}
		}
		fclose(fp);
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

void message_box(const char* message) {
	MessageBoxA(main_window, message, "Slideviewer", MB_ICONERROR);
}


// Window related procecures

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
	old_input = &inputs[0];
	curr_input = &inputs[1];
}

win32_window_dimension_t win32_get_window_dimension(HWND window) {
	RECT rect;
	GetClientRect(window, &rect);
	return (win32_window_dimension_t){rect.right - rect.left, rect.bottom - rect.top};
}

#if 0
void win32_resize_DIB_section(surface_t* buffer, int width, int height) {

	ASSERT(width >= 0);
	ASSERT(height >= 0);

	buffer->width = width;
	buffer->height = height;
	buffer->pitch = width * BYTES_PER_PIXEL;

	// NOTE: When the biHeight field is negative, this is the clue to
	// Windows to treat this bitmap as top-down, not bottom-up, meaning that
	// the first byte of the image is the top-left pixel.
	buffer->win32.bitmapinfo.bmiHeader = (BITMAPINFOHEADER){
			.biSize = sizeof(BITMAPINFOHEADER),
			.biWidth = width,
			.biHeight = -height,
			.biPlanes = 1,
			.biBitCount = 32,
			.biCompression = BI_RGB,
	};

	size_t memory_needed = (size_t) BYTES_PER_PIXEL * width * height;

	if (buffer->memory != NULL) {
		// If enough memory already allocated, just reuse. Otherwise, we need to reallocate.
		if (buffer->memory_size < memory_needed) {
			VirtualFree(buffer->memory, 0, MEM_RELEASE);
			buffer->memory = NULL;
		} else {
			memset(buffer->memory, 0, buffer->memory_size);
		}
	}

	if (buffer->memory == NULL) {
		buffer->memory = VirtualAlloc(0, memory_needed, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!buffer->memory) panic();
		buffer->memory_size = memory_needed;
	}
}
#endif

enum menu_ids {
	IDM_FILE_OPEN,
	IDM_FILE_QUIT,
	IDM_VIEW_FULLSCREEN,
	IDM_VIEW_USE_IMAGE_ADJUSTMENTS,
};
HMENU menubar;
HMENU file_menu;
HMENU view_menu;

void init_menus(HWND hwnd) {

	menubar = CreateMenu();
	file_menu = CreateMenu();
	view_menu = CreateMenu();

	AppendMenuA(file_menu, MF_STRING, IDM_FILE_OPEN, "&Open...\tCtrl+O");
	AppendMenuA(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenuA(file_menu, MF_STRING, IDM_FILE_QUIT, "&Quit\tAlt+F4");

	AppendMenuA(view_menu, MF_STRING | MF_UNCHECKED, IDM_VIEW_FULLSCREEN, "&Fullscreen\tAlt+Enter");
	AppendMenuA(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenuA(view_menu, MF_STRING | MF_UNCHECKED, IDM_VIEW_USE_IMAGE_ADJUSTMENTS, "Use image &adjustments");

	AppendMenuA(menubar, MF_POPUP, (UINT_PTR) file_menu, "&File");
	AppendMenuA(menubar, MF_POPUP, (UINT_PTR) view_menu, "&View");
	SetMenu(hwnd, menubar);
}

bool32 is_fullscreen(HWND window) {
	LONG style = GetWindowLong(window, GWL_STYLE);
	bool32 result = style & WS_OVERLAPPEDWINDOW;
	return result;
}

void toggle_fullscreen(HWND window) {
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
			CheckMenuItem(view_menu, IDM_VIEW_FULLSCREEN, MF_CHECKED);

		}
	} else {
		SetWindowLong(window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(window, &window_position);
		SetWindowPos(window, 0, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		CheckMenuItem(view_menu, IDM_VIEW_FULLSCREEN, MF_UNCHECKED);

	}
}


LRESULT CALLBACK main_window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
	LRESULT result = 0;

	switch(message) {

		case WM_CREATE: {
			DragAcceptFiles(window, true);
			init_menus(window);
		} break;
		case WM_DROPFILES: {
			HDROP hdrop = (HDROP) wparam;
			char buffer[2048];
			if (DragQueryFile(hdrop, 0, buffer, sizeof(buffer))) {
				on_file_dragged(buffer);
			}
			DragFinish(hdrop);

		} break;
		case WM_COMMAND: {
			switch(LOWORD(wparam)) {
				case IDM_FILE_OPEN: {
					// Adapted from https://docs.microsoft.com/en-us/windows/desktop/dlgbox/using-common-dialog-boxes#open_file
					OPENFILENAME ofn = {};       // common dialog box structure
					char filename[4096];       // buffer for file name
					filename[0] = '\0';

					printf("Attempting to open a file\n");

					// Initialize OPENFILENAME
					ofn.lStructSize = sizeof(ofn);
					ofn.hwndOwner = window;
					ofn.lpstrFile = filename;
					ofn.nMaxFile = sizeof(filename);
					ofn.lpstrFilter = "All\0*.*\0Text\0*.TXT\0";
					ofn.nFilterIndex = 1;
					ofn.lpstrFileTitle = NULL;
					ofn.nMaxFileTitle = 0;
					ofn.lpstrInitialDir = NULL;
					ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

					// Display the Open dialog box.
					if (GetOpenFileName(&ofn)==TRUE) {
						on_file_dragged(filename);
					}
				} break;
				case IDM_FILE_QUIT: {
					SendMessage(window, WM_CLOSE, 0, 0);
				} break;
				case IDM_VIEW_FULLSCREEN: {
					toggle_fullscreen(window);
				} break;
				case IDM_VIEW_USE_IMAGE_ADJUSTMENTS: {
					use_image_adjustments = !use_image_adjustments;
					u32 new_state = (use_image_adjustments) ? MF_CHECKED : MF_UNCHECKED;
					CheckMenuItem(view_menu, IDM_VIEW_USE_IMAGE_ADJUSTMENTS, new_state);
				} break;
			}
		} break;

#if 0
		case WM_SIZE: {
			if (is_main_window_initialized) {
				glDrawBuffer(GL_BACK);
				win32_window_dimension_t dimension = win32_get_window_dimension(main_window);
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

		case WM_SETCURSOR: {
			if (show_cursor) {
//				SetCursor(the_cursor);
				result = DefWindowProcA(window, message, wparam, lparam);
			} else {
				SetCursor(NULL);
			}
		} break;

		case WM_DESTROY: {
			// TODO: Handle this as an error - recreate window?
			is_program_running = false;
		} break;

		case WM_ACTIVATEAPP: {
#if 0
			if (wparam == TRUE) { // app is activated
				SetLayeredWindowAttributes(window, RGB(0,0,0), 255, LWA_ALPHA);
			} else {
				SetLayeredWindowAttributes(window, RGB(0,0,0), 128, LWA_ALPHA);
			}
#endif
		} break;

		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP: {
			ASSERT(!"Keyboard messages should not be dispatched!");
		} break;

		case WM_INPUT: {
			u32 size;
			GetRawInputData((HRAWINPUT) lparam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
			RAWINPUT* raw = alloca(size);
			GetRawInputData((HRAWINPUT) lparam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));

			if (raw->header.dwType == RIM_TYPEMOUSE)
			{
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
					curr_input->drag_vector = (v2i){};
					curr_input->drag_start_xy = curr_input->mouse_xy;
				}

				// We want relative mouse movement
				if (!(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
					if (curr_input->mouse_buttons[0].down) {
						curr_input->drag_vector.x += raw->data.mouse.lLastX;
						curr_input->dmouse_xy.x += raw->data.mouse.lLastX;
						curr_input->drag_vector.y += raw->data.mouse.lLastY;
						curr_input->dmouse_xy.y += raw->data.mouse.lLastY;
//						printf("Dragging: dx=%d dy=%d\n", curr_input->delta_mouse_x, curr_input->delta_mouse_y);
					}
				}


			}



		} break;

#if 0
		case WM_PAINT: {
			if (is_main_window_initialized) {
				glDrawBuffer(GL_BACK);
				win32_window_dimension_t dimension = win32_get_window_dimension(main_window);
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


bool32 cursor_hidden;
POINT stored_mouse_pos;

void mouse_hide() {
	if (!cursor_hidden) {
		GetCursorPos(&stored_mouse_pos);
		ShowCursor(0);
		cursor_hidden = true;
	}
}

void mouse_show() {
	if (cursor_hidden) {
//		SetCursorPos(stored_mouse_pos.x, stored_mouse_pos.y);
		ShowCursor(1);
		cursor_hidden = false;
	}
}

void win32_process_pending_messages(input_t* input, HWND window) {
	controller_input_t* keyboard_input = &input->keyboard;
	MSG message;
	while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
		if (message.message == WM_QUIT) {
			is_program_running = false;
		}

		switch (message.message) {
			default: {
				TranslateMessage(&message);
				DispatchMessageA(&message);
			}
				break;

			case WM_QUIT: {
				is_program_running = false;
			}
				break;

			case WM_MOUSEWHEEL: {
				u32 par = (u32)message.wParam;
				i32 z_delta = GET_WHEEL_DELTA_WPARAM(message.wParam);
				input->mouse_z = z_delta;
			} break;

			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP: {
				u32 vk_code = (u32) message.wParam;
				int repeat_count = message.lParam & 0xFFFF;
				bool32 was_down = ((message.lParam & (1 << 30)) != 0);
				bool32 is_down = ((message.lParam & (1 << 31)) == 0);
				i16 ctrl_state = GetKeyState(VK_CONTROL);
				bool32 ctrl_down = (ctrl_state < 0); // 'down' determined by high order bit == sign bit
				if (was_down && is_down) break; // uninteresting: repeated key

				bool32 alt_down = message.lParam & (1 << 29);
				win32_process_keyboard_event(&keyboard_input->keys[vk_code & 0xFF], is_down);

				switch (vk_code) {
					default:
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

					case VK_F4: {
						if (is_down && alt_down) {
							is_program_running = false;
						}
					} break;

					case 'O': {
						if (is_down && ctrl_down) {
							SendMessage(window, WM_COMMAND, IDM_FILE_OPEN, 0);
						}
					}

					case VK_RETURN: {
						if (is_down && !was_down && alt_down && message.hwnd) {
							toggle_fullscreen(message.hwnd);
						}

					}
						break;
				}
			} break;


		}
	}
}

void win32_process_xinput_controllers() {
	// NOTE: performance bug in XInput! May stall if no controller is connected.
	int game_max_controller_count = MIN(XUSER_MAX_COUNT, COUNT(curr_input->controllers));
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
				new_controller_input->x_end = 0;
				new_controller_input->y_end = 0;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_UP) new_controller_input->y_end += 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_DOWN) new_controller_input->y_end -= 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_LEFT) new_controller_input->x_end += 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_RIGHT) new_controller_input->x_end -= 1.0f;
			}

			{
				i16 xinput_stick_x = xinput_gamepad->sThumbLX;
				i16 xinput_stick_y = xinput_gamepad->sThumbLY;

				float stick_x, stick_y;
				if (xinput_stick_x < 0) ++xinput_stick_x;
				if (xinput_stick_y < 0) ++xinput_stick_y;
				stick_x = (float) xinput_stick_x / 32767.f;
				stick_y = (float) xinput_stick_y / 32767.f;

				float magnitude_squared = SQUARE(stick_x) + SQUARE(stick_y);
				if (magnitude_squared > SQUARE(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.f)) {
					new_controller_input->is_analog = true;
				} else {
					stick_x = 0;
					stick_y = 0;
				}

				new_controller_input->x_start = old_controller_input->x_start;
				new_controller_input->y_start = old_controller_input->y_start;
				new_controller_input->x_end = stick_x;
				new_controller_input->y_end = stick_y;

				const float threshold = 0.4f;
				WORD move_up_state = (stick_y > threshold) ? 1 : 0;
				WORD move_down_state = (stick_y < -threshold) ? 1 : 0;
				WORD move_left_state = (stick_x < -threshold) ? 1 : 0;
				WORD move_right_state = (stick_x > threshold) ? 1 : 0;

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


// https://stackoverflow.com/questions/589064/how-to-enable-vertical-sync-in-opengl/589232#589232
bool win32_wgl_extension_supported(const char *extension_name) {
	ASSERT(wgl_extensions_string);
	bool32 supported = (strstr(wgl_extensions_string, extension_name) != NULL);
	return supported;
}


void win32_gl_swap_interval(int interval) {
	if (wglSwapIntervalEXT) {
		wglSwapIntervalEXT(interval);
	}
}



HMODULE opengl32_dll_handle;

void* gl_get_proc_address(const char *name) {
	void* proc = GetProcAddress(opengl32_dll_handle, name);
	if (!proc) {
		proc = wglGetProcAddress(name);
		if (!proc) {
			printf("Error initalizing OpenGL: could not load proc '%s'.\n", name);
		}
	}
	return proc;
}

void GLAPIENTRY opengl_debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                              const char* message, const void* user_param)
{
	printf("GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
	       ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
	       type, severity, message );
}

void win32_init_opengl(HWND window) {
	i64 debug_start = get_clock();

	opengl32_dll_handle = LoadLibraryA("opengl32.dll");
	if (!opengl32_dll_handle) {
		win32_diagnostic("LoadLibraryA");
		printf("Error initializing OpenGL: failed to load opengl32.dll.\n");
	}

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

	PIXELFORMATDESCRIPTOR desired_pixel_format = {
			.nSize = sizeof(desired_pixel_format),
			.nVersion = 1,
			.iPixelType = PFD_TYPE_RGBA,
			.dwFlags = PFD_SUPPORT_OPENGL|PFD_DRAW_TO_WINDOW|PFD_DOUBLEBUFFER,
			.cColorBits = 32,
			.cAlphaBits = 8,
			.iLayerType = PFD_MAIN_PLANE,
	};

	int suggested_pixel_format_index = ChoosePixelFormat(dummy_dc, &desired_pixel_format);
	PIXELFORMATDESCRIPTOR suggested_pixel_format;
	DescribePixelFormat(dummy_dc, suggested_pixel_format_index, sizeof(suggested_pixel_format), &suggested_pixel_format);
	SetPixelFormat(dummy_dc, suggested_pixel_format_index, &suggested_pixel_format);

	// Create the OpenGL context for the main thread.
	HGLRC dummy_glrc = wglCreateContext(dummy_dc);

	if (!wglMakeCurrent(dummy_dc, dummy_glrc)) {
		win32_diagnostic("wglMakeCurrent");
		panic();
	}

	// Before we go any further, try to report the supported OpenGL version from openg32.dll
	PFNGLGETSTRINGPROC temp_glGetString = (PFNGLGETSTRINGPROC) gl_get_proc_address("glGetString");
	if (temp_glGetString == NULL) {
		panic();
	}
	char* version_string = (char*)temp_glGetString(GL_VERSION);
	printf("OpenGL supported version: %s\n", version_string);

	// Now try to load the extensions we will need.

#define GET_WGL_PROC(proc) do { proc = (void*) wglGetProcAddress(#proc); } while(0)

	GET_WGL_PROC(wglGetExtensionsStringEXT);
	if (!wglGetExtensionsStringEXT) {
		printf("Error: wglGetExtensionsStringEXT is unavailable\n");
		panic();
	}
	wgl_extensions_string = wglGetExtensionsStringEXT();
//	puts(wgl_extensions_string);

	if (win32_wgl_extension_supported("WGL_EXT_swap_control")) {
		GET_WGL_PROC(wglSwapIntervalEXT);
		GET_WGL_PROC(wglGetSwapIntervalEXT);
	} else {
		printf("Error: WGL_EXT_swap_control is unavailable\n");
		panic();
	}

	if (win32_wgl_extension_supported("WGL_ARB_create_context")) {
		GET_WGL_PROC(wglCreateContextAttribsARB);
	} else {
		printf("Error: WGL_ARB_create_context is unavailable\n");
		panic();
	}

	if (win32_wgl_extension_supported("WGL_ARB_pixel_format")) {
		GET_WGL_PROC(wglChoosePixelFormatARB);
	} else {
		printf("Error: WGL_ARB_pixel_format is unavailable\n");
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
	if (status == false || num_formats == 0) {
		printf("wglChoosePixelFormatARB() failed.");
		panic();
	}
	DescribePixelFormat(dc, suggested_pixel_format_index, sizeof(suggested_pixel_format), &suggested_pixel_format);
	if (!SetPixelFormat(dc, suggested_pixel_format_index, &suggested_pixel_format)) {
		win32_diagnostic("SetPixelFormat");
	}

	int context_attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
			WGL_CONTEXT_MINOR_VERSION_ARB, 5,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB, // Ask for a debug context
			0
	};

	glrcs[0] = wglCreateContextAttribsARB(dc, NULL, context_attribs);
	if (glrcs[0] == NULL) {
		printf("wglCreateContextAttribsARB() failed.");
		panic();
	}


	// Delete the dummy context and start using the real one.
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(dummy_glrc);
	ReleaseDC(dummy_window, dummy_dc);
	DestroyWindow(dummy_window);
	if (!wglMakeCurrent(dc, glrcs[0])) {
		win32_diagnostic("wglMakeCurrent");
		panic();
	}
	ReleaseDC(window, dc);

	// Now, get the OpenGL proc addresses using GLAD.
	if (!gladLoadGLLoader((GLADloadproc) gl_get_proc_address)) {
		printf("Error initializing OpenGL: failed to initialize GLAD.\n");
		panic();
	}


	// Create separate OpenGL contexts for each worker thread, so that they can load textures (etc.) on the fly
	ASSERT(logical_cpu_count > 0);
	for (i32 thread_index = 1; thread_index < total_thread_count; ++thread_index) {
		HGLRC glrc = wglCreateContextAttribsARB(dc, glrcs[0], context_attribs);
		if (!glrc) {
			printf("Thread %d: wglCreateContextAttribsARB() failed.", thread_index);
			panic();
		}
/*		if (!wglShareLists(glrcs[0], glrc)) {
			printf("Thread %d: ", thread_index);
			win32_diagnostic("wglShareLists");
			panic();
		}*/
		glrcs[thread_index] = glrc;
	}

	// Try to enable debug output on the main thread.
	i32 gl_context_flags;
	glGetIntegerv(GL_CONTEXT_FLAGS, &gl_context_flags);
	if (gl_context_flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
//		printf("enabling debug output for thread %d...\n", 0);
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(opengl_debug_message_callback, 0);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, true);
	}

	// debug
	printf("Initialized OpenGL in %g seconds.\n", get_seconds_elapsed(debug_start, get_clock()));

	// for software renderer only; remove this??
//	glGenTextures(1, &global_blit_texture_handle);


}


void win32_process_input(HWND window) {
	// Swap
	input_t* temp = old_input;
	old_input = curr_input;
	curr_input = temp;

	{
		// reset the transition counts.
		memset_zero(&curr_input->keyboard);
		for (int i = 0; i < COUNT(curr_input->keyboard.buttons); ++i) {
			curr_input->keyboard.buttons[i].down = old_input->keyboard.buttons[i].down;
		}
		for (int i = 0; i < COUNT(curr_input->keyboard.keys); ++i) {
			curr_input->keyboard.keys[i].down = old_input->keyboard.keys[i].down;
		}
		memset_zero(&curr_input->mouse_buttons);
		for (int i = 0; i < COUNT(curr_input->mouse_buttons); ++i) {
			curr_input->mouse_buttons[i].down = old_input->mouse_buttons[i].down;
		}
	}

	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	ScreenToClient(window, &cursor_pos);
	curr_input->mouse_xy = (v2i){ cursor_pos.x, cursor_pos.y };
	curr_input->mouse_z = 0;

	win32_process_keyboard_event(&curr_input->mouse_buttons[0], GetKeyState(VK_LBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[1], GetKeyState(VK_RBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[2], GetKeyState(VK_MBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[3], GetKeyState(VK_XBUTTON1) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[4], GetKeyState(VK_XBUTTON2) & (1<<15));

	win32_process_pending_messages(curr_input, window);
	win32_process_xinput_controllers();

}



void add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata) {
	// Circular FIFO buffer
	i32 new_next_entry_to_submit = (queue->next_entry_to_submit + 1) % COUNT(queue->entries);
	ASSERT(new_next_entry_to_submit != queue->next_entry_to_execute);
	queue->entries[queue->next_entry_to_submit] = (work_queue_entry_t){ .data = userdata, .callback = callback };
	++queue->completion_goal;
	write_barrier;
	queue->next_entry_to_submit = new_next_entry_to_submit;
	ReleaseSemaphore(queue->semaphore_handle, 1, NULL);
}

work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue) {
	work_queue_entry_t result = {};

	i32 original_entry_to_execute = queue->next_entry_to_execute;
	i32 new_next_entry_to_execute = (original_entry_to_execute + 1) % COUNT(queue->entries);
	if (original_entry_to_execute != queue->next_entry_to_submit) {
		i32 entry_index = interlocked_compare_exchange(&queue->next_entry_to_execute,
		                                               new_next_entry_to_execute, original_entry_to_execute);
		if (entry_index == original_entry_to_execute) {
			result.data = queue->entries[entry_index].data;
			result.callback = queue->entries[entry_index].callback;
			result.is_valid = true;
			read_barrier;
		}
	}
	return result;
}

void win32_mark_queue_entry_completed(work_queue_t* queue) {
	interlocked_increment(&queue->completion_count);
}

bool32 do_worker_work(work_queue_t* queue, int logical_thread_index) {
	work_queue_entry_t entry = get_next_work_queue_entry(queue);
	if (entry.is_valid) {
		if (!entry.callback) panic();
		entry.callback(logical_thread_index, entry.data);
		win32_mark_queue_entry_completed(queue);
	}
	return entry.is_valid;
}


bool32 is_queue_work_in_progress(work_queue_t* queue) {
	bool32 result = (queue->completion_goal < queue->completion_count);
	return result;
}



DWORD WINAPI _Noreturn thread_proc(void* parameter) {
	win32_thread_info_t* thread_info = parameter;
	i64 init_start_time = get_clock();

	// Create a dedicated OpenGL context for this thread, to be used for on-the-fly texture loading
	ASSERT(main_window);
	HDC dc = 0;
	for (;;) {
		dc = GetDC(main_window);
		if (dc) break; else {
			Sleep(1);
		}
	}
	HGLRC glrc = glrcs[thread_info->logical_thread_index];
	ASSERT(glrc);
	while (!wglMakeCurrent(dc, glrc)) {
		win32_diagnostic("wglMakeCurrent"); // for some reason, this can fail, but with error code 0, retrying seems harmless??
		Sleep(1000);
	}
	ReleaseDC(main_window, dc);

	i32 gl_context_flags;
	glGetIntegerv(GL_CONTEXT_FLAGS, &gl_context_flags);
	if (gl_context_flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
//		printf("enabling debug output for thread %d...\n", thread_info->logical_thread_index);
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(opengl_debug_message_callback, 0);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, true);
	}

//	printf("Thread %d reporting for duty (init took %.3f seconds)\n", thread_info->logical_thread_index, get_seconds_elapsed(init_start_time, get_clock()));

	for (;;) {
		if (!is_queue_work_in_progress(thread_info->queue)) {
//			Sleep(1);
			WaitForSingleObjectEx(thread_info->queue->semaphore_handle, 1, FALSE);
		}
		do_worker_work(thread_info->queue, thread_info->logical_thread_index);
	}
}

//#define TEST_THREAD_QUEUE
#ifdef TEST_THREAD_QUEUE
void echo_task(int logical_thread_index, void* userdata) {
	printf("thread %d: %s\n", logical_thread_index, (char*) userdata);
}
#endif

void win32_init_multithreading() {
	i32 semaphore_initial_count = 0;
	i32 worker_thread_count = total_thread_count - 1;
	work_queue.semaphore_handle = CreateSemaphoreExA(0, semaphore_initial_count, worker_thread_count, 0, 0, SEMAPHORE_ALL_ACCESS);

	// NOTE: the main thread is considered thread 0.
	for (i32 i = 1; i < total_thread_count; ++i) {
		thread_infos[i] = (win32_thread_info_t){ .logical_thread_index = i, .queue = &work_queue};

		DWORD thread_id;
		HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, thread_infos + i, 0, &thread_id);
		CloseHandle(thread_handle);

	}


#ifdef TEST_THREAD_QUEUE
	add_work_queue_entry(&work_queue, echo_task, "NULL entry");
	add_work_queue_entry(&work_queue, echo_task, "string 0");
	add_work_queue_entry(&work_queue, echo_task, "string 1");
	add_work_queue_entry(&work_queue, echo_task, "string 2");
	add_work_queue_entry(&work_queue, echo_task, "string 3");
	add_work_queue_entry(&work_queue, echo_task, "string 4");
	add_work_queue_entry(&work_queue, echo_task, "string 5");
	add_work_queue_entry(&work_queue, echo_task, "string 6");
	add_work_queue_entry(&work_queue, echo_task, "string 7");
	add_work_queue_entry(&work_queue, echo_task, "string 8");
	add_work_queue_entry(&work_queue, echo_task, "string 9");
	add_work_queue_entry(&work_queue, echo_task, "string 10");
	add_work_queue_entry(&work_queue, echo_task, "string 11");

	while (is_queue_work_in_progress(&work_queue)) {
		do_worker_work(&work_queue, total_thread_count);
	}
#endif


}

void win32_init_main_window() {
	main_window_class = (WNDCLASSA){
			.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
			.lpfnWndProc = main_window_callback,
			.hInstance = g_instance,
			.hCursor = the_cursor,
//			.hIcon = ,
			.lpszClassName = "SlideviewerMainWindow",
			.hbrBackground = NULL,
	};

	if (!RegisterClassA(&main_window_class)) {
		win32_diagnostic("RegisterClassA");
		panic();
	};

	int desired_width = 1260;
	int desired_height = 740;

	RECT desired_window_rect = {};
	desired_window_rect.right = desired_width;
	desired_window_rect.bottom = desired_height;
	DWORD window_style = WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_EX_ACCEPTFILES;
	AdjustWindowRect(&desired_window_rect, window_style, 0);
	int initial_window_width = desired_window_rect.right - desired_window_rect.left;
	int initial_window_height = desired_window_rect.bottom - desired_window_rect.top;


	main_window = CreateWindowExA(0,//WS_EX_TOPMOST|WS_EX_LAYERED,
	                              main_window_class.lpszClassName, "Slideviewer",
	                              window_style,
	                              CW_USEDEFAULT, CW_USEDEFAULT, initial_window_width, initial_window_height,
	                              0, 0, g_instance, 0);
	if (!main_window) {
		win32_diagnostic("CreateWindowExA");
		panic();
	}

	win32_init_opengl(main_window);
	win32_gl_swap_interval(1);

//	win32_resize_DIB_section(&backbuffer, desired_width, desired_height);
	is_main_window_initialized = true; // prevent trying to redraw while resizing too early!

}


int main(int argc, char** argv) {
	g_instance = GetModuleHandle(NULL);
	g_cmdline = GetCommandLine();
	g_argc = argc;
	g_argv = argv;

	SYSTEM_INFO sysinfo = {};
	GetSystemInfo(&sysinfo);
	logical_cpu_count = sysinfo.dwNumberOfProcessors;
	total_thread_count = MIN(logical_cpu_count, MAX_THREAD_COUNT);

	win32_init_timer();
	win32_init_cursor();
	win32_init_main_window();
	win32_init_multithreading();
	// Load OpenSlide in the background, we might not need it immediately.
#if 1
	add_work_queue_entry(&work_queue, load_openslide_task, NULL);
#else
    load_openslide_task(0, NULL);
#endif
	win32_init_input();


	is_program_running = true;

	win32_window_dimension_t dimension = win32_get_window_dimension(main_window);
	first(dimension.width, dimension.height);

	while (is_program_running) {

		win32_process_input(main_window);

		glDrawBuffer(GL_BACK);
		win32_window_dimension_t dimension = win32_get_window_dimension(main_window);
		viewer_update_and_render(curr_input, dimension.width, dimension.height);

//		HDC hdc = GetDC(main_window);
		SwapBuffers(wglGetCurrentDC());
//		ReleaseDC(main_window, hdc);


	}

	return 0;
}
