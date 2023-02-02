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

#define PANIC_IMPLEMENTATION
#define STB_DS_IMPLEMENTATION
#include "common.h"

#define PLATFORM_IMPL
#include "platform.h"
#include "intrinsics.h"

#if !IS_SERVER
#include "work_queue.c"
#endif

#if APPLE
#include <sys/sysctl.h> // for sysctlbyname()
#endif

#if WINDOWS
#include "win32_platform.c"
#else
#include "linux_platform.c"
#endif


mem_t* platform_allocate_mem_buffer(size_t capacity) {
	size_t allocation_size = sizeof(mem_t) + capacity + 1;
	mem_t* result = (mem_t*) malloc(allocation_size);
	result->len = 0;
	result->capacity = capacity;
	return result;
}

mem_t* platform_read_entire_file(const char* filename) {
	mem_t* result = NULL;
	file_stream_t fp = file_stream_open_for_reading(filename);
	if (fp) {
		i64 filesize = file_stream_get_filesize(fp);
		if (filesize > 0) {
			size_t allocation_size = sizeof(mem_t) + filesize + 1;
			result = (mem_t*) malloc(allocation_size);
			if (result) {
				((u8*)result)[allocation_size-1] = '\0';
				result->len = filesize;
				result->capacity = filesize;
				size_t bytes_read = file_stream_read(result->data, filesize, fp);
				if (bytes_read != filesize) {
					panic();
				}
			}
		}
		file_stream_close(fp);
	}
	return result;
}


u64 file_read_at_offset(void* dest, file_stream_t fp, u64 offset, u64 num_bytes) {
	i64 prev_read_pos = file_stream_get_pos(fp);
	file_stream_set_pos(fp, offset);
	u64 result = file_stream_read(dest, num_bytes, fp);
	file_stream_set_pos(fp, prev_read_pos);
	return result;
}

bool file_exists(const char* filename) {
	return (access(filename, F_OK) != -1);
}

bool is_directory(const char* path) {
	struct stat st = {0};
	platform_stat(path, &st);
	return S_ISDIR(st.st_mode);
}


void get_system_info(bool verbose) {
#if WINDOWS
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
	logical_cpu_count = (i32)system_info.dwNumberOfProcessors;
	physical_cpu_count = logical_cpu_count; // TODO: how to read this on Windows?
	os_page_size = system_info.dwPageSize;
#elif APPLE
    size_t physical_cpu_count_len = sizeof(physical_cpu_count);
	size_t logical_cpu_count_len = sizeof(logical_cpu_count);
	sysctlbyname("hw.physicalcpu", &physical_cpu_count, &physical_cpu_count_len, NULL, 0);
	sysctlbyname("hw.logicalcpu", &logical_cpu_count, &logical_cpu_count_len, NULL, 0);
	os_page_size = (u32) getpagesize();
	page_alignment_mask = ~((u64)(sysconf(_SC_PAGE_SIZE) - 1));
	is_macos = true;
#elif LINUX
    logical_cpu_count = sysconf( _SC_NPROCESSORS_ONLN );
    physical_cpu_count = logical_cpu_count; // TODO: how to read this on Linux?
    os_page_size = (u32) getpagesize();
    page_alignment_mask = ~((u64)(sysconf(_SC_PAGE_SIZE) - 1));
#endif
    if (verbose) console_print("There are %d logical CPU cores\n", logical_cpu_count);
    total_thread_count = MIN(logical_cpu_count, MAX_THREAD_COUNT);
}


#if !IS_SERVER

//TODO: move this
bool profiling = false;

i64 profiler_end_section(i64 start, const char* name, float report_threshold_ms) {
	i64 end = get_clock();
	if (profiling) {
		float ms_elapsed = get_seconds_elapsed(start, end) * 1000.0f;
		if (ms_elapsed > report_threshold_ms) {
			console_print("[profiler] %s: %g ms\n", name, ms_elapsed);
		}
	}
	return end;
}
#endif

// Based on:
// https://preshing.com/20120226/roll-your-own-lightweight-mutex/


benaphore_t benaphore_create(void) {
	benaphore_t result = {0};
#if WINDOWS
	result.semaphore = CreateSemaphore(NULL, 0, 1, NULL);
#elif (APPLE || LINUX)
	static i32 counter = 1;
	char semaphore_name[64];
	i32 c = atomic_increment(&counter);
	snprintf(semaphore_name, sizeof(semaphore_name)-1, "/benaphore%d", c);
	result.semaphore = sem_open(semaphore_name, O_CREAT, 0644, 0);
#endif
	return result;
}

void benaphore_destroy(benaphore_t* benaphore) {
#if WINDOWS
	CloseHandle(benaphore->semaphore);
#elif (APPLE || LINUX)
	sem_close(benaphore->semaphore);
#endif
}

void benaphore_lock(benaphore_t* benaphore) {
	if (atomic_increment(&benaphore->counter) > 1) {
		semaphore_wait(benaphore->semaphore);
	}
}

void benaphore_unlock(benaphore_t* benaphore) {
	if (atomic_decrement(&benaphore->counter) > 0) {
		semaphore_post(benaphore->semaphore);
	}
}

// Performance considerations for async I/O on Linux:
// https://github.com/littledan/linux-aio#performance-considerations

// TODO: aiocb64 doesn't exist on macOS? need #defines?



/*void async_read_submit(io_operation_t* op) {
#if WINDOWS
	// stub
#elif (APPLE || LINUX)
	memset(&op->cb, 0, sizeof(op->cb));
	op->cb.aio_nbytes = op->size_to_read;
	op->cb.aio_fildes = op->file;
	op->cb.aio_offset = op->offset;
	op->cb.aio_buf = op->dest;

	if (aio_read(&op->cb) == -1) {
		console_print_error("submit_async_read(): unable to create I/O request\n");
	}
#endif
}

bool async_read_has_finished(io_operation_t* op) {
#if WINDOWS
	// stub
#elif (APPLE || LINUX)
	int status = aio_error(&op->cb);
	return (status != EINPROGRESS);
#endif
}

i64 async_read_finalize(io_operation_t* op) {
#if WINDOWS
	// stub
#elif (APPLE || LINUX)
	i64 bytes_read = aio_return(&op->cb);
	return bytes_read;
#endif
}*/





void init_thread_memory(i32 logical_thread_index) {
	// Allocate a private memory buffer
	u64 thread_memory_size = MEGABYTES(16);
	local_thread_memory = (thread_memory_t*) platform_alloc(thread_memory_size); // how much actually needed?
	thread_memory_t* thread_memory = local_thread_memory;
	memset(thread_memory, 0, sizeof(thread_memory_t));
#if !WINDOWS
	// TODO: implement creation of async I/O events
#endif
	thread_memory->thread_memory_raw_size = thread_memory_size;

	thread_memory->aligned_rest_of_thread_memory = (void*)
			((((u64)thread_memory + sizeof(thread_memory_t) + os_page_size - 1) / os_page_size) * os_page_size); // round up to next page boundary
	thread_memory->thread_memory_usable_size = thread_memory_size - ((u64)thread_memory->aligned_rest_of_thread_memory - (u64)thread_memory);
	init_arena(&thread_memory->temp_arena, thread_memory->thread_memory_usable_size, thread_memory->aligned_rest_of_thread_memory);

}


