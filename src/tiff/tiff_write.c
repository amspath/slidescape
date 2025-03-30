/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2025  Pieter Valkema

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
#include "mathutils.h"
#include "stringutils.h"

#include "tiff.h"
#include "viewer.h"
#include "image_resize.h"
#include "jpeg_decoder.h"

#include "tiff_write.h"

#include "stb_image_write.h" //for debugging only

enum export_region_format_enum {
	EXPORT_REGION_FORMAT_BIGTIFF = 0,
	EXPORT_REGION_FORMAT_JPEG = 1,
	EXPORT_REGION_FORMAT_PNG = 2,
};

typedef struct encode_tile_task_t {
	image_t* image;
	tiff_t* tiff;
	tiff_ifd_t* ifd;
	i32 level;
	u32 export_tile_width;
	i32 export_tile_x;
	i32 export_tile_y;
	bounds2i pixel_bounds;
} encode_tile_task_t;

void encode_tile_func(i32 logical_thread_index, void* userdata) {
	encode_tile_task_t* task = (encode_tile_task_t*) userdata;
	image_t* image = task->image;
	tiff_t* tiff = task->tiff;
	tiff_ifd_t* ifd = task->ifd;
	i32 level = task->level;
	u32 export_tile_width = task->export_tile_width;
	i32 export_tile_x = task->export_tile_x;
	i32 export_tile_y = task->export_tile_y;
	bounds2i pixel_bounds = task->pixel_bounds;

	i32 export_width_in_pixels = pixel_bounds.right - pixel_bounds.left;
	i32 export_height_in_pixels = pixel_bounds.bottom - pixel_bounds.top;
	ASSERT(export_width_in_pixels > 0);
	ASSERT(export_height_in_pixels > 0);

	bounds2i source_tile_bounds = pixel_bounds;
	source_tile_bounds.left = div_floor(pixel_bounds.left, export_tile_width);
	source_tile_bounds.top = div_floor(pixel_bounds.top, export_tile_width);
	source_tile_bounds.right = div_floor(pixel_bounds.right, export_tile_width);
	source_tile_bounds.bottom = div_floor(pixel_bounds.bottom, export_tile_width);

	i32 source_bounds_width_in_tiles = source_tile_bounds.right - source_tile_bounds.left;
	i32 source_bounds_height_in_tiles = source_tile_bounds.bottom - source_tile_bounds.top;
	u32 source_tile_count = source_bounds_width_in_tiles * source_bounds_height_in_tiles;

	cached_tile_t* source_tiles = alloca(source_tile_count * sizeof(cached_tile_t));
	u8* dest_pixels = malloc(export_tile_width * export_tile_width * 4);

	for (i32 source_tile_y = 0; source_tile_y < source_bounds_height_in_tiles; ++source_tile_y) {
		for (i32 source_tile_x = 0; source_tile_x < source_bounds_width_in_tiles; ++source_tile_x) {

			// load the tile into memory
		}
	}

}

typedef struct offset_fixup_t {
	u64 offset_to_fix;
	u64 offset_from_unknown_base;
} offset_fixup_t;

static inline void add_fixup(memrw_t* fixups_buffer, u64 offset_to_fix, u64 offset_from_unknown_base) {
	// NOTE: 'offset_to_fix' can't be a direct pointer to the value that needs to be fixed up, because
	// pointers to the destination buffer might be unstable because the destination buffer might resize.
	// so instead we are storing it as an offset from the start of the destination buffer.
	offset_fixup_t new_fixup = {offset_to_fix, offset_from_unknown_base};
	memrw_push_back(fixups_buffer, &new_fixup, sizeof(new_fixup));
}

static inline u64 memrw_push_bigtiff_tag(memrw_t* buffer, raw_bigtiff_tag_t* tag) {
	u64 write_offset = memrw_push_back(buffer, tag, sizeof(*tag));
	return write_offset;
}

static u64 add_large_bigtiff_tag(memrw_t* tag_buffer, memrw_t* data_buffer, memrw_t* fixups_buffer,
                                                u16 tag_code, u16 tag_type, u64 tag_data_count, void* tag_data) {
	// NOTE: tag_data is allowed to be NULL, in that case we are only pushing placeholder data (zeroes)
	u32 field_size = get_tiff_field_size(tag_type);
	u64 tag_data_size = field_size * tag_data_count;
	if (tag_data_size <= 8) {
		raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_count, .data_u64 = 0};
		if (tag_data) {
			memcpy(tag.data, tag_data, tag_data_size);
		}
		u64 write_offset = memrw_push_bigtiff_tag(tag_buffer, &tag);
		return write_offset;
	} else {
		u64 data_offset = memrw_push_back(data_buffer, tag_data, tag_data_size);
		raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_count, .offset = data_offset};
		u64 write_offset = memrw_push_bigtiff_tag(tag_buffer, &tag);
		// NOTE: we cannot store a raw pointer to the offset we need to fix later, because the buffer
		// might resize (pointer is unstable).
		add_fixup(fixups_buffer, write_offset + offsetof(raw_bigtiff_tag_t, offset), data_offset);
		return write_offset;
	}
}

typedef struct export_level_task_data_t {
	i32 level;
	bool is_represented;
	u64 offset_of_tile_offsets;
	u64 offset_of_tile_bytecounts;
	bool are_tile_offsets_inlined_in_tag;
	bounds2i pixel_bounds;
	u32 export_width_in_tiles;
	u32 export_height_in_tiles;
	u32 export_tile_count;
	bounds2i source_tile_bounds;
	u32 source_bounds_width_in_tiles;
	u32 source_bounds_height_in_tiles;
	u32 source_tile_count;
	tile_t** source_tiles;
} export_level_task_data_t;

typedef struct export_task_data_t {
	i32 ifd_count;
	i32 max_level;
	i32 source_tile_width;
	i32 export_tile_width;
	i32 quality;
	u64 image_data_base_offset;
	u64 current_image_data_write_offset;
	u64 total_tiles_to_export;
	float progress_per_exported_tile; // for progress bar
	volatile i32 tiles_left_to_compress_in_batch;
	FILE* fp;
	bool use_rgb;
	bool allow_sparse_tile_storage;
	bool is_valid;
	export_level_task_data_t level_task_datas[WSI_MAX_LEVELS];
} export_task_data_t;

void export_notify_load_tile_completed(int logical_thread_index, void* userdata) {
	viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*)userdata;
	work_queue_submit_task(&global_export_completion_queue, export_notify_load_tile_completed, userdata,
	                       sizeof(viewer_notify_tile_completed_task_t));
}

void construct_new_tile_from_source_tiles(export_task_data_t* export_task, export_level_task_data_t* level_task, i32 export_tile_x, i32 export_tile_y, u8** jpeg_buffer, u32* jpeg_size) {
	u32 export_tile_width = export_task->export_tile_width;
	i32 source_tile_width = export_task->source_tile_width;
	u64 tile_size_in_bytes = SQUARE(export_tile_width) * BYTES_PER_PIXEL;
	u8* dest = malloc(tile_size_in_bytes);
	memset(dest, 0xFF, tile_size_in_bytes);

	i32 source_tile_offset_x = level_task->pixel_bounds.left % source_tile_width;
	i32 source_tile_offset_y = level_task->pixel_bounds.top % source_tile_width;
	i32 remainder_x = (level_task->pixel_bounds.right - level_task->pixel_bounds.left) % export_tile_width;
	i32 remainder_y = (level_task->pixel_bounds.bottom - level_task->pixel_bounds.top) % export_tile_width;
	i32 extra_tiles_x = (source_tile_offset_x + export_tile_width - 1) / source_tile_width;
	i32 extra_tiles_y = (source_tile_offset_y + export_tile_width - 1) / source_tile_width;
	// TODO: Find a way that this makes more sense. We don't want to go out of bounds for the source tile!
	if (extra_tiles_x > 0 && export_tile_x == level_task->export_width_in_tiles - 1) {
		extra_tiles_x = (source_tile_offset_x + remainder_x - 1) / source_tile_width;
	}
	if (extra_tiles_y > 0 && export_tile_y == level_task->export_height_in_tiles - 1) {
		extra_tiles_y = (source_tile_offset_y + remainder_y - 1) / source_tile_width;
	}


	i32 source_tile_index = export_tile_y * level_task->source_bounds_width_in_tiles + export_tile_x;
//					i32 source_tile_x = source_tile_index % level_task->source_bounds_width_in_tiles;
//					i32 source_tile_y = source_tile_index / level_task->source_bounds_width_in_tiles;

	i32 source_pitch = source_tile_width * BYTES_PER_PIXEL;
	i32 dest_pitch = export_task->export_tile_width * BYTES_PER_PIXEL;

	i32 dest_left_section_width = source_tile_width - source_tile_offset_x;
	i32 dest_top_section_height = source_tile_width - source_tile_offset_y;
	i32 dest_right_section_width = export_tile_width - dest_left_section_width;
	i32 dest_bottom_section_height = export_tile_width - dest_top_section_height;

	// TODO: what if source tile size and export tile size are not the same?

	i32 contributing_source_tiles_count = 0;

	// Top-left source tile
	{
		tile_t* source_tile = level_task->source_tiles[source_tile_index];
		if (source_tile && !source_tile->is_empty) {
			++contributing_source_tiles_count;
			ASSERT(source_tile->is_cached && source_tile->pixels);
			u8* source_pos = source_tile->pixels +
			                 source_tile_offset_y * source_pitch + source_tile_offset_x * BYTES_PER_PIXEL;
			u8* dest_pos = dest;
			for(i32 y = 0; y < dest_top_section_height; ++y) {
				memcpy(dest_pos, source_pos, dest_left_section_width * BYTES_PER_PIXEL);
				dest_pos += dest_pitch;
				source_pos += source_pitch;
			}
		}
	}

	// Top-right source tile
	if (extra_tiles_x == 1) {
		tile_t* source_tile = level_task->source_tiles[source_tile_index + 1];
		if (source_tile && !source_tile->is_empty) {
			++contributing_source_tiles_count;
			ASSERT(source_tile->is_cached && source_tile->pixels);
			u8* source_pos = source_tile->pixels + source_tile_offset_y * source_pitch;
			u8* dest_pos = dest + dest_left_section_width * BYTES_PER_PIXEL;
			for(i32 y = 0; y < dest_top_section_height; ++y) {
				memcpy(dest_pos, source_pos, dest_right_section_width * BYTES_PER_PIXEL);
				dest_pos += dest_pitch;
				source_pos += source_pitch;
			}
		}
	}

	// Bottom-left source tile
	if (extra_tiles_y == 1) {
		tile_t* source_tile = level_task->source_tiles[source_tile_index + level_task->source_bounds_width_in_tiles];
		if (source_tile && !source_tile->is_empty) {
			++contributing_source_tiles_count;
			ASSERT(source_tile->is_cached && source_tile->pixels);
			u8* source_pos = source_tile->pixels + source_tile_offset_x * BYTES_PER_PIXEL;
			u8* dest_pos = dest + dest_top_section_height * dest_pitch;
			for(i32 y = 0; y < dest_bottom_section_height; ++y) {
				memcpy(dest_pos, source_pos, dest_left_section_width * BYTES_PER_PIXEL);
				dest_pos += dest_pitch;
				source_pos += source_pitch;
			}
		}
	}

	// Bottom-right source tile
	if (extra_tiles_x == 1 && extra_tiles_y == 1) {
		tile_t* source_tile = level_task->source_tiles[source_tile_index + level_task->source_bounds_width_in_tiles + 1];
		if (source_tile && !source_tile->is_empty) {
			++contributing_source_tiles_count;
			ASSERT(source_tile->is_cached && source_tile->pixels);
			u8* source_pos = source_tile->pixels;
			u8* dest_pos = dest + dest_top_section_height * dest_pitch + dest_left_section_width * BYTES_PER_PIXEL;
			for(i32 y = 0; y < dest_bottom_section_height; ++y) {
				memcpy(dest_pos, source_pos, dest_right_section_width * BYTES_PER_PIXEL);
				dest_pos += dest_pitch;
				source_pos += source_pitch;
			}
		}
	}

		// Now we have a fully assembled tile.

#if 0
		static bool already_tested_jpeg_encode;
					if (!already_tested_jpeg_encode) {
						already_tested_jpeg_encode = true;
						u8* jpeg_buffer = NULL;
						u32 jpeg_size = 0;
						jpeg_encode_image(dest, export_tile_width, export_tile_width, quality,
						                  &jpeg_buffer, &jpeg_size);

						FILE* test_fp = fopen("test.jpeg", "wb");
						if (test_fp) {
							fwrite(jpeg_buffer, jpeg_size, 1, test_fp);
							fclose(test_fp);
						}
					}
#endif

	// empty tiles would only waste space, so skip them
	bool skip = export_task->allow_sparse_tile_storage && (contributing_source_tiles_count == 0);
	if (!skip) {
		u8* compressed_buffer = NULL;
		u64 compressed_size = 0;
		jpeg_encode_tile(dest, export_tile_width, export_tile_width, export_task->quality, NULL, NULL,
		                 &compressed_buffer, &compressed_size, export_task->use_rgb);

		*jpeg_buffer = compressed_buffer;
		*jpeg_size = compressed_size;
	} else {
		console_print_verbose("Skipped empty tile %d, %d (level %d)\n", export_tile_x, export_tile_y, level_task->level);
	}

	free(dest);
}

