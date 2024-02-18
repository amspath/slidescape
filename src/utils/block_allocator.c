/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2024  Pieter Valkema

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

#include "block_allocator.h"

/*
void* block_allocator_proc(allocator_t* this_allocator, size_t size_to_allocate, u32 mode, void* ptr_to_free_or_realloc) {
	ASSERT(this_allocator);
	void* result = NULL;
	switch(mode) {
		default: break;
		case ALLOCATOR_MODE_ALLOC: {

		} break;
		case ALLOCATOR_MODE_REALLOC: {

		} break;
		case ALLOCATOR_MODE_FREE: {

		} break;
	}
}
*/

block_allocator_t block_allocator_create(size_t block_size, size_t max_capacity_in_blocks, size_t chunk_size) {
	u64 total_capacity = (u64)block_size * (u64)max_capacity_in_blocks;
	u64 chunk_count = total_capacity / chunk_size;
	u64 chunk_capacity_in_blocks = max_capacity_in_blocks / chunk_count;
	block_allocator_t result = {0};
	result.block_size = block_size;
	result.chunk_capacity_in_blocks = chunk_capacity_in_blocks;
	result.chunk_size = chunk_size;
	ASSERT(chunk_count > 0);
	result.chunk_count = chunk_count;
	result.used_chunks = 1;
	result.chunks = calloc(1, chunk_count * sizeof(block_allocator_chunk_t));
	result.chunks[0].memory = (u8*)malloc(chunk_size);
	result.free_list_storage = calloc(1, max_capacity_in_blocks * sizeof(block_allocator_item_t));
	result.lock = benaphore_create();
	result.is_valid = true;
	return result;
}

void block_allocator_destroy(block_allocator_t* allocator) {
	for (i32 i = 0; i < allocator->used_chunks; ++i) {
		block_allocator_chunk_t* chunk = allocator->chunks + i;
		if (chunk->memory) free(chunk->memory);
	}
	if (allocator->chunks) free(allocator->chunks);
	if (allocator->free_list_storage) free(allocator->free_list_storage);
	benaphore_destroy(&allocator->lock);
	memset(allocator, 0, sizeof(block_allocator_t));
}

void* block_alloc(block_allocator_t* allocator) {
	void* result = NULL;
	benaphore_lock(&allocator->lock);
	if (allocator->free_list != NULL) {
		// Grab a block from the free list
		block_allocator_item_t* free_item = allocator->free_list;
		result = allocator->chunks[free_item->chunk_index].memory + free_item->block_index * allocator->block_size;
		allocator->free_list = free_item->next;
		--allocator->free_list_length;
	} else {
		ASSERT(allocator->used_chunks >= 1);
		i32 chunk_index = allocator->used_chunks-1;
		block_allocator_chunk_t* current_chunk = allocator->chunks + chunk_index;
		if (current_chunk->used_blocks < allocator->chunk_capacity_in_blocks) {
			i32 block_index = current_chunk->used_blocks++;
			result = current_chunk->memory + block_index * allocator->block_size;
		} else {
			// Chunk is full, allocate a new chunk
			if (allocator->used_chunks < allocator->chunk_count) {
//				console_print("block_alloc(): allocating a new chunk\n");
				chunk_index = allocator->used_chunks++;
				current_chunk = allocator->chunks + chunk_index;
				ASSERT(current_chunk->memory == NULL);
				current_chunk->memory = (u8*)malloc(allocator->chunk_size);
				i32 block_index = current_chunk->used_blocks++;
				result = current_chunk->memory + block_index * allocator->block_size;
			} else {
				console_print_error("block_alloc(): out of memory!\n");
				fatal_error();
			}
		}
	}
	benaphore_unlock(&allocator->lock);
	return result;
}

void block_free(block_allocator_t* allocator, void* ptr_to_free) {
	benaphore_lock(&allocator->lock);
	block_allocator_item_t free_item = {0};
	// Find the right chunk
	i32 chunk_index = -1;
	for (i32 i = 0; i < allocator->used_chunks; ++i) {
		block_allocator_chunk_t* chunk = allocator->chunks + i;
		bool match = ((u8*)ptr_to_free >= chunk->memory && (u8*)ptr_to_free < (chunk->memory + allocator->chunk_size));
		if (match) {
			chunk_index = i;
			break;
		}
	}
	if (chunk_index >= 0) {
		block_allocator_chunk_t* chunk = allocator->chunks + chunk_index;
		free_item.next = allocator->free_list;
		free_item.chunk_index = chunk_index;
		free_item.block_index = ((u8*)ptr_to_free - chunk->memory) / allocator->block_size;
		i32 free_index = allocator->free_list_length++;
		allocator->free_list_storage[free_index] = free_item;
		allocator->free_list = allocator->free_list_storage + free_index;
		benaphore_unlock(&allocator->lock);
	} else {
		console_print_error("block_free(): invalid pointer!\n");
		fatal_error();
	}

}
