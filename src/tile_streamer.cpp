/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2021  Pieter Valkema

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

#include "common.h"
#include "viewer.h"


void request_tiles(app_state_t* app_state, image_t* image, load_tile_task_t* wishlist, i32 tiles_to_load) {
	if (tiles_to_load > 0){
		app_state->allow_idling_next_frame = false;



		if (image->backend == IMAGE_BACKEND_TIFF && image->tiff.tiff.is_remote) {
			// For remote slides, only send out a batch request every so often, instead of single tile requests every frame.
			// (to reduce load on the server)
			static u32 intermittent = 0;
			++intermittent;
			u32 intermittent_interval = 1;
			intermittent_interval = 5; // reduce load on remote server; can be tweaked
			if (intermittent % intermittent_interval == 0) {
				load_tile_task_batch_t* batch = (load_tile_task_batch_t*) calloc(1, sizeof(load_tile_task_batch_t));
				batch->task_count = ATMOST(COUNT(batch->tile_tasks), tiles_to_load);
				memcpy(batch->tile_tasks, wishlist, batch->task_count * sizeof(load_tile_task_t));
				if (add_work_queue_entry(&global_work_queue, tiff_load_tile_batch_func, batch)) {
					// success
					for (i32 i = 0; i < batch->task_count; ++i) {
						load_tile_task_t* task = batch->tile_tasks + i;
						tile_t* tile = task->tile;
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task->need_gpu_residency;
						tile->need_keep_in_cache = task->need_keep_in_cache;
					}
				}
			}
		} else {
			// regular file loading
			for (i32 i = 0; i < tiles_to_load; ++i) {
				load_tile_task_t* task = (load_tile_task_t*) malloc(sizeof(load_tile_task_t)); // should be freed after uploading the tile to the gpu
				*task = wishlist[i];

				tile_t* tile = task->tile;
				if (tile->is_cached && tile->texture == 0 && task->need_gpu_residency) {
					// only GPU upload needed
					if (add_work_queue_entry(&global_completion_queue, viewer_upload_already_cached_tile_to_gpu, task)) {
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task->need_gpu_residency;
						tile->need_keep_in_cache = task->need_keep_in_cache;
					}
				} else {
					if (add_work_queue_entry(&global_work_queue, load_tile_func, task)) {
						// TODO: should we even allow this to fail?
						// success
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task->need_gpu_residency;
						tile->need_keep_in_cache = task->need_keep_in_cache;
					}
				}


			}
		}


	}
}


void submit_tile_completed(void* tile_pixels, i32 scale, i32 tile_index, i32 tile_width, i32 tile_height) {
	viewer_notify_tile_completed_task_t* completion_task = (viewer_notify_tile_completed_task_t*) calloc(1, sizeof(viewer_notify_tile_completed_task_t));
	completion_task->pixel_memory = (u8*)tile_pixels;
	completion_task->tile_width = tile_width;
	completion_task->tile_height = tile_height;
	completion_task->scale = scale;
	completion_task->tile_index = tile_index;
	completion_task->want_gpu_residency = true;

	//	console_print("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);
	add_work_queue_entry(&global_completion_queue, viewer_notify_load_tile_completed, completion_task);

}

static void isyntax_init_dummy_codeblocks(isyntax_t* isyntax) {
	// Blocks with 'background' coefficients, to use for filling in margins at the edges (in case the neighboring codeblock doesn't exist)
	if (!isyntax->black_dummy_coeff) {
		isyntax->black_dummy_coeff = (icoeff_t*)calloc(1, isyntax->block_width * isyntax->block_height * sizeof(icoeff_t));
	}
	if (!isyntax->white_dummy_coeff) {
		isyntax->white_dummy_coeff = (icoeff_t*)malloc(isyntax->block_width * isyntax->block_height * sizeof(icoeff_t));
		for (i32 i = 0; i < isyntax->block_width * isyntax->block_height; ++i) {
			isyntax->white_dummy_coeff[i] = 255;
		}
	}
}

//isyntax_data_chunk_t isyntax_read_data_chunk(isyntax_t* isyntax, isyntax_image_t* wsi_image, i32 base_codeblock_index);

static i32 isyntax_load_all_tiles_in_level(isyntax_t* isyntax, isyntax_image_t* wsi, i32 scale, bool use_worker_threads) {
	i32 tiles_loaded = 0;
	i32 tile_index = 0;
	isyntax_level_t* level = wsi->levels + scale;
	for (i32 tile_y = 0; tile_y < level->height_in_tiles; ++tile_y) {
		for (i32 tile_x = 0; tile_x < level->width_in_tiles; ++tile_x, ++tile_index) {
			isyntax_tile_t* tile = level->tiles + tile_index;
			if (!tile->exists) continue;
			if (use_worker_threads) {
				isyntax_begin_load_tile(isyntax, wsi, scale, tile_x, tile_y);
			} else {
				u32* tile_pixels = isyntax_load_tile(isyntax, wsi, scale, tile_x, tile_y);
				submit_tile_completed(tile_pixels, scale, tile_index, isyntax->tile_width, isyntax->tile_height);
			}
			++tiles_loaded;
		}
	}

	// TODO: free top level coefficients

	// TODO: more graceful multithreading
	if (use_worker_threads) {
		// Wait for all tiles to be finished loading
		tile_index = 0;
		for (i32 tile_y = 0; tile_y < level->height_in_tiles; ++tile_y) {
			for (i32 tile_x = 0; tile_x < level->width_in_tiles; ++tile_x, ++tile_index) {
				isyntax_tile_t* tile = level->tiles + tile_index;
				if (!tile->exists) continue;
				while (!tile->is_loaded) {
					do_worker_work(&global_work_queue, 0);
				}
			}
		}
	}

	level->is_fully_loaded = true;
	return tiles_loaded;
}