typedef struct construct_tile_task_t {
	export_task_data_t* export_task;
	export_level_task_data_t* level_task;
	i32 export_tile_x;
	i32 export_tile_y;
	u8** jpeg_buffer;
	u32* jpeg_size;
} construct_tile_task_t;

void construct_new_tile_from_source_tiles_func(i32 logical_thread_id, void* userdata) {
	construct_tile_task_t* task = (construct_tile_task_t*) userdata;
	construct_new_tile_from_source_tiles(task->export_task, task->level_task, task->export_tile_x, task->export_tile_y, task->jpeg_buffer, task->jpeg_size);
	atomic_decrement(&task->export_task->tiles_left_to_compress_in_batch);
}

void begin_construct_new_tile_from_source_tiles(export_task_data_t* export_task, export_level_task_data_t* level_task, i32 export_tile_x, i32 export_tile_y, u8** jpeg_buffer, u32* jpeg_size) {
	construct_tile_task_t task = {0};
	task.export_task = export_task;
	task.level_task = level_task;
	task.export_tile_x = export_tile_x;
	task.export_tile_y = export_tile_y;
	task.jpeg_buffer = jpeg_buffer;
	task.jpeg_size = jpeg_size;

	if (!work_queue_submit_task(&global_work_queue, construct_new_tile_from_source_tiles_func, &task, sizeof(task))) {
		fatal_error();
	}
}

