/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2024  Pieter Valkema

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

#include "win32_graphical_app.h"
#include "win32_gui.h"

//#define OPENSLIDE_API_IMPL
#include "openslide_api.h"
#include "dicom.h"

#include "viewer.h"

//#define WIN32_LEAN_AND_MEAN
//#define VC_EXTRALEAN
#include <windows.h>
#include <xinput.h>
#include <psapi.h> // for EnumProcesses() and GetModuleFileNameExA()
#include <shlobj.h> // for SHGetFolderPathA
#include <shlwapi.h> // for PathStripToRootA

#include <glad/glad.h>
#include <GL/wgl.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"

#include "keycode.h"
#include "keytable.h"

#include "stringutils.h"
#include "intrinsics.h"

#include "gui.h"


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


struct win32_copydata_message_t {
	i32 argc;
	bool need_open;
	char filename[512];
};

#define SV_COPYDATA_TYPE 0x511d3
static HWND global_already_running_app_hwnd;

HINSTANCE g_instance;
HINSTANCE g_prev_instance;
LPSTR g_cmdline;
int g_cmdshow;

WINDOWPLACEMENT window_position = { sizeof(window_position) };

WNDCLASSA main_window_class;

platform_thread_info_t thread_infos[MAX_THREAD_COUNT];
HGLRC glrcs[MAX_THREAD_COUNT];

static char g_exe_name[512];
static char g_root_dir[512];
static char g_appdata_path[MAX_PATH];



HKEY win32_registry_create_empty_key(const char* key) {
	HKEY hkey = NULL;
	DWORD disposition = 0;
	DWORD ret = RegCreateKeyExA(HKEY_CURRENT_USER, key, 0, NULL,
	                            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey, &disposition);
	if (ret != ERROR_SUCCESS) {
		printf("Error opening or creating new key\n");
		return NULL;
	}
	return hkey;
}

bool win32_registry_set_value(HKEY hkey, const char* key, DWORD type, const char* value, DWORD size) {
	if (RegSetValueExA(hkey, key, 0, type, (const BYTE*)value, size) != ERROR_SUCCESS) {

		win32_diagnostic("RegSetValueExA");
		return false;
	}
	return true;
}

bool win32_registry_add_to_open_with_list(const char* ext) {
	char key_string[256];
	snprintf(key_string, sizeof(key_string)-1, "Software\\Classes\\.%s\\OpenWithProgids", ext);
	key_string[sizeof(key_string)-1] = '\0';
	{
		HKEY hkey = win32_registry_create_empty_key(key_string);
		if (hkey) {
			if (RegSetValueExA(hkey, "Slidescape.Image", 0, REG_SZ, NULL, 0) != ERROR_SUCCESS) {
				win32_diagnostic("RegSetValueExA");
				RegCloseKey(hkey);
				return false;
			}
			RegCloseKey(hkey);
		} else {
			return false;
		}
	}
	return true;
}

