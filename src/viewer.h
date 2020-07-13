/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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

#define SLIDEVIEWER_VERSION "0.16"


#include "common.h"
#include "mathutils.h"

#include "arena.h"
#include "tiff.h"
#include "openslide_api.h"
#include "caselist.h"
#include "annotation.h"

typedef struct texture_t {
	u32 texture;
	i32 width;
	i32 height;
} texture_t;


#define TILE_DIM 512
#define BYTES_PER_PIXEL 4
#define TILE_PITCH (TILE_DIM * BYTES_PER_PIXEL)
#define WSI_BLOCK_SIZE (TILE_DIM * TILE_DIM * BYTES_PER_PIXEL)

typedef struct {
	i64 width;
	i64 height;
	i64 width_in_tiles;
	i64 height_in_tiles;
	i32 tile_count;
	float um_per_pixel_x;
	float um_per_pixel_y;
	float x_tile_side_in_um;
	float y_tile_side_in_um;
} wsi_level_t;

#define WSI_MAX_LEVELS 16

typedef struct wsi_t {
	i64 width;
	i64 height;
	i32 level_count;
	openslide_t* osr;
	const char* barcode;
	float mpp_x;
	float mpp_y;

	wsi_level_t levels[WSI_MAX_LEVELS];
} wsi_t;

typedef enum {
	IMAGE_TYPE_SIMPLE,
	IMAGE_TYPE_TIFF,
	IMAGE_TYPE_WSI,
} image_type_enum;


typedef struct tile_t {
	u32 texture;
	bool32 is_submitted_for_loading;
	bool32 is_empty;
} tile_t;

typedef struct {
	tile_t* tiles;
	u64 tile_count;
	u32 width_in_tiles;
	u32 height_in_tiles;
	float x_tile_side_in_um;
	float y_tile_side_in_um;
	float um_per_pixel_x;
	float um_per_pixel_y;
} level_image_t;

typedef struct {
	image_type_enum type;
	bool32 is_freshly_loaded; // TODO: remove or refactor, is this still needed?
	union {
		struct {
			i32 channels_in_file;
			i32 channels;
			i32 width;
			i32 height;
			u8* pixels;
			u32 texture;
		} simple;
		struct {
			tiff_t tiff;
		} tiff;
		struct {
			wsi_t wsi;
		} wsi;
	};
	i32 level_count;
	level_image_t* level_images;
	float mpp_x;
	float mpp_y;
	i64 width_in_pixels;
	i64 width_in_um;
	i64 height_in_pixels;
	i64 height_in_um;
} image_t;

typedef struct load_tile_task_t {
	image_t* image;
	tile_t* tile;
	i32 level;
	i32 tile_x;
	i32 tile_y;
	i32 priority;
} load_tile_task_t;

#define TILE_LOAD_BATCH_MAX 8

typedef struct load_tile_task_batch_t {
	i32 task_count;
	load_tile_task_t tile_tasks[TILE_LOAD_BATCH_MAX];
} load_tile_task_batch_t;


enum entity_type_enum {
	ENTITY_SIMPLE_IMAGE = 1,
	ENTITY_TILED_IMAGE = 2,
};

typedef struct entity_t {
	u32 type;
	v2f pos;
	union {
		struct {
			image_t* image;
		} simple_image;
		struct {
			image_t* image;
		} tiled_image;
	};
} entity_t;

#define MAX_ENTITIES 1000

typedef struct scene_t {
	rect2i viewport;
	v2f camera;
	//v2f camera_min;
	v2f mouse;
	float pixel_width;
	float pixel_height;
	float zoom_position;
	i32 current_level;
	v4f clear_color;
	u32 entity_count;
	entity_t entities[MAX_ENTITIES];
	annotation_set_t annotation_set;
	bool8 is_dragging; // if mouse down: is this scene being dragged?
	v2i cumulative_drag_vector;
	bool8 initialized;
} scene_t;

typedef struct app_state_t {
	u8* temp_storage_memory;
	arena_t temp_arena;
	rect2i client_viewport;
	scene_t scene;
	v4f clear_color;
	float black_level;
	float white_level;
	image_t* loaded_images; // sb
	i32 displayed_image;
	caselist_t caselist;
	case_t* selected_case;
	bool use_builtin_tiff_backend;
	bool use_image_adjustments;
	bool initialized;
	bool allow_idling_next_frame;
} app_state_t;


//  prototypes
void unload_all_images(app_state_t* app_state);
void reset_scene(image_t* image, scene_t* scene);
void add_image_from_tiff(app_state_t* app_state, tiff_t tiff);
bool32 load_generic_file(app_state_t* app_state, const char* filename);
bool32 load_image_from_file(app_state_t* app_state, const char* filename);
void load_wsi(wsi_t* wsi, const char* filename);
void unload_wsi(wsi_t* wsi);
i32 tile_pos_from_world_pos(float world_pos, float tile_side);
bool32 was_button_pressed(button_state_t* button);
bool32 was_button_released(button_state_t* button);
bool32 was_key_pressed(input_t* input, i32 keycode);
bool32 is_key_down(input_t* input, i32 keycode);
void init_scene(app_state_t *app_state, scene_t *scene);
void init_app_state(app_state_t* app_state);
void autosave(app_state_t* app_state, bool force_ignore_delay);
void viewer_update_and_render(app_state_t* app_state, input_t* input, i32 client_width, i32 client_height, float delta_t);

void init_opengl_stuff();


// globals
#if defined(VIEWER_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern app_state_t global_app_state;

#undef INIT
#undef extern


#ifdef __cplusplus
}
#endif