void export_bigtiff_encode_level(app_state_t* app_state, image_t* image, export_task_data_t* export_task, i32 level) {
	export_level_task_data_t* level_task = export_task->level_task_datas + level;
	if (!level_task->is_represented) return;

	float seconds_taken_reading = 0.0f;
	float seconds_taken_compressing = 0.0f;

	u64* tile_offsets = calloc(level_task->export_tile_count, sizeof(u64));
	u64* tile_bytecounts = calloc(level_task->export_tile_count, sizeof(u64));


	u32 batch_size = ATLEAST(global_worker_thread_count, 1);
	u32 batch_count = (level_task->export_tile_count + batch_size - 1) / batch_size;

	// TODO: don't assume unaligned!
//			bool x_aligned = level_task->export_width_in_tiles == level_task->source_bounds_width_in_tiles;
//			bool y_aligned = level_task->export_height_in_tiles == level_task->source_bounds_height_in_tiles;

	u32 export_tile_width = export_task->export_tile_width;
	i32 source_tile_width = export_task->source_tile_width;
	i32 source_tile_offset_x = level_task->pixel_bounds.left % source_tile_width;
	i32 source_tile_offset_y = level_task->pixel_bounds.top % source_tile_width;
	i32 remainder_x = (level_task->pixel_bounds.right - level_task->pixel_bounds.left) % export_tile_width;
	i32 remainder_y = (level_task->pixel_bounds.bottom - level_task->pixel_bounds.top) % export_tile_width;
	i32 extra_tiles_x = (source_tile_offset_x + export_tile_width - 1) / source_tile_width;
	i32 extra_tiles_y = (source_tile_offset_y + export_tile_width - 1) / source_tile_width;

	i32 source_tile_pitch = level_task->source_bounds_width_in_tiles;

	for (i32 batch = 0; batch < batch_count; ++batch) {
//		console_print_verbose("  starting batch %d out of %d\n", batch, batch_count);
		u32 start_tile_index = batch * batch_size;
		i32 tiles_left = level_task->export_tile_count - start_tile_index;
		i32 current_batch_size = MIN(tiles_left, batch_size);
//				bool is_last_batch = (tiles_left / batch_size) == 0;
		u32 end_tile_index = start_tile_index + (current_batch_size - 1);
		i32 start_tile_x = start_tile_index % level_task->export_width_in_tiles;
		i32 start_tile_y = start_tile_index / level_task->export_width_in_tiles;
		i32 end_tile_x = end_tile_index % level_task->export_width_in_tiles;
		i32 end_tile_y = end_tile_index / level_task->export_width_in_tiles;

		// TODO: Find a way that this makes more sense. We don't want to go out of bounds for the source tile!
		/*if (end_tile_x == level_task->export_width_in_tiles - 1) {
			extra_tiles_x = (source_tile_offset_x + remainder_x - 1) / source_tile_width;
		}
		if (end_tile_y == level_task->export_height_in_tiles - 1) {
			extra_tiles_y = (source_tile_offset_y + remainder_y - 1) / source_tile_width;
		}*/
//				ASSERT(end_tile_x + extra_tiles_x < level_task->source_bounds_width_in_tiles);
//				ASSERT(end_tile_y + extra_tiles_y < level_task->source_bounds_height_in_tiles);

		u32 jpeg_compressed_sizes[MAX_THREAD_COUNT] = {0};
		u8* jpeg_compressed_buffers[MAX_THREAD_COUNT] = {0};

		i32 first_source_tile_needed = start_tile_y * source_tile_pitch + start_tile_x;
		i32 last_source_tile_needed = (end_tile_y + extra_tiles_y) * source_tile_pitch + end_tile_x + extra_tiles_x;
		last_source_tile_needed = ATMOST(last_source_tile_needed, level_task->source_tile_count - 1);
		i32 source_tiles_needed = last_source_tile_needed - first_source_tile_needed + 1;

		load_tile_task_t* wishlist = calloc(source_tiles_needed, sizeof(load_tile_task_t));
		i32 tiles_to_load = 0;

		i64 read_time_start = get_clock();

		benaphore_lock(&image->lock);
		for (i32 tile_index = 0; tile_index <= last_source_tile_needed; ++tile_index) {
			tile_t* tile = level_task->source_tiles[tile_index];
			if (tile_index < first_source_tile_needed) {
				// Release tiles that are no longer needed.
				if (tile && tile->is_cached && tile->pixels) {
					tile_release_cache(tile);
				}
			} else {
//				console_print_verbose("   tile %d out of %d\n", tile_index, level_task->export_tile_count);
				// Load needed tiles into system cache.
				if (tile) {
					if (tile->is_empty) continue; // no need to load empty tiles
					if (tile->is_cached && tile->pixels) {
						continue; // already cached!
					} else {
						tile->need_keep_in_cache = true;
						wishlist[tiles_to_load++] = (load_tile_task_t){
								.resource_id = image->resource_id,
								.image = image, .tile = tile, .level = level,
								.tile_x = tile->tile_x,
								.tile_y = tile->tile_y,
								.need_gpu_residency = tile->need_gpu_residency,
								.need_keep_in_cache = true,
								.completion_callback = export_notify_load_tile_completed,
						};
					}
				}
			}
		}

		request_tiles(image, wishlist, tiles_to_load);
		benaphore_unlock(&image->lock);

		free(wishlist);
		wishlist = NULL;

		// TODO: fix the copy-pasta
		i32 pixel_transfer_index_start = app_state->next_pixel_transfer_to_submit;
		while (work_queue_is_work_in_progress(&global_work_queue) ||
               work_queue_is_work_in_progress(&global_export_completion_queue)) {
			work_queue_entry_t entry = work_queue_get_next_entry(&global_export_completion_queue);
			if (entry.is_valid) {
				if (!entry.callback) fatal_error();
                work_queue_mark_entry_completed(&global_export_completion_queue);

				if (entry.callback == export_notify_load_tile_completed) {
					benaphore_lock(&image->lock);
					viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*) entry.userdata;
					tile_t* tile = get_tile_from_tile_index(image, task->scale, task->tile_index);
					if (task->pixel_memory) {
						bool need_free_pixel_memory = true;
						if (tile) {
							tile->is_submitted_for_loading = false;
							if (tile->need_gpu_residency) {
								/*pixel_transfer_state_t* transfer_state =
										submit_texture_upload_via_pbo(app_state, task->tile_width, task->tile_width,
																	  4, task->pixel_memory, finalize_textures_immediately);
								if (finalize_textures_immediately) {
									tile->texture = transfer_state->texture;
								} else {
									transfer_state->userdata = (void*) tile;
								}*/

							}
							if (tile->need_keep_in_cache) {
								need_free_pixel_memory = false;
								tile->pixels = task->pixel_memory;
								tile->is_cached = true;
							}
						}
						if (need_free_pixel_memory) {
							free(task->pixel_memory);
						}
					} else {
						tile->is_empty = true;
					}
					benaphore_unlock(&image->lock);

//					free(entry.data);
				} /*else if (entry.callback == viewer_upload_already_cached_tile_to_gpu) {
							load_tile_task_t* task = (load_tile_task_t*) entry.data;
							tile_t* tile = task->tile;
							if (tile->is_cached && tile->pixels) {
								if (tile->need_gpu_residency) {
									pixel_transfer_state_t* transfer_state = submit_texture_upload_via_pbo(app_state, TILE_DIM,
									                                                                       TILE_DIM, 4,
									                                                                       tile->pixels, finalize_textures_immediately);
									tile->texture = transfer_state->texture;
								} else {
									ASSERT(!"viewer_only_upload_cached_tile() called but !tile->need_gpu_residency\n");
								}

								if (!task->need_keep_in_cache) {
									free(tile->pixels);
									tile->pixels = NULL;
									tile->is_cached = false;
								}
							} else {
								console_print("Warning: viewer_only_upload_cached_tile() called on a non-cached tile\n");
							}
						}*/
			}


		}//end of while loop

		// Verify that all tiles are now available
		for (i32 tile_index = first_source_tile_needed; tile_index <= last_source_tile_needed; ++tile_index) {
			tile_t* tile = level_task->source_tiles[tile_index];

			if (tile) {
				if (tile->is_empty) continue;
				if (tile->is_cached && tile->pixels) {
					continue; // already cached!
				} else {
					ASSERT(!"This tile should have been loaded!\n");
				}
			}

		}
		seconds_taken_reading += get_seconds_elapsed(read_time_start, get_clock());


		// Now we can proceed with constructing the new tiles from the source tiles, and writing them to disk.
		i64 compress_time_start = get_clock();
		export_task->tiles_left_to_compress_in_batch = current_batch_size;

		// Begin JPEG compression tasks for each tile.
		for (i32 tile_index = start_tile_index; tile_index <= end_tile_index; ++tile_index) {
			i32 export_tile_x = tile_index % level_task->export_width_in_tiles;
			i32 export_tile_y = tile_index / level_task->export_width_in_tiles;

			i32 work_index = tile_index % batch_size;
			u8** jpeg_buffer = &jpeg_compressed_buffers[work_index];
			u32* jpeg_size = &jpeg_compressed_sizes[work_index];

			begin_construct_new_tile_from_source_tiles(export_task, level_task, export_tile_x, export_tile_y, jpeg_buffer, jpeg_size);
		}

		// Wait for all compression tasks in the batch to finish.
		float saved_tiff_export_progress = global_tiff_export_progress;
		for(;;) {
			i32 tiles_left_to_compress_in_batch = export_task->tiles_left_to_compress_in_batch;
			global_tiff_export_progress = saved_tiff_export_progress + (current_batch_size - export_task->tiles_left_to_compress_in_batch) * export_task->progress_per_exported_tile;
			if (!(tiles_left_to_compress_in_batch > 0)) {
				break;
			} else {
				if (work_queue_is_work_waiting_to_start(&global_work_queue)) {
                    work_queue_do_work(&global_work_queue, 0);
				}
			}
//			if (tiles_left_to_compress_in_batch > 0) {
//				console_print_verbose("tiles left to compress: %d\n", tiles_left_to_compress_in_batch);
//			}
		}
		global_tiff_export_progress = saved_tiff_export_progress + current_batch_size* export_task->progress_per_exported_tile;

		// batch completed.

		fseeko64(export_task->fp, export_task->current_image_data_write_offset, SEEK_SET);

		for (i32 work_index = 0; work_index < current_batch_size; ++work_index) {
			u8* compressed_buffer = jpeg_compressed_buffers[work_index];
			u32 compressed_size = jpeg_compressed_sizes[work_index];

			i32 tile_index = start_tile_index + work_index;
			tile_offsets[tile_index] = export_task->current_image_data_write_offset;
			tile_bytecounts[tile_index] = compressed_size;

			fwrite(compressed_buffer, compressed_size, 1, export_task->fp);
			libc_free(compressed_buffer);
			jpeg_compressed_buffers[work_index] = NULL;
			export_task->current_image_data_write_offset += compressed_size;

		}
		seconds_taken_compressing += get_seconds_elapsed(compress_time_start, get_clock());
	}
	// level export completed

	console_print_verbose("Export level %d: tile count = %d, read time = %g, compress time = %g\n",
						  level, level_task->export_tile_count, seconds_taken_reading, seconds_taken_compressing);

	// Rewrite the tile offsets and tile bytecounts
	fseeko64(export_task->fp, level_task->offset_of_tile_offsets, SEEK_SET);
	fwrite(tile_offsets, sizeof(u64), level_task->export_tile_count, export_task->fp);

	fseeko64(export_task->fp, level_task->offset_of_tile_bytecounts, SEEK_SET);
	fwrite(tile_bytecounts, sizeof(u64), level_task->export_tile_count, export_task->fp);

	free(tile_offsets);
	free(tile_bytecounts);

	for (i32 tile_index = 0; tile_index < level_task->source_tile_count; ++tile_index) {
		tile_t* tile = level_task->source_tiles[tile_index];

		if (tile) {
			if (tile->is_cached && tile->pixels) {
				tile_release_cache(tile);
			}
		}

	}

	free(level_task->source_tiles);
}

bool export_cropped_bigtiff(app_state_t* app_state, image_t* image, bounds2f world_bounds, bounds2i level0_bounds, const char* filename,
                              u32 export_tile_width, u16 desired_photometric_interpretation, i32 quality, u32 export_flags) {


	switch(desired_photometric_interpretation) {
		case TIFF_PHOTOMETRIC_YCBCR: break;
		case TIFF_PHOTOMETRIC_RGB: break;
		default: {
			console_print_error("Error exporting BigTIFF: unsupported photometric interpretation (%d)\n", desired_photometric_interpretation);
		} return false;
	}

	// TODO: make ASAP understand the resolution in the exported file
	u32 tile_width = image->tile_width;
	u32 tile_height = image->tile_height;
	if (image->backend == IMAGE_BACKEND_TIFF) {
		tiff_t* tiff = &image->tiff;
		if (!(tiff && tiff->main_image_ifd && (tiff->mpp_x > 0.0f) && (tiff->mpp_y > 0.0f))) {
			return false;
		}
		tiff_ifd_t* source_level0_ifd = tiff->main_image_ifd;
		tile_width = source_level0_ifd->tile_width;
		tile_height = source_level0_ifd->tile_height;
		bool is_tile_aligned = ((level0_bounds.left % tile_width) == 0) && ((level0_bounds.top % tile_height) == 0);

		bool need_reuse_tiles = is_tile_aligned && (desired_photometric_interpretation == source_level0_ifd->color_space)
		                        && (export_tile_width == tile_width) && (tile_width == tile_height); // only allow square tiles for re-use
	}

	export_task_data_t export_task = {0};
	export_task.source_tile_width = tile_width;
	export_task.export_tile_width = export_tile_width;
	export_task.quality = quality;
	export_task.use_rgb = (desired_photometric_interpretation == TIFF_PHOTOMETRIC_RGB);
	export_task.allow_sparse_tile_storage = false;
	export_task.total_tiles_to_export = 0;

	FILE* fp = fopen64(filename, "wb");
	bool32 success = false;
	if (fp) {

		// We will prepare all the tags, and push them into a temporary buffer, to be written to file later.
		// For non-inlined tags, the 'offset' field gets a placeholder offset because we don't know yet
		// where the tag data will be located in the file. For such tags we will:
		// - Push the data into a separate buffer, and remember the relative offset within that buffer
		// - Create a 'fixup', so that we can later substitute the offset once we know the base offset
		//   where we will store the separate data buffer in the output file.

		// temporary buffer for only the TIFF header + IFD tags
		memrw_t tag_buffer = memrw_create(KILOBYTES(64));
		// temporary buffer for all data >8 bytes not fitting in the raw TIFF tags (leaving out the pixel data)
		memrw_t small_data_buffer = memrw_create(MEGABYTES(1));
		// temporary buffer tracking the offsets that we need to fix after writing all the IFDs
		memrw_t fixups_buffer = memrw_create(1024);

		// Write the TIFF header (except the offset to the first IFD, which we will push when iterating over the IFDs)
		tiff_header_t header = {0};
		header.byte_order_indication = 0x4949; // little-endian
		header.filetype = 0x002B; // BigTIFF
		header.bigtiff.offset_size = 0x0008;
		header.bigtiff.always_zero = 0;
		memrw_push_back(&tag_buffer, &header, 8);

		// NOTE: the downsampling level does not necessarily equal the ifd index.

		// TODO: reconstruct tiles from level0 instead of doing it this way
		if (image->backend == IMAGE_BACKEND_TIFF) {
			tiff_t* tiff = &image->tiff;
			tiff_ifd_t* source_ifd = tiff->main_image_ifd;
			i32 source_ifd_index = 0;
			for (i32 level = 0; level < image->level_count; ++level) {
				export_level_task_data_t* level_task_data = export_task.level_task_datas + level;
				level_task_data->level = level;

				// Find an IFD for this downsampling level
				if (source_ifd->downsample_level == level) {
					level_task_data->is_represented = true;
				} else {
					++source_ifd_index;
					bool found = false;
					for (i32 i = source_ifd_index; i < tiff->level_image_ifd_count; ++i) {
						tiff_ifd_t* ifd = tiff->level_images_ifd + i;
						if (ifd->downsample_level == level) {
							found = true;
							level_task_data->is_represented = true;
							source_ifd_index = i;
							source_ifd = ifd;
							break;
						}
					}
					if (!found) {
						console_print_verbose("Warning: source TIFF does not contain level %d, will be skipped\n", level);
						continue;
					}
				}
			}
		}

		i32 export_ifd_count = 0;
		i32 export_max_level = 0;

		// Tags that are the same for all image levels
		raw_bigtiff_tag_t tag_new_subfile_type = {TIFF_TAG_NEW_SUBFILE_TYPE, TIFF_UINT32, 1, .offset = TIFF_FILETYPE_REDUCEDIMAGE};
		u16 bits_per_sample[4] = {8, 8, 8, 0};
		raw_bigtiff_tag_t tag_bits_per_sample = {TIFF_TAG_BITS_PER_SAMPLE, TIFF_UINT16, 3, .offset = *(u64*)bits_per_sample};
		raw_bigtiff_tag_t tag_compression = {TIFF_TAG_COMPRESSION, TIFF_UINT16, 1, .offset = TIFF_COMPRESSION_JPEG};
		raw_bigtiff_tag_t tag_photometric_interpretation = {TIFF_TAG_PHOTOMETRIC_INTERPRETATION, TIFF_UINT16, 1, .offset = desired_photometric_interpretation};
		raw_bigtiff_tag_t tag_orientation = {TIFF_TAG_ORIENTATION, TIFF_UINT16, 1, .offset = TIFF_ORIENTATION_TOPLEFT};
		raw_bigtiff_tag_t tag_samples_per_pixel = {TIFF_TAG_SAMPLES_PER_PIXEL, TIFF_UINT16, 1, .offset = 3};
		raw_bigtiff_tag_t tag_tile_length = {TIFF_TAG_TILE_LENGTH, TIFF_UINT16, 1, .offset = export_tile_width};
		raw_bigtiff_tag_t tag_tile_width = {TIFF_TAG_TILE_WIDTH, TIFF_UINT16, 1, .offset = export_tile_width};
		raw_bigtiff_tag_t tag_resolution_unit = {TIFF_TAG_RESOLUTION_UNIT, TIFF_UINT16, 1, .data_u16 = 3 /*RESUNIT_CENTIMETER*/};
		// NOTE: chroma subsampling is used for YCbCr-encoded images, but not for RGB
		u16 chroma_subsampling[4] = {2, 2, 0, 0};
		raw_bigtiff_tag_t tag_chroma_subsampling = {TIFF_TAG_YCBCRSUBSAMPLING, TIFF_UINT16, 2, .offset = *(u64*)(chroma_subsampling)};

		bool reached_level_with_only_one_tile_in_it = false;
		for (i32 level = 0; level < image->level_count && !reached_level_with_only_one_tile_in_it; ++level) {

			export_level_task_data_t* level_task_data = export_task.level_task_datas + level;

			// TODO: reconstruct tiles from level0 instead of doing it this way
			if (image->backend == IMAGE_BACKEND_TIFF) {
				if (!level_task_data->is_represented) {
					continue; // skip
				}
			}

			export_max_level = level;
			++export_ifd_count;
			level_image_t* source_level_image = image->level_images + level;

			// Offset to the beginning of the next IFD (= 8 bytes directly after the current offset)
			u64 next_ifd_offset = tag_buffer.used_size + sizeof(u64);
			memrw_push_back(&tag_buffer, &next_ifd_offset, sizeof(u64));

			u64 tag_count_for_ifd = 0;
			u64 tag_count_for_ifd_offset = memrw_push_back(&tag_buffer, &tag_count_for_ifd, sizeof(u64));

			// Calculate dimensions for the current downsampling level
			bounds2i pixel_bounds = level0_bounds;
			pixel_bounds.left >>= level;
			pixel_bounds.top >>= level;
			pixel_bounds.right >>= level;
			pixel_bounds.bottom >>= level;

			i32 export_width_in_pixels = pixel_bounds.right - pixel_bounds.left;
			i32 export_height_in_pixels = pixel_bounds.bottom - pixel_bounds.top;

			u32 export_width_in_tiles = (export_width_in_pixels + (export_tile_width - 1)) / export_tile_width;
			u32 export_height_in_tiles = (export_height_in_pixels + (export_tile_width - 1)) / export_tile_width;
			u64 export_tile_count = export_width_in_tiles * export_height_in_tiles;
			ASSERT(export_tile_count > 0);
			if (export_tile_count <= 1) {
				reached_level_with_only_one_tile_in_it = true; // should be the last level, no point in further downsampling
			}
			export_task.total_tiles_to_export += export_tile_count;

			level_task_data->is_represented = true;
			level_task_data->pixel_bounds = pixel_bounds;
			level_task_data->export_width_in_tiles = export_width_in_tiles;
			level_task_data->export_height_in_tiles = export_height_in_tiles;
			level_task_data->export_tile_count = export_tile_count;

			// Make some preparations for requesting the source tiles we will need to generate the new tiles.

			i32 source_bounds_width_in_tiles = export_width_in_tiles + 1;//source_tile_bounds.right - source_tile_bounds.left + 1;
			i32 source_bounds_height_in_tiles = export_height_in_tiles + 1; //source_tile_bounds.bottom - source_tile_bounds.top + 1;
			u32 source_tile_count = source_bounds_width_in_tiles * source_bounds_height_in_tiles;

			bounds2i source_tile_bounds = pixel_bounds;
			source_tile_bounds.left = div_floor(pixel_bounds.left, export_tile_width);
			source_tile_bounds.top = div_floor(pixel_bounds.top, export_tile_width);
			source_tile_bounds.right = source_tile_bounds.left + export_tile_width * source_bounds_width_in_tiles;//div_floor(pixel_bounds.right + export_tile_width - 1, export_tile_width);
			source_tile_bounds.bottom = source_tile_bounds.top + export_tile_width * source_bounds_height_in_tiles;//div_floor(pixel_bounds.bottom  + export_tile_width - 1, export_tile_width);

			level_task_data->source_tile_bounds = source_tile_bounds;
			level_task_data->source_bounds_width_in_tiles = source_bounds_width_in_tiles;
			level_task_data->source_bounds_height_in_tiles = source_bounds_height_in_tiles;
			level_task_data->source_tile_count = source_tile_count;


			// Create a 'subsetted' tile map to request source tiles from.
			// We store pointers to tile_t, and will use those with the usual routines for tile loading.
			level_task_data->source_tiles = calloc(source_tile_count, sizeof(tile_t*));
			for (i32 rel_source_tile_y = 0; rel_source_tile_y < source_bounds_height_in_tiles; ++rel_source_tile_y) {
				i32 abs_source_tile_y = level_task_data->source_tile_bounds.top + rel_source_tile_y;
				bool out_of_bounds = (abs_source_tile_y < 0 || abs_source_tile_y >= source_level_image->height_in_tiles);
				if (!out_of_bounds) {
					for (i32 rel_source_tile_x = 0; rel_source_tile_x < source_bounds_width_in_tiles; ++rel_source_tile_x) {
						i32 abs_source_tile_x = level_task_data->source_tile_bounds.left + rel_source_tile_x;
						out_of_bounds = (abs_source_tile_x < 0 || abs_source_tile_x >= source_level_image->width_in_tiles);
						if (!out_of_bounds) {
							tile_t* tile = get_tile(source_level_image, abs_source_tile_x, abs_source_tile_y);
							level_task_data->source_tiles[rel_source_tile_y * source_bounds_width_in_tiles + rel_source_tile_x] = tile;
						}
					}
				}

			}

			// Include the NewSubfileType tag in every IFD except the first one
			if (level > 0) {
				memrw_push_back(&tag_buffer, &tag_new_subfile_type, sizeof(raw_bigtiff_tag_t));
				++tag_count_for_ifd;
			}


			raw_bigtiff_tag_t tag_image_width = {TIFF_TAG_IMAGE_WIDTH , TIFF_UINT32, 1, .offset = export_width_in_pixels};
			raw_bigtiff_tag_t tag_image_length = {TIFF_TAG_IMAGE_LENGTH , TIFF_UINT32, 1, .offset = export_height_in_pixels};

			// NOTE: The TIFF specification requires the tags to be in strict ascending order in the IFD.
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_width); ++tag_count_for_ifd; // 256
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_length); ++tag_count_for_ifd; // 257
			memrw_push_bigtiff_tag(&tag_buffer, &tag_bits_per_sample); ++tag_count_for_ifd; // 258
			memrw_push_bigtiff_tag(&tag_buffer, &tag_compression); ++tag_count_for_ifd; // 259
			memrw_push_bigtiff_tag(&tag_buffer, &tag_photometric_interpretation); ++tag_count_for_ifd; // 262

			// NOTE: For files cropped from Philips TIFF, ASAP will not correctly read the XResolution and YResolution tags
			// if the ImageDescription exists (or, if the ImageDescription still contains a reference to the Philips metadata?)
			// So for now, leave out the ImageDescription tag; it isn't really needed and only confuses some software.
			/*
			// Image description will be copied verbatim from the source
			if (source_ifd->image_description != NULL && source_ifd->image_description_length > 0) {
				add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer, TIFF_TAG_IMAGE_DESCRIPTION,
				                      TIFF_ASCII, source_ifd->image_description_length, source_ifd->image_description);
				++tag_count_for_ifd;
			}
			*/

			// unused tag: strip offsets

			memrw_push_bigtiff_tag(&tag_buffer, &tag_orientation); ++tag_count_for_ifd; // 274
			memrw_push_bigtiff_tag(&tag_buffer, &tag_samples_per_pixel); ++tag_count_for_ifd; // 277

			// unused tag: rows per strip
			// unused tag: strip byte counts

			raw_bigtiff_tag_t tag_x_resolution = {TIFF_TAG_X_RESOLUTION, TIFF_RATIONAL, 1, 0};
			float downsample_factor = (float)(1 << level);
			if (image->is_mpp_known) {
				tiff_rational_t resolution = float_to_tiff_rational((1.0f / image->mpp_x) * downsample_factor * 10000.0f);
				tag_x_resolution.offset = *(u64*)(&resolution);
				memrw_push_bigtiff_tag(&tag_buffer, &tag_x_resolution); // 282
				++tag_count_for_ifd;
			}
			raw_bigtiff_tag_t tag_y_resolution = {TIFF_TAG_Y_RESOLUTION, TIFF_RATIONAL, 1, 0};
			if (image->is_mpp_known) {
				tiff_rational_t resolution = float_to_tiff_rational((1.0f / image->mpp_y) * downsample_factor * 10000.0f);
				tag_y_resolution.offset = *(u64*)(&resolution);
				memrw_push_bigtiff_tag(&tag_buffer, &tag_y_resolution); // 283
				++tag_count_for_ifd;
			}
			memrw_push_bigtiff_tag(&tag_buffer, &tag_resolution_unit); ++tag_count_for_ifd; // 296

#if 0
			// Copy Software tag verbatim from the source image.
			// TODO: conform to Philips format by default to have mpp known in OpenSLide?
			if (source_ifd->software != NULL && source_ifd->software_length > 0) {
				add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer, TIFF_TAG_SOFTWARE,
				                      TIFF_ASCII, source_ifd->software_length, source_ifd->software); // 305
				++tag_count_for_ifd;
			}
