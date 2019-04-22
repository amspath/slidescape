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

input_t inputs[2];
input_t *old_input;
input_t *curr_input;

static GLuint global_blit_texture_handle;

openslide_api openslide;

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
		//TODO: diagnostic
		printf("Could not load libopenslide-0.dll\n");
		return false;
	}

}

void win32_diagnostic(const char* prefix) {
	DWORD error_id = GetLastError();
	char* message_buffer;
	/*size_t size = */FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                                 NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
	printf("%s: %s\n", prefix, message_buffer);
	LocalFree(message_buffer);
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

		case WM_SIZE: {
			if (is_main_window_initialized) {
				win32_window_dimension_t dimension = win32_get_window_dimension(main_window);
				win32_resize_DIB_section(&backbuffer, dimension.width, dimension.height);
			}
		} break;

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

		case WM_PAINT: {
			PAINTSTRUCT paint;
			HDC device_context = BeginPaint(window, &paint);
			win32_window_dimension_t dimension = win32_get_window_dimension(window);
			viewer_update_and_render(NULL, dimension.width, dimension.height);
			SwapBuffers(device_context);
			EndPaint(window, &paint);

		} break;

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

// https://stackoverflow.com/questions/589064/how-to-enable-vertical-sync-in-opengl/589232#589232
bool win32_wgl_extension_supported(const char *extension_name)
{
	// this is pointer to function which returns pointer to string with list of all wgl extensions
	PFNWGLGETEXTENSIONSSTRINGEXTPROC _wglGetExtensionsStringEXT = NULL;

	// determine pointer to wglGetExtensionsStringEXT function
	_wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC) wglGetProcAddress("wglGetExtensionsStringEXT");

	if (strstr(_wglGetExtensionsStringEXT(), extension_name) == NULL)
	{
		// string was not found
		return false;
	}

	// extension is supported
	return true;
}

PFNWGLSWAPINTERVALEXTPROC       wglSwapIntervalEXT = NULL;
PFNWGLGETSWAPINTERVALEXTPROC    wglGetSwapIntervalEXT = NULL;
int swap_interval;

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

void win32_init_opengl(HWND window) {
	i64 debug_start = get_clock();
	HDC window_dc = GetDC(window);

	PIXELFORMATDESCRIPTOR desired_pixel_format = {
			.nSize = sizeof(desired_pixel_format),
			.nVersion = 1,
			.iPixelType = PFD_TYPE_RGBA,
			.dwFlags = PFD_SUPPORT_OPENGL|PFD_DRAW_TO_WINDOW|PFD_DOUBLEBUFFER,
			.cColorBits = 32,
			.cAlphaBits = 8,
			.iLayerType = PFD_MAIN_PLANE,
	};

	int suggested_pixel_format_index = ChoosePixelFormat(window_dc, &desired_pixel_format);
	PIXELFORMATDESCRIPTOR suggested_pixel_format;
	DescribePixelFormat(window_dc, suggested_pixel_format_index, sizeof(suggested_pixel_format), &suggested_pixel_format);
	SetPixelFormat(window_dc, suggested_pixel_format_index, &suggested_pixel_format);

	HGLRC opengl_rc = wglCreateContext(window_dc);
	if (wglMakeCurrent(window_dc, opengl_rc)) {
		// Success
		if (win32_wgl_extension_supported("WGL_EXT_swap_control"))
		{
			// Extension is supported, init pointers.
			wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC) wglGetProcAddress("wglSwapIntervalEXT");

			// this is another function from WGL_EXT_swap_control extension
			wglGetSwapIntervalEXT = (PFNWGLGETSWAPINTERVALEXTPROC) wglGetProcAddress("wglGetSwapIntervalEXT");
		}
	} else {
		panic();
		// TODO: diagnostic
	}

	opengl32_dll_handle = LoadLibraryA("opengl32.dll");
	if (!opengl32_dll_handle) {
		printf("Error initializing OpenGL: failed to load opengl32.dll.\n");
	}

	if (!gladLoadGLLoader((GLADloadproc) gl_get_proc_address)) {
		printf("Error initializing OpenGL: failed to initialize GLAD.\n");
		panic();
	}
	// debug
	printf("Initialized OpenGL in %g seconds.\n", get_seconds_elapsed(debug_start, get_clock()));


	glGenTextures(1, &global_blit_texture_handle);

	ReleaseDC(window, window_dc);
}


