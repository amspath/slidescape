/*
  Slidescape, a whole-slide image viewer for digital pathology.
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

#pragma once

typedef struct arena_t {
	size_t size;
	u8* base;
	size_t used;
	i32 temp_count;
} arena_t;

typedef struct temp_memory_t {
	arena_t* arena;
	size_t used;
	i32 temp_index;
} temp_memory_t;

static inline void init_arena(arena_t* arena, size_t size, void* base) {
	*arena = (arena_t) {
			.size = size,
			.base = (u8*) base,
			.used = 0,
			.temp_count = 0,
	};
}

static inline void* arena_current_pos(arena_t* arena) {
	return arena->base + arena->used;
}

#define arena_push_struct(arena, type) ((type*)arena_push_size((arena), sizeof(type)))
#define arena_push_array(arena, count, type) ((type*) arena_push_size((arena), (count)* sizeof(type)))
static inline void* arena_push_size(arena_t* arena, size_t size) {
	ASSERT((arena->used + size) <= arena->size);
	void* result = arena->base + arena->used;
	arena->used += size;
	return result;
}

static inline void arena_align(arena_t* arena, u32 alignment) {
	ASSERT(alignment > 0);
	i64 pos = (i64)arena->base + arena->used;
	i64 new_pos = ((pos + alignment - 1) / alignment) * alignment;
	arena->used += (new_pos - pos);
}

static inline temp_memory_t begin_temp_memory(arena_t* arena) {
	temp_memory_t result = {
			.arena = arena,
			.used = arena->used,
			.temp_index = arena->temp_count,
	};
	++arena->temp_count;
	return result;
}

static inline void end_temp_memory(temp_memory_t* temp) {
	ASSERT(temp->arena->temp_count > 0);
	temp->arena->temp_count--;
	temp->arena->used = temp->used;
	ASSERT(temp->temp_index == temp->arena->temp_count);
}
