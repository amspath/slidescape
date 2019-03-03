#define _WIN32_WINNT 0x0600
#define WINVER 0x0600

#include "common.h"
#include "viewer.h"

#include <stdio.h>

//#define WIN32_LEAN_AND_MEAN
//#define VC_EXTRALEAN
#include <windows.h>
#include <xinput.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#include "platform.h"

#include "win32_main.h"

#include "openslide_api.h"

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

viewer_t viewer;
input_t inputs[2];
input_t *old_input;
input_t *curr_input;

static GLuint global_blit_texture_handle;

openslide_api openslide;

bool32 win32_init_openslide() {
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

void win32_display_buffer(surface_t* buffer, HDC device_context, int client_width, int client_height) {

#if 0
	if (client_width == buffer->width * 2 && client_height == buffer->height * 2) {
		StretchDIBits(device_context,
		              0, 0, client_width, client_height,
		              0, 0, buffer->width, buffer->height,
		              buffer->memory,
		              &buffer->win32.bitmapinfo,
		              DIB_RGB_COLORS,
		              SRCCOPY);
	} else {
		int offset_x = 0;
		int offset_y = 0;

		PatBlt(device_context, 0, 0, client_width, offset_y, BLACKNESS);
		PatBlt(device_context, 0, buffer->height + offset_y, client_width, client_height, BLACKNESS);
		PatBlt(device_context, 0, 0, offset_x, client_height, BLACKNESS);
		PatBlt(device_context, buffer->width + offset_x, 0, client_width, client_height, BLACKNESS);

		StretchDIBits(device_context,
		              offset_x, offset_y, buffer->width, buffer->height,
		              0, 0, buffer->width, buffer->height,
		              buffer->memory,
		              &buffer->win32.bitmapinfo,
		              DIB_RGB_COLORS,
		              SRCCOPY);
	}
#endif
	glViewport(0, 0, client_width, client_height);
	 
	glBindTexture(GL_TEXTURE_2D, global_blit_texture_handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, buffer->width, buffer->height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer->memory);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glEnable(GL_TEXTURE_2D);

	glClearColor(1.0f, 0.0f, 1.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	glBegin(GL_TRIANGLES);
	float p = 1.0f;

	// lower triangle
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(-p,-p);

	glTexCoord2f(1.0f, 0.0f);
	glVertex2f(p,-p);

	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(p,p);

	// upper triange
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(-p,-p);

	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(p,p);

	glTexCoord2f(0.0f, 1.0f);
	glVertex2f(-p, p);

	glEnd();

	SwapBuffers(device_context);
}

typedef struct mouse_state_t {

} mouse_state_t;

LRESULT CALLBACK main_window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
	LRESULT result = 0;

	switch(message) {

		case WM_CREATE: {
			DragAcceptFiles(window, true);
		} break;
		case WM_DROPFILES: {
			HDROP hdrop = (HDROP) wparam;
			char buffer[2048];
			if (DragQueryFile(hdrop, 0, buffer, sizeof(buffer))) {
				on_file_dragged(buffer);
			}
			DragFinish(hdrop);

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
			win32_display_buffer(&backbuffer, device_context, dimension.width, dimension.height);
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
		}
	} else {
		SetWindowLong(window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(window, &window_position);
		SetWindowPos(window, 0, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
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

void win32_process_pending_messages(input_t* input) {
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
#if HANDMADE_INTERNAL
					case 'P': {
											if (is_down) global_pause = !global_pause;
										} break;
										case VK_BACK: {
											win32_process_keyboard_event(&keyboard_input->start, is_down);
										} break;
#endif

					case VK_SPACE: {
						win32_process_keyboard_event(&keyboard_input->button_a, is_down);
					}
						break;

					case VK_ESCAPE: {
						is_program_running = false;
					}
						break;

					case VK_F4: {
						if (is_down && alt_down) {
							is_program_running = false;
						}
					}
						break;

					case VK_RETURN: {
						if (is_down && !was_down && alt_down && message.hwnd) {
							toggle_fullscreen(message.hwnd);
						}

					}
						break;

/*					case 'L': {
						if (!is_down) break;
						if (state->input_playing_index > 0) {
							win32_end_replay_input(state);
						} else if (state->input_recording_index == 0) {
							win32_begin_recording_input(state, 1);
						} else {
							win32_end_recording_input(state);
							win32_begin_replay_input(state, 1);
						}
					}
						break;*/
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

void win32_init_opengl(HWND window) {
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

	if (!RegisterClass(&main_window_class)) {
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



void win32_process_input() {
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
	ScreenToClient(main_window, &cursor_pos);
	curr_input->mouse_xy = (v2i){ cursor_pos.x, cursor_pos.y };
	curr_input->mouse_z = 0; // TODO: support mousewheel

	win32_process_keyboard_event(&curr_input->mouse_buttons[0], GetKeyState(VK_LBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[1], GetKeyState(VK_MBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[2], GetKeyState(VK_RBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[3], GetKeyState(VK_XBUTTON1) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[4], GetKeyState(VK_XBUTTON2) & (1<<15));



	win32_process_pending_messages(curr_input);
	win32_process_xinput_controllers();

}



void first() {
//	void simulation_main();
//	simulation_main();
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
	first();

	win32_init_multithreading();
	win32_init_timer();
	win32_init_cursor();
	win32_init_main_window();
	win32_init_input();
	win32_init_openslide();

	is_program_running = true;
	while (is_program_running) {

		win32_process_input();

		viewer_update_and_render(&backbuffer, &viewer, curr_input);

		// NOTE: Display the frame (AFTER the clock)!
		win32_window_dimension_t dimension = win32_get_window_dimension(main_window);
		HDC hdc = GetDC(main_window);
		win32_display_buffer(&backbuffer, hdc, dimension.width, dimension.height);
		ReleaseDC(main_window, hdc);


	}

	return 0;
}
