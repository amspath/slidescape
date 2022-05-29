/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2022  Pieter Valkema

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
#include "memrw.h"

void memrw_maybe_grow(memrw_t* buffer, u64 new_size) {
	if (new_size > buffer->capacity) {
		u64 new_capacity = next_pow2(new_size);
		void* new_ptr = realloc(buffer->data, new_capacity);
		if (!new_ptr) panic();
		buffer->data = new_ptr;
//#if DO_DEBUG
//		console_print_verbose("memrw_maybe_grow(): expanded buffer size from %u to %u\n", buffer->capacity, new_capacity);
//#endif
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
	memset(buffer, 0, sizeof(*buffer));
	buffer->data = (u8*) malloc(capacity);
	buffer->capacity = capacity;
}

memrw_t memrw_create(u64 capacity) {
	memrw_t result = {0};
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

i64 memrw_putc(i64 c, memrw_t* buffer) {
	return memrw_write(&c, buffer, 1);
}

i64 memrw_write_string(const char* s, memrw_t* buffer) {
	size_t len = strlen(s);
	return memrw_write(s, buffer, len);
}

i64 memrw_printf(memrw_t* buffer, const char* fmt, ...) {
	char buf[4096];
	va_list args;
	va_start(args, fmt);
	i32 ret = vsnprintf(buf, sizeof(buf)-1, fmt, args);
	buf[sizeof(buf)-1] = 0;
	memrw_write_string(buf, buffer);
	va_end(args);
	return ret;
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

