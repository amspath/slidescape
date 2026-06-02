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

#include "tile_streamer.h"

#include "mathutils.h"

#define TILE_STREAM_WISHLIST_MAX 128

static int tile_stream_priority_compare_func(const void* a, const void* b) {
	return ((load_tile_task_t*)b)->priority - ((load_tile_task_t*)a)->priority;
}

static bool bounds2f_equal(bounds2f a, bounds2f b) {
	return a.min.x == b.min.x && a.min.y == b.min.y && a.max.x == b.max.x && a.max.y == b.max.y;
}

static bool v2f_equal(v2f a, v2f b) {
	return a.x == b.x && a.y == b.y;
}

static tile_streamer_policy_t tile_streamer_make_policy(image_t* image) {
	tile_streamer_policy_t result = {0};
	result.max_inflight_tiles = 16;
	result.max_submit_per_tick = 10;
	result.batch_size = TILE_LOAD_BATCH_MAX;
	result.discard_stale_before_read = true;
	result.discard_stale_before_decode = true;
	result.discard_stale_before_upload = true;

	if (image->backend == IMAGE_BACKEND_SLIDE_SCORE) {
		result.max_inflight_tiles = 32;
		result.max_submit_per_tick = 8;
		result.batch_size = 1;
		result.latency_bound = true;
	} else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
		result.max_inflight_tiles = 8;
	} else if (image->backend == IMAGE_BACKEND_TIFF && image->tiff.is_remote) {
		result.max_inflight_tiles = 8;
		result.max_submit_per_tick = 3;
		result.latency_bound = true;
	}

	return result;
}

static tile_streamer_t* tile_streamer_create(image_t* image) {
	tile_streamer_t* result = (tile_streamer_t*)calloc(1, sizeof(tile_streamer_t));
	if (!result) {
		return NULL;
	}
	result->image = image;
	result->policy = tile_streamer_make_policy(image);
	for (i32 scale = 0; scale < image->level_count; ++scale) {
		level_image_t* level_image = image->level_images + scale;
		if (level_image->exists && level_image->tile_count > 0) {
			result->level_states[scale] = (tile_stream_tile_state_t*)calloc(level_image->tile_count, sizeof(tile_stream_tile_state_t));
		}
	}
	return result;
}

static tile_streamer_t* tile_streamer_get_or_create(image_t* image) {
	if (!image->tile_streamer) {
		image->tile_streamer = tile_streamer_create(image);
	}
	return image->tile_streamer;
}

void tile_streamer_destroy(tile_streamer_t* streamer) {
	if (!streamer) return;
	for (i32 scale = 0; scale < COUNT(streamer->level_states); ++scale) {
		free(streamer->level_states[scale]);
		streamer->level_states[scale] = NULL;
	}
	free(streamer);
}

static tile_stream_tile_state_t* tile_streamer_get_tile_state(tile_streamer_t* streamer, i32 scale, i32 tile_index) {
	// TODO: can these be assertions instead of runtime checks? (potentially hot code)
	if (!streamer) return NULL;
	if (scale < 0 || scale >= COUNT(streamer->level_states)) return NULL;
	if (!streamer->level_states[scale]) return NULL;
	image_t* image = streamer->image;
	if (!image || scale >= image->level_count) return NULL;
	level_image_t* level_image = image->level_images + scale;
	if (tile_index < 0 || tile_index >= level_image->tile_count) return NULL;
	return streamer->level_states[scale] + tile_index;
}

static bool tile_stream_camera_changed(tile_streamer_t* streamer, tile_streamer_request_t* request) {
	if (!streamer->state_initialized) return true;
	if (!bounds2f_equal(streamer->last_camera_bounds, request->camera_bounds)) return true;
	if (!bounds2f_equal(streamer->last_crop_bounds, request->crop_bounds)) return true;
	if (!v2f_equal(streamer->last_camera_center, request->camera_center)) return true;
	if (streamer->last_zoom_level != request->zoom_level) return true;
	if (streamer->last_is_cropped != request->is_cropped) return true;
	return false;
}

static void tile_stream_update_camera_generation(tile_streamer_t* streamer, tile_streamer_request_t* request) {
	if (tile_stream_camera_changed(streamer, request)) {
		++streamer->generation;
		if (streamer->generation <= 0) {
			streamer->generation = 1;
		}
		streamer->last_camera_bounds = request->camera_bounds;
		streamer->last_crop_bounds = request->crop_bounds;
		streamer->last_camera_center = request->camera_center;
		streamer->last_zoom_level = request->zoom_level;
		streamer->last_is_cropped = request->is_cropped;
		streamer->state_initialized = true;
	}
}