// NOTE: The number of levels present in the highest data chunks depends on the highest scale:
// Highest scale = 8  --> chunk contains levels 6, 7, 8 (most often this is the case)
// Highest scale = 7  --> chunk contains levels 6, 7
// Highest scale = 6  --> chunk contains only level 6
// Highest scale = 5  --> chunk contains levels 3, 4, 5
// Highest scale = 4  --> chunk contains levels 3, 4



static void isyntax_do_first_load(arena_t* temp_arena, isyntax_t* isyntax, isyntax_image_t* wsi) {

	i64 start_first_load = get_clock();
	i32 tiles_loaded = 0;

	isyntax_init_dummy_codeblocks(isyntax);

	i32 scale = wsi->max_scale;
	isyntax_level_t* current_level = wsi->levels + scale;
	i32 codeblocks_per_color = isyntax_get_chunk_codeblocks_per_color_for_level(scale, true); // most often 1 + 4 + 16 (for scale n, n-1, n-2) + 1 (LL block)
	i32 chunk_codeblock_count = codeblocks_per_color * 3;
	i32 block_color_offsets[3] = {0, codeblocks_per_color, 2 * codeblocks_per_color};

	i32 levels_in_chunk = ((wsi->codeblocks + current_level->tiles[0].codeblock_chunk_index)->scale % 3) + 1;

	temp_memory_t temp_memory = begin_temp_memory(temp_arena);

	u8** data_chunks = arena_push_array(temp_arena, current_level->tile_count, u8*);
	memset(data_chunks, 0, current_level->tile_count * sizeof(u8*));

	// Read codeblock data from disk
	{
		i64 start = get_clock();

		i32 tile_index = 0;
		for (i32 tile_y = 0; tile_y < current_level->height_in_tiles; ++tile_y) {
			for (i32 tile_x = 0; tile_x < current_level->width_in_tiles; ++tile_x, ++tile_index) {
				isyntax_tile_t* tile = current_level->tiles + tile_index;
				if (!tile->exists) continue;
				isyntax_codeblock_t* top_chunk_codeblock = wsi->codeblocks + tile->codeblock_chunk_index;
				u64 offset0 = top_chunk_codeblock->block_data_offset;

				isyntax_codeblock_t* last_codeblock = wsi->codeblocks + tile->codeblock_chunk_index + chunk_codeblock_count - 1;
				u64 offset1 = last_codeblock->block_data_offset + last_codeblock->block_size;
				u64 read_size = offset1 - offset0;
				arena_align(temp_arena, 64);
				data_chunks[tile_index] = (u8*) arena_push_size(temp_arena, read_size);

#if WINDOWS
				// TODO: 64 bit read offsets?
				win32_overlapped_read(global_thread_memory, isyntax->file_handle, data_chunks[tile_index], read_size, offset0);
#else
				size_t bytes_read = pread(isyntax->file_handle, data_chunks[tile_index], read_size, offset0);
#endif
			}
		}
		float elapsed = get_seconds_elapsed(start, get_clock());
		console_print_verbose("I/O + decompress: scale=%d  time=%g\n", scale, elapsed);
	}

	// Decompress the top level tiles
	i32 tile_index = 0;
	for (i32 tile_y = 0; tile_y < current_level->height_in_tiles; ++tile_y) {
		for (i32 tile_x = 0; tile_x < current_level->width_in_tiles; ++tile_x, ++tile_index) {
			isyntax_tile_t* tile = current_level->tiles + tile_index;
			if (!tile->exists) continue;
			isyntax_codeblock_t* top_chunk_codeblock = wsi->codeblocks + tile->codeblock_chunk_index;
			u64 offset0 = top_chunk_codeblock->block_data_offset;

			isyntax_codeblock_t* h_blocks[3];
			isyntax_codeblock_t* ll_blocks[3];
			i32 ll_block_offset = (codeblocks_per_color - 1);
			for (i32 i = 0; i < 3; ++i) {
				h_blocks[i] = top_chunk_codeblock + block_color_offsets[i];
				ll_blocks[i] = top_chunk_codeblock + block_color_offsets[i] + ll_block_offset;
			}
			for (i32 i = 0; i < 3; ++i) {
				isyntax_codeblock_t* h_block =  h_blocks[i];
				isyntax_codeblock_t* ll_block = ll_blocks[i];
				isyntax_tile_channel_t* color_channel = tile->color_channels + i;
				color_channel->coeff_h = isyntax_decompress_codeblock_in_chunk(h_block, isyntax->block_width, isyntax->block_height, data_chunks[tile_index], offset0);
				color_channel->coeff_ll = isyntax_decompress_codeblock_in_chunk(ll_block, isyntax->block_width, isyntax->block_height, data_chunks[tile_index], offset0);

				// We're loading everything at once for this level, so we can set every tile as having their neighors loaded as well.
				color_channel->neighbors_loaded = isyntax_get_adjacent_tiles_mask(current_level, tile_x, tile_y);
			}
		}
	}

	// Transform and submit the top level tiles
	tiles_loaded += isyntax_load_all_tiles_in_level(isyntax, wsi, scale, true);

	// Decompress and transform the remaining levels in the data chunks.
	if (levels_in_chunk >= 2) {
		scale = wsi->max_scale - 1;
		current_level = wsi->levels + scale;
		// First do the Hulsken decompression on all tiles for this level
		i32 chunk_index = 0;
		for (i32 tile_y = 0; tile_y < current_level->height_in_tiles; tile_y += 2) {
			for (i32 tile_x = 0; tile_x < current_level->width_in_tiles; tile_x += 2, ++chunk_index) {
				tile_index = tile_y * current_level->width_in_tiles + tile_x;
				isyntax_tile_t* tile = current_level->tiles + tile_index;
				if (!tile->exists) continue;
				// LL blocks should already be available (these were 'donated' when we were loading the higher level)
				ASSERT(tile->color_channels[0].coeff_ll != NULL);
				ASSERT(tile->color_channels[1].coeff_ll != NULL);
				ASSERT(tile->color_channels[2].coeff_ll != NULL);
				isyntax_codeblock_t* top_chunk_codeblock = wsi->codeblocks + tile->codeblock_chunk_index;
				u64 offset0 = top_chunk_codeblock->block_data_offset;

				i32 chunk_codeblock_indices_for_color[4] = {1, 2, 3, 4};
				i32 tile_delta_x[4] = {0, 1, 0, 1};
				i32 tile_delta_y[4] = {0, 0, 1, 1};
				for (i32 color = 0; color < 3; ++color) {
					// Decompress the codeblocks in this chunk for this level and this color channel
					for (i32 i = 0; i < 4; ++i) {
						isyntax_codeblock_t* codeblock = top_chunk_codeblock + chunk_codeblock_indices_for_color[i];
						ASSERT(codeblock->scale == scale);
						i64 offset_in_chunk = codeblock->block_data_offset - offset0;
						ASSERT(offset_in_chunk >= 0);
						icoeff_t* decompressed = isyntax_hulsken_decompress(data_chunks[chunk_index] + offset_in_chunk, codeblock->block_size, isyntax->block_width,
						                                                    isyntax->block_height, codeblock->coefficient, 1); // TODO: free using _aligned_free()

						i32 tile_x_in_chunk = tile_x + tile_delta_x[i];
						i32 tile_y_in_chunk = tile_y + tile_delta_y[i];
						tile_index = (tile_y_in_chunk * current_level->width_in_tiles) + tile_x_in_chunk;
						isyntax_tile_t* tile_in_chunk = current_level->tiles + tile_index;
						isyntax_tile_channel_t* color_channel = tile_in_chunk->color_channels + color;
						color_channel->coeff_h = decompressed;

						// We're loading everything at once for this level, so we can set every tile as having their neighors loaded as well.
						color_channel->neighbors_loaded = isyntax_get_adjacent_tiles_mask(current_level, tile_x_in_chunk, tile_y_in_chunk);
					}

					// Move to the next color channel in the chunk of codeblocks
					for (i32 i = 0; i < 4; ++i) {
						chunk_codeblock_indices_for_color[i] += codeblocks_per_color;
					}
				}
			}
		}
		// Now do the inverse wavelet transforms
		tiles_loaded += isyntax_load_all_tiles_in_level(isyntax, wsi, scale, true);
	}

	// Now for the next level down (if present in the chunk)
	if (levels_in_chunk >= 3) {
		scale = wsi->max_scale - 2;
		ASSERT(scale >= 0);
		current_level = wsi->levels + scale;
		// First do the Hulsken decompression on all tiles for this level
		i32 chunk_index = 0;
		for (i32 tile_y = 0; tile_y < current_level->height_in_tiles; tile_y += 4) {
			for (i32 tile_x = 0; tile_x < current_level->width_in_tiles; tile_x += 4, ++chunk_index) {
				tile_index = tile_y * current_level->width_in_tiles + tile_x;
				isyntax_tile_t* tile = current_level->tiles + tile_index;
				if (!tile->exists) continue;
				// LL blocks should already be available (these were 'donated' when we were loading the higher level)
				ASSERT(tile->color_channels[0].coeff_ll != NULL);
				ASSERT(tile->color_channels[1].coeff_ll != NULL);
				ASSERT(tile->color_channels[2].coeff_ll != NULL);
				isyntax_codeblock_t* top_chunk_codeblock = wsi->codeblocks + tile->codeblock_chunk_index;
				u64 offset0 = top_chunk_codeblock->block_data_offset;

				i32 chunk_codeblock_indices_for_color[16] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
				i32 tile_delta_x[16] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
				i32 tile_delta_y[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3};
				for (i32 color = 0; color < 3; ++color) {
					// Decompress the codeblocks in this chunk for this level and this color channel
					for (i32 i = 0; i < 16; ++i) {
						isyntax_codeblock_t* codeblock = top_chunk_codeblock + chunk_codeblock_indices_for_color[i];
						ASSERT(codeblock->scale == scale);
						i64 offset_in_chunk = codeblock->block_data_offset - offset0;
						ASSERT(offset_in_chunk >= 0);
						icoeff_t* decompressed = isyntax_hulsken_decompress(data_chunks[chunk_index] + offset_in_chunk, codeblock->block_size, isyntax->block_width,
						                                                    isyntax->block_height, codeblock->coefficient, 1); // TODO: free using _aligned_free()

						i32 tile_x_in_chunk = tile_x + tile_delta_x[i];
						i32 tile_y_in_chunk = tile_y + tile_delta_y[i];
						tile_index = (tile_y_in_chunk * current_level->width_in_tiles) + tile_x_in_chunk;
						isyntax_tile_t* tile_in_chunk = current_level->tiles + tile_index;
						isyntax_tile_channel_t* color_channel = tile_in_chunk->color_channels + color;
						color_channel->coeff_h = decompressed;

						// We're loading everything at once for this level, so we can set every tile as having their neighors loaded as well.
						color_channel->neighbors_loaded = isyntax_get_adjacent_tiles_mask(current_level, tile_x_in_chunk, tile_y_in_chunk);
					}

					// Move to the next color channel in the chunk of codeblocks
					for (i32 i = 0; i < 16; ++i) {
						chunk_codeblock_indices_for_color[i] += codeblocks_per_color;
					}
				}
			}
		}
		// Now do the inverse wavelet transforms
		tiles_loaded += isyntax_load_all_tiles_in_level(isyntax, wsi, scale, true);
	}

	console_print("   iSyntax: loading the first %d tiles took %g seconds\n", tiles_loaded, get_seconds_elapsed(start_first_load, get_clock()));

	end_temp_memory(&temp_memory); // deallocate data chunk

	wsi->first_load_complete = true;

}

