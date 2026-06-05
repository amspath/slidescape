/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2026  Pieter Valkema

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
#include "image.h"
#include "platform.h"

typedef enum tile_cache_request_state_enum {
	TILE_CACHE_UNREQUESTED,
	TILE_CACHE_QUEUED,
	TILE_CACHE_IN_FLIGHT,
	TILE_CACHE_READY,
	TILE_CACHE_FAILED,
} tile_cache_request_state_enum;

typedef enum tile_cache_demand_flags_enum {
	TILE_CACHE_DEMAND_VIEWER_VISIBLE = 1 << 0,
	TILE_CACHE_DEMAND_VIEWER_PREFETCH = 1 << 1,
	TILE_CACHE_DEMAND_READ_REGION = 1 << 2,
	TILE_CACHE_DEMAND_EXPORT = 1 << 3,
	TILE_CACHE_DEMAND_REGISTRATION = 1 << 4,
	TILE_CACHE_DEMAND_CPU_RESIDENCY = 1 << 5,
	TILE_CACHE_DEMAND_GPU_RESIDENCY = 1 << 6,
} tile_cache_demand_flags_enum;

typedef struct tile_cache_tile_t {
	u8 request_state;
	u32 demand_mask;
	u32 cpu_pin_count;
	u32 gpu_pin_count;
	bool8 cpu_resident;
	bool8 gpu_resident;
	bool8 decode_in_flight;
	bool8 upload_pending;
	bool8 is_empty;
	u8* pixels;
	renderer_texture_handle_t texture;
	i32 generation;
	i32 priority;
	i64 last_cpu_access_time;
	i64 last_gpu_access_time;
} tile_cache_tile_t;

typedef struct tile_cache_t {
	image_t* image;
	platform_mutex_t lock;
	bool8 lock_initialized;
	tile_cache_tile_t* level_tiles[IMAGE_PYRAMID_MAX_LEVELS];
} tile_cache_t;

tile_cache_t* tile_cache_create(image_t* image);
tile_cache_t* tile_cache_get_or_create(image_t* image);
void tile_cache_destroy(tile_cache_t* cache);
tile_cache_tile_t* tile_cache_get_tile_state(image_t* image, i32 level, i32 tile_index);
void tile_cache_pin_cpu_tile(image_t* image, i32 level, i32 tile_index, u32 demand_flags);
void tile_cache_unpin_cpu_tile(image_t* image, i32 level, i32 tile_index, u32 demand_flags);
bool tile_cache_tile_has_cpu_pixels(image_t* image, i32 level, i32 tile_index);
bool tile_cache_tile_is_cpu_pinned(image_t* image, i32 level, i32 tile_index);
bool tile_cache_tile_is_busy(image_t* image, i32 level, i32 tile_index);
bool tile_cache_try_begin_decode(image_t* image, i32 level, i32 tile_index, u32 demand_flags, i32 priority);
bool tile_cache_try_begin_upload(image_t* image, i32 level, i32 tile_index, u32 demand_flags, i32 priority);
void tile_cache_cancel_decode(image_t* image, i32 level, i32 tile_index);
void tile_cache_cancel_upload(image_t* image, i32 level, i32 tile_index);
void tile_cache_mark_decode_finished(image_t* image, i32 level, i32 tile_index, bool failed);
void tile_cache_mark_upload_pending(image_t* image, i32 level, i32 tile_index);
void tile_cache_mark_upload_finished(image_t* image, i32 level, i32 tile_index);
renderer_texture_handle_t tile_cache_get_gpu_texture(image_t* image, i32 level, i32 tile_index);
void tile_cache_store_gpu_texture(image_t* image, i32 level, i32 tile_index, renderer_texture_handle_t texture);
renderer_texture_handle_t tile_cache_take_gpu_texture(image_t* image, i32 level, i32 tile_index);
u8* tile_cache_get_cpu_pixels(image_t* image, i32 level, i32 tile_index);
void tile_cache_store_cpu_pixels(image_t* image, i32 level, i32 tile_index, u8* pixels);
void tile_cache_release_cpu_pixels_if_unpinned(image_t* image, i32 level, i32 tile_index);

#ifdef __cplusplus
}
#endif
