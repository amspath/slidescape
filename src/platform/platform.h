/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2023  Pieter Valkema

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
#include "arena.h"
#include "memrw.h"
#include "timerutils.h"
#include "work_queue.h"
#include "benaphore.h"


#if WINDOWS
#include <windows.h>
#else
#include <semaphore.h>
#include <unistd.h>
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

#if WINDOWS
typedef HANDLE semaphore_handle_t;
typedef HANDLE file_handle_t;
typedef HANDLE file_stream_t;
#else
typedef sem_t* semaphore_handle_t;
typedef int file_handle_t;
typedef FILE* file_stream_t;
#endif

typedef struct platform_thread_info_t {
	i32 logical_thread_index;
	work_queue_t* queue;
} platform_thread_info_t;

#define MAX_ASYNC_IO_EVENTS 32

typedef struct {
#if WINDOWS
	HANDLE async_io_events[MAX_ASYNC_IO_EVENTS];
	i32 async_io_index;
	OVERLAPPED overlapped;
#else
	// TODO: implement this
#endif
	u64 thread_memory_raw_size;
	u64 thread_memory_usable_size; // free space from aligned_rest_of_thread_memory onward
	void* aligned_rest_of_thread_memory;
	u32 pbo;
	arena_t temp_arena;
} thread_memory_t;



typedef struct directory_listing_t directory_listing_t;

// Inline procedures as wrappers for system routines
#if WINDOWS

static inline void semaphore_post(semaphore_handle_t semaphore) {
	ReleaseSemaphore(semaphore, 1, NULL);
}

static inline void semaphore_wait(semaphore_handle_t semaphore) {
	WaitForSingleObject(semaphore, INFINITE);
}

#else // Linux, macOS

static inline void semaphore_post(semaphore_handle_t semaphore) {
	sem_post(semaphore);
}

static inline void semaphore_wait(semaphore_handle_t semaphore) {
	sem_wait(semaphore);
}

#endif

u8* platform_alloc(size_t size);
mem_t* platform_allocate_mem_buffer(size_t capacity);
mem_t* platform_read_entire_file(const char* filename);
u64 file_read_at_offset(void* dest, file_stream_t fp, u64 offset, u64 num_bytes);

int platform_stat(const char* filename, struct stat* st);
file_stream_t file_stream_open_for_reading(const char* filename);
file_stream_t file_stream_open_for_writing(const char* filename);
i64 file_stream_read(void* dest, size_t bytes_to_read, file_stream_t file_stream);
void file_stream_write(void* source, size_t bytes_to_write, file_stream_t file_stream);
i64 file_stream_get_filesize(file_stream_t file_stream);
i64 file_stream_get_pos(file_stream_t file_stream);
bool file_stream_set_pos(file_stream_t file_stream, i64 offset);
void file_stream_close(file_stream_t file_stream);
file_handle_t open_file_handle_for_simultaneous_access(const char* filename);
void file_handle_close(file_handle_t file_handle);
size_t file_handle_read_at_offset(void* dest, file_handle_t file_handle, u64 offset, size_t bytes_to_read);


bool file_exists(const char* filename);
bool is_directory(const char* path);

void get_system_info(bool verbose);

void init_thread_memory(i32 logical_thread_index);

// globals
#if defined(PLATFORM_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern THREAD_LOCAL thread_memory_t* local_thread_memory;
static inline temp_memory_t begin_temp_memory_on_local_thread() { return begin_temp_memory(&local_thread_memory->temp_arena); }

extern int g_argc;
extern const char** g_argv;


extern u32 os_page_size;
extern u64 page_alignment_mask;
extern i32 total_thread_count;
extern i32 worker_thread_count;
extern i32 active_worker_thread_count;
extern i32 physical_cpu_count;
extern i32 logical_cpu_count;
extern bool is_macos;
extern work_queue_t global_completion_queue;

extern bool is_verbose_mode INIT(= false);


#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif

