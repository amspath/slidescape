#pragma once
#include "common.h"
#include "viewer.h"

typedef struct arena_t {
	size_t size;
	u8* base;
	size_t used;
	i32 temp_count;
} arena_t;

typedef struct temporary_memory_t {
	arena_t* arena;
	size_t used;
} temporary_memory_t;

void init_arena(arena_t* arena, size_t size, void* base) {
	*arena = (arena_t) {
			.size = size,
			.base = (u8*) base,
			.used = 0,
			.temp_count = 0,
	};
}

#define push_struct(arena, type) (type*)push_size_((arena), sizeof(type))
#define push_array(arena, count, type) (type*) push_size_((arena), (count)* sizeof(type))
#define push_size(arena, size) push_size_((arena), (size))
void* push_size_(arena_t* arena, size_t size) {
	ASSERT((arena->used + size) <= arena->size);
	void* result = arena->base + arena->used;
	arena->used += size;
	return result;
}

temporary_memory_t begin_temporary_memory(arena_t* arena) {
	temporary_memory_t result = {
			.arena = arena,
			.used = arena->used
	};
	++arena->temp_count;
	return result;
}



typedef struct visible_piece_t {
	// bitmap
	v4f color;
} visible_piece_t;

typedef struct render_group_t {
	u32 max_pushbuffer_size;
	u32 pushbuffer_size;
	u8* pushbuffer_base;

} render_group_t;

typedef struct render_basis_t {

} render_basis_t;


render_group_t* allocate_render_group(arena_t* arena, u32 max_pushbuffer_size) {
	render_group_t* result = push_struct(arena, render_group_t);
	*result = (render_group_t) {
		.pushbuffer_base = (u8*) push_size(arena, max_pushbuffer_size),
	};
}

void* push_render_entry(render_group_t* group, u32 size) {
	void* result = NULL;
	if (group->pushbuffer_size + size < group->max_pushbuffer_size) {
		result = group->pushbuffer_base + group->pushbuffer_size;
	} else {
		panic();
	}
	return result;
}