#pragma once

#include "isyntax.h"
#include "benaphore.h"

typedef struct isyntax_tile_list_t {
    isyntax_tile_t* head;
    isyntax_tile_t* tail;
    int count;
    const char* dbg_name;
} isyntax_tile_list_t;

typedef struct isyntax_cache_t {
    isyntax_tile_list_t cache_list;
    benaphore_t mutex;
    // TODO(avirodov): int refcount;
    int target_cache_size;
    block_allocator_t ll_coeff_block_allocator;
    block_allocator_t h_coeff_block_allocator;
    int allocator_block_width;
    int allocator_block_height;
} isyntax_cache_t;

uint32_t* isyntax_read_tile_bgra(isyntax_t* isyntax, isyntax_cache_t* cache, int scale, int tile_x, int tile_y);

void tile_list_init(isyntax_tile_list_t* list, const char* dbg_name);
void tile_list_remove(isyntax_tile_list_t* list, isyntax_tile_t* tile);
