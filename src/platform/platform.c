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

#define FATAL_ERROR_IMPLEMENTATION
#define STB_DS_IMPLEMENTATION
#include "common.h"

#define PLATFORM_IMPL
#include "platform.h"
#include "intrinsics.h"

#if APPLE
#include <sys/sysctl.h> // for sysctlbyname()
#endif



mem_t* platform_allocate_mem_buffer(size_t capacity) {
	size_t allocation_size = sizeof(mem_t) + capacity + 1;
	mem_t* result = (mem_t*) malloc(allocation_size);
	result->len = 0;
	result->capacity = capacity;
    result->cursor = 0;
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
                result->cursor = 0;
				size_t bytes_read = file_stream_read(result->data, filesize, fp);
				if (bytes_read != filesize) {
					fatal_error();
				}
			}
		}
		file_stream_close(fp);
	}
	return result;
}

i64 mem_write(void* src, mem_t* mem, size_t bytes_to_write) {
    i32 bytes_left = mem->capacity - mem->cursor;
    if (bytes_left >= 1) {
        bytes_to_write = MIN(bytes_to_write, (size_t)bytes_left);
        memcpy(mem->data + mem->cursor, src, bytes_to_write);
        mem->cursor += bytes_to_write;
        mem->len = MAX(mem->cursor, (i64)mem->len);
        return bytes_to_write;
    }
    return 0;
}

i64 mem_read(void* dest, mem_t* mem, size_t bytes_to_read) {
    i64 bytes_left = mem->len - mem->cursor;
    if (bytes_left >= 1) {
        bytes_to_read = MIN(bytes_to_read, (size_t)bytes_left);
        memcpy(dest, mem->data + mem->cursor, bytes_to_read);
        mem->cursor += bytes_to_read;
        return bytes_to_read;
    }
    return 0;
}

void mem_seek(mem_t* mem, i32 offset) {
    if (offset >= 0 && (u32)offset < mem->len) {
        mem->cursor = offset;
    } else {
        fatal_error();
    };
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
    system_info_t system_info = {0};
#if WINDOWS
    SYSTEM_INFO win32_system_info;
    GetSystemInfo(&win32_system_info);
	system_info.logical_cpu_count = (i32)win32_system_info.dwNumberOfProcessors;
	system_info.physical_cpu_count = system_info.logical_cpu_count; // TODO(pvalkema): how to read this on Windows?
	system_info.os_page_size = win32_system_info.dwPageSize;
#elif APPLE
    size_t physical_cpu_count_len = sizeof(system_info.physical_cpu_count);
	size_t logical_cpu_count_len = sizeof(system_info.logical_cpu_count);
	sysctlbyname("hw.physicalcpu", &system_info.physical_cpu_count, &physical_cpu_count_len, NULL, 0);
	sysctlbyname("hw.logicalcpu", &system_info.logical_cpu_count, &logical_cpu_count_len, NULL, 0);
    system_info.os_page_size = (u32) getpagesize();
    system_info.page_alignment_mask = ~((u64)(sysconf(_SC_PAGE_SIZE) - 1));
    system_info.is_macos = true;
#elif LINUX
    system_info.logical_cpu_count = sysconf( _SC_NPROCESSORS_ONLN );
    system_info.physical_cpu_count = system_info.logical_cpu_count; // TODO(pvalkema): how to read this on Linux?
    system_info.os_page_size = (u32) getpagesize();
    system_info.page_alignment_mask = ~((u64)(sysconf(_SC_PAGE_SIZE) - 1));
#endif
    if (verbose) console_print("There are %d logical CPU cores\n", system_info.logical_cpu_count);
    system_info.suggested_total_thread_count = MIN(system_info.logical_cpu_count, MAX_THREAD_COUNT);

    //TODO(pvalkema): think about returning this instead of setting global state.
    global_system_info = system_info;
}


void init_thread_memory(i32 logical_thread_index, system_info_t* system_info) {
	// Allocate a private memory buffer
	u64 thread_memory_size = MEGABYTES(16);
	local_thread_memory = (thread_memory_t*) malloc(thread_memory_size); // how much actually needed?
	thread_memory_t* thread_memory = local_thread_memory;
	memset(thread_memory, 0, sizeof(thread_memory_t));
#if !WINDOWS
	// TODO(pvalkema): think about whether implement creation of async I/O events is needed here
#endif
	thread_memory->thread_memory_raw_size = thread_memory_size;

    u32 os_page_size = system_info->os_page_size;
	thread_memory->aligned_rest_of_thread_memory = (void*)
			((((u64)thread_memory + sizeof(thread_memory_t) + os_page_size - 1) / os_page_size) * os_page_size); // round up to next page boundary
	thread_memory->thread_memory_usable_size = thread_memory_size - ((u64)thread_memory->aligned_rest_of_thread_memory - (u64)thread_memory);
	init_arena(&thread_memory->temp_arena, thread_memory->thread_memory_usable_size, thread_memory->aligned_rest_of_thread_memory);

}