#endif

			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_width); ++tag_count_for_ifd; // 322
			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_length); ++tag_count_for_ifd; // 323

			u64 tag_tile_offsets_write_offset = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_TILE_OFFSETS, TIFF_UINT64, export_tile_count, NULL); // 324
			level_task_data->offset_of_tile_offsets = tag_tile_offsets_write_offset + offsetof(raw_bigtiff_tag_t, offset);
			if (export_tile_count == 1) {
				level_task_data->are_tile_offsets_inlined_in_tag = true; // no indirection (inlined, no offset in data buffer)
			}
			++tag_count_for_ifd;

			u64 tag_tile_bytecounts_write_offset = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_TILE_BYTE_COUNTS, TIFF_UINT64, export_tile_count, NULL); // 325
			level_task_data->offset_of_tile_bytecounts = tag_tile_bytecounts_write_offset + offsetof(raw_bigtiff_tag_t, offset);


			++tag_count_for_ifd;

			// unused tag: SMinSampleValue
			// unused tag: SMaxSampleValue

			u8* tables_buffer = NULL;
			u64 tables_size = 0;
			jpeg_encode_tile(NULL, export_tile_width, export_tile_width, quality, &tables_buffer, &tables_size, NULL,
			                 NULL, 0);
			add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_JPEG_TABLES, TIFF_UNDEFINED, tables_size, tables_buffer); // 347
			++tag_count_for_ifd;
			if (tables_buffer) libc_free(tables_buffer);

			if (desired_photometric_interpretation == TIFF_PHOTOMETRIC_YCBCR) {
				memrw_push_bigtiff_tag(&tag_buffer, &tag_chroma_subsampling); // 530
				++tag_count_for_ifd;
			}

			// Update the tag count, which was written incorrectly as a placeholder at the beginning of the IFD
			*(u64*)(tag_buffer.data + tag_count_for_ifd_offset) = tag_count_for_ifd;

			/*if (need_reuse_tiles && level == 0) {
				u32 tile_offset_x = level0_pixel_bounds.left / tile_width;
				u32 tile_offset_y = level0_pixel_bounds.top / tile_height;
			}*/

		}
		// TODO: progress bar progress managed on the main thread?
		global_tiff_export_progress = 0.05f;

		u64 next_ifd_offset_terminator = 0;
		memrw_push_back(&tag_buffer, &next_ifd_offset_terminator, sizeof(u64));

		// TODO: macro/label images

		// Adjust the offsets in the TIFF tags, so that they are counted from the beginning of the file.
		u64 data_buffer_base_offset = tag_buffer.used_size;
		offset_fixup_t* fixups = (offset_fixup_t*)fixups_buffer.data;
		u64 fixup_count = fixups_buffer.used_count;
		for (i32 i = 0; i < fixup_count; ++i) {
			offset_fixup_t* fixup = fixups + i;
			u64 fixed_offset = fixup->offset_from_unknown_base + data_buffer_base_offset;
			*(u64*)(tag_buffer.data + fixup->offset_to_fix) = fixed_offset;
		}

		// Resolve indirection to get the actual locations of the tile offsets and byte counts in the TIFF file.
		// (At this point these sections still contain only placeholder zeroes. We need to rewrite these later.)
		for (i32 level = 0; level <= export_max_level; ++level) {
			export_level_task_data_t* level_task_data = export_task.level_task_datas + level;
			if (!level_task_data->is_represented) continue;
			if (!level_task_data->are_tile_offsets_inlined_in_tag) {
				u64 tile_offsets_actual_offset_in_file    = *(u64*)(tag_buffer.data + level_task_data->offset_of_tile_offsets);
				u64 tile_bytecounts_actual_offset_in_file = *(u64*)(tag_buffer.data + level_task_data->offset_of_tile_bytecounts);
				level_task_data->offset_of_tile_offsets    = tile_offsets_actual_offset_in_file;
				level_task_data->offset_of_tile_bytecounts = tile_bytecounts_actual_offset_in_file;
			}

		}

		fwrite(tag_buffer.data, tag_buffer.used_size, 1, fp);
		fwrite(small_data_buffer.data, small_data_buffer.used_size, 1,fp);
		export_task.image_data_base_offset = tag_buffer.used_size + small_data_buffer.used_size;
		export_task.fp = fp;
		export_task.ifd_count = export_ifd_count;
		export_task.max_level = export_max_level;
		export_task.is_valid = true;

		memrw_destroy(&tag_buffer);
		memrw_destroy(&small_data_buffer);
		memrw_destroy(&fixups_buffer);

	}

	// 'Part 2': doing the actual tile data
	if (export_task.is_valid) {

		export_task.current_image_data_write_offset = export_task.image_data_base_offset;

		float progress_left = 0.99f - global_tiff_export_progress;
		export_task.progress_per_exported_tile = progress_left / (float)(ATLEAST(1, export_task.total_tiles_to_export));

		console_print_verbose("Starting TIFF export, total tiles to export = %d\n", export_task.total_tiles_to_export);
		for (i32 level = 0; level <= export_task.max_level; ++level) {
			export_bigtiff_encode_level(app_state, image, &export_task, level);
		}
		fclose(export_task.fp);

		success = true;
		console_print("Exported region to '%s'\n", filename);
	}

	if (export_flags & EXPORT_FLAGS_ALSO_EXPORT_ANNOTATIONS) {
		bool push_coordinates_inward = export_flags & EXPORT_FLAGS_PUSH_ANNOTATION_COORDINATES_INWARD;
		annotation_set_t derived_set = create_offsetted_annotation_set_for_area(&app_state->scene.annotation_set, world_bounds, push_coordinates_inward);

		size_t filename_len = strlen(filename);
		char* xml_filename = (char*)alloca(filename_len + 4);
		memcpy(xml_filename, filename, filename_len + 1);
		replace_file_extension(xml_filename, filename_len + 4, "xml");
		save_asap_xml_annotations(&derived_set, xml_filename);

		destroy_annotation_set(&derived_set);
	}


	return success;
}

typedef struct export_region_task_t {
	app_state_t* app_state;
	image_t* image;
	bounds2f world_bounds;
	bounds2i level0_bounds;
	const char* filename;
	u32 export_tile_width;
	u16 desired_photometric_interpretation;
	i32 quality;
	u32 export_flags;
    bool need_resize;
    v2f target_mpp;
} export_region_task_t;

void export_cropped_bigtiff_func(i32 logical_thread_index, void* userdata) {
	export_region_task_t* task = (export_region_task_t*) userdata;
	bool success = export_cropped_bigtiff(task->app_state, task->image, task->world_bounds, task->level0_bounds,
	                                      task->filename, task->export_tile_width,
	                                      task->desired_photometric_interpretation, task->quality, task->export_flags);
	global_tiff_export_progress = 1.0f;
	task->app_state->is_export_in_progress = false;

//	atomic_decrement(&task->isyntax->refcount); // TODO: release
}

void begin_export_cropped_bigtiff(app_state_t* app_state, image_t* image, bounds2f world_bounds, bounds2i level0_bounds, const char* filename,
                                  u32 export_tile_width, u16 desired_photometric_interpretation, i32 quality, u32 export_flags) {
	export_region_task_t task = {0};
	task.app_state = app_state;
	task.image = image;
	task.world_bounds = world_bounds;
	task.level0_bounds = level0_bounds;
	task.filename = filename;
	task.export_tile_width = export_tile_width;
	task.desired_photometric_interpretation = desired_photometric_interpretation;
	task.quality = quality;
	task.export_flags = export_flags;

	global_tiff_export_progress = 0.0f;
	app_state->is_export_in_progress = true;

//	atomic_increment(&isyntax->refcount); // TODO: retain; don't destroy  while busy
	if (!work_queue_submit_task(&global_work_queue, export_cropped_bigtiff_func, &task, sizeof(task))) {
//		tile->is_submitted_for_loading = false; // chicken out
//		atomic_decrement(&isyntax->refcount);
		app_state->is_export_in_progress = false;
	};
}

typedef struct image_draft_tile_t {
    image_buffer_t buffer;
    i32 tile_x;
    i32 tile_y;
    i32 tile_index;
    i32 level;
} image_draft_tile_t;

typedef struct image_draft_level_t {
    i32 level;
    i32 width_in_pixels;
    i32 height_in_pixels;
    i32 width_in_tiles;
    i32 height_in_tiles;
    i32 tile_count;
    image_draft_tile_t* tiles;
    u64 offset_of_tile_offsets;
    u64 offset_of_tile_bytecounts;
    bool are_tile_offsets_inlined_in_tag;
    u64* tile_offsets;
    u64* tile_bytecounts;
} image_draft_level_t;

typedef struct image_draft_t {
    i32 base_width;
    i32 base_height;
    i32 tile_width;
    i32 tile_height;
    i32 level_count;
    image_draft_level_t levels[10];
    image_t* source_image;
    bounds2i source_level0_bounds;
    i32 source_base_level;
    float base_downsample_factor_x;
    float base_downsample_factor_y;
    memrw_t tag_buffer;
    memrw_t small_data_buffer;
    memrw_t fixups_buffer;
    tiff_header_t header;
    bool is_mpp_known;
    bool is_background_black;
    v2f mpp;
    u64 image_data_base_offset;
    u64 current_image_data_write_offset;
    i32 total_tiles_to_export;
    float progress_per_exported_tile;
    float supertile_width;
    float supertile_height;
    i32 supertile_width_read;
    i32 supertile_height_read;
    u16 desired_photometric_interpretation;
    i32 quality;
} image_draft_t;