void win32_set_file_type_associations() {

	// We want to register the application, and set file type associations, but ONLY
	// if the executable is located on a fixed drive or network location. This prevents the registry
	// settings carrying over if the executable is in an 'unstable' location (e.g., on a USB drive)

	UINT drive_type = GetDriveTypeA(g_root_dir);
//	console_print("g_root_dir = %s, drive_type = %d\n", g_root_dir, drive_type);

	if (drive_type == DRIVE_FIXED || drive_type == DRIVE_REMOTE) {
		// "C:\path\to\slidescape.exe" "%1"
		BYTE open_command[512];
		snprintf((char*)open_command, sizeof(open_command) - 1, "\"%s\" \"%%1\"", g_exe_name);
		open_command[sizeof(open_command) - 1] = '\0';
		size_t open_command_length = strlen((char*)open_command) + 1;



		// Register the application

		// HKEY_CURRENT_USER\Software\Classes
		//   Applications
		//     slidescape.exe
		//       FriendlyAppName = @"C:\path\to\slidescape.exe"-201
		//       DefaultIcon
		//         (Default) = %SystemRoot%\System32\imageres.dll,-122
		//       shell
		//         open
		//           command = "C:\path\to\slidescape.exe" "%1"
		//       SupportedTypes
		//         .isyntax
		//         (...)
		{
			HKEY key = win32_registry_create_empty_key("Software\\Classes\\Applications\\slidescape.exe");
			if (key) {
				char friendly_app_name[512];
				snprintf((char*)friendly_app_name, sizeof(friendly_app_name) - 1, "@\"%s\",-201", g_exe_name);
				friendly_app_name[sizeof(friendly_app_name) - 1] = '\0';
				if (RegSetValueExA(key, "FriendlyAppName", 0, REG_SZ, (BYTE*)friendly_app_name, strlen(friendly_app_name) + 1) != ERROR_SUCCESS) {
					win32_diagnostic("RegSetValueExA");
					RegCloseKey(key);
					return;
				}
				RegCloseKey(key);
			} else {
				return;
			}
		}
		{
			HKEY key = win32_registry_create_empty_key("Software\\Classes\\Applications\\slidescape.exe\\DefaultIcon");
			if (key) {
				static const BYTE value[] = "%SystemRoot%\\System32\\imageres.dll,-122";
				if (RegSetValueExA(key, NULL, 0, REG_SZ, value, COUNT(value)) != ERROR_SUCCESS) {
					win32_diagnostic("RegSetValueExA");
					RegCloseKey(key);
					return;
				}
				RegCloseKey(key);
			} else {
				return;
			}
		}
		{
			HKEY hkey = win32_registry_create_empty_key("Software\\Classes\\Applications\\slidescape.exe\\shell\\open\\command");
			if (hkey) {
				if (!win32_registry_set_value(hkey, NULL, REG_SZ, (char*)open_command, open_command_length)) {
					RegCloseKey(hkey);
					return;
				}
				RegCloseKey(hkey);
			} else {
				return;
			}
		}
		{
			HKEY hkey = win32_registry_create_empty_key("Software\\Classes\\Applications\\slidescape.exe\\SupportedTypes");
			if (hkey) {
				if (!win32_registry_set_value(hkey, ".isyntax", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".i2syntax", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".tif", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".tiff", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".svs", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".ndpi", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".vms", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".scn", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".mrxs", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				if (!win32_registry_set_value(hkey, ".bif", REG_SZ, NULL, 0)) goto fail_SupportedTypes;
				RegCloseKey(hkey);
			} else {
				fail_SupportedTypes:
				if (hkey) RegCloseKey(hkey);
				return;
			}
		}


		// Create the ProgID
		{
			HKEY hkey = win32_registry_create_empty_key("Software\\Classes\\Slidescape.Image");
			if (hkey) {
				static const BYTE value[] = "Slidescape";
				if (RegSetValueExA(hkey, NULL, 0, REG_SZ, value, COUNT(value)) != ERROR_SUCCESS) {
					win32_diagnostic("RegSetValueExA");
					RegCloseKey(hkey);
					return;
				}

				char friendly_type_name[512];
				snprintf((char*)friendly_type_name, sizeof(friendly_type_name)-1, "@\"%s\",-202", g_exe_name);
				friendly_type_name[sizeof(friendly_type_name)-1] = '\0';
				if (RegSetValueExA(hkey, "FriendlyTypeName", 0, REG_SZ, (BYTE*)friendly_type_name, strlen(friendly_type_name) + 1) != ERROR_SUCCESS) {
					win32_diagnostic("RegSetValueExA");
					RegCloseKey(hkey);
					return;
				}

				RegCloseKey(hkey);
			} else {
				return;
			}
		}

		{
			HKEY hkey = win32_registry_create_empty_key("Software\\Classes\\Slidescape.Image\\DefaultIcon");
			if (hkey) {
				static const BYTE value[] = "%SystemRoot%\\System32\\imageres.dll,-122";
				if (RegSetValueExA(hkey, NULL, 0, REG_SZ, value, COUNT(value)) != ERROR_SUCCESS) {
					win32_diagnostic("RegSetValueExA");
					RegCloseKey(hkey);
					return;
				}
				RegCloseKey(hkey);
			} else {
				return;
			}
		}
		{
			HKEY hkey = win32_registry_create_empty_key("Software\\Classes\\Slidescape.Image\\shell\\open\\command");
			if (hkey) {
				if (RegSetValueExA(hkey, NULL, 0, REG_SZ, open_command, open_command_length) != ERROR_SUCCESS) {
					win32_diagnostic("RegSetValueExA");
					RegCloseKey(hkey);
					return;
				}
				RegCloseKey(hkey);
			} else {
				return;
			}
		}

		// Create the filetype associations
		if (!win32_registry_add_to_open_with_list("isyntax")) return;
		if (!win32_registry_add_to_open_with_list("i2syntax")) return;
		if (!win32_registry_add_to_open_with_list("tiff")) return;
		if (!win32_registry_add_to_open_with_list("tif")) return;
		if (!win32_registry_add_to_open_with_list("ptif")) return;

		// Let the system know that file associations have been changed
		SHChangeNotify(SHCNE_ASSOCCHANGED, 0, 0, 0);
	}
}

void load_openslide_task(int logical_thread_index, void* userdata) {
	is_openslide_available = init_openslide();
	if (is_openslide_available) {
		// Add the OpenSlide formats to the "Open With..." window
		if (!win32_registry_add_to_open_with_list("svs")) return;
		if (!win32_registry_add_to_open_with_list("ndpi")) return;
		if (!win32_registry_add_to_open_with_list("vms")) return;
		if (!win32_registry_add_to_open_with_list("scn")) return;
		if (!win32_registry_add_to_open_with_list("mrxs")) return;
		if (!win32_registry_add_to_open_with_list("bif")) return;

		// Let the system know that file associations have been changed
		SHChangeNotify(SHCNE_ASSOCCHANGED, 0, 0, 0);
	}
	is_openslide_loading_done = true;
}

void load_dicom_task(int logical_thread_index, void* userdata) {
	is_dicom_available = dicom_init();
	is_dicom_loading_done = true;
}


void win32_setup_appdata() {
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, g_appdata_path))) {
		i32 appdata_path_len = strlen(g_appdata_path);
		strncpy(g_appdata_path + appdata_path_len, "\\Slidescape", sizeof(g_appdata_path) - appdata_path_len);
//		console_print("%s\n", path_buf);
		if (!file_exists(g_appdata_path)) {
			if (!CreateDirectoryA(g_appdata_path, 0)) {
				win32_diagnostic("CreateDirectoryA");
				return;
			}
		}
		global_settings_dir = g_appdata_path;
	}
}

void win32_init_cursor() {
	global_cursor_arrow = LoadCursorA(NULL, IDC_ARROW);
	global_cursor_crosshair = LoadCursorA(NULL, IDC_CROSS);
	global_current_cursor = global_cursor_arrow;
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
		fatal_error();
	}

	win32_init_xinput();

	// Flip-flop
	old_input = &inputs[0];
	curr_input = &inputs[1];
}

win32_window_dimension_t win32_get_window_dimension(HWND window) {
	RECT rect;
	GetClientRect(window, &rect);
	win32_window_dimension_t result = {rect.right - rect.left, rect.bottom - rect.top};
	return result;
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

INT CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lp, LPARAM pData) {
	// SHBrowseForFolder doesn't give an option for setting the initial selected directory, we need a callback for this
	if (uMsg==BFFM_INITIALIZED) {
		SendMessageW(hwnd, BFFM_SETEXPANDED, FALSE, pData);
	}
	return 0;
}

