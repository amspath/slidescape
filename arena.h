#pragma once

typedef struct arena_t {
	size_t size;
	u8* base;
	size_t used;
	i32 temp_count;
} arena_t;

typedef struct temporary_memory_t {
	arena_t* arena;
	size_t used;
} temp_memory_t;

static void init_arena(arena_t* arena, size_t size, void* base) {
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
static void* push_size_(arena_t* arena, size_t size) {
	ASSERT((arena->used + size) <= arena->size);
	void* result = arena->base + arena->used;
	arena->used += size;
	return result;
}

static temp_memory_t begin_temp_memory(arena_t* arena) {
	temp_memory_t result = {
			.arena = arena,
			.used = arena->used
	};
	++arena->temp_count;
	return result;
}

static void end_temp_memory(temp_memory_t* temp) {
	temp->arena->temp_count--;
}
