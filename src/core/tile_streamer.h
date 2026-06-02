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
#include "tile_loader.h"

typedef enum tile_stream_request_state_enum {
	TILE_STREAM_UNREQUESTED,
	TILE_STREAM_QUEUED,
	TILE_STREAM_IN_FLIGHT,
	TILE_STREAM_READY,
	TILE_STREAM_FAILED,
} tile_stream_request_state_enum;

typedef enum tile_stream_demand_flags_enum {
	TILE_STREAM_DEMAND_VIEWER_VISIBLE = 1 << 0,
	TILE_STREAM_DEMAND_VIEWER_PREFETCH = 1 << 1,
	TILE_STREAM_DEMAND_READ_REGION = 1 << 2,
	TILE_STREAM_DEMAND_EXPORT = 1 << 3,
	TILE_STREAM_DEMAND_REGISTRATION = 1 << 4,
} tile_stream_demand_flags_enum;

typedef struct tile_stream_tile_state_t {
	u8 request_state;
	i32 generation;
	i32 priority;
	i32 last_wanted_generation;
	i64 last_requested_time;
	i64 last_completed_time;
	u32 active_pin_count;
	u32 demand_mask;
	bool8 stale_ok_to_cancel;
} tile_stream_tile_state_t;

typedef struct tile_streamer_policy_t {
	i32 max_inflight_tiles;
	i32 max_submit_per_tick;
	i32 batch_size;
	bool8 discard_stale_before_read;
	bool8 discard_stale_before_decode;
	bool8 discard_stale_before_upload;
	bool8 sort_by_file_offset;
	bool8 latency_bound;
} tile_streamer_policy_t;

typedef struct tile_streamer_stats_t {
	u64 submitted;
	u64 completed;
	u64 failed;
	u64 stale;
	u64 pinned;
	u64 unpinned;
	u64 merged_or_protected_from_stale;
	i32 inflight_count;
	i32 queued_count;
} tile_streamer_stats_t;

struct tile_streamer_t {
	image_t* image;
	i32 generation;
	tile_streamer_policy_t policy;
	tile_streamer_stats_t stats;
	bounds2f last_camera_bounds;
	bounds2f last_crop_bounds;
	v2f last_camera_center;
	i32 last_zoom_level;
	bool8 last_is_cropped;
	bool8 state_initialized;
	tile_stream_tile_state_t* level_states[IMAGE_PYRAMID_MAX_LEVELS];
};

typedef struct tile_streamer_request_t {
	bounds2f camera_bounds;
	bounds2f crop_bounds;
	v2f camera_center;
	i32 zoom_level;
	i32 client_width;
	i32 client_height;
	bool is_cropped;
	completion_queue_t* completion_queue;
	completion_event_kind_t completion_event_kind;
} tile_streamer_request_t;

i32 tile_streamer_request_tiles(image_t* image, tile_streamer_request_t* request);
bool tile_streamer_is_task_stale(image_t* image, load_tile_task_t* task);
void tile_streamer_mark_task_submitted(image_t* image, load_tile_task_t* task);
void tile_streamer_mark_task_completed(image_t* image, i32 scale, i32 tile_index, i32 stream_generation, bool failed, bool stale);
void tile_streamer_pin_tile(image_t* image, i32 scale, i32 tile_index, u32 demand_flags);
void tile_streamer_unpin_tile(image_t* image, i32 scale, i32 tile_index, u32 demand_flags);
const tile_streamer_stats_t* tile_streamer_get_stats(image_t* image);
i32 tile_streamer_get_batch_size(image_t* image, i32 default_batch_size);
void tile_streamer_destroy(tile_streamer_t* streamer);

#ifdef __cplusplus
}
#endif
