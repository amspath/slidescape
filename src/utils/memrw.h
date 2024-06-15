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

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef struct memrw_t {
	u8* data;
	i64 cursor;
	u64 used_size;
	u64 used_count;
	u64 capacity;
} memrw_t;


void memrw_maybe_grow(memrw_t* buffer, u64 new_size);
u64 memrw_push_back(memrw_t* buffer, void* data, u64 size);
void memrw_init(memrw_t* buffer, u64 capacity);
memrw_t memrw_create(u64 capacity);
void memrw_rewind(memrw_t* buffer);
void memrw_seek(memrw_t* buffer, i64 offset);
i64 memrw_write(const void* src, memrw_t* buffer, i64 bytes_to_write);
i64 memrw_putc(i64 c, memrw_t* buffer);
i64 memrw_write_string(const char* s, memrw_t* buffer);
i64 memrw_write_string_urlencode(const char* s, memrw_t* buffer);
i64 memrw_string_pool_push(memrw_t* buffer, const char* s);
i64 memrw_printf(memrw_t* buffer, const char* fmt, ...);
#define memrw_write_literal(s, buffer) memrw_write((s), (buffer), COUNT(s)-1)
i64 memrw_read(void* dest, memrw_t* buffer, size_t bytes_to_read);
void memrw_destroy(memrw_t* buffer);

#ifdef __cplusplus
};
#endif
