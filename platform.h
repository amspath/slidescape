/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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

#ifdef TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_THREAD_COUNT 128

typedef struct {
	size_t len;
	u8 data[0];
} file_mem_t;

typedef void (work_queue_callback_t)(int logical_thread_index, void* userdata);

typedef struct work_queue_entry_t {
	void* data;
	work_queue_callback_t* callback;
	bool32 is_valid;
} work_queue_entry_t;

typedef struct work_queue_t work_queue_t;




// Platform specific function prototypes

i64 get_clock();
float get_seconds_elapsed(i64 start, i64 end);
void platform_sleep(u32 ms);


u8* platform_alloc(size_t size); // required to be zeroed by the platform
file_mem_t* platform_read_entire_file(const char* filename);

void mouse_show();
void mouse_hide();

void message_box(const char* message);

bool32 add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata);
bool32 is_queue_work_in_progress(work_queue_t* queue);
bool32 do_worker_work(work_queue_t* queue, int logical_thread_index);


// globals
#if defined(WIN32_MAIN_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern int g_argc;
extern char** g_argv;
extern void* thread_local_storage[MAX_THREAD_COUNT];
extern work_queue_t work_queue;

#undef INIT
#undef extern

#ifdef __cplusplus
};
#endif

