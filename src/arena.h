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

#define arena_push_struct(arena, type) ((type*)push_size_((arena), sizeof(type)))
#define arena_push_array(arena, count, type) ((type*) push_size_((arena), (count)* sizeof(type)))
#define arena_push_size(arena, size) push_size_((arena), (size))
static inline void* push_size_(arena_t* arena, size_t size) {
	ASSERT((arena->used + size) <= arena->size);
	void* result = arena->base + arena->used;
	arena->used += size;
	return result;
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
