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
#include "platform.h" // for benaphore

// See:
// https://github.com/SasLuca/rayfork/blob/rayfork-0.9/source/core/rayfork-core.c

enum allocator_mode {
	ALLOCATOR_MODE_UNKNOWN = 0,
	ALLOCATOR_MODE_ALLOC,
	ALLOCATOR_MODE_REALLOC,
	ALLOCATOR_MODE_FREE,
};

typedef struct allocator_t allocator_t;
struct allocator_t {
	void* userdata;
	void* (*proc)(allocator_t* this_allocator, size_t size_to_allocate, u32 mode, void* ptr_to_free_or_realloc);
};


typedef struct block_allocator_item_t block_allocator_item_t;
struct block_allocator_item_t {
	i32 chunk_index;
	i32 block_index;
	block_allocator_item_t* next;
};

typedef struct block_allocator_chunk_t {
	size_t used_blocks;
	u8* memory;
} block_allocator_chunk_t;

typedef struct block_allocator_t {
	size_t block_size;
	i32 chunk_capacity_in_blocks;
	size_t chunk_size;
	i32 chunk_count;
	i32 used_chunks;
	block_allocator_chunk_t* chunks;
	block_allocator_item_t* free_list_storage;
	block_allocator_item_t* free_list;
	i32 free_list_length;
	benaphore_t lock;
	bool is_valid;
} block_allocator_t;

block_allocator_t block_allocator_create(size_t block_size, size_t max_capacity_in_blocks, size_t chunk_size);
void block_allocator_destroy(block_allocator_t* allocator);
void* block_alloc(block_allocator_t* allocator);
void block_free(block_allocator_t* allocator, void* ptr_to_free);

#ifdef __cplusplus
};
#endif