typedef struct isyntax_load_tile_task_t {
	isyntax_t* isyntax;
	isyntax_image_t* wsi;
	i32 scale;
	i32 tile_x;
	i32 tile_y;
	i32 tile_index;
} isyntax_load_tile_task_t;

void isyntax_load_tile_task_func(i32 logical_thread_index, void* userdata) {
	isyntax_load_tile_task_t* task = (isyntax_load_tile_task_t*) userdata;
	u32* tile_pixels = isyntax_load_tile(task->isyntax, task->wsi, task->scale, task->tile_x, task->tile_y);
	submit_tile_completed(tile_pixels, task->scale, task->tile_index, task->isyntax->tile_width, task->isyntax->tile_height);
	atomic_decrement(&task->isyntax->refcount); // release
	free(userdata);
}

void isyntax_begin_load_tile(isyntax_t* isyntax, isyntax_image_t* wsi, i32 scale, i32 tile_x, i32 tile_y) {
	isyntax_level_t* level = wsi->levels + scale;
	i32 tile_index = tile_y * level->width_in_tiles + tile_x;
	isyntax_tile_t* tile = level->tiles + tile_index;
	if (!tile->is_submitted_for_loading) {
		atomic_increment(&isyntax->refcount); // retain; don't destroy isyntax while busy
		tile->is_submitted_for_loading = true;
		isyntax_load_tile_task_t* task = (isyntax_load_tile_task_t*) calloc(1, sizeof(isyntax_load_tile_task_t));
		task->isyntax = isyntax;
		task->wsi = wsi;
		task->scale = scale;
		task->tile_x = tile_x;
		task->tile_y = tile_y;
		task->tile_index = tile_index;
		add_work_queue_entry(&global_work_queue, isyntax_load_tile_task_func, task);
	}

}