static i32 tile_stream_count_inflight_tiles(image_t* image) {
	tile_streamer_t* streamer = image->tile_streamer;
	i32 result = 0;
	for (i32 scale = 0; scale < image->level_count; ++scale) {
		level_image_t* level_image = image->level_images + scale;
		if (!level_image->exists || !level_image->tiles) continue;
		for (u64 tile_index = 0; tile_index < level_image->tile_count; ++tile_index) {
			tile_t* tile = level_image->tiles + tile_index;
			tile_stream_tile_state_t* state = tile_streamer_get_tile_state(streamer, scale, (i32)tile_index);
			if (state && state->request_state == TILE_STREAM_IN_FLIGHT) {
				++result;
			} else if (tile->is_submitted_for_loading) {
				// TODO: eventually phase out is_submitted_for_loading
				++result;
			}
		}
	}
	if (streamer) {
		streamer->stats.inflight_count = result;
	}
	return result;
}

static bool tile_stream_state_has_hard_demand(tile_stream_tile_state_t* state) {
	if (!state) return false;
	u32 hard_demand_flags = TILE_STREAM_DEMAND_READ_REGION | TILE_STREAM_DEMAND_EXPORT | TILE_STREAM_DEMAND_REGISTRATION;
	return (state->demand_mask & hard_demand_flags) != 0 || state->active_pin_count > 0;
}

static bool tile_stream_state_is_active(tile_stream_tile_state_t* state) {
	return state && (state->request_state == TILE_STREAM_QUEUED || state->request_state == TILE_STREAM_IN_FLIGHT);
}

static void tile_stream_mark_viewer_wanted(tile_stream_tile_state_t* state, tile_streamer_t* streamer, i32 priority) {
	if (!state) return;
	state->last_wanted_generation = streamer->generation;
	state->priority = priority;
	state->demand_mask |= TILE_STREAM_DEMAND_VIEWER_VISIBLE;
	if (state->request_state == TILE_STREAM_UNREQUESTED || state->request_state == TILE_STREAM_FAILED) {
		state->generation = streamer->generation;
	}
}

bool tile_streamer_is_task_stale(image_t* image, load_tile_task_t* task) {
	if (!task->may_discard_if_stale) return false;
	tile_streamer_t* streamer = image->tile_streamer;
	if (!streamer) return false;
	level_image_t* task_level_image = (task->level >= 0 && task->level < image->level_count) ? image->level_images + task->level : NULL;
	i32 task_tile_index = task_level_image ? task->tile_y * task_level_image->width_in_tiles + task->tile_x : -1;
	tile_stream_tile_state_t* task_state = tile_streamer_get_tile_state(streamer, task->level, task_tile_index);
	if (tile_stream_state_has_hard_demand(task_state)) {
		++streamer->stats.merged_or_protected_from_stale;
		return false;
	}
	if (task_state && !task_state->stale_ok_to_cancel) {
		++streamer->stats.merged_or_protected_from_stale;
		return false;
	}
	if (task->stream_generation == streamer->generation) return false;
	if (!streamer->state_initialized) return true;
	if (task->level < 0 || task->level >= image->level_count) return true;

	i32 highest_visible_scale = ATLEAST(image->level_count - 1, 0);
	i32 lowest_visible_scale = ATLEAST(streamer->last_zoom_level, 0);
	lowest_visible_scale = ATMOST(highest_visible_scale, lowest_visible_scale);
	for (; lowest_visible_scale > 0; --lowest_visible_scale) {
		if (image->level_images[lowest_visible_scale].exists) {
			break;
		}
	}
	if (task->level < lowest_visible_scale || task->level > highest_visible_scale) {
		return true;
	}

	level_image_t* level_image = image->level_images + task->level;
	if (!level_image->exists) return true;

	bounds2i level_tiles_bounds = BOUNDS2I(0, 0, (i32)level_image->width_in_tiles, (i32)level_image->height_in_tiles);
	bounds2i visible_tiles = world_bounds_to_tile_bounds(&streamer->last_camera_bounds,
	                                                     level_image->x_tile_side_in_um,
	                                                     level_image->y_tile_side_in_um,
	                                                     image->origin_offset);
	visible_tiles = clip_bounds2i(visible_tiles, level_tiles_bounds);
	if (streamer->last_is_cropped) {
		bounds2i crop_tile_bounds = world_bounds_to_tile_bounds(&streamer->last_crop_bounds,
		                                                        level_image->x_tile_side_in_um,
		                                                        level_image->y_tile_side_in_um,
		                                                        image->origin_offset);
		visible_tiles = clip_bounds2i(visible_tiles, crop_tile_bounds);
	}

	bool still_visible = task->tile_x >= visible_tiles.min.x && task->tile_x < visible_tiles.max.x &&
	                     task->tile_y >= visible_tiles.min.y && task->tile_y < visible_tiles.max.y;
	return !still_visible;
}

