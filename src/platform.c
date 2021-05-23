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

#if (APPLE || LINUX)
// For async io
#include "aio.h"
#include "errno.h"
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
	FILE* fp = fopen(filename, "rb");
	if (fp) {
		struct stat st;
		if (fstat(fileno(fp), &st) == 0) {
			i64 filesize = st.st_size;
			if (filesize > 0) {
				size_t allocation_size = sizeof(mem_t) + filesize + 1;
				result = (mem_t*) malloc(allocation_size);
				if (result) {
					((u8*)result)[allocation_size-1] = '\0';
					result->len = filesize;
					result->capacity = filesize;
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


u64 file_read_at_offset(void* dest, FILE* fp, u64 offset, u64 num_bytes) {
	fpos_t prev_read_pos = {0}; // NOTE: fpos_t may be a struct!
	int ret = fgetpos64(fp, &prev_read_pos); // for restoring the file position later
	ASSERT(ret == 0); (void)ret;

	fseeko64(fp, offset, SEEK_SET);
	u64 result = fread(dest, num_bytes, 1, fp);

	ret = fsetpos64(fp, &prev_read_pos); // restore previous file position
	ASSERT(ret == 0); (void)ret;

	return result;
}

bool file_exists(const char* filename) {
	return (access(filename, F_OK) != -1);
}

void memrw_maybe_grow(memrw_t* buffer, u64 new_size) {
	if (new_size > buffer->capacity) {
		u64 new_capacity = next_pow2(new_size);
		void* new_ptr = realloc(buffer->data, new_capacity);
		if (!new_ptr) panic();
		buffer->data = new_ptr;
#if DO_DEBUG
		console_print_verbose("memrw_maybe_grow(): expanded buffer size from %u to %u\n", buffer->capacity, new_capacity);
#endif
		buffer->capacity = new_capacity;
	}
}

// TODO: do we actually want this to be a dynamic array, or a read/write stream?
u64 memrw_push_back(memrw_t* buffer, void* data, u64 size) {
	u64 new_size = buffer->used_size + size;
	memrw_maybe_grow(buffer, new_size);
	u64 write_offset = buffer->used_size;
	void* write_pos = buffer->data + write_offset;
	if (data) {
		memcpy(write_pos, data, size);
	} else {
		memset(write_pos, 0, size);
	}
	buffer->used_size += size;
	buffer->cursor = buffer->used_size;
	buffer->used_count += 1;
	return write_offset;
}

void memrw_init(memrw_t* buffer, u64 capacity) {
	memset(buffer, 0, sizeof(*memset));
	buffer->data = (u8*) malloc(capacity);
	buffer->capacity = capacity;
}

memrw_t memrw_create(u64 capacity) {
	memrw_t result = {};
	memrw_init(&result, capacity);
	return result;
}

void memrw_rewind(memrw_t* buffer) {
	buffer->used_size = 0;
	buffer->used_count = 0;
	buffer->cursor = 0;
}

void memrw_seek(memrw_t* buffer, i64 offset) {
	if (offset >= 0 && (i64)offset < buffer->used_size) {
		buffer->cursor = offset;
	} else {
		panic();
	};
}

i64 memrw_write(const void* src, memrw_t* buffer, i64 bytes_to_write) {
	ASSERT(bytes_to_write >= 0);
	memrw_maybe_grow(buffer, buffer->cursor + bytes_to_write);
	i64 bytes_left = buffer->capacity - buffer->cursor;
	if (bytes_left >= 1) {
		bytes_to_write = MIN(bytes_to_write, bytes_left);
		memcpy(buffer->data + buffer->cursor, src, bytes_to_write);
		buffer->cursor += bytes_to_write;
		buffer->used_size = MAX(buffer->cursor, (i64)buffer->used_size);
		return bytes_to_write;
	}
	return 0;
}

i64 memrw_read(void* dest, memrw_t* buffer, size_t bytes_to_read) {
	i64 bytes_left = buffer->used_size - buffer->cursor;
	if (bytes_left >= 1) {
		bytes_to_read = MIN(bytes_to_read, (size_t)bytes_left);
		memcpy(dest, buffer->data + buffer->cursor, bytes_to_read);
		buffer->cursor += bytes_to_read;
		return bytes_to_read;
	}
	return 0;
}

void memrw_destroy(memrw_t* buffer) {
	if (buffer->data) free(buffer->data);
	memset(buffer, 0, sizeof(*buffer));
}

void get_system_info() {
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
    console_print("There are %d logical CPU cores\n", logical_cpu_count);
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
	benaphore_t result = {};
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

#if 0
// Performance considerations for async I/O on Linux:
// https://github.com/littledan/linux-aio#performance-considerations

typedef struct {
	void* dest;
	file_handle_t file;
	i64 offset;
	size_t size_to_read;
#if WINDOWS
	OVERLAPPED overlapped;
#elif (APPLE || LINUX)
	struct aiocb64 cb;
#endif
} io_operation_t;

void async_read_submit(io_operation_t* op) {
#if WINDOWS
	// stub
#elif (APPLE || LINUX)
	memset(&op->cb, 0, sizeof(op->cb));
	op->cb.aio_nbytes = op->size_to_read;
	op->cb.aio_fildes = op->file;
	op->cb.aio_offset = op->offset;
	op->cb.aio_buf = op->dest;

	if (aio_read64(&op->cb) == -1) {
		console_print_error("submit_async_read(): unable to create I/O request\n");
	}
#endif
}

bool async_read_has_finished(io_operation_t* op) {
#if WINDOWS
	// stub
#elif (APPLE || LINUX)
	int status = aio_error64(&op->cb);
	return (status != EINPROGRESS);
#endif
}

i64 async_read_finalize(io_operation_t* op) {
#if WINDOWS
	// stub
#elif (APPLE || LINUX)
	i64 bytes_read = aio_return64(&op->cb);
	return bytes_read;
#endif
}

#endif