typedef struct isyntax_first_load_task_t {
	isyntax_t* isyntax;
	isyntax_image_t* wsi;
} isyntax_first_load_task_t;

void isyntax_first_load_task_func(i32 logical_thread_index, void* userdata) {
	thread_memory_t* thread_memory = global_thread_memory;
	init_arena(&thread_memory->temp_arena, thread_memory->thread_memory_usable_size, thread_memory->aligned_rest_of_thread_memory);

	isyntax_first_load_task_t* task = (isyntax_first_load_task_t*) userdata;
	isyntax_do_first_load(&thread_memory->temp_arena, task->isyntax, task->wsi);
	atomic_decrement(&task->isyntax->refcount); // release
	free(userdata);
}

void isyntax_begin_first_load(isyntax_t* isyntax, isyntax_image_t* wsi_image) {
	isyntax_first_load_task_t* task = (isyntax_first_load_task_t*) calloc(1, sizeof(isyntax_first_load_task_t));
	task->isyntax = isyntax;
	task->wsi = wsi_image;
	atomic_increment(&isyntax->refcount); // retain; don't destroy isyntax while busy
	add_work_queue_entry(&global_work_queue, isyntax_first_load_task_func, task);
	// TODO: retain isyntax (don't unload until initial load is complete!)
}

typedef struct isyntax_tile_req_t {
	i32 tile_x;
	i32 tile_y;
	i32 level_tile_index;
	i32 region_tile_index;
	u32 adj_need_ll_mask;
	u32 adj_need_h_mask;
	bool want_load;
	bool need_h_coeff;
	bool need_ll_coeff;
} isyntax_tile_req_t;

typedef struct isyntax_load_region_t {
	i32 scale;
	bounds2i padded_bounds; // tile bounds
	bounds2i visible_bounds; // tile bounds
	i32 width_in_tiles;
	i32 height_in_tiles;
	isyntax_tile_req_t* tile_req;
} isyntax_load_region_t;

void isyntax_stream_image_tiles(tile_streamer_t* tile_streamer, isyntax_t* isyntax) {

	isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;

	if (!wsi_image->first_load_complete) {
		isyntax_begin_first_load(isyntax, wsi_image);
	} else {
		ASSERT(wsi_image->level_count >= 0);
		i32 highest_visible_scale = ATLEAST(wsi_image->max_scale, 0);
		i32 lowest_visible_scale = ATLEAST(tile_streamer->zoom.level, 0);
		lowest_visible_scale = ATMOST(highest_visible_scale, lowest_visible_scale);

		// Never look at highest scales, which have already been loaded at first load
		i32 highest_scale_to_load = highest_visible_scale;
		for (i32 scale = highest_visible_scale; scale >= lowest_visible_scale; --scale) {
			isyntax_level_t* level = wsi_image->levels + scale;
			if (level->is_fully_loaded) {
				--highest_scale_to_load;
			} else {
				break;
			}
		}

		i32 scales_to_load_count = (highest_scale_to_load+1) - lowest_visible_scale;
		if (scales_to_load_count <= 0) {
			return;
		}
		isyntax_load_region_t* regions = (isyntax_load_region_t*) alloca(scales_to_load_count * sizeof(isyntax_load_region_t));
		memset(regions, 0, scales_to_load_count * sizeof(isyntax_load_region_t));

		u32 chunks_to_load_count = 0;
		u32 chunks_to_load[16];
		u32 max_chunks_to_load = COUNT(chunks_to_load);



		i32 scale_to_load_index = 0;
		for (i32 scale = highest_scale_to_load; scale >= lowest_visible_scale; --scale, ++scale_to_load_index) {
			isyntax_level_t* level = wsi_image->levels + scale;
			bounds2i level_tiles_bounds = {{ 0, 0, (i32)level->width_in_tiles, (i32)level->height_in_tiles }};

			bounds2i visible_tiles = world_bounds_to_tile_bounds(&tile_streamer->camera_bounds, level->x_tile_side_in_um,
			                                                     level->y_tile_side_in_um, tile_streamer->origin_offset);
			// TODO: Fix visible tiles not being calculated correctly for higher levels while zoomed in very far?
			visible_tiles.min.x -= 1;
			visible_tiles.min.y -= 1;
			visible_tiles.max.x += 1;
			visible_tiles.max.y += 1;

			visible_tiles = clip_bounds2i(&visible_tiles, &level_tiles_bounds);

			if (tile_streamer->is_cropped) {
				bounds2i crop_tile_bounds = world_bounds_to_tile_bounds(&tile_streamer->camera_bounds,
				                                                        level->x_tile_side_in_um,
				                                                        level->y_tile_side_in_um, tile_streamer->origin_offset);
				visible_tiles = clip_bounds2i(&visible_tiles, &crop_tile_bounds);
			}


			// Check which tiles need loading


			// Expand bounds by one for I/O of H coefficients
			bounds2i padded_bounds = visible_tiles;
			if (padded_bounds.min.x > 0) padded_bounds.min.x -= 1;
			if (padded_bounds.min.y > 0) padded_bounds.min.y -= 1;
			if (padded_bounds.max.x < level->width_in_tiles-1) padded_bounds.max.x += 1;
			if (padded_bounds.max.y < level->height_in_tiles-1) padded_bounds.max.y += 1;

			i32 local_bounds_width = padded_bounds.max.x - padded_bounds.min.x;
			i32 local_bounds_height = padded_bounds.max.y - padded_bounds.min.y;

			size_t tile_req_size = (local_bounds_width) * (local_bounds_height) * sizeof(isyntax_tile_req_t);
			isyntax_tile_req_t* tile_req = (isyntax_tile_req_t*)malloc(tile_req_size);
			memset(tile_req, 0, tile_req_size);

			isyntax_load_region_t* region = regions + scale_to_load_index;
			region->width_in_tiles = local_bounds_width;
			region->height_in_tiles = local_bounds_height;
			region->scale = scale;
			region->padded_bounds = padded_bounds;
			region->visible_bounds = visible_tiles;
			region->tile_req = tile_req;

			i32 local_tile_index = 0;
			for (i32 tile_y = padded_bounds.min.y; tile_y < padded_bounds.max.y; ++tile_y) {
				for (i32 tile_x = padded_bounds.min.x; tile_x < padded_bounds.max.x; ++tile_x, ++local_tile_index) {
					isyntax_tile_t* central_tile = level->tiles + tile_y * level->width_in_tiles + tile_x;
					i32 local_tile_x = tile_x - padded_bounds.min.x;
					i32 local_tile_y = tile_y - padded_bounds.min.y;
					if (!central_tile->is_loaded) {

						u32 adjacent = isyntax_get_adjacent_tiles_mask(level, tile_x, tile_y);

						if (tile_x >= visible_tiles.min.x && tile_y >= visible_tiles.min.y &&
						    tile_x < visible_tiles.max.x && tile_y < visible_tiles.max.y)
						{
							tile_req[local_tile_index].want_load = true;

							u32 need_ll_mask = 0;
							u32 need_h_mask = 0;
							if (adjacent & ISYNTAX_ADJ_TILE_TOP_LEFT) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x-1);
								if (adj_tile->exists) {
									i32 adj_local_tile_index = (local_tile_y-1) * local_bounds_width + (local_tile_x-1);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_TOP_LEFT;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_TOP_LEFT;
										adj_tile_req->need_h_coeff = true;
									}
								}


							}
							if (adjacent & ISYNTAX_ADJ_TILE_TOP_CENTER) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x);
								if (adj_tile->exists) {
									i32 adj_local_tile_index = (local_tile_y - 1) * local_bounds_width + (local_tile_x);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_TOP_CENTER;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_TOP_CENTER;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}
							if (adjacent & ISYNTAX_ADJ_TILE_TOP_RIGHT) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x+1);
								if (adj_tile->exists) {
									i32 adj_local_tile_index =
											(local_tile_y - 1) * local_bounds_width + (local_tile_x + 1);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_TOP_RIGHT;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_TOP_RIGHT;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}
							if (adjacent & ISYNTAX_ADJ_TILE_CENTER_LEFT) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x-1);
								if (adj_tile->exists) {
									i32 adj_local_tile_index = (local_tile_y) * local_bounds_width + (local_tile_x - 1);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_CENTER_LEFT;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_CENTER_LEFT;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}
							if (adjacent & ISYNTAX_ADJ_TILE_CENTER) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x);
								if (adj_tile->exists) {
									i32 adj_local_tile_index = (local_tile_y) * local_bounds_width + (local_tile_x);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_CENTER;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_CENTER;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}
							if (adjacent & ISYNTAX_ADJ_TILE_CENTER_RIGHT) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x+1);
								if (adj_tile->exists) {
									i32 adj_local_tile_index = (local_tile_y) * local_bounds_width + (local_tile_x + 1);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_CENTER_RIGHT;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_CENTER_RIGHT;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}
							if (adjacent & ISYNTAX_ADJ_TILE_BOTTOM_LEFT) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x-1);
								if (adj_tile->exists) {
									i32 adj_local_tile_index =
											(local_tile_y + 1) * local_bounds_width + (local_tile_x - 1);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_BOTTOM_LEFT;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_BOTTOM_LEFT;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}
							if (adjacent & ISYNTAX_ADJ_TILE_BOTTOM_CENTER) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x);
								if (adj_tile->exists) {
									i32 adj_local_tile_index = (local_tile_y + 1) * local_bounds_width + (local_tile_x);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_BOTTOM_CENTER;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_BOTTOM_CENTER;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}
							if (adjacent & ISYNTAX_ADJ_TILE_BOTTOM_RIGHT) {
								isyntax_tile_t* adj_tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x+1);
								if (adj_tile->exists) {
									i32 adj_local_tile_index = (local_tile_y + 1) * local_bounds_width + (local_tile_x + 1);
									isyntax_tile_req_t* adj_tile_req = tile_req + adj_local_tile_index;
									if (!adj_tile->has_ll) {
										need_ll_mask |= ISYNTAX_ADJ_TILE_BOTTOM_RIGHT;
										adj_tile_req->need_ll_coeff = true;
									}
									if (!adj_tile->has_h) {
										need_h_mask |= ISYNTAX_ADJ_TILE_BOTTOM_RIGHT;
										adj_tile_req->need_h_coeff = true;
									}
								}
							}

							tile_req[local_tile_index].adj_need_ll_mask = need_ll_mask;
							tile_req[local_tile_index].adj_need_h_mask = need_h_mask;

						}
//						if (!central_tile->has_ll) {
//							tile_req[local_tile_index].need_ll_coeff = true;
//						}
					}

					// TODO: record which tiles require H coefficients to be loaded

				}
			}


			// Pass: determine which chunks need to be loaded
			local_tile_index = 0;
			for (i32 tile_y = padded_bounds.min.y; tile_y < padded_bounds.max.y; ++tile_y) {
				for (i32 tile_x = padded_bounds.min.x; tile_x < padded_bounds.max.x; ++tile_x, ++local_tile_index) {
					if (chunks_to_load_count == max_chunks_to_load) {
						goto break_out_of_loop;
					}
					isyntax_tile_t* tile = level->tiles + tile_y * level->width_in_tiles + tile_x;
					isyntax_tile_req_t* req = tile_req + local_tile_index;
					if (req->need_h_coeff) {
						u32 chunk_index = tile->data_chunk_index;
						bool already_in_list = false;
						for (i32 i = 0; i < chunks_to_load_count; ++i) {
							if (chunks_to_load[i] == chunk_index) {
								already_in_list = true;
								break;
							}
						}
						if (!already_in_list) {
							chunks_to_load[chunks_to_load_count++] = chunk_index;
						}
					}
				}
			}
			break_out_of_loop:;

		}

//		io_operation_t ops[COUNT(chunks_to_load)];

		i64 io_start = get_clock();

		for (i32 i = 0; i < chunks_to_load_count; ++i) {
			u32 chunk_index = chunks_to_load[i];
			isyntax_data_chunk_t * chunk = wsi_image->data_chunks + chunk_index;
			if (!chunk->data) {
				isyntax_codeblock_t* last_codeblock = wsi_image->codeblocks + chunk->top_codeblock_index + (chunk->codeblock_count_per_color * 3) - 1;
				u64 offset1 = last_codeblock->block_data_offset + last_codeblock->block_size;
				u64 read_size = offset1 - chunk->offset;
				chunk->data = (u8*)malloc(read_size);
				// TODO: async I/O
#if WINDOWS
				// TODO: 64 bit read offsets?
				win32_overlapped_read(global_thread_memory, isyntax->file_handle, chunk->data, read_size, chunk->offset);
#else
				size_t bytes_read = pread(isyntax->file_handle, chunk->data, read_size, chunk->offset);

				// TODO: async I/O??
//				io_operation_t* op = ops + i;
//				op->file = isyntax->file_handle;
//				op->dest = chunk->data;
//				op->size_to_read = read_size;
//				op->offset = chunk->offset;
//
//				async_read_submit(op);
#endif
			}
		}

		/*for (i32 i = 0; i < chunks_to_load_count; ++i) {
			io_operation_t* op = ops + i;
			while (!async_read_has_finished(op)) {
//				aio_fsync()
//				aio_suspend((const aiocb *const *)(&ops[i].cb), 1, NULL);
			}
			async_read_finalize(op);
		}*/

		if (chunks_to_load_count > 0) {
			//console_print("IO time for %d chunks: %g seconds\n", chunks_to_load_count, get_seconds_elapsed(io_start, get_clock()));
		}

		// Now try to reconstruct the tiles
		// Decompress tiles
		scale_to_load_index = 0;
		for (i32 scale = highest_scale_to_load; scale >= lowest_visible_scale; --scale, ++scale_to_load_index) {
			isyntax_level_t* level = wsi_image->levels + scale;
			isyntax_load_region_t* region = regions + scale_to_load_index;

			i32 local_tile_index = 0;
			for (i32 tile_y = region->padded_bounds.min.y; tile_y < region->padded_bounds.max.y; ++tile_y) {
				for (i32 tile_x = region->padded_bounds.min.x; tile_x < region->padded_bounds.max.x; ++tile_x, ++local_tile_index) {

					isyntax_tile_t* tile = level->tiles + tile_y * level->width_in_tiles + tile_x;
					isyntax_tile_req_t* req = region->tile_req + local_tile_index;


					if (req->need_h_coeff) {
						isyntax_data_chunk_t* chunk = wsi_image->data_chunks + tile->data_chunk_index;
						if (chunk->data) {
							i32 scale_in_chunk = chunk->scale - scale;
							ASSERT(scale_in_chunk >= 0 && scale_in_chunk < 3);
							i32 codeblock_index_in_chunk = 0;
							if (scale_in_chunk == 0) {
								codeblock_index_in_chunk = 0;
							} else if (scale_in_chunk == 1) {
								codeblock_index_in_chunk = 1 + (tile_y % 2) * 2 + (tile_x % 2);
							} else if (scale_in_chunk == 2) {
								codeblock_index_in_chunk = 5 + (tile_y % 4) * 4 + (tile_x % 4);
							} else {
								panic();
							}
							i32 chunk_codeblock_indices_for_color[3] = {codeblock_index_in_chunk,
														  chunk->codeblock_count_per_color + codeblock_index_in_chunk,
														  2 * chunk->codeblock_count_per_color + codeblock_index_in_chunk};

							isyntax_codeblock_t* top_chunk_codeblock = wsi_image->codeblocks + tile->codeblock_chunk_index;

							for (i32 color = 0; color < 3; ++color) {
								isyntax_codeblock_t* codeblock = top_chunk_codeblock + chunk_codeblock_indices_for_color[color];
								ASSERT(codeblock->scale == scale);
								i64 offset_in_chunk = codeblock->block_data_offset - chunk->offset;
								ASSERT(offset_in_chunk >= 0);
								icoeff_t* decompressed = isyntax_hulsken_decompress(chunk->data + offset_in_chunk, codeblock->block_size, isyntax->block_width,
								                                                    isyntax->block_height, codeblock->coefficient, 1); // TODO: free using _aligned_free()

								isyntax_tile_channel_t* color_channel = tile->color_channels + color;
								color_channel->coeff_h = decompressed;
							}
							tile->has_h = true;
						}
					}
				}
			}

		}

//		i32 max_tiles_to_load = 5; // TODO: remove this restriction
		i32 tiles_to_load = 0;
		scale_to_load_index = 0;
		for (i32 scale = highest_scale_to_load; scale >= lowest_visible_scale; --scale, ++scale_to_load_index) {
			isyntax_level_t *level = wsi_image->levels + scale;
			isyntax_load_region_t *region = regions + scale_to_load_index;

			for (i32 tile_y = region->visible_bounds.min.y; tile_y < region->visible_bounds.max.y; ++tile_y) {
				for (i32 tile_x = region->visible_bounds.min.x; tile_x < region->visible_bounds.max.x; ++tile_x) {
					i32 tile_index = tile_y * level->width_in_tiles + tile_x;
					isyntax_tile_t* tile = level->tiles + tile_index;
					if (tile->is_submitted_for_loading) {
						continue; // a worker thread is already on it, don't resubmit
					}
					if (!tile->has_ll) {
						continue; // higher level tile needs to load first
					}
					if (!tile->has_h) {
						continue; // codeblocks not decompressed (this should never trigger)
					}

					i32 local_tile_x = tile_x - region->padded_bounds.min.x;
					i32 local_tile_y = tile_y - region->padded_bounds.min.y;
					i32 local_tile_index = local_tile_y * region->width_in_tiles + local_tile_x;
					isyntax_tile_req_t* req = region->tile_req + local_tile_index;

					if (!req->want_load) {
						continue; // This tile does not need to be loaded (probably because it has already been loaded)
					}

					// TODO: move this check to isyntax_load_tile()?
					u32 adj_tiles = isyntax_get_adjacent_tiles_mask(level, tile_x, tile_y);
					// Check if all prerequisites have been met, for the surrounding tiles as well
					if (adj_tiles & ISYNTAX_ADJ_TILE_TOP_LEFT) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x-1);
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_TOP_CENTER) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y-1) * level->width_in_tiles + tile_x;
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_TOP_RIGHT) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x+1);
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_CENTER_LEFT) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x-1);
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_CENTER) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x);
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_CENTER_RIGHT) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x+1);
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_BOTTOM_LEFT) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x-1);
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_BOTTOM_CENTER) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y+1) * level->width_in_tiles + tile_x;
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}
					if (adj_tiles & ISYNTAX_ADJ_TILE_BOTTOM_RIGHT) {
						isyntax_tile_t* source_tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x+1);
						if (source_tile->exists && !(source_tile->has_h && source_tile->has_ll)) {
							continue;
						}
					}



					++tiles_to_load;

					// All the prerequisites have been met, we should be able to load this tile
					isyntax_begin_load_tile(isyntax, wsi_image, scale, tile_x, tile_y);

					if (is_tile_streamer_frame_boundary_passed) {
						goto break_out_of_loop2; // camera bounds updated, recalculate
					}
					i32 tasks_waiting = get_work_queue_task_count(&global_work_queue);
					if (tasks_waiting > logical_cpu_count * 2) {
						goto break_out_of_loop2;
					}



				/*	if (global_worker_thread_idle_count > 0) {
						isyntax_begin_load_tile(isyntax, wsi_image, scale, tile_x, tile_y);
					} else {
						// TODO: abort and restart from the beginning if instructions have changed
						ASSERT(!tile->is_submitted_for_loading);
						tile->is_submitted_for_loading = true;
						u32* tile_pixels = isyntax_load_tile(isyntax, wsi_image, scale, tile_x, tile_y);
						submit_tile_completed(tile_pixels, scale, tile_index, isyntax->tile_width, isyntax->tile_height);
					}*/



				}
			}

			// NOTE: the highest scales need to load before the lower scales!
//			while(is_queue_work_in_progress(&global_work_queue)) {
//				do_worker_work(&global_work_queue, 0);
//			}
		}
		break_out_of_loop2:;

//		if (tiles_to_load > 0) {
//			console_print("Requested %d tiles, tasks waiting = %d, idle = %d\n", tiles_to_load, get_work_queue_task_count(&global_work_queue), global_worker_thread_idle_count);
//		}
		// Cleanup
		for (i32 i = 0; i < scales_to_load_count; ++i) {
			isyntax_load_region_t* region = regions + i;
			if (region->tile_req) free(region->tile_req);
		}
	}

}

typedef struct stream_image_tiles_task_t {
	isyntax_t* isyntax;
	isyntax_image_t* wsi;
} stream_image_tiles_task_t;

void isyntax_stream_image_tiles_func(i32 logical_thread_index, void* userdata) {

	tile_streamer_t* tile_streamer;
	bool need_repeat = false;
	do {
		tile_streamer = (tile_streamer_t*) userdata;
		if (tile_streamer) {
			tile_streamer_t tile_streamer_copy = *tile_streamer; // original may be updated next frame
			isyntax_stream_image_tiles(&tile_streamer_copy, &tile_streamer->image->isyntax.isyntax);
		}
		need_repeat = is_tile_streamer_frame_boundary_passed;
		if (need_repeat) {
			is_tile_streamer_frame_boundary_passed = false;
		}
	} while (need_repeat);
	is_tile_stream_task_in_progress = false;
	atomic_decrement(&tile_streamer->image->isyntax.isyntax.refcount); // release

}



void stream_image_tiles(tile_streamer_t* tile_streamer) {

	if (!is_tile_stream_task_in_progress) {
		atomic_increment(&tile_streamer->image->isyntax.isyntax.refcount); // retain; don't destroy isyntax while busy
		is_tile_stream_task_in_progress = true;
		add_work_queue_entry(&global_work_queue, isyntax_stream_image_tiles_func, tile_streamer);
	} else {
		// TODO: abort/restart stream task when frame boundary passed
		is_tile_streamer_frame_boundary_passed = true;
	}
}

// TODO: decouple I/O operations from decompression, etc.

// TODO: synchronize WSI unloading
void stream_image_tiles2(thread_memory_t* thread_memory) {

	// TODO: move to platform code
	init_arena(&thread_memory->temp_arena, thread_memory->thread_memory_usable_size, thread_memory->aligned_rest_of_thread_memory);


	for (;;) {
		// Get updated task instructions from the main thread.
		benaphore_lock(&tile_streamer_benaphore);
		tile_streamer_t tile_streamer = global_tile_streamer; // local copy
		benaphore_unlock(&tile_streamer_benaphore);

		image_t* image = tile_streamer.image;

		if (image != NULL) {



			switch(tile_streamer.image->backend) {
				default: case IMAGE_BACKEND_NONE: {} break;

				case IMAGE_BACKEND_TIFF:
				case IMAGE_BACKEND_OPENSLIDE: {

				} break;

				case IMAGE_BACKEND_ISYNTAX: {
					isyntax_stream_image_tiles(&tile_streamer, &image->isyntax.isyntax);
				} break;

			}
		}
	}
}