void shrink_tile_and_propagate_to_next_level(image_draft_t* draft, image_draft_tile_t* tile) {
    if (tile->level + 1 < draft->level_count) {

        // Find the parent tile and initialize an image buffer for it if needed
        image_draft_level_t* parent_level = draft->levels + tile->level + 1;
        i32 parent_tile_x = tile->tile_x / 2;
        i32 parent_tile_y = tile->tile_y / 2;
        image_draft_tile_t* parent_tile = parent_level->tiles + parent_tile_y * parent_level->width_in_tiles + parent_tile_x;

        // TODO: how to make this safe for multithreading?
        if (!parent_tile->buffer.pixels) {
            parent_tile->buffer = create_bgra_image_buffer(draft->tile_width, draft->tile_height);
        }

        // Prepare a 'view' of the parent tile, for the quadrant we want to fill with a 2x2 shrink of the tile
        image_buffer_t dest = parent_tile->buffer;
        i32 quadrant = (tile->tile_x % 2) + 2 * (tile->tile_y % 2);
        dest.pixels = parent_tile->buffer.pixels;
        if (quadrant % 2 == 1) {
            dest.pixels += (tile->buffer.width / 2) * tile->buffer.channels; // right half
        }
        if (quadrant >= 2) {
            dest.pixels += tile->buffer.width * (tile->buffer.height / 2) * tile->buffer.channels; // bottom half
        }
        dest.width /= 2;
        dest.height /= 2;

        // Do the shrink
        image_shrink_2x2(&tile->buffer, &dest, (rect2i){0, 0, draft->tile_width, draft->tile_height});
    }
}

void write_finished_bigtiff_tile(image_draft_t* draft, image_draft_tile_t* tile, file_stream_t fp) {

    bool use_rgb = (draft->desired_photometric_interpretation == TIFF_PHOTOMETRIC_RGB);

    u8* compressed_buffer = NULL;
    u64 compressed_size = 0;
    jpeg_encode_tile(tile->buffer.pixels, draft->tile_width, draft->tile_width, draft->quality, NULL, NULL,
                     &compressed_buffer, &compressed_size, use_rgb);

    // TODO: make an asynchronous version for I/O

    fseeko64(fp, draft->current_image_data_write_offset, SEEK_SET); // this needed?

    fwrite(compressed_buffer, compressed_size, 1, fp);

    image_draft_level_t* draft_level = draft->levels + tile->level;

    draft_level->tile_offsets[tile->tile_index] = draft->current_image_data_write_offset;
    draft_level->tile_bytecounts[tile->tile_index] = compressed_size;

    draft->current_image_data_write_offset += compressed_size;
    global_tiff_export_progress += draft->progress_per_exported_tile;

    libc_free(compressed_buffer);

}

void construct_base_tile_with_resampling(image_draft_t* draft, image_draft_tile_t* tile, file_stream_t fp) {

    temp_memory_t temp = begin_temp_memory_on_local_thread();
    bool supertile_need_destroy = false;
    bool resized_tile_need_destroy = false;
    image_buffer_t resized_tile = create_bgra_image_buffer_using_arena(temp.arena, draft->tile_width, draft->tile_height);
    if (!resized_tile.is_valid) {
        resized_tile = create_bgra_image_buffer(draft->tile_width, draft->tile_height);
        resized_tile_need_destroy = true;
    }
    image_buffer_t supertile = create_bgra_image_buffer_using_arena(temp.arena, draft->supertile_width_read, draft->supertile_height_read);
    if (!supertile.is_valid) {
        supertile = create_bgra_image_buffer(draft->tile_width, draft->tile_height);
        supertile_need_destroy = true;
    }

    float supertile_y = draft->source_level0_bounds.top + draft->supertile_height * (tile->tile_y << draft->source_base_level);
    i32 supertile_read_y = (i32)floorf(supertile_y) - 4;
    float supertile_offset_y = supertile_y - (float)supertile_read_y;

    float supertile_x = draft->source_level0_bounds.left + draft->supertile_width * (tile->tile_x << draft->source_base_level);
    i32 supertile_read_x = (i32)floorf(supertile_x) - 4;
    float supertile_offset_x = supertile_x - (float)supertile_read_x;

    image_read_region(draft->source_image, draft->source_base_level, supertile_read_x, supertile_read_y,
                      draft->supertile_width_read, draft->supertile_height_read, supertile.pixels,
                      supertile.pixel_format);
    rect2f box = {supertile_offset_x, supertile_offset_y, draft->supertile_width, draft->supertile_height};

    if (image_resample_lanczos3(&supertile, &resized_tile, box)) {
//        stbi_write_png("debug_resample_result.png", draft->tile_width, draft->tile_width, 4, resized_tile.pixels, resized_tile.width * resized_tile.channels);
        tile->buffer = resized_tile;
        shrink_tile_and_propagate_to_next_level(draft, tile);
        write_finished_bigtiff_tile(draft, tile, fp);
    }

    if (supertile_need_destroy) {
        destroy_image_buffer(&supertile);
    }
    if (resized_tile_need_destroy) {
        destroy_image_buffer(&resized_tile);
    }
    release_temp_memory(&temp);
}

void construct_tiles_recursive(image_draft_t* draft, image_draft_tile_t* tile, file_stream_t fp) {
    ASSERT(tile->level >= 0);
    if (tile->level == 0) {
        construct_base_tile_with_resampling(draft, tile, fp);
    } else {
        // find child tiles
        image_draft_level_t* child_level = draft->levels + tile->level - 1;
        image_draft_tile_t* topleft = child_level->tiles + tile->tile_y * 2 * child_level->width_in_tiles + tile->tile_x * 2;
        construct_tiles_recursive(draft, topleft, fp);
        i32 tile_x_right = tile->tile_x * 2 + 1;
        i32 tile_y_bottom = tile->tile_y * 2 + 1;
        if (tile_x_right < child_level->width_in_tiles) {
            image_draft_tile_t* topright = topleft + 1;
            construct_tiles_recursive(draft, topright, fp);
        }
        if (tile_y_bottom < child_level->height_in_tiles) {
            image_draft_tile_t* bottomleft = topleft + child_level->width_in_tiles;
            construct_tiles_recursive(draft, bottomleft, fp);
        }
        if (tile_x_right < child_level->width_in_tiles && tile_y_bottom < child_level->height_in_tiles) {
            image_draft_tile_t* bottomright = topleft + child_level->width_in_tiles + 1;
            construct_tiles_recursive(draft, bottomright, fp);
        }

        // Now all quadrants of the tile are filled -> write out to file
//        stbi_write_png("debug_resample_result.png", draft->tile_width, draft->tile_width, 4, tile->buffer.pixels, tile->buffer.width * tile->buffer.channels);
        shrink_tile_and_propagate_to_next_level(draft, tile);
        write_finished_bigtiff_tile(draft, tile, fp);
        destroy_image_buffer(&tile->buffer);
    }
}

