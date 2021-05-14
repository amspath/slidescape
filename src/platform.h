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

#pragma once

#include "common.h"
#include "mathutils.h"

#include "keycode.h"
#include "keytable.h"

#if LINUX
#include <SDL2/SDL.h>
#endif

#if WINDOWS
#include "windows.h"
#else
#include <semaphore.h>
#include <unistd.h>
#if APPLE
#include <stddef.h> // for offsetof()
#endif

#endif

#ifdef TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_THREAD_COUNT 128

typedef struct app_state_t app_state_t;

typedef struct mem_t {
	size_t len;
	size_t capacity;
	u8 data[0];
} mem_t;

typedef struct memrw_t {
	u8* data;
	i64 cursor;
	u64 used_size;
	u64 used_count;
	u64 capacity;
} memrw_t;

typedef void (work_queue_callback_t)(int logical_thread_index, void* userdata);

typedef struct work_queue_entry_t {
	void* data;
	work_queue_callback_t* callback;
	bool is_valid;
} work_queue_entry_t;

#if WINDOWS
typedef HANDLE semaphore_handle_t;
#else
typedef sem_t* semaphore_handle_t;
#endif

typedef struct work_queue_t {
	semaphore_handle_t semaphore;
	i32 volatile next_entry_to_submit;
	i32 volatile next_entry_to_execute;
	i32 volatile completion_count;
	i32 volatile completion_goal;
	work_queue_entry_t entries[256];
} work_queue_t;

typedef struct benaphore_t {
	void* semaphore;
	volatile i32 counter;
} benaphore_t;

typedef struct platform_thread_info_t {
	i32 logical_thread_index;
	work_queue_t* queue;
} platform_thread_info_t;

typedef struct {
#if WINDOWS
	HANDLE async_io_event;
	OVERLAPPED overlapped;
#else
	// TODO: implement this
#endif
	u64 thread_memory_raw_size;
	u64 thread_memory_usable_size; // free space from aligned_rest_of_thread_memory onward
	void* aligned_rest_of_thread_memory;
	u32 pbo;
} thread_memory_t;

typedef struct button_state_t {
	bool8 down;
	u8 transition_count;
} button_state_t;

typedef struct controller_input_t {
	bool32 is_connected;
	bool32 is_analog;
	v2f stick_start;
	v2f stick_end;
	union {
		button_state_t buttons[533];
		struct {
			button_state_t move_up;
			button_state_t move_down;
			button_state_t move_left;
			button_state_t move_right;
			button_state_t action_up;
			button_state_t action_down;
			button_state_t action_left;
			button_state_t action_right;
			button_state_t left_shoulder;
			button_state_t right_shoulder;
			button_state_t start;
			button_state_t back;
			button_state_t button_a;
			button_state_t button_b;
			button_state_t button_x;
			button_state_t button_y;

			button_state_t keys[512];
			button_state_t key_shift;
			button_state_t key_ctrl;
			button_state_t key_alt;
			button_state_t key_super;

			// NOTE: add buttons above this line
			// cl complains about zero-sized arrays, so this the terminator is a full blown button now :(
			button_state_t terminator;
		};
	};

} controller_input_t;
// Does the count of the controller_input_t.buttons[] array add up?
COMPILE_TIME_ASSERT(controller_input_t, sizeof(((controller_input_t*)0)->buttons) == (offsetof(controller_input_t, terminator) - offsetof(controller_input_t, buttons) + sizeof(button_state_t)))

typedef struct input_t {
	button_state_t mouse_buttons[5];
	float mouse_z_start;
	float mouse_z;
	v2f drag_start_xy;
	v2f drag_vector;
	v2f mouse_xy;
	float delta_t;
	union {
		controller_input_t abstract_controllers[5];
		struct {
			controller_input_t keyboard;
			controller_input_t controllers[4];
		};
	};
	bool are_any_buttons_down;

} input_t;

#if WINDOWS
typedef HWND window_handle_t;
#elif APPLE
typedef void* window_handle_t;
#else
typedef SDL_Window* window_handle_t;
#endif


// Platform specific function prototypes

#if !IS_SERVER
i64 get_clock();
float get_seconds_elapsed(i64 start, i64 end);
void platform_sleep(u32 ms);
void platform_sleep_ns(i64 ns);
i64 profiler_end_section(i64 start, const char* name, float report_threshold_ms);
void set_swap_interval(int interval);
#endif

u8* platform_alloc(size_t size); // required to be zeroed by the platform
mem_t* platform_allocate_mem_buffer(size_t capacity);
mem_t* platform_read_entire_file(const char* filename);
u64 file_read_at_offset(void* dest, FILE* fp, u64 offset, u64 num_bytes);

void mouse_show();
void mouse_hide();

void open_file_dialog(app_state_t* app_state, u32 filetype_hint);
bool save_file_dialog(app_state_t* app_state, char* path_buffer, i32 path_buffer_size, const char* filter_string);
void toggle_fullscreen(window_handle_t window);
bool check_fullscreen(window_handle_t window);
void set_window_title(window_handle_t window, const char* title);
void reset_window_title(window_handle_t window);

void message_box(app_state_t* app_state, const char* message);

bool add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata);
bool is_queue_work_in_progress(work_queue_t* queue);
work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue);
void mark_queue_entry_completed(work_queue_t* queue);
bool do_worker_work(work_queue_t* queue, int logical_thread_index);
void test_multithreading_work_queue();

bool file_exists(const char* filename);

void memrw_maybe_grow(memrw_t* buffer, u64 new_size);
u64 memrw_push_back(memrw_t* buffer, void* data, u64 size);
void memrw_init(memrw_t* buffer, u64 capacity);
memrw_t memrw_create(u64 capacity);
void memrw_rewind(memrw_t* buffer);
void memrw_seek(memrw_t* buffer, i64 offset);
i64 memrw_write(const void* src, memrw_t* buffer, i64 bytes_to_write);
i64 memrw_read(void* dest, memrw_t* buffer, size_t bytes_to_read);
void memrw_destroy(memrw_t* buffer);

void get_system_info();

benaphore_t benaphore_create(void);
void benaphore_destroy(benaphore_t* benaphore);
void benaphore_lock(benaphore_t* benaphore);
void benaphore_unlock(benaphore_t* benaphore);

#if IS_SERVER
#define console_print printf
#define console_print_error(...) fprintf(stderr, __VA_ARGS__)
#else
void console_print(const char* fmt, ...); // defined in gui.cpp
void console_print_verbose(const char* fmt, ...); // defined in gui.cpp
void console_print_error(const char* fmt, ...);
#endif


// globals
#if defined(PLATFORM_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern int g_argc;
extern const char** g_argv;
extern bool is_fullscreen;
extern bool is_program_running;
extern void* thread_local_storage[MAX_THREAD_COUNT];
extern input_t inputs[2];
extern input_t *old_input;
extern input_t *curr_input;
extern u32 os_page_size;
extern u64 page_alignment_mask;
extern i32 total_thread_count;
extern i32 worker_thread_count;
extern i32 physical_cpu_count;
extern i32 logical_cpu_count;
extern bool is_vsync_enabled;
extern bool is_nvidia_gpu;
extern bool is_macos;
extern work_queue_t global_work_queue;
extern work_queue_t global_completion_queue;
extern bool is_verbose_mode INIT(= false);
extern benaphore_t console_printer_benaphore;

#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif

