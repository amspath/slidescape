/*
  BSD 2-Clause License

  Copyright (c) 2019-2026, Pieter Valkema

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
    bool is_growing_disallowed;
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
