#pragma once

#define TILE_DIM 512
#define BYTES_PER_PIXEL 4
#define TILE_PITCH (TILE_DIM * BYTES_PER_PIXEL)
#define WSI_BLOCK_SIZE (TILE_DIM * TILE_DIM * BYTES_PER_PIXEL)

typedef struct {
	i64 capacity;
	u32 capacity_in_blocks;
	u32 blocks_in_use;
	u8* data;
} slide_memory_t;

typedef struct wsi_t wsi_t;
typedef struct load_tile_task_t load_tile_task_t;
struct load_tile_task_t {
	wsi_t* wsi;
	i32 level;
	i32 tile_x;
	i32 tile_y;
	u32* cached_pixels;
};

typedef struct {
	u32 volatile texture;
	bool32 volatile is_submitted_for_loading;
	bool32 volatile is_loading_complete;
} wsi_tile_t;

typedef struct {
	i64 width;
	i64 height;
	i64 width_in_tiles;
	i64 height_in_tiles;
	i32 num_tiles;
	wsi_tile_t* tiles;
	float um_per_pixel_x;
	float um_per_pixel_y;
	float x_tile_side_in_um;
	float y_tile_side_in_um;
} wsi_level_t;

#define WSI_MAX_LEVELS 16

typedef struct wsi_t {
	i64 width;
	i64 height;
	i64 width_pow2;
	i64 height_pow2;
	i32 num_levels;
	openslide_t* osr;
	const char* barcode;
	float mpp_x;
	float mpp_y;

	wsi_level_t levels[WSI_MAX_LEVELS];
} wsi_t;

void load_wsi(wsi_t* wsi, char* filename);
void unload_wsi(wsi_t* wsi);