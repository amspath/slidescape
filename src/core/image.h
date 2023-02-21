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

#include "common.h"
#include "mathutils.h"

// backends
#include "openslide_api.h"
#include "tiff.h"
#include "isyntax.h"
#include "dicom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pixel_format_enum {
    PIXEL_FORMAT_UNDEFINED = 0,
    PIXEL_FORMAT_U8_BGRA = 1,
    PIXEL_FORMAT_U8_RGBA = 2,
    PIXEL_FORMAT_F32_Y = 3,
} pixel_format_enum;

#define WSI_TILE_DIM 512

typedef struct image_t image_t;

typedef struct {
    i64 width;
    i64 height;
    i64 width_in_tiles;
    i64 height_in_tiles;
    u32 tile_width;
    u32 tile_height;
    i32 tile_count;
    float um_per_pixel_x;
    float um_per_pixel_y;
    float x_tile_side_in_um;
    float y_tile_side_in_um;
    i32 downsample_level;
    float downsample_factor;
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
    bool is_mpp_known;
    i32 max_downsample_level;
    u32 tile_width;
    u32 tile_height;

    wsi_level_t levels[WSI_MAX_LEVELS];
} wsi_t;





#define IMAGE_PYRAMID_MAX_LEVELS 16

typedef enum {
    IMAGE_TYPE_NONE,
//	IMAGE_TYPE_SIMPLE,
//	IMAGE_TYPE_TIFF,
    IMAGE_TYPE_WSI,
} image_type_enum;

typedef enum {
    IMAGE_BACKEND_NONE,
    IMAGE_BACKEND_STBI,
    IMAGE_BACKEND_TIFF,
    IMAGE_BACKEND_OPENSLIDE,
    IMAGE_BACKEND_ISYNTAX,
    IMAGE_BACKEND_DICOM,
} image_backend_enum;

typedef struct tile_t {
    u32 tile_index;
    i32 tile_x;
    i32 tile_y;
    u8* pixels;
    u32 texture;
    bool8 is_submitted_for_loading;
    bool8 is_empty;
    bool8 is_cached;
    bool8 need_keep_in_cache;
    bool8 need_gpu_residency; // TODO: revise: still needed?
    i64 time_last_drawn;
} tile_t;

typedef struct cached_tile_t {
    i32 tile_width;
    u8* pixels;
} cached_tile_t;

typedef struct {
    i64 width_in_pixels;
    i64 height_in_pixels;
    tile_t* tiles;
    u64 tile_count;
    u32 width_in_tiles;
    u32 height_in_tiles;
    u32 tile_width;
    u32 tile_height;
    float x_tile_side_in_um;
    float y_tile_side_in_um;
    float um_per_pixel_x;
    float um_per_pixel_y;
    float downsample_factor;
    v2f origin_offset;
    i32 pyramid_image_index;
    bool exists;
    bool needs_indexing; //TODO: implement
    bool indexing_job_submitted;
} level_image_t;

typedef struct simple_image_t {
    i32 channels_in_file;
    i32 channels;
    i32 width;
    i32 height;
    u8* pixels;
    u32 texture;
    float mpp;
    v2f world_pos;
    bool is_valid;
} simple_image_t;

typedef struct image_t {
    char name[512];
    char directory[512];
    bool is_local; // i.e. not remote (accessed over network using client/server interface)
    image_type_enum type;
    image_backend_enum backend;
    bool is_freshly_loaded; // TODO: remove or refactor, is this still needed?
    bool is_valid;
    bool is_enabled;
    bool is_overlay;
    union {
        simple_image_t simple;
        tiff_t tiff;
        isyntax_t isyntax;
        wsi_t openslide_wsi;
        dicom_series_t dicom;
    };
    i32 level_count;
    u32 tile_width;
    u32 tile_height;
    level_image_t level_images[IMAGE_PYRAMID_MAX_LEVELS];
    float mpp_x;
    float mpp_y;
    bool is_mpp_known;
    i64 width_in_pixels;
    float width_in_um;
    i64 height_in_pixels;
    float height_in_um;
    v2f origin_offset;
    simple_image_t macro_image;
    simple_image_t label_image;
    i32 resource_id;
	i32 refcount;
	benaphore_t lock;
} image_t;


static inline tile_t* get_tile(level_image_t* image_level, i32 tile_x, i32 tile_y) {
	i32 tile_index = tile_y * image_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < image_level->tile_count);
	tile_t* result = image_level->tiles + tile_index;
	return result;
}

static inline tile_t* get_tile_from_tile_index(image_t* image, i32 scale, i32 tile_index) {
	ASSERT(image);
	ASSERT(scale < image->level_count);
	level_image_t* level_image = image->level_images + scale;
	tile_t* tile = level_image->tiles + tile_index;
	return tile;
}

static inline u32 get_texture_for_tile(image_t* image, i32 level, i32 tile_x, i32 tile_y) {
	level_image_t* level_image = image->level_images + level;

	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < level_image->tile_count);
	tile_t* tile = level_image->tiles + tile_index;

	return tile->texture;
}

float f32_rgb_to_f32_y(float R, float G, float B);
void image_convert_u8_rgba_to_f32_y(u8* src, float* dest, i32 w, i32 h, i32 components);
void tile_release_cache(tile_t* tile);
const char* get_image_backend_name(image_t* image);
const char* get_image_descriptive_type_name(image_t* image);
bool init_image_from_tiff(image_t* image, tiff_t tiff, bool is_overlay, image_t* parent_image);
bool init_image_from_isyntax(image_t* image, isyntax_t* isyntax, bool is_overlay);
bool init_image_from_dicom(image_t* image, dicom_series_t* dicom, bool is_overlay);
bool init_image_from_stbi(image_t* image, simple_image_t* simple, bool is_overlay);
void init_image_from_openslide(image_t* image, wsi_t* wsi, bool is_overlay);
bool image_read_region(image_t* image, i32 level, i32 x, i32 y, i32 w, i32 h, void* dest, pixel_format_enum desired_pixel_format);
void begin_level_image_indexing(image_t* image, level_image_t* level_image, i32 scale);
void image_destroy(image_t* image);

#ifdef __cplusplus
}
#endif