void tile_streamer_mark_task_submitted(image_t* image, load_tile_task_t* task) {
	if (!task->may_discard_if_stale) return;
	tile_streamer_t* streamer = image->tile_streamer;
	if (!streamer) return; // TODO: maybe assertions instead of runtime checks?
	level_image_t* level_image = image->level_images + task->level;
	// TODO: store tile_index in task to avoid this lookup?
	i32 tile_index = task->tile_y * level_image->width_in_tiles + task->tile_x;
	tile_stream_tile_state_t* state = tile_streamer_get_tile_state(streamer, task->level, tile_index);
	if (!state) return;
	state->request_state = TILE_STREAM_IN_FLIGHT;
	state->generation = task->stream_generation;
	state->priority = task->priority;
	state->last_requested_time = get_clock();
	state->demand_mask |= TILE_STREAM_DEMAND_VIEWER_VISIBLE;
	state->stale_ok_to_cancel = !tile_stream_state_has_hard_demand(state);
	++streamer->stats.submitted;
	++streamer->stats.inflight_count;
}

void tile_streamer_mark_task_completed(image_t* image, i32 scale, i32 tile_index, i32 stream_generation, bool failed, bool stale) {
	tile_streamer_t* streamer = image->tile_streamer;
	if (!streamer) return;
	tile_stream_tile_state_t* state = tile_streamer_get_tile_state(streamer, scale, tile_index);
	if (!state) return;
	if (state->generation != stream_generation) return;
	state->last_completed_time = get_clock();
	if (stale) {
		state->request_state = TILE_STREAM_UNREQUESTED;
		++streamer->stats.stale;
	} else if (failed) {
		state->request_state = TILE_STREAM_FAILED;
		++streamer->stats.failed;
	} else {
		state->request_state = TILE_STREAM_READY;
		++streamer->stats.completed;
	}
	if (streamer->stats.inflight_count > 0) {
		--streamer->stats.inflight_count;
	}
}

void tile_streamer_pin_tile(image_t* image, i32 scale, i32 tile_index, u32 demand_flags) {
	tile_streamer_t* streamer = tile_streamer_get_or_create(image);
	if (!streamer) return;
	tile_stream_tile_state_t* state = tile_streamer_get_tile_state(streamer, scale, tile_index);
	if (!state) return;
	++state->active_pin_count;
	state->demand_mask |= demand_flags;
	state->stale_ok_to_cancel = false;
	++streamer->stats.pinned;
}

void tile_streamer_unpin_tile(image_t* image, i32 scale, i32 tile_index, u32 demand_flags) {
	tile_streamer_t* streamer = image->tile_streamer;
	if (!streamer) return;
	tile_stream_tile_state_t* state = tile_streamer_get_tile_state(streamer, scale, tile_index);
	if (!state) return;
	if (state->active_pin_count > 0) {
		--state->active_pin_count;
	}
	if (state->active_pin_count == 0) {
		state->demand_mask &= ~demand_flags;
		state->stale_ok_to_cancel = !tile_stream_state_has_hard_demand(state);
	}
	++streamer->stats.unpinned;
}

const tile_streamer_stats_t* tile_streamer_get_stats(image_t* image) {
	tile_streamer_t* streamer = image ? image->tile_streamer : NULL;
	return streamer ? &streamer->stats : NULL;
}

i32 tile_streamer_get_batch_size(image_t* image, i32 default_batch_size) {
	tile_streamer_t* streamer = image ? image->tile_streamer : NULL;
	if (!streamer || streamer->policy.batch_size <= 0) {
		return default_batch_size;
	}
	return streamer->policy.batch_size;
}