void image_draft_prepare_bigtiff_ifds_and_tags(image_draft_t* draft) {
    // We will prepare all the tags, and push them into a temporary buffer, to be written to file later.
    // For non-inlined tags, the 'offset' field gets a placeholder offset because we don't know yet
    // where the tag data will be located in the file. For such tags we will:
    // - Push the data into a separate buffer, and remember the relative offset within that buffer
    // - Create a 'fixup', so that we can later substitute the offset once we know the base offset
    //   where we will store the separate data buffer in the output file.

    // temporary buffer for only the TIFF header + IFD tags
    draft->tag_buffer = memrw_create(KILOBYTES(64));
    // temporary buffer for all data >8 bytes not fitting in the raw TIFF tags (leaving out the pixel data)
    draft->small_data_buffer = memrw_create(MEGABYTES(1));
    // temporary buffer tracking the offsets that we need to fix after writing all the IFDs
    draft->fixups_buffer = memrw_create(1024);

    // Write the TIFF header (except the offset to the first IFD, which we will push when iterating over the IFDs)
    memset(&draft->header, 0, sizeof(draft->header));
    draft->header.byte_order_indication = 0x4949; // little-endian
    draft->header.filetype = 0x002B; // BigTIFF
    draft->header.bigtiff.offset_size = 0x0008;
    draft->header.bigtiff.always_zero = 0;
    memrw_push_back(&draft->tag_buffer, &draft->header, 8);

    i32 export_ifd_count = 0;
    i32 export_max_level = 0;

    // Tags that are the same for all image levels
    raw_bigtiff_tag_t tag_new_subfile_type = {TIFF_TAG_NEW_SUBFILE_TYPE, TIFF_UINT32, 1, .offset = TIFF_FILETYPE_REDUCEDIMAGE};
    u16 bits_per_sample[4] = {8, 8, 8, 0};
    raw_bigtiff_tag_t tag_bits_per_sample = {TIFF_TAG_BITS_PER_SAMPLE, TIFF_UINT16, 3, .offset = *(u64*)bits_per_sample};
    raw_bigtiff_tag_t tag_compression = {TIFF_TAG_COMPRESSION, TIFF_UINT16, 1, .offset = TIFF_COMPRESSION_JPEG};
    raw_bigtiff_tag_t tag_photometric_interpretation = {TIFF_TAG_PHOTOMETRIC_INTERPRETATION, TIFF_UINT16, 1, .offset = draft->desired_photometric_interpretation};
    raw_bigtiff_tag_t tag_orientation = {TIFF_TAG_ORIENTATION, TIFF_UINT16, 1, .offset = TIFF_ORIENTATION_TOPLEFT};
    raw_bigtiff_tag_t tag_samples_per_pixel = {TIFF_TAG_SAMPLES_PER_PIXEL, TIFF_UINT16, 1, .offset = 3};
    raw_bigtiff_tag_t tag_tile_length = {TIFF_TAG_TILE_LENGTH, TIFF_UINT16, 1, .offset = draft->tile_height};
    raw_bigtiff_tag_t tag_tile_width = {TIFF_TAG_TILE_WIDTH, TIFF_UINT16, 1, .offset = draft->tile_width};
    raw_bigtiff_tag_t tag_resolution_unit = {TIFF_TAG_RESOLUTION_UNIT, TIFF_UINT16, 1, .data_u16 = 3 /*RESUNIT_CENTIMETER*/};
    // NOTE: chroma subsampling is used for YCbCr-encoded images, but not for RGB
    u16 chroma_subsampling[4] = {2, 2, 0, 0};
    raw_bigtiff_tag_t tag_chroma_subsampling = {TIFF_TAG_YCBCRSUBSAMPLING, TIFF_UINT16, 2, .offset = *(u64*)(chroma_subsampling)};

    for (i32 level = 0; level < draft->level_count; ++level) {
        image_draft_level_t* draft_level = draft->levels + level;


        export_max_level = level;
        ++export_ifd_count;

        // Offset to the beginning of the next IFD (= 8 bytes directly after the current offset)
        u64 next_ifd_offset = draft->tag_buffer.used_size + sizeof(u64);
        memrw_push_back(&draft->tag_buffer, &next_ifd_offset, sizeof(u64));

        u64 tag_count_for_ifd = 0;
        u64 tag_count_for_ifd_offset = memrw_push_back(&draft->tag_buffer, &tag_count_for_ifd, sizeof(u64));




        // Include the NewSubfileType tag in every IFD except the first one
        if (level > 0) {
            memrw_push_back(&draft->tag_buffer, &tag_new_subfile_type, sizeof(raw_bigtiff_tag_t)); // 254
            ++tag_count_for_ifd;
        }


        raw_bigtiff_tag_t tag_image_width = {TIFF_TAG_IMAGE_WIDTH , TIFF_UINT32, 1, .offset = draft_level->width_in_pixels};
        raw_bigtiff_tag_t tag_image_length = {TIFF_TAG_IMAGE_LENGTH , TIFF_UINT32, 1, .offset = draft_level->height_in_pixels};

        // NOTE: The TIFF specification requires the tags to be in strict ascending order in the IFD.
        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_image_width); ++tag_count_for_ifd; // 256
        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_image_length); ++tag_count_for_ifd; // 257
        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_bits_per_sample); ++tag_count_for_ifd; // 258
        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_compression); ++tag_count_for_ifd; // 259
        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_photometric_interpretation); ++tag_count_for_ifd; // 262




        // NOTE: For files cropped from Philips TIFF, ASAP will not correctly read the XResolution and YResolution tags
        // if the ImageDescription exists (or, if the ImageDescription still contains a reference to the Philips metadata?)
        // So for now, leave out the ImageDescription tag; it isn't really needed and only confuses some software.
        /*
        // Image description will be copied verbatim from the source
        if (source_ifd->image_description != NULL && source_ifd->image_description_length > 0) {
            add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer, TIFF_TAG_IMAGE_DESCRIPTION,
                                  TIFF_ASCII, source_ifd->image_description_length, source_ifd->image_description);
            ++tag_count_for_ifd;
        }
        */

        // unused tag: strip offsets

        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_orientation); ++tag_count_for_ifd; // 274
        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_samples_per_pixel); ++tag_count_for_ifd; // 277

        // unused tag: rows per strip
        // unused tag: strip byte counts

        if (draft->is_mpp_known) {
            raw_bigtiff_tag_t tag_x_resolution = {TIFF_TAG_X_RESOLUTION, TIFF_RATIONAL, 1, 0};
            float downsample_factor = (float)(1 << level);
            tiff_rational_t resolution_x = float_to_tiff_rational((1.0f / draft->mpp.x) * downsample_factor * 10000.0f);
            tag_x_resolution.offset = *(u64*)(&resolution_x);
            memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_x_resolution); // 282
            ++tag_count_for_ifd;

            raw_bigtiff_tag_t tag_y_resolution = {TIFF_TAG_Y_RESOLUTION, TIFF_RATIONAL, 1, 0};
            tiff_rational_t resolution_y = float_to_tiff_rational((1.0f / draft->mpp.y) * downsample_factor * 10000.0f);
            tag_y_resolution.offset = *(u64*)(&resolution_y);
            memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_y_resolution); // 283
            ++tag_count_for_ifd;

            memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_resolution_unit); ++tag_count_for_ifd; // 296
        }

#if 0
        // Copy Software tag verbatim from the source image.
        if (source_ifd->software != NULL && source_ifd->software_length > 0) {
            add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer, TIFF_TAG_SOFTWARE,
                                  TIFF_ASCII, source_ifd->software_length, source_ifd->software); // 305
            ++tag_count_for_ifd;
        }
#endif

        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_tile_width); ++tag_count_for_ifd; // 322
        memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_tile_length); ++tag_count_for_ifd; // 323

        u64 tag_tile_offsets_write_offset = add_large_bigtiff_tag(&draft->tag_buffer, &draft->small_data_buffer, &draft->fixups_buffer,
                                                                  TIFF_TAG_TILE_OFFSETS, TIFF_UINT64, draft_level->tile_count, NULL); // 324
        draft_level->offset_of_tile_offsets = tag_tile_offsets_write_offset + offsetof(raw_bigtiff_tag_t, offset);
        if (draft_level->tile_count == 1) {
            draft_level->are_tile_offsets_inlined_in_tag = true; // no indirection (inlined, no offset in data buffer)
        }
        ++tag_count_for_ifd;

        u64 tag_tile_bytecounts_write_offset = add_large_bigtiff_tag(&draft->tag_buffer, &draft->small_data_buffer, &draft->fixups_buffer,
                                                                     TIFF_TAG_TILE_BYTE_COUNTS, TIFF_UINT64, draft_level->tile_count, NULL); // 325
        draft_level->offset_of_tile_bytecounts = tag_tile_bytecounts_write_offset + offsetof(raw_bigtiff_tag_t, offset);


        ++tag_count_for_ifd;

        // unused tag: SMinSampleValue
        // unused tag: SMaxSampleValue

        u8* tables_buffer = NULL;
        u64 tables_size = 0;
        jpeg_encode_tile(NULL, draft->tile_width, draft->tile_height, draft->quality, &tables_buffer, &tables_size, NULL,
                         NULL, 0);
        add_large_bigtiff_tag(&draft->tag_buffer, &draft->small_data_buffer, &draft->fixups_buffer,
                              TIFF_TAG_JPEG_TABLES, TIFF_UNDEFINED, tables_size, tables_buffer); // 347
        ++tag_count_for_ifd;
        if (tables_buffer) libc_free(tables_buffer);

        if (draft->desired_photometric_interpretation == TIFF_PHOTOMETRIC_YCBCR) {
            memrw_push_bigtiff_tag(&draft->tag_buffer, &tag_chroma_subsampling); // 530
            ++tag_count_for_ifd;
        }

        // Update the tag count, which was written incorrectly as a placeholder at the beginning of the IFD
        *(u64*)(draft->tag_buffer.data + tag_count_for_ifd_offset) = tag_count_for_ifd;

        /*if (need_reuse_tiles && level == 0) {
            u32 tile_offset_x = level0_pixel_bounds.left / tile_width;
            u32 tile_offset_y = level0_pixel_bounds.top / tile_height;
        }*/


    }

    u64 next_ifd_offset_terminator = 0;
    memrw_push_back(&draft->tag_buffer, &next_ifd_offset_terminator, sizeof(u64));

    // TODO: macro/label images

    // Adjust the offsets in the TIFF tags, so that they are counted from the beginning of the file.
    u64 data_buffer_base_offset = draft->tag_buffer.used_size;
    offset_fixup_t* fixups = (offset_fixup_t*)draft->fixups_buffer.data;
    u64 fixup_count = draft->fixups_buffer.used_count;
    for (i32 i = 0; i < fixup_count; ++i) {
        offset_fixup_t* fixup = fixups + i;
        u64 fixed_offset = fixup->offset_from_unknown_base + data_buffer_base_offset;
        *(u64*)(draft->tag_buffer.data + fixup->offset_to_fix) = fixed_offset;
    }

    // Resolve indirection to get the actual locations of the tile offsets and byte counts in the TIFF file.
    // (At this point these sections still contain only placeholder zeroes. We need to rewrite these later.)
    for (i32 level = 0; level <= export_max_level; ++level) {
        image_draft_level_t* draft_level = draft->levels + level;
        if (!draft_level->are_tile_offsets_inlined_in_tag) {
            u64 tile_offsets_actual_offset_in_file    = *(u64*)(draft->tag_buffer.data + draft_level->offset_of_tile_offsets);
            u64 tile_bytecounts_actual_offset_in_file = *(u64*)(draft->tag_buffer.data + draft_level->offset_of_tile_bytecounts);
            draft_level->offset_of_tile_offsets    = tile_offsets_actual_offset_in_file;
            draft_level->offset_of_tile_bytecounts = tile_bytecounts_actual_offset_in_file;
        }

    }


}

void image_draft_write_bigtiff_ifds_and_small_data(image_draft_t* draft, file_stream_t fp) {
    fwrite(draft->tag_buffer.data, draft->tag_buffer.used_size, 1, fp);
    fwrite(draft->small_data_buffer.data, draft->small_data_buffer.used_size, 1,fp);
    draft->image_data_base_offset = draft->tag_buffer.used_size + draft->small_data_buffer.used_size;
}

void image_draft_destroy(image_draft_t* draft) {
    for (i32 i = 0; i < draft->level_count; ++i) {
        image_draft_level_t* draft_level = draft->levels + i;
        if (draft_level->tiles) {
            free(draft_level->tiles);
        }
        if (draft_level->tile_offsets) {
            free(draft_level->tile_offsets);
        }
        if (draft_level->tile_bytecounts) {
            free(draft_level->tile_bytecounts);
        }
    }
    memrw_destroy(&draft->tag_buffer);
    memrw_destroy(&draft->small_data_buffer);
    memrw_destroy(&draft->fixups_buffer);
}