void open_file_dialog(app_state_t* app_state, u32 action, u32 filetype_hint) {
	// Adapted from https://docs.microsoft.com/en-us/windows/desktop/dlgbox/using-common-dialog-boxes#open_file
	OPENFILENAMEW ofn = {};       // common dialog box structure
	wchar_t filename[2048];       // buffer for file name
	filename[0] = '\0';

	if (action == OPEN_FILE_DIALOG_LOAD_GENERIC_FILE) {
		console_print("Attempting to open a file\n");

		// Initialize OPENFILENAME
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = app_state->main_window;
		ofn.lpstrFile = filename;
		ofn.nMaxFile = sizeof(filename);
		ofn.lpstrFilter = L"All\0*.*\0Text\0*.TXT\0";
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = win32_string_widen(get_active_directory(app_state), 1024, (wchar_t*)alloca(1024 * sizeof(wchar_t)));
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		// Display the Open dialog box.
		mouse_show();
		if (GetOpenFileNameW(&ofn)==TRUE) {
			char narrow_filename[4096];
			win32_string_narrow(filename, narrow_filename, sizeof(narrow_filename));
			load_generic_file(&global_app_state, narrow_filename, filetype_hint);
		}
	} else if (action == OPEN_FILE_DIALOG_CHOOSE_DIRECTORY) {
		console_print("Attempting to choose a directory\n");

		// Convert the initial directory to display to the right format
		ITEMIDLIST * root = 0;
		wchar_t root_path[512];
		root_path[0] = '\0';
		win32_string_widen(get_annotation_directory(app_state), COUNT(root_path), root_path);
		SHParseDisplayName(root_path, NULL, &root, 0, NULL);

		BROWSEINFOW browse_info = {};
		browse_info.hwndOwner = app_state->main_window;
		browse_info.pidlRoot = NULL;
		browse_info.pszDisplayName = filename;
		browse_info.lpszTitle = L"Select annotation directory";
		browse_info.ulFlags = BIF_NEWDIALOGSTYLE | BIF_EDITBOX ;
		browse_info.lpfn = BrowseCallbackProc;
		browse_info.lParam = (LPARAM)root; // passed to BrowseCallbackProc to set initial directory
		browse_info.iImage = 0;
		PIDLIST_ABSOLUTE pidlist = SHBrowseForFolderW(&browse_info);
		if (pidlist) {
			wchar_t path[MAX_PATH];
			if (SHGetPathFromIDListW(pidlist, path)) {
				char narrow_filename[1024];
				win32_string_narrow(path, narrow_filename, sizeof(narrow_filename));
				set_annotation_directory(app_state, narrow_filename);
			}
		}
	}


}