i32 tile_streamer_request_tiles(image_t* image, tile_streamer_request_t* request) {
	tile_streamer_t* streamer = tile_streamer_get_or_create(image);
	if (!streamer) {
		return 0;
	}
	tile_stream_update_camera_generation(streamer, request);

	i32 inflight_count = tile_stream_count_inflight_tiles(image);
	i32 inflight_room = streamer->policy.max_inflight_tiles - inflight_count;
	if (inflight_room <= 0) {
		return 0;
	}

	load_tile_task_t tile_wishlist[TILE_STREAM_WISHLIST_MAX];
	i32 num_tasks_on_wishlist = 0;
	float screen_radius = ATLEAST(1.0f, sqrtf(SQUARE(request->client_width / 2) + SQUARE(request->client_height / 2)));

	i32 highest_visible_scale = ATLEAST(image->level_count - 1, 0);
	i32 lowest_visible_scale = ATLEAST(request->zoom_level, 0);
	lowest_visible_scale = ATMOST(highest_visible_scale, lowest_visible_scale);
	for (; lowest_visible_scale > 0; --lowest_visible_scale) {
		if (image->level_images[lowest_visible_scale].exists) {
			break;
		}
	}

	for (i32 scale = lowest_visible_scale; scale <= highest_visible_scale; ++scale) {
		ASSERT(scale >= 0 && scale < COUNT(image->level_images));
		level_image_t* drawn_level = image->level_images + scale;
		if (!drawn_level->exists || drawn_level->needs_indexing) {
			continue;
		}

		bounds2i level_tiles_bounds = BOUNDS2I(0, 0, (i32)drawn_level->width_in_tiles, (i32)drawn_level->height_in_tiles);
		bounds2i visible_tiles = world_bounds_to_tile_bounds(&request->camera_bounds, drawn_level->x_tile_side_in_um,
		                                                     drawn_level->y_tile_side_in_um, image->origin_offset);
		visible_tiles = clip_bounds2i(visible_tiles, level_tiles_bounds);

		if (request->is_cropped) {
			bounds2i crop_tile_bounds = world_bounds_to_tile_bounds(&request->crop_bounds,
			                                                        drawn_level->x_tile_side_in_um,
			                                                        drawn_level->y_tile_side_in_um, image->origin_offset);
			visible_tiles = clip_bounds2i(visible_tiles, crop_tile_bounds);
		}

		i32 base_priority = (image->level_count - scale) * 100;
		for (i32 tile_y = visible_tiles.min.y; tile_y < visible_tiles.max.y; ++tile_y) {
			for (i32 tile_x = visible_tiles.min.x; tile_x < visible_tiles.max.x; ++tile_x) {
				tile_t* tile = get_tile(drawn_level, tile_x, tile_y);
				i32 tile_index = tile_y * drawn_level->width_in_tiles + tile_x;
				tile_stream_tile_state_t* state = tile_streamer_get_tile_state(streamer, scale, tile_index);
				if (tile->texture != 0 || tile->is_empty || tile->is_submitted_for_loading || tile_stream_state_is_active(state)) {
					continue;
				}

				float tile_distance_from_center_of_screen_x =
						(request->camera_center.x - ((tile_x + 0.5f) * drawn_level->x_tile_side_in_um)) / drawn_level->um_per_pixel_x;
				float tile_distance_from_center_of_screen_y =
						(request->camera_center.y - ((tile_y + 0.5f) * drawn_level->y_tile_side_in_um)) / drawn_level->um_per_pixel_y;
				float tile_distance_from_center_of_screen =
						sqrtf(SQUARE(tile_distance_from_center_of_screen_x) + SQUARE(tile_distance_from_center_of_screen_y));
				tile_distance_from_center_of_screen /= screen_radius;
				float priority_bonus = (1.0f - tile_distance_from_center_of_screen) * 300.0f;
				i32 tile_priority = base_priority + (i32)priority_bonus;
				tile_stream_mark_viewer_wanted(state, streamer, tile_priority);

				if (num_tasks_on_wishlist >= COUNT(tile_wishlist)) {
					goto wishlist_full;
				}
				load_tile_task_t task = {
						.resource_id = image->resource_id,
						.image = image,
						.tile = tile,
						.level = scale,
						.tile_x = tile_x,
						.tile_y = tile_y,
						.priority = tile_priority,
						.need_gpu_residency = true,
						.need_keep_in_cache = tile->need_keep_in_cache,
						.may_discard_if_stale = true,
						.completion_queue = request->completion_queue,
						.completion_event_kind = request->completion_event_kind,
						.refcount_to_decrement = 1,
						.stream_generation = streamer->generation,
				};
				tile_wishlist[num_tasks_on_wishlist++] = task;
			}
		}
	}
wishlist_full:

	qsort(tile_wishlist, num_tasks_on_wishlist, sizeof(load_tile_task_t), tile_stream_priority_compare_func);

	i32 max_submit = streamer->policy.max_submit_per_tick;
	i32 tiles_to_load = ATMOST(num_tasks_on_wishlist, max_submit);
	tiles_to_load = ATMOST(tiles_to_load, inflight_room);
	if (tiles_to_load <= 0) {
		return 0;
	}

	return request_tiles(image, tile_wishlist, tiles_to_load);
}