bool export_cropped_bigtiff_with_resample(app_state_t* app_state, image_t* image, bounds2f world_bounds, bounds2i level0_bounds, const char* filename,
                            u32 export_tile_width, u16 desired_photometric_interpretation, i32 quality, u32 export_flags, bool need_resize, v2f target_mpp) {

    switch(desired_photometric_interpretation) {
        case TIFF_PHOTOMETRIC_YCBCR: break;
        case TIFF_PHOTOMETRIC_RGB: break;
        default: {
            console_print_error("Error exporting BigTIFF: unsupported photometric interpretation (%d)\n", desired_photometric_interpretation);
        } return false;
    }

    // We need to downscale the image by some factor.
    if (need_resize && !image->is_mpp_known) {
        console_print_error("Error exporting BigTIFF: source image resolution is unknown.\n");
        return false;
    }

    if (!(target_mpp.x > 0.0f && target_mpp.y > 0.0f)) {
        console_print_verbose("Error exporting BigTIFF: invalid target resolution.\n");
        return false;
    }

    float downsample_factor_x = 1.0f;
    float downsample_factor_y = 1.0f;
    i32 source_base_level = 0;
    if (need_resize) {
        // Only resize if the target resolution is actually different, otherwise don't bother.
        downsample_factor_x = image->mpp_x / target_mpp.x;
        downsample_factor_y = image->mpp_y / target_mpp.y;
        if (fabs(1.0f - downsample_factor_x) + fabs(1.0f - downsample_factor_y) < 0.001f) {
            need_resize = false;
        }
        float largest_downsample_factor = MAX(downsample_factor_x, downsample_factor_y);
        ASSERT(largest_downsample_factor > 0.0f);
        if (largest_downsample_factor < 0.25f) {
            float source_mpp_x = image->mpp_x;
            float source_mpp_y = image->mpp_y;
            do {
                source_mpp_x *= 2.0f;
                source_mpp_y *= 2.0f;
                ++source_base_level;
                downsample_factor_x = source_mpp_x / target_mpp.x;
                downsample_factor_y = source_mpp_y / target_mpp.y;
                largest_downsample_factor = MAX(downsample_factor_x, downsample_factor_y);
            } while (largest_downsample_factor < 0.25f);
        }
    }

    i32 target_level0_width_in_pixels = (level0_bounds.right - level0_bounds.left) >> source_base_level;
    i32 target_level0_height_in_pixels = (level0_bounds.bottom - level0_bounds.top) >> source_base_level;
    if (need_resize) {
        target_level0_width_in_pixels = roundf((float)target_level0_width_in_pixels * downsample_factor_x);
        target_level0_height_in_pixels = roundf((float)target_level0_height_in_pixels * downsample_factor_y);
    }

    image_draft_t draft = {};
    draft.level_count = 9; // default value, maybe override later if fewer are needed
    draft.base_width = target_level0_width_in_pixels;
    draft.base_height = target_level0_height_in_pixels;
    draft.tile_width = export_tile_width;
    draft.tile_height = export_tile_width;
    draft.is_mpp_known = true;
    draft.is_background_black = image->is_background_black;
    draft.mpp = target_mpp;
    draft.desired_photometric_interpretation = desired_photometric_interpretation;
    draft.quality = quality;

    draft.source_image = image;
    draft.source_level0_bounds = level0_bounds;
    draft.source_base_level = source_base_level;
    draft.base_downsample_factor_x = downsample_factor_x;
    draft.base_downsample_factor_y = downsample_factor_y;
    draft.supertile_width = (float)draft.tile_width / draft.base_downsample_factor_x;
    draft.supertile_height = (float)draft.tile_height / draft.base_downsample_factor_y;
    draft.supertile_width_read = ((i32)ceilf(draft.supertile_width) + 8);
    draft.supertile_height_read = ((i32)ceilf(draft.supertile_height) + 8);

    for (i32 i = 0; i < 9; ++i) {
        image_draft_level_t* draft_level = draft.levels + i;
        draft_level->level = i;

        // Calculate dimensions for the current downsampling i
        draft_level->width_in_pixels = draft.base_width >> i;
        draft_level->height_in_pixels = draft.base_height >> i;

        draft_level->width_in_tiles = (draft_level->width_in_pixels + (draft.tile_width - 1)) / draft.tile_width;
        draft_level->height_in_tiles = (draft_level->height_in_pixels + (draft.tile_height - 1)) / draft.tile_height;
        draft_level->tile_count = draft_level->width_in_tiles * draft_level->height_in_tiles;
        ASSERT(draft_level->tile_count > 0);
        draft.total_tiles_to_export += draft_level->tile_count;
        draft_level->tiles = calloc(1, draft_level->tile_count * sizeof(image_draft_tile_t));
        draft_level->tile_offsets = calloc(1, draft_level->tile_count * sizeof(u64));
        draft_level->tile_bytecounts = calloc(1, draft_level->tile_count * sizeof(u64));

        // Initialize tiles with self-referencing of the level and tile position (tile_x, tile_y)
        for (i32 tile_y = 0; tile_y < draft_level->height_in_tiles; ++tile_y) {
            image_draft_tile_t* tile_row = draft_level->tiles + tile_y * draft_level->width_in_tiles;
            for (i32 tile_x = 0; tile_x < draft_level->width_in_tiles; ++tile_x) {
                image_draft_tile_t* tile = tile_row + tile_x;
                tile->tile_x = tile_x;
                tile->tile_y = tile_y;
                tile->tile_index = tile_y * draft_level->width_in_tiles + tile_x;
                tile->level = i;
            }
        }

        // Don't bother adding more levels if everything already fits within a single tile
        if (draft_level->tile_count <= 1) {
            draft.level_count = i;
            break;
        }
    }

    if (draft.level_count <= 0) {
        fatal_error("invalid level count");
        return false;
    }

    // To construct the pyramid, we'll construct the base level first, then afterwards propagate its contents
    // up to higher levels

    // Problem: we need to somehow reconstruct the pyramid without using too much RAM.
    // Can we do it with some clever caching techniques while also preserving speed?
    // Idea: recurse from topmost layer touching each quadrant dependency in turn

    // To construct the base layer:
    // for each base layer tile, prepare a 'supertile' that can be resized into the final base tile
    // The first step will be Lanczos resampling, after which there will be several box shrink steps


    // Prepare all the IFDs and TIFF tags to be written out to file later
    image_draft_prepare_bigtiff_ifds_and_tags(&draft);

    FILE* fp = fopen64(filename, "wb");
    bool success = false;

    if (fp) {
        // Write out the IFDs and TIFF tags to file
        image_draft_write_bigtiff_ifds_and_small_data(&draft, fp);
        draft.current_image_data_write_offset = draft.image_data_base_offset;

        // TODO: progress bar progress managed on the main thread?
        global_tiff_export_progress = 0.05f;
        float progress_left = 0.99f - global_tiff_export_progress;
        draft.progress_per_exported_tile = progress_left / (float)(ATLEAST(1, draft.total_tiles_to_export));

        console_print_verbose("Starting TIFF export, total tiles to export = %d\n", draft.total_tiles_to_export);

        // Construct the images for each tile of the pyramid
        image_draft_level_t* top_level = draft.levels + draft.level_count - 1;
        for (i32 tile_y = 0; tile_y < top_level->height_in_tiles; ++tile_y) {
            for (i32 tile_x = 0; tile_x < top_level->width_in_tiles; ++tile_x) {
                image_draft_tile_t* tile = top_level->tiles + tile_y * top_level->width_in_tiles + tile_x;
                construct_tiles_recursive(&draft, tile, fp);
            }
        }

        // Rewrite the tile offsets and tile bytecounts
        for (i32 i = 0; i < draft.level_count; ++i) {
            image_draft_level_t* draft_level = draft.levels + i;

            fseeko64(fp, draft_level->offset_of_tile_offsets, SEEK_SET);
            fwrite(draft_level->tile_offsets, sizeof(u64), draft_level->tile_count, fp);

            fseeko64(fp, draft_level->offset_of_tile_bytecounts, SEEK_SET);
            fwrite(draft_level->tile_bytecounts, sizeof(u64), draft_level->tile_count, fp);
        }


        fclose(fp);

        success = true;
        console_print("Exported region to '%s'\n", filename);
    }

    image_draft_destroy(&draft);

    if (export_flags & EXPORT_FLAGS_ALSO_EXPORT_ANNOTATIONS) {
        bool push_coordinates_inward = export_flags & EXPORT_FLAGS_PUSH_ANNOTATION_COORDINATES_INWARD;
        annotation_set_t derived_set = create_offsetted_annotation_set_for_area(&app_state->scene.annotation_set, world_bounds, push_coordinates_inward);

        size_t filename_len = strlen(filename);
        char* xml_filename = (char*)alloca(filename_len + 4);
        memcpy(xml_filename, filename, filename_len + 1);
        replace_file_extension(xml_filename, filename_len + 4, "xml");
        save_asap_xml_annotations(&derived_set, xml_filename);

        destroy_annotation_set(&derived_set);
    }


    return success;
}

void export_cropped_bigtiff_with_resample_func(i32 logical_thread_index, void* userdata) {
    export_region_task_t* task = (export_region_task_t*) userdata;
    bool success = export_cropped_bigtiff_with_resample(task->app_state, task->image, task->world_bounds, task->level0_bounds,
                                          task->filename, task->export_tile_width,
                                          task->desired_photometric_interpretation, task->quality, task->export_flags,
                                          task->need_resize, task->target_mpp);
    global_tiff_export_progress = 1.0f;
    task->app_state->is_export_in_progress = false;

//	atomic_decrement(&task->isyntax->refcount); // TODO: release
}

void begin_export_cropped_bigtiff_with_resample(app_state_t* app_state, image_t* image, bounds2f world_bounds, bounds2i level0_bounds, const char* filename,
                                                u32 export_tile_width, u16 desired_photometric_interpretation, i32 quality, u32 export_flags, v2f target_mpp) {

    export_region_task_t task = {0};
    task.app_state = app_state;
    task.image = image;
    task.world_bounds = world_bounds;
    task.level0_bounds = level0_bounds;
    task.filename = filename;
    task.export_tile_width = export_tile_width;
    task.desired_photometric_interpretation = desired_photometric_interpretation;
    task.quality = quality;
    task.export_flags = export_flags;
    task.need_resize = true;
    task.target_mpp = target_mpp;

    global_tiff_export_progress = 0.0f;
    app_state->is_export_in_progress = true;

    //	atomic_increment(&isyntax->refcount); // TODO: retain; don't destroy  while busy
    if (!work_queue_submit_task(&global_work_queue, export_cropped_bigtiff_with_resample_func, &task, sizeof(task))) {
//		tile->is_submitted_for_loading = false; // chicken out
//		atomic_decrement(&isyntax->refcount);
        app_state->is_export_in_progress = false;
    };
}
