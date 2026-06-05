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
#include "platform.h"
#include "image.h"

typedef enum tile_loader_completion_event_kind_t {
	TILE_LOADER_COMPLETION_EVENT_NONE = 0,
	TILE_LOADER_COMPLETION_EVENT_TILE_LOADED,
	TILE_LOADER_COMPLETION_EVENT_UPLOAD_CACHED_TILE,
} tile_loader_completion_event_kind_t;

typedef enum load_tile_error_code_enum {
	LOAD_TILE_SUCCESS,
	LOAD_TILE_EMPTY,
	LOAD_TILE_READ_LOCAL_FAILED,
	LOAD_TILE_READ_REMOTE_FAILED,
} load_tile_error_code_enum;

typedef struct load_tile_task_t {
	i32 resource_id;
	image_t* image;
	tile_t* tile;
	i32 level;
	i32 tile_x;
	i32 tile_y;
	i32 priority;
	bool8 need_gpu_residency;
	bool8 need_cpu_residency;
	bool8 invert_colors;
	completion_queue_t* completion_queue;
	completion_event_kind_t completion_event_kind;
	task_group_t* task_group;
	i32 refcount_to_decrement;
} load_tile_task_t;

typedef struct tile_load_completion_task_t {
	u8* pixel_memory;
	i32 scale;
	i32 tile_index;
	i32 tile_width;
	i32 tile_height;
	i32 resource_id;
	bool need_gpu_residency;
	bool need_cpu_residency;
	bool is_empty;
	bool failed;
} tile_load_completion_task_t;

#define TILE_LOAD_BATCH_MAX 8

typedef struct load_tile_task_batch_t {
	i32 task_count;
	load_tile_task_t tile_tasks[TILE_LOAD_BATCH_MAX];
} load_tile_task_batch_t;

void load_tile_func(i32 logical_thread_index, void* userdata);
i32 request_tiles(image_t* image, load_tile_task_t* wishlist, i32 tiles_to_load);
void tile_loader_set_remote_tiff_batch_callback(work_queue_callback_t* callback);
void tile_loader_set_slide_score_batch_callback(work_queue_callback_t* callback);

#ifdef __cplusplus
}
#endif