void win32_init_main_window() {
	main_window_class = (WNDCLASSA){
			.style = CS_HREDRAW|CS_VREDRAW,
			.lpfnWndProc = main_window_callback,
			.hInstance = g_instance,
			.hCursor = the_cursor,
//			.hIcon = ,
			.lpszClassName = "SlideviewerMainWindow",
	};

	if (!RegisterClassA(&main_window_class)) {
		panic();
		// TODO: Diagnostic
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
		panic();
		// TODO: logging
	}

	win32_init_opengl(main_window);
	win32_gl_swap_interval(1);

	win32_resize_DIB_section(&backbuffer, desired_width, desired_height);
	is_main_window_initialized = true; // prevent WM_SIZE messages calling win32_resize_DIB_section() too early!

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
		memset_zero(&curr_input->mouse_buttons);
		for (int i = 0; i < COUNT(curr_input->mouse_buttons); ++i) {
			curr_input->mouse_buttons[i].down = old_input->mouse_buttons[i].down;
		}
	}

	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	ScreenToClient(window, &cursor_pos);
	curr_input->mouse_xy = (v2i){ cursor_pos.x, cursor_pos.y };
	curr_input->mouse_z = 0; // TODO: support mousewheel

	win32_process_keyboard_event(&curr_input->mouse_buttons[0], GetKeyState(VK_LBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[1], GetKeyState(VK_MBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[2], GetKeyState(VK_RBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[3], GetKeyState(VK_XBUTTON1) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[4], GetKeyState(VK_XBUTTON2) & (1<<15));



	win32_process_pending_messages(curr_input, window);
	win32_process_xinput_controllers();

}

typedef struct work_queue_entry_t {
	void* data;
	bool32 is_valid;
} work_queue_entry_t;

typedef struct work_queue_t {
	HANDLE semaphore_handle;
	i32 volatile entry_count;
	i32 volatile next_entry_to_do;
	i32 volatile entry_completion_count;
	work_queue_entry_t entries[256];
} work_queue_t;


typedef struct win32_thread_info_t {
	i32 logical_thread_index;
	work_queue_t* queue;
} win32_thread_info_t;

work_queue_t work_queue;
win32_thread_info_t infos[3] = {};
i32 num_threads = COUNT(infos);

#define write_barrier do { _WriteBarrier(); _mm_sfence(); } while (0)
#define read_barrier _ReadBarrier()

#define interlocked_increment(x) InterlockedIncrement((volatile long*)x)
#define interlocked_compare_exchange(x) InterlockedCompareExchange((volatile long*)x, )


void add_work_queue_entry(work_queue_t* queue, void* ptr) {
	ASSERT(queue->entry_count < COUNT(queue->entries));
	queue->entries[queue->entry_count].data = ptr;

	write_barrier;
	++queue->entry_count;
	ReleaseSemaphore(queue->semaphore_handle, 1, NULL);
}

work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue) {
	work_queue_entry_t result = {};

	i32 original_entry_to_do = queue->next_entry_to_do;
	if (queue->next_entry_to_do < queue->entry_count) {
		i32 entry_index = InterlockedCompareExchange(
				(volatile long*) &queue->next_entry_to_do,original_entry_to_do + 1, original_entry_to_do);
		if (entry_index == original_entry_to_do) {
			result.data = queue->entries[entry_index].data;
			result.is_valid = true;
			read_barrier;
		}
	}
	return result;
}

void mark_queue_entry_completed(work_queue_t* queue) {
	interlocked_increment(&queue->entry_completion_count);
}

bool32 do_worker_work(work_queue_t* queue, int logical_thread_index) {
	work_queue_entry_t entry = get_next_work_queue_entry(queue);
	if (entry.is_valid) {
		printf("thread %d: %s\n", logical_thread_index, (char*) entry.data);
		mark_queue_entry_completed(queue);
	}
	return entry.is_valid;
}


bool32 is_queue_work_in_progress(work_queue_t* queue) {
	bool32 result = (queue->entry_count == queue->entry_completion_count);
	return result;
}



DWORD WINAPI _Noreturn thread_proc(void* parameter) {
	win32_thread_info_t* thread_info = parameter;
	for (;;) {
		if (!is_queue_work_in_progress(thread_info->queue)) {
			WaitForSingleObjectEx(thread_info->queue->semaphore_handle, INFINITE, FALSE);
		}
	}
}



void win32_init_multithreading() {

	i32 semaphore_initial_count = 0;

	work_queue.semaphore_handle = CreateSemaphoreExA(0, semaphore_initial_count, num_threads, 0, 0, SEMAPHORE_ALL_ACCESS);

	for (i32 i = 0; i < num_threads; ++i) {
		infos[i] = (win32_thread_info_t){ .logical_thread_index = i, .queue = &work_queue};

		DWORD thread_id;
		HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, infos + i, 0, &thread_id);
		CloseHandle(thread_handle);

	}


	add_work_queue_entry(&work_queue, "NULL entry");
	add_work_queue_entry(&work_queue, "string 0");
	add_work_queue_entry(&work_queue, "string 1");
	add_work_queue_entry(&work_queue, "string 2");
	add_work_queue_entry(&work_queue, "string 3");
	add_work_queue_entry(&work_queue, "string 4");
	add_work_queue_entry(&work_queue, "string 5");
	add_work_queue_entry(&work_queue, "string 6");
	add_work_queue_entry(&work_queue, "string 7");
	add_work_queue_entry(&work_queue, "string 8");
	add_work_queue_entry(&work_queue, "string 9");
	add_work_queue_entry(&work_queue, "string 10");
	add_work_queue_entry(&work_queue, "string 11");

	while (is_queue_work_in_progress(&work_queue)) {
		do_worker_work(&work_queue, num_threads);
	}


}

int main(int argc, char** argv) {
	g_instance = GetModuleHandle(NULL);
	g_cmdline = GetCommandLine();
	g_argc = argc;
	g_argv = argv;

	win32_init_multithreading();
	win32_init_timer();
	win32_init_cursor();
	win32_init_main_window();
	win32_init_input();
	win32_init_openslide();


	is_program_running = true;
	first();
	while (is_program_running) {

		win32_process_input(main_window);

		win32_window_dimension_t dimension = win32_get_window_dimension(main_window);
		viewer_update_and_render(curr_input, dimension.width, dimension.height);

		HDC hdc = GetDC(main_window);
		SwapBuffers(hdc);
		ReleaseDC(main_window, hdc);


	}

	return 0;
}
