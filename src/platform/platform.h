/*
  BSD 2-Clause License

  Copyright (c) 2019-2024, Pieter Valkema

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
	work_queue_t* high_priority_queue;
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

typedef struct system_info_t {
    u32 os_page_size;
    u64 page_alignment_mask;
    i32 physical_cpu_count;
    i32 logical_cpu_count;
    i32 suggested_total_thread_count;
    bool is_macos;
} system_info_t;


typedef struct directory_listing_t directory_listing_t;

// Inline procedures as wrappers for system routines
#if WINDOWS

static inline void platform_semaphore_post(semaphore_handle_t semaphore) {
	ReleaseSemaphore(semaphore, 1, NULL);
}

static inline void platform_semaphore_wait(semaphore_handle_t semaphore) {
	WaitForSingleObject(semaphore, INFINITE);
}

#else // Linux, macOS

static inline void platform_semaphore_post(semaphore_handle_t semaphore) {
	sem_post(semaphore);
}

static inline void platform_semaphore_wait(semaphore_handle_t semaphore) {
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

void init_thread_memory(i32 logical_thread_index, system_info_t* system_info);

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
extern system_info_t global_system_info;
extern i32 global_worker_thread_count;
extern i32 global_active_worker_thread_count;
extern work_queue_t global_completion_queue;

extern bool is_verbose_mode INIT(= false);


#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif

