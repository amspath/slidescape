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


void create_tile_wishlist() {

}

void submit_tile_completed(void* tile_pixels, i32 scale, i32 tile_index, i32 tile_width, i32 tile_height) {
	viewer_notify_tile_completed_task_t* completion_task = (viewer_notify_tile_completed_task_t*) calloc(1, sizeof(viewer_notify_tile_completed_task_t));
	completion_task->pixel_memory = (u8*)tile_pixels;
	completion_task->tile_width = tile_width;
	completion_task->tile_height = tile_height;
	completion_task->scale = scale;
	completion_task->tile_index = tile_index;

	//	console_print("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);
	add_work_queue_entry(&global_completion_queue, viewer_notify_load_tile_completed, completion_task);

}

typedef struct isyntax_data_chunk_t {
	i32 codeblocks_per_color;
	i32 codeblock_count;
	i32 color_offsets[3];
	i32 levels_in_chunk;
	i32 offset_in_file;
	i32 read_size;
	u8* data;
} isyntax_data_chunk_t;

//isyntax_data_chunk_t isyntax_read_data_chunk(isyntax_t* isyntax, isyntax_image_t* wsi_image, i32 base_codeblock_index);

// NOTE: The number of levels present in the highest data chunks depends on the highest scale:
// Highest scale = 8  --> chunk contains levels 6, 7, 8 (most often this is the case)
// Highest scale = 7  --> chunk contains levels 6, 7
// Highest scale = 6  --> chunk contains only level 6
// Highest scale = 5  --> chunk contains levels 3, 4, 5
// Highest scale = 4  --> chunk contains levels 3, 4

void isyntax_do_first_load(thread_memory_t* thread_info, isyntax_t* isyntax, isyntax_image_t* wsi_image) {
	i32 scale = wsi_image->max_scale;
	isyntax_level_t* isyntax_level = wsi_image->levels + scale;
	i32 codeblocks_per_color = isyntax_get_chunk_codeblocks_per_color_for_level(scale, true); // 1 + 4 + 16 (for scale n, n-1, n-2) + 1 (LL block)
	i32 chunk_codeblock_count = codeblocks_per_color * 3;
	i32 block_color_offsets[3] = {0, codeblocks_per_color, 2 * codeblocks_per_color};

	isyntax_tile_t* first_tile = isyntax_level->tiles + 0;
	i32 levels_in_chunk = (wsi_image->codeblocks + isyntax_level->tiles[0].codeblock_chunk_index)->scale % 3;

	temp_memory_t temp_memory = begin_temp_memory(&thread_info->temp_arena);

	u8** data_chunks = arena_push_array(&thread_info->temp_arena, isyntax_level->tile_count, u8*);
	memset(data_chunks, 0, isyntax_level->tile_count * sizeof(u8*));

	// Read codeblock data from disk
	i32 tile_index = 0;
	for (i32 tile_y = 0; tile_y < isyntax_level->height_in_tiles; ++tile_y) {
		for (i32 tile_x = 0; tile_x < isyntax_level->width_in_tiles; ++tile_x, ++tile_index) {
			isyntax_tile_t* isyntax_tile = isyntax_level->tiles + tile_index;
			isyntax_codeblock_t* top_chunk_codeblock = wsi_image->codeblocks + isyntax_tile->codeblock_chunk_index;
			u64 offset0 = top_chunk_codeblock->block_data_offset;

			isyntax_codeblock_t* last_codeblock = wsi_image->codeblocks + isyntax_tile->codeblock_chunk_index + chunk_codeblock_count - 1;
			u64 offset1 = last_codeblock->block_data_offset + last_codeblock->block_size;
			u64 read_size = offset1 - offset0;
			arena_align(&thread_info->temp_arena, 64);
			data_chunks[tile_index] = (u8*) arena_push_size(&thread_info->temp_arena, read_size);

#if WINDOWS
			// TODO: 64 bit read offsets?
			win32_overlapped_read(thread_info, isyntax->win32_file_handle, data_chunks[tile_index], read_size, offset0);
#else
			size_t bytes_read = pread(isyntax->fd, data_chunks[tile_index], read_size, offset0);
#endif
		}
	}
	// Decompress the top level tiles
	for (i32 tile_y = 0; tile_y < isyntax_level->height_in_tiles; ++tile_y) {
		for (i32 tile_x = 0; tile_x < isyntax_level->width_in_tiles; ++tile_x, ++tile_index) {
			isyntax_tile_t* isyntax_tile = isyntax_level->tiles + tile_index;
			isyntax_codeblock_t* top_chunk_codeblock = wsi_image->codeblocks + isyntax_tile->codeblock_chunk_index;
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
				isyntax_decompress_codeblock_in_chunk(h_block, isyntax->block_width, isyntax->block_height, data_chunks[tile_index], offset0);
				isyntax_decompress_codeblock_in_chunk(ll_block, isyntax->block_width, isyntax->block_height, data_chunks[tile_index], offset0);
				isyntax_tile->transformed_ll[i] = isyntax_idwt_tile(ll_block->decoded, h_block->decoded,
				                                         isyntax->block_width, isyntax->block_height, true, i);
			}
			u32 tile_width = isyntax->block_width * 2;
			u32 tile_height = isyntax->block_height * 2;
			i32* Y_coefficients = isyntax_tile->transformed_ll[0];
			i32* Co_coefficients = isyntax_tile->transformed_ll[1];
			i32* Cg_coefficients = isyntax_tile->transformed_ll[2];

			void* tile_pixels = malloc(tile_width * tile_height * sizeof(u32));
			isyntax_wavelet_coefficients_to_bgr_tile((rgba_t*)tile_pixels, Y_coefficients, Co_coefficients, Cg_coefficients, tile_width * tile_height);

			submit_tile_completed(tile_pixels, scale, tile_index, tile_width, tile_height);
		}
	}
	// Decompress the remaining levels in the data chunks.

	if (levels_in_chunk >= 2) {
		i32 current_level = wsi_image->max_scale - 1;
		// First do the Hulsken decompression on all tiles for this level
		for (i32 tile_y = 0; tile_y < isyntax_level->height_in_tiles; ++tile_y) {
			for (i32 tile_x = 0; tile_x < isyntax_level->width_in_tiles; ++tile_x, ++tile_index) {
				isyntax_tile_t* isyntax_tile = isyntax_level->tiles + tile_index;
				isyntax_codeblock_t* top_chunk_codeblock = wsi_image->codeblocks + isyntax_tile->codeblock_chunk_index;
				u64 offset0 = top_chunk_codeblock->block_data_offset;

				for (i32 i = 0; i < chunk_codeblock_count; ++i) {
					isyntax_codeblock_t* codeblock = top_chunk_codeblock + i;
					if (codeblock->scale == current_level) {
						i64 offset_in_chunk = codeblock->block_data_offset - offset0;
						ASSERT(offset_in_chunk >= 0);

						codeblock->data = data_chunks[tile_index] + offset_in_chunk;
						codeblock->decoded = isyntax_hulsken_decompress(codeblock, isyntax->block_width, isyntax->block_height, 1); // TODO: free using _aligned_free()

					}

				}

				/*for (i32 j = 0; j < 4; ++j) {
					isyntax_codeblock_t* h_blocks[3];
					isyntax_codeblock_t* ll_blocks[3];
					i32 h_block_indices[3] = {0, codeblocks_per_color, 2 * codeblocks_per_color};
					i32 ll_block_indices[3] = {codeblocks_per_color - 1, 2 * codeblocks_per_color - 1, 3 * codeblocks_per_color - 1};
					for (i32 i = 0; i < 3; ++i) {
						h_blocks[i] = top_chunk_codeblock + h_block_indices[i];
						ll_blocks[i] = top_chunk_codeblock + ll_block_indices[i];
					}
					for (i32 i = 0; i < 3; ++i) {
						isyntax_codeblock_t* h_block =  h_blocks[i];
						isyntax_codeblock_t* ll_block = ll_blocks[i];
						isyntax_decompress_codeblock_in_chunk(h_block, isyntax->block_width, isyntax->block_height, data_chunks[tile_index], offset0);
						isyntax_decompress_codeblock_in_chunk(ll_block, isyntax->block_width, isyntax->block_height, data_chunks[tile_index], offset0);
						isyntax_tile->transformed_ll[i] = isyntax_idwt_tile(ll_block->decoded, h_block->decoded,
						                                                    isyntax->block_width, isyntax->block_height, true, i);
					}
				}*/
			}
		}
		// Now do the inverse wavelet transforms

	}

	for (i32 tile_y = 0; tile_y < isyntax_level->height_in_tiles; ++tile_y) {
		for (i32 tile_x = 0; tile_x < isyntax_level->width_in_tiles; ++tile_x, ++tile_index) {
			isyntax_tile_t* isyntax_tile = isyntax_level->tiles + tile_index;
			isyntax_codeblock_t* top_chunk_codeblock = wsi_image->codeblocks + isyntax_tile->codeblock_chunk_index;
			u64 offset0 = top_chunk_codeblock->block_data_offset;

			i32 remaining_levels_in_chunk = top_chunk_codeblock->scale % 3;
			if (remaining_levels_in_chunk-- >= 1) {

			}
		}
	}

	end_temp_memory(&temp_memory);

}

void isyntax_stream_image_tiles(thread_memory_t* thread_memory, tile_streamer_t* tile_streamer, isyntax_t* isyntax) {

	isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;

	if (!wsi_image->first_load_complete) {
		isyntax_do_first_load(thread_memory, isyntax, wsi_image);
	} else {
		ASSERT(wsi_image->level_count >= 0);
		i32 highest_visible_scale = ATLEAST(wsi_image->max_scale, 0);
		i32 lowest_visible_scale = ATLEAST(tile_streamer->zoom.level, 0);
		lowest_visible_scale = ATMOST(highest_visible_scale, lowest_visible_scale);

		for (i32 scale = highest_visible_scale; scale >= lowest_visible_scale; --scale) {
			isyntax_level_t* level = wsi_image->levels + scale;


			bounds2i level_tiles_bounds = {{ 0, 0, (i32)level->width_in_tiles, (i32)level->height_in_tiles }};

			bounds2i visible_tiles = world_bounds_to_tile_bounds(&tile_streamer->camera_bounds, level->x_tile_side_in_um,
			                                                     level->y_tile_side_in_um, tile_streamer->origin_offset);
			visible_tiles = clip_bounds2i(&visible_tiles, &level_tiles_bounds);

		}
	}


}

// TODO: decouple I/O operations from decompression, etc.

// TODO: synchronize WSI unloading
void stream_image_tiles(thread_memory_t* thread_memory) {

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
					isyntax_stream_image_tiles(thread_memory, &tile_streamer, &image->isyntax.isyntax);
				} break;

			}
		}
	}
}


void isyntax_idwt_tile(isyntax_t* isyntax, i32 scale, i32 tile_x, i32 tile_y) {
	isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;
	if (scale == wsi_image->max_scale) {
		//
	}
}

void isyntax_load_top_level_tiles(isyntax_t* isyntax) {
	isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;
	ASSERT(wsi_image->image_type == ISYNTAX_IMAGE_TYPE_WSI);
	isyntax_level_t* top_level = wsi_image->levels + wsi_image->max_scale;
	i32 tile_index = 0;
	for (i32 tile_y = 0; tile_y < top_level->height_in_tiles; ++tile_y) {
		for (i32 tile_x = 0; tile_x < top_level->width_in_tiles; ++tile_x, ++tile_index) {
			isyntax_tile_t* tile = top_level->tiles + tile_index;
			
		}
	}
}