bool save_file_dialog(app_state_t* app_state, char* path_buffer, i32 path_buffer_size, const char* filter_string, const char* filename_hint) {
	OPENFILENAMEW ofn = {};       // common dialog box structure
	path_buffer[0] = '\0';
	ASSERT(path_buffer_size > 1);

	wchar_t* path_buffer_wide = win32_string_widen(filename_hint, path_buffer_size, (wchar_t*)alloca(path_buffer_size * 2));

	size_t filter_string_len = strlen(filter_string) + 1;
	wchar_t* filter_string_wide = win32_string_widen(filter_string, filter_string_len, (wchar_t*)alloca(filter_string_len * 2));

	// Initialize OPENFILENAME
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = app_state->main_window;
	ofn.lpstrFile = path_buffer_wide;
	ofn.nMaxFile = path_buffer_size-1;
	ofn.lpstrFilter = filter_string_wide;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;

	// Display the Open dialog box.
	mouse_show();
	if (GetSaveFileNameW(&ofn)==TRUE) {
		win32_string_narrow(ofn.lpstrFile, path_buffer, path_buffer_size);
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
			wchar_t wide_buffer[1024];
			char narrow_buffer[2048];
			if (DragQueryFileW(hdrop, 0, wide_buffer, sizeof(wide_buffer))) {
				size_t len = wcslen(wide_buffer);
				win32_string_narrow(wide_buffer, narrow_buffer, sizeof(narrow_buffer));
				u32 filetype_hint = load_next_image_as_overlay ? FILETYPE_HINT_OVERLAY : 0;
				load_generic_file(&global_app_state, narrow_buffer, filetype_hint);
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
			need_quit = true;
		} break;

		case WM_SETCURSOR: {
			if (message == WM_SETCURSOR) {
				u16 hit_test_result = LOWORD(lparam);
				if (hit_test_result >= HTLEFT && hit_test_result <= HTBOTTOMRIGHT) {
					gui_user_can_resize_at_window_edge = true;
					return DefWindowProcW(window, message, wparam, lparam);
				} else {
					gui_user_can_resize_at_window_edge = false;
					// NOTE: cursor is set each frame from gui_draw(), where update_cursor() is called
//					SetCursor(global_cursor_arrow);
				}
			}

		} break;

		case WM_DESTROY: {
			// TODO: Handle this as an error - recreate window?
			is_program_running = false;
		} break;

		case WM_INPUT: {
			result = DefWindowProcA(window, message, wparam, lparam); // apparently this is needed for cleanup?
		}; break;

		case WM_COPYDATA: {
			PCOPYDATASTRUCT cds = (PCOPYDATASTRUCT) lparam;
			if (cds->dwData == SV_COPYDATA_TYPE) {
				win32_copydata_message_t* message_data = (win32_copydata_message_t*) cds->lpData;
				if (message_data->argc > 1 && message_data->need_open) {
					u32 filetype_hint = load_next_image_as_overlay ? FILETYPE_HINT_OVERLAY : 0;
					load_generic_file(&global_app_state, message_data->filename, filetype_hint);
				}
			}
		} break;

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
	BOOL has_message = PeekMessageA(&message, NULL, 0, 0, PM_REMOVE);
	if (!has_message) {
		if (!allow_idling) {
			return false; // don't idle waiting for messages if there e.g. animations on screen
		} else {
			did_idle = true;
			BOOL ret = GetMessageA(&message, NULL, 0, 0); // blocks until there is a message
			if (ret == -1) {
				win32_diagnostic("GetMessageA");
				fatal_error();
			}
		}
	}

	do {
		++messages_processed;
		i64 last_message_clock = get_clock();

		if (message.message == WM_QUIT) {
			need_quit = true;
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
					input->mouse_z = (float)z_delta / (WHEEL_DELTA);
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
					/*console_print("WM_INPUT: usFlags=%d usButtonFlags=%x lLastX=%d lLastY=%d\n",
								  raw->data.mouse.usFlags,
								  raw->data.mouse.usButtonFlags,
								  raw->data.mouse.lLastX,
								  raw->data.mouse.lLastY);*/
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
						curr_input->drag_vector = v2f();
						curr_input->drag_start_xy = curr_input->mouse_xy;
					}

					// We want raw relative mouse movement (this allows mouse input even at the edges of the screen!)
					// But, it isn't guaranteed that we will actually get relative mouse movement.
					// https://docs.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse
					i32 relative_x = 0;
					i32 relative_y = 0;
					if ((raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == MOUSE_MOVE_ABSOLUTE) {
						// Input is absolute (unfortunately).
						// So, we have try to calculate the relative mouse movement as best as we can.
						bool is_virtual_desktop = (raw->data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) == MOUSE_VIRTUAL_DESKTOP;

						i32 width = GetSystemMetrics(is_virtual_desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
						i32 height = GetSystemMetrics(is_virtual_desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);

						i32 absolute_x = i32((raw->data.mouse.lLastX / 65535.0f) * width);
						i32 absolute_y = i32((raw->data.mouse.lLastY / 65535.0f) * height);

						static i32 prev_absolute_x;
						static i32 prev_absolute_y;
						static bool have_prev_absolute_xy = false;
						if (have_prev_absolute_xy) {
							relative_x = absolute_x - prev_absolute_x;
							relative_y = absolute_y - prev_absolute_y;
						} else {
							have_prev_absolute_xy = true;
						}
						prev_absolute_x = absolute_x;
						prev_absolute_y = absolute_y;
					} else if (raw->data.mouse.lLastX != 0 || raw->data.mouse.lLastY != 0) {
						// Input is relative (this is actually what we want!)
						relative_x = raw->data.mouse.lLastX;
						relative_y = raw->data.mouse.lLastY;
					}

					if (relative_x != 0 || relative_y != 0) {
						if (curr_input->mouse_buttons[0].down) {
							curr_input->drag_vector.x += (float)relative_x;
							curr_input->drag_vector.y += (float)relative_y;
							//console_print("Dragging: dx=%d dy=%d\n", curr_input->delta_mouse_x, curr_input->delta_mouse_y);
						} else {
							// not dragging
							mouse_show();
						}
					}


				} else {
//					console_print("WM_INPUT: %d - \n");
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
				if (vk_code == VK_SPACE) {
					hid_code = KEY_Space; // NOTE: for some reason, Space is missing from the table in keycode_windows_to_hid()
				}
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
							open_file_dialog(&global_app_state, OPEN_FILE_DIALOG_LOAD_GENERIC_FILE, 0);
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
					} break;
				}


				win32_process_keyboard_event(&keyboard_input->keys[hid_code], is_down);

				if (hid_code == KEY_LeftAlt || hid_code == KEY_RightAlt) {
					win32_process_keyboard_event(&keyboard_input->key_alt, is_down);
				} else if (keyboard_input->key_alt.down && !alt_down) {
					// NOTE: workaround for a bug: sometimes Alt doesn't get released properly??
					win32_process_keyboard_event(&keyboard_input->key_alt, false);
				}

				switch (vk_code) {
					default:
						break;
					case VK_SHIFT:
						win32_process_keyboard_event(&keyboard_input->key_shift, is_down);
						break;
					case VK_CONTROL:
						win32_process_keyboard_event(&keyboard_input->key_ctrl, is_down);
						break;
//					case VK_MENU:
//						win32_process_keyboard_event(&keyboard_input->key_alt, is_down);
//						break;
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

analog_stick_t win32_get_xinput_analog_stick_input(SHORT x, SHORT y, v2f old_stick_position) {
	analog_stick_t stick = {};
	stick.start = old_stick_position;
	if (x < 0) ++x;
	if (y < 0) ++y;
	stick.end.x = (float) x / 32767.f;
	stick.end.y = (float) y / 32767.f;

	float magnitude_squared = SQUARE(stick.end.x) + SQUARE(stick.end.y);
	if (magnitude_squared > SQUARE(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.f)) {
		stick.has_input = true;
	} else {
		stick.end.x = 0;
		stick.end.y = 0;
	}
	return stick;
}

analog_trigger_t win32_get_xinput_analog_trigger_input(BYTE x, float old_trigger_position) {
	analog_trigger_t trigger = {};
	trigger.start = old_trigger_position;
	if (x > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
		trigger.has_input = true;
		trigger.end = (float) x / 255.f;
	}
	return trigger;
}

void win32_process_xinput_controllers() {
	// NOTE: performance bug in XInput! May stall if no controller is connected.
	// We space out XInputGetState calls for unconnected controllers, but prioritize controller index 0 and 1
	static u8 poll_order[] = {0,1,0,2,0,1,0,3};
	static i32 poll_index;
	static i64 last_poll_time;
	i64 current_clock = get_clock();
	bool need_poll = false;
	float seconds_since_last_poll = get_seconds_elapsed(last_poll_time, current_clock);
	if (seconds_since_last_poll > 1.0f) {
		need_poll = true;
	}

	u32 game_max_controller_count = MIN(XUSER_MAX_COUNT, COUNT(curr_input->controllers));
	for (DWORD controller_index = 0; controller_index < game_max_controller_count; ++controller_index) {
		controller_input_t* old_controller_input = &old_input->controllers[controller_index];
		controller_input_t* new_controller_input = &curr_input->controllers[controller_index];

		if (old_input->controllers[controller_index].is_connected) {
			if (need_poll && poll_order[poll_index] == controller_index) {
				// We don't need to poll for a controller that is already connected
				poll_index = (poll_index + 1) % COUNT(poll_order);
			}
		} else {
			if (need_poll && controller_index == poll_order[poll_index]) {
				// Device is not connected and it is time to poll
//				console_print("XInput: polling for controller index %d\n", controller_index);
				poll_index = (poll_index + 1) % COUNT(poll_order);
				last_poll_time = current_clock;
				need_poll = false;
			} else {
				// Device is not connected and it is not yet time to poll -> skip
				continue;
			}
		}

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
				new_controller_input->left_stick.end = v2f();
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_UP) new_controller_input->left_stick.end.y += 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_DOWN) new_controller_input->left_stick.end.y -= 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_LEFT) new_controller_input->left_stick.end.x += 1.0f;
				if (xinput_button_state & XINPUT_GAMEPAD_DPAD_RIGHT) new_controller_input->left_stick.end.x -= 1.0f;
			}

			new_controller_input->left_stick = win32_get_xinput_analog_stick_input(xinput_gamepad->sThumbLX, xinput_gamepad->sThumbLY, old_controller_input->left_stick.end);
			new_controller_input->right_stick = win32_get_xinput_analog_stick_input(xinput_gamepad->sThumbRX, xinput_gamepad->sThumbRY, old_controller_input->left_stick.end);
			new_controller_input->is_analog = (new_controller_input->left_stick.has_input || new_controller_input->right_stick.has_input);

			new_controller_input->left_trigger = win32_get_xinput_analog_trigger_input(xinput_gamepad->bLeftTrigger, old_controller_input->left_trigger.end);
			new_controller_input->right_trigger = win32_get_xinput_analog_trigger_input(xinput_gamepad->bRightTrigger, old_controller_input->right_trigger.end);

			if (new_controller_input->back.down) {
				need_quit = true;
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

bool win32_init_opengl(HWND window, bool use_software_renderer) {
	i64 debug_start = get_clock();

	// Set environment variable needed for Mesa3D software driver support
	// See:
	// https://github.com/pal1000/mesa-dist-win (section 'OpenGL context configuration override')
	// https://gitlab.freedesktop.org/mesa/mesa/-/blob/master/src/mesa/main/version.c
	// https://stackoverflow.com/questions/4788398/changes-via-setenvironmentvariable-do-not-take-effect-in-library-that-uses-geten
	_putenv_s("MESA_GL_VERSION_OVERRIDE", "4.3FC");

	if (use_software_renderer) {
		char dll_path[4096];
		GetModuleFileNameA(NULL, dll_path, sizeof(dll_path));
		char* pos = (char*)one_past_last_slash(dll_path, sizeof(dll_path));
		i32 chars_left = sizeof(dll_path) - (pos - dll_path);
		strncpy(pos, "softwarerenderer", chars_left);
		SetDllDirectoryA(dll_path);
		opengl32_dll_handle = LoadLibraryA("opengl32software.dll");
		use_fast_rendering = true;
		// NOTE: We again need access to the extra dll file in win32_init_gui(),
		// because ImGui has its own OpenGL loader code.
		// So, don't call SetDllDirectoryA() which ordinarily we would want to do here.
//		SetDllDirectoryA(NULL);
	} else {
		opengl32_dll_handle = LoadLibraryA("opengl32.dll");

	}

	if (!opengl32_dll_handle) {
		win32_diagnostic("LoadLibraryA");
		console_print("Error initializing OpenGL: failed to load opengl32.dll.\n");
		return false;
	}


	// Initializing OpenGL on Windows is somewhat tricky.
	// https://mariuszbartosik.com/opengl-4-x-initialization-in-windows-without-a-framework/

	// Dynamically import all of the functions we need from opengl32.dll and all the wgl functions
	wglGetProcAddress_alt = (PFNWGLGETPROCADDRESSPROC) GetProcAddress(opengl32_dll_handle, "wglGetProcAddress");
	if (!wglGetProcAddress_alt) {
		console_print("Error initalizing OpenGL: could not load proc 'wglGetProcAddress'.\n");
		fatal_error();
	}
	wglCreateContext_alt = (PFNWGLCREATECONTEXTPROC) gl_get_proc_address("wglCreateContext");
	if (!wglCreateContext_alt) { fatal_error(); }
	wglMakeCurrent_alt = (PFNWGLMAKECURRENTPROC) gl_get_proc_address("wglMakeCurrent");
	if (!wglMakeCurrent_alt) { fatal_error(); }
	wglDeleteContext_alt = (PFNWGLDELETECONTEXTPROC) gl_get_proc_address("wglDeleteContext");
	if (!wglDeleteContext_alt) { fatal_error(); }
	wglGetCurrentDC_alt = (PFNWGLGETCURRENTDCPROC) gl_get_proc_address("wglGetCurrentDC");
	if (!wglGetCurrentDC_alt) { fatal_error(); }
	wglSetPixelFormat = (PFNSETPIXELFORMATPROC) gl_get_proc_address("wglSetPixelFormat");
	if (!wglSetPixelFormat) { fatal_error(); }
	wglDescribePixelFormat = (PFNDESCRIBEPIXELFORMATPROC) gl_get_proc_address("wglDescribePixelFormat");
	if (!wglDescribePixelFormat) { fatal_error(); }
	wglChoosePixelFormat = (PFNCHOOSEPIXELFORMATPROC) gl_get_proc_address("wglChoosePixelFormat");
	if (!wglChoosePixelFormat) { fatal_error(); }
	wglSwapBuffers = (PFNSWAPBUFFERSPROC) gl_get_proc_address("wglSwapBuffers");
	if (!wglSwapBuffers) { fatal_error(); }

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

	PIXELFORMATDESCRIPTOR desired_pixel_format = {};
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
	if (use_software_renderer) {
		if (!wglSetPixelFormat(dummy_dc, suggested_pixel_format_index, &suggested_pixel_format)) {
			win32_diagnostic("wglSetPixelFormat");
			fatal_error();
		}
	} else {
		if (!SetPixelFormat(dummy_dc, suggested_pixel_format_index, &suggested_pixel_format)) {
			win32_diagnostic("SetPixelFormat");
			fatal_error();
		}
	}

	// Create the OpenGL context for the main thread.
	HGLRC dummy_glrc = wglCreateContext_alt(dummy_dc);
	if (!dummy_glrc) {
		win32_diagnostic("wglCreateContext");
		fatal_error();
	}

	if (!wglMakeCurrent_alt(dummy_dc, dummy_glrc)) {
		win32_diagnostic("wglMakeCurrent");
		fatal_error();
	}

	// Before we go any further, try to report the supported OpenGL version from openg32.dll
	PFNGLGETSTRINGPROC temp_glGetString = (PFNGLGETSTRINGPROC) gl_get_proc_address("glGetString");
	if (temp_glGetString == NULL) {
		fatal_error();
	}

	char version_string[256] = {};
	char* version_string_retrieved = (char*)temp_glGetString(GL_VERSION);
	strncpy(version_string, version_string_retrieved, sizeof(version_string)-1);
	if (use_software_renderer) {
		console_print("OpenGL software renderer: %s\n", version_string);
	} else {
		console_print("OpenGL supported version: %s\n", version_string);
	}

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

	// To test the software renderer
//	if (!use_software_renderer) is_opengl_version_supported = false;

	if (!is_opengl_version_supported) {

		bool success = false;
		if (!use_software_renderer) {
			// If the hardware renderer isn't working (maybe on a remote desktop environment?),
			// we could try using the Mesa3D software renderer instead (if available).
			wglMakeCurrent_alt(dummy_dc, NULL);
			wglDeleteContext_alt(dummy_glrc);

			wglSwapIntervalEXT = NULL;
			wglGetSwapIntervalEXT = NULL;
			wglGetExtensionsStringEXT = NULL;
			wglCreateContextAttribsARB = NULL;
			wglChoosePixelFormatARB = NULL;
			wglSetPixelFormat = NULL;
			wglDescribePixelFormat = NULL;
			wglChoosePixelFormat = NULL;
			wglGetProcAddress_alt = NULL;
			wglCreateContext_alt = NULL;
			wglMakeCurrent_alt = NULL;
			wglDeleteContext_alt = NULL;
			wglGetCurrentDC_alt = NULL;
			wglSwapBuffers = NULL;

			FreeLibrary(opengl32_dll_handle);
			opengl32_dll_handle = NULL;

			global_is_using_software_renderer = true;
			success = win32_init_opengl(window, true);
		}

		if (!success) {
			char buf[4096];
			snprintf(buf, sizeof(buf), "Error: OpenGL version is insufficient.\n"
									   "Required: %d.%d\n\n"
									   "Available on this system:\n%s", major_required, minor_required, version_string);
			console_print_error("%s\n", buf);
			message_box(window, buf);
			exit(0);
		}
		return success;
	}

	if (strstr(version_string, "NVIDIA")) {
	    is_nvidia_gpu = true;
	}

	// Now try to load the extensions we will need.

#define GET_WGL_PROC(proc, type) do { proc = (type) wglGetProcAddress_alt(#proc); } while(0)

	GET_WGL_PROC(wglGetExtensionsStringEXT, PFNWGLGETEXTENSIONSSTRINGEXTPROC);
	if (!wglGetExtensionsStringEXT) {
		console_print("Error: wglGetExtensionsStringEXT is unavailable\n");
		fatal_error();
	}
	wgl_extensions_string = wglGetExtensionsStringEXT();
//	puts(wgl_extensions_string);

	if (win32_wgl_extension_supported("WGL_EXT_swap_control")) {
		GET_WGL_PROC(wglSwapIntervalEXT, PFNWGLSWAPINTERVALEXTPROC);
		GET_WGL_PROC(wglGetSwapIntervalEXT, PFNWGLGETSWAPINTERVALEXTPROC);
	} else {
		console_print("Error: WGL_EXT_swap_control is unavailable\n");
		fatal_error();
	}

	if (win32_wgl_extension_supported("WGL_ARB_create_context")) {
		GET_WGL_PROC(wglCreateContextAttribsARB, PFNWGLCREATECONTEXTATTRIBSARBPROC);
	} else {
		console_print("Error: WGL_ARB_create_context is unavailable\n");
		fatal_error();
	}

	if (win32_wgl_extension_supported("WGL_ARB_pixel_format")) {
		GET_WGL_PROC(wglChoosePixelFormatARB, PFNWGLCHOOSEPIXELFORMATARBPROC);
	} else {
		console_print("Error: WGL_ARB_pixel_format is unavailable\n");
		fatal_error();
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
		fatal_error();
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
		fatal_error();
	}


	// Delete the dummy context and start using the real one.
	wglMakeCurrent_alt(NULL, NULL);
	wglDeleteContext_alt(dummy_glrc);
	ReleaseDC(dummy_window, dummy_dc);
	DestroyWindow(dummy_window);
	if (!wglMakeCurrent_alt(dc, glrcs[0])) {
		win32_diagnostic("wglMakeCurrent");
		fatal_error();
	}
	ReleaseDC(window, dc);

	// Now, get the OpenGL proc addresses using GLAD.
	if (!gladLoadGLLoader((GLADloadproc) gl_get_proc_address)) {
		console_print("Error initializing OpenGL: failed to initialize GLAD.\n");
		fatal_error();
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
			fatal_error();
		}
/*		if (!wglShareLists(glrcs[0], glrc)) {
			console_print("Thread %d: ", thread_index);
			win32_diagnostic("wglShareLists");
			fatal_error();
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

	return true;
}


bool win32_process_input(app_state_t* app_state) {

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
//		controller->left_stick.start = controller->left_stick.end;
		// Reset transition counts.
		for (u32 i = 0; i < COUNT(controller->buttons); ++i) {
			controller->buttons[i].transition_count = 0;
		}
	}

	// The preferred controller is the first connected controller.
	i32 preferred_controller_index = 0;
	for (i32 i = 0; i < COUNT(curr_input->controllers); ++i) {
		if (curr_input->controllers[i].is_connected) {
			preferred_controller_index = i;
			break;
		}
	}
	curr_input->preferred_controller_index = preferred_controller_index;
	controller_input_t* preferred_controller = curr_input->controllers + preferred_controller_index;

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
	curr_input->mouse_xy = V2F((float)cursor_pos.x, (float)cursor_pos.y);
	curr_input->mouse_z = 0;

	// NOTE: should we call GetAsyncKeyState or GetKeyState?
	win32_process_keyboard_event(&curr_input->mouse_buttons[0], GetAsyncKeyState(VK_LBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[1], GetAsyncKeyState(VK_RBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[2], GetAsyncKeyState(VK_MBUTTON) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[3], GetAsyncKeyState(VK_XBUTTON1) & (1<<15));
	win32_process_keyboard_event(&curr_input->mouse_buttons[4], GetAsyncKeyState(VK_XBUTTON2) & (1<<15));

	bool did_idle = win32_process_pending_messages(curr_input, app_state->main_window, app_state->allow_idling_next_frame);

	win32_process_xinput_controllers();

	// Check if at least one button/key is pressed at all (if no buttons are pressed,
	// we might be allowed to idle waiting for input (skipping frames!) as long as nothing is animating on the screen).
	curr_input->are_any_buttons_down = false;
	for (u32 i = 0; i < COUNT(curr_input->keyboard.buttons); ++i) {
		curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->keyboard.buttons[i].down;
	}
	for (u32 i = 0; i < COUNT(preferred_controller->buttons); ++i) {
		curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || preferred_controller->buttons[i].down;
	}
	for (u32 i = 0; i < COUNT(curr_input->keyboard.keys); ++i) {
		curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->keyboard.keys[i].down;
	}
	for (u32 i = 0; i < COUNT(curr_input->mouse_buttons); ++i) {
		curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->mouse_buttons[i].down;
	}

	curr_input->keyboard.modifiers = 0;
	if (curr_input->keyboard.key_ctrl.down) curr_input->keyboard.modifiers |= KMOD_CTRL;
	if (curr_input->keyboard.key_alt.down) curr_input->keyboard.modifiers |= KMOD_ALT;
	if (curr_input->keyboard.key_shift.down) curr_input->keyboard.modifiers |= KMOD_SHIFT;
	if (curr_input->keyboard.key_super.down) curr_input->keyboard.modifiers |= KMOD_GUI;

	return did_idle;
}


static DWORD WINAPI thread_proc(void* parameter) {
	platform_thread_info_t* thread_info = (platform_thread_info_t*) parameter;
	i64 init_start_time = get_clock();

	atomic_increment(&global_worker_thread_idle_count);

	init_thread_memory(thread_info->logical_thread_index, &global_system_info);
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
			fatal_error();
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
		if (thread_info->logical_thread_index > global_active_worker_thread_count) {
			// Worker is disabled, do nothing
			Sleep(100);
			continue;
		}
		if (!work_queue_do_work(thread_info->high_priority_queue, thread_info->logical_thread_index)) {
			if (!work_queue_do_work(thread_info->queue, thread_info->logical_thread_index)) {
				if (!(work_queue_is_work_waiting_to_start(thread_info->queue) || work_queue_is_work_waiting_to_start(thread_info->high_priority_queue))) {
					WaitForSingleObjectEx(thread_info->queue->semaphore, 1, FALSE);
				}
			}
		}
	}
}

void win32_init_multithreading() {
	init_thread_memory(0, &global_system_info);

	global_worker_thread_count = global_system_info.suggested_total_thread_count - 1;
	global_active_worker_thread_count = global_worker_thread_count;

	global_work_queue = work_queue_create("/worksem", 1024); // Queue for newly submitted tasks
	// Queue for tasks that take priority over normal tasks (e.g. because they are short tasks submitted on the main thread)
	global_high_priority_work_queue = work_queue_create_with_existing_semaphore(global_work_queue.semaphore, 1024);
	global_completion_queue = work_queue_create("/completionsem", 1024); // Message queue for completed tasks
	global_export_completion_queue = work_queue_create("/exportcompletionsem", 1024); // Message queue for export task

	// NOTE: the main thread is considered thread 0.
	for (i32 i = 1; i < global_system_info.suggested_total_thread_count; ++i) {
		platform_thread_info_t thread_info = { .logical_thread_index = i, .queue = &global_work_queue, .high_priority_queue = &global_high_priority_work_queue};
		thread_infos[i] = thread_info;

		DWORD thread_id;
		HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, thread_infos + i, 0, &thread_id);
		CloseHandle(thread_handle);

	}

	test_multithreading_work_queue();

}

void win32_init_main_window(app_state_t* app_state) {
	ImGui_ImplWin32_EnableDpiAwareness();
	main_window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	main_window_class.lpfnWndProc = main_window_callback;
	main_window_class.hInstance = g_instance;
	main_window_class.hCursor = NULL;
	main_window_class.hIcon = LoadIconA(g_instance, MAKEINTRESOURCE(101));
	main_window_class.lpszClassName = "SlidescapeMainWindow";
	main_window_class.hbrBackground = NULL;

	if (!RegisterClassA(&main_window_class)) {
		win32_diagnostic("RegisterClassA");
		fatal_error();
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
	                              main_window_class.lpszClassName, APP_TITLE,
	                                     window_style,
	                                     0/*CW_USEDEFAULT*/, 0/*CW_USEDEFAULT*/, initial_window_width, initial_window_height,
	                                     0, 0, g_instance, 0);
	if (!app_state->main_window) {
		win32_diagnostic("CreateWindowExA");
		fatal_error();
	}

	win32_init_opengl(app_state->main_window, false);

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

BOOL CALLBACK win32_enum_windows_proc_func(HWND hwnd, LPARAM lparam) {
	DWORD process_id;
	GetWindowThreadProcessId(hwnd, &process_id);
//	console_print("process_id=%d lparam=%d\n", process_id, lparam);
	if (process_id == lparam) {
//		console_print("Found hwnd=%d\n", hwnd);
		global_already_running_app_hwnd = hwnd;
		return FALSE;
	}
	return TRUE;
}

void win32_check_already_running() {
	DWORD processes[4096];
	DWORD bytes_read;
	EnumProcesses(processes, sizeof(processes), &bytes_read);
	DWORD process_count = bytes_read / sizeof(DWORD);

	CHAR curr_name[MAX_PATH];
	GetModuleFileNameExA(GetCurrentProcess(), NULL, curr_name, MAX_PATH);
	DWORD curr_process_id = GetCurrentProcessId();

	for(i32 i = 0; i < process_count; ++i) {
		HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processes[i]);
		CHAR name[MAX_PATH];
		GetModuleFileNameExA(process, NULL, name, MAX_PATH);
		CloseHandle(process);
		if (processes[i] != curr_process_id && strcmp(name, curr_name) == 0) {
			console_print("Already running!\n");
			EnumWindows(win32_enum_windows_proc_func, (LPARAM)processes[i]);
			if (global_already_running_app_hwnd) {
				if (!IsWindowVisible(global_already_running_app_hwnd)) {
					ShowWindow(global_already_running_app_hwnd, SW_SHOW);
				}
				SetForegroundWindow(global_already_running_app_hwnd);

				// TODO: prevent race condition when opening multiple files

				win32_copydata_message_t message_data = {};
				message_data.argc = g_argc;
				if (g_argc > 1) {
					message_data.need_open = true;
					strncpy(message_data.filename, g_argv[1], sizeof(message_data.filename)-1);
				}

				COPYDATASTRUCT cds = {};
				cds.dwData = SV_COPYDATA_TYPE;
				cds.cbData = sizeof(win32_copydata_message_t);
				cds.lpData = &message_data;

				SendMessageA(global_already_running_app_hwnd, WM_COPYDATA, NULL, (LPARAM)(&cds));

				exit(0);
			}
		}
	}
}



void win32_init_cmdline() {

	// Ideally, the codepage should be 65001 (UTF-1), which should make special characters 'just work'.
	// However, there may still be corner cases.
	// Specifically, debugging does not seem to work predictably.
	// TODO: Fix codepage being incorrect while debugging (maybe due to parent process codepage not being UTF-8)
	// TODO: Check if workarounds for Unicode also work on Windows 7.
//	UINT acp = GetACP();
//	UINT console_output_cp = GetConsoleOutputCP();
//	UINT console_cp = GetConsoleCP();
//	console_print("Active codepage = %d\n", acp);
//	console_print("console_output_cp = %d\n", console_output_cp);
//	console_print("console_cp = %d\n", console_cp);

//	setlocale(LC_ALL, ".UTF8"); // Is this needed?

	g_instance = GetModuleHandleA(NULL);
	g_cmdline = GetCommandLineA();
//	console_print("cmdline = %s\n", g_cmdline);

	// Convert UTF-16 characters from the command-line to UTF-8
	wchar_t* wcmdline = GetCommandLineW();
	size_t cmdline_len = wcslen(wcmdline);
	char* cmdline = win32_string_narrow(wcmdline, (char*) malloc(cmdline_len * 4), cmdline_len * 4);
//	console_print("cmdline = %s\n", cmdline);

	int argc = 0;
	char** argv = CommandLineToArgvA(cmdline, &argc);

	g_argc = argc;
	g_argv = (const char**)argv;

	GetModuleFileNameA(NULL, g_exe_name, sizeof(g_exe_name));
	g_exe_name[sizeof(g_exe_name)-1] = '\0';

	strncpy(g_root_dir, g_exe_name, sizeof(g_root_dir));
	PathStripToRootA(g_root_dir);
	// N.B. the root path requires a trailing backslash for e.g. GetDriveType to work:
	// https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdrivetypea
	i32 root_dir_len = strlen(g_root_dir);
	if (root_dir_len > 0 && g_root_dir[root_dir_len-1] != '\\') {
		g_root_dir[root_dir_len] = '\\';
	}
}

static void win32_init_headless_console() {
	// When compiling with -mwindows or /subsytem:windows (which we do in release mode),
	// stdout and stderr aren't displayed, even if you run the program from the command-line.
	// It is possible to get console output back, but we have to jump through some hoops.
#if !DO_DEBUG
	BOOL ret = AttachConsole(ATTACH_PARENT_PROCESS);
	HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (stdout_handle != INVALID_HANDLE_VALUE) {
		freopen("CONOUT$", "w", stdout);
		setvbuf(stdout, NULL, _IONBF, 0);
	}
	HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
	if (stderr_handle != INVALID_HANDLE_VALUE) {
		freopen("CONOUT$", "w", stderr);
		setvbuf(stderr, NULL, _IONBF, 0);
	}
	putc('\n', stdout);
	putc('\n', stdout);
#endif //DO_DEBUG
}

static void win32_prepare_exit_console() {
#if !DO_DEBUG
	// Hack: send an enter so that the regular command prompt is displayed again
	// NOTE: This doesn't work on Windows Terminal, see:
	// https://github.com/microsoft/terminal/issues/6887
	HWND console_window = GetConsoleWindow();
	SendMessageA(console_window, WM_CHAR, VK_RETURN, 0);
#endif
	exit(0);
}

int main() {
	win32_init_cmdline();

#if CREATE_ICO
	create_ico();
#endif

	app_command_t app_command = app_parse_commandline(g_argc, g_argv);
	if (app_command.exit_immediately) {
		win32_init_headless_console();
		app_command_execute_immediately(&app_command);
		win32_prepare_exit_console();
		exit(0);
	}
    bool verbose_console = !app_command.headless;

    console_printer_benaphore = benaphore_create();
    if (verbose_console) console_print("Starting up...\n");

	// Don't open multiple instances of the program when opening a file -> switch to the existing instance
	// (unless Shift is being held down)
	if (g_argc > 1 && !(GetKeyState(VK_SHIFT) & 0x8000)) {
		win32_check_already_running();
	}

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	get_system_info(verbose_console);

	win32_setup_appdata();
#ifndef DONT_REGISTER_FILETYPE_ASSOCIATIONS // suppress filetype associations on separate console build
	win32_set_file_type_associations();
#endif
	win32_init_timer();
	win32_init_multithreading();

	app_state_t* app_state = &global_app_state;
	init_app_state(app_state, app_command);

	is_vsync_enabled = true;

	viewer_init_options(app_state);

	if (app_command.headless) {
		load_openslide_task(0, NULL);
		return app_command_execute(app_state);
	}

	win32_init_cursor();
	win32_init_main_window(app_state);

	// Load OpenSlide in the background, we might not need it immediately.
	work_queue_submit_task(&global_work_queue, load_openslide_task, NULL, 0);

	// Load DICOM support in the background
	work_queue_submit_task(&global_work_queue, load_dicom_task, NULL, 0);

	win32_init_input();

	is_program_running = true;

	win32_init_gui(app_state);

	init_opengl_stuff(app_state);

	// Load a slide from the command line or through the OS (double-click / drag on executable, etc.)
	if (arrlen(app_command.inputs) > 0) {
		// TODO: allow loading multiple files from the commandline?
		const char* filename = app_command.inputs[0];
//		console_print("filename = %s\n", filename);
		load_generic_file(app_state, filename, 0);
	}

	HDC glrc_hdc = wglGetCurrentDC_alt();

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

		float delta_t = get_seconds_elapsed(last_clock, current_clock);
		last_clock = current_clock;
		delta_t = ATMOST(2.0f / 60.0f, delta_t); // prevent physics overshoot at lag spikes

		bool did_idle = win32_process_input(app_state);
		if (did_idle) {
			last_clock = get_clock();
		}

		win32_gui_new_frame(app_state);

		// Update and render our application
		win32_window_dimension_t dimension = win32_get_window_dimension(app_state->main_window);
		viewer_update_and_render(app_state, curr_input, dimension.width, dimension.height, delta_t);

		if (!is_program_running) {
			ShowWindow(app_state->main_window, SW_HIDE);
			break;
		}

		// Render the UI
		ImGui::Render();
		glViewport(0, 0, dimension.width, dimension.height);

		// Render any ImGui content submitted to the extra draw lists on worker threads
		if (global_active_extra_drawlists > 0) {
			static ImDrawData draw_data = ImDrawData();
			draw_data.DisplayPos = ImGui::GetMainViewport()->Pos;
			draw_data.DisplaySize = ImGui::GetMainViewport()->Size;
			draw_data.FramebufferScale = ImVec2(1.0f, 1.0f);
			draw_data.TotalIdxCount = 0;
			draw_data.TotalVtxCount = 0;
			draw_data.CmdListsCount = 0;
			ImVector<ImDrawList*> drawlists;
			for (i32 i = 0; i < global_active_extra_drawlists; ++i) {
				ImDrawList* drawlist = global_extra_drawlists[i];
				if (drawlist) {
					i32 vertex_buffer_size = drawlist->VtxBuffer.size();
					if (vertex_buffer_size > 0) {
						drawlists.push_back(drawlist);
						draw_data.CmdListsCount += 1;
						draw_data.TotalIdxCount += drawlist->IdxBuffer.size();
						draw_data.TotalVtxCount += vertex_buffer_size;
					}
				}
			}
			draw_data.CmdLists = drawlists;
			draw_data.Valid = true;
			if (draw_data.CmdListsCount > 0 && draw_data.TotalVtxCount > 0) {
				ImGui_ImplOpenGL3_RenderDrawData(&draw_data);
			}
		}

		// Render the rest of the ImGui draw data (submitted on the main thread)
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
	}

	autosave(app_state, true, false); // save any unsaved changes

	return 0;
}
