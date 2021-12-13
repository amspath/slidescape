/*
  Slidescape, a whole-slide image viewer for digital pathology.
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
#include "mathutils.h"

#include "tiff.h"
#include "viewer.h"

#include "jpeg_decoder.h"


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
	FILE* fp;
	bool use_rgb;
	bool is_valid;
	export_level_task_data_t level_task_datas[WSI_MAX_LEVELS];
} export_task_data_t;

void export_notify_load_tile_completed(int logical_thread_index, void* userdata) {
	ASSERT(!"export_notify_load_tile_completed() is a dummy, it should not be called");
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

	bool skip = (contributing_source_tiles_count == 0); // empty tiles would only waste space, so skip them
	if (!skip) {
		u8* compressed_buffer = NULL;
		u32 compressed_size = 0;
		jpeg_encode_tile(dest, export_tile_width, export_tile_width, export_task->quality, NULL, NULL,
		                 &compressed_buffer, &compressed_size, export_task->use_rgb);

		*jpeg_buffer = compressed_buffer;
		*jpeg_size = compressed_size;
	}

	free(dest);
}

void export_bigtiff_encode_level(app_state_t* app_state, image_t* image, export_task_data_t* export_task, i32 level) {
	export_level_task_data_t* level_task = export_task->level_task_datas + level;
	if (!level_task->is_represented) return;

	u64* tile_offsets = calloc(level_task->export_tile_count, sizeof(u64));
	u64* tile_bytecounts = calloc(level_task->export_tile_count, sizeof(u64));


	u32 batch_size = ATLEAST(worker_thread_count, 1);
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

		for (i32 tile_index = 0; tile_index <= last_source_tile_needed; ++tile_index) {
			console_print_verbose("   tile %d out of %d\n", tile_index, last_source_tile_needed);
			tile_t* tile = level_task->source_tiles[tile_index];
			if (tile_index < first_source_tile_needed) {
				// Release tiles that are no longer needed.
				if (tile && tile->is_cached && tile->pixels) {
					tile_release_cache(tile);
				}
			} else {
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

		request_tiles(app_state, image, wishlist, tiles_to_load);
		free(wishlist);
		wishlist = NULL;

		// TODO: fix the copy-pasta
		i32 pixel_transfer_index_start = app_state->next_pixel_transfer_to_submit;
		while (is_queue_work_in_progress(&global_work_queue) || is_queue_work_in_progress(&global_completion_queue)) {
			work_queue_entry_t entry = get_next_work_queue_entry(&global_completion_queue);
			if (entry.is_valid) {
				if (!entry.callback) panic();
				mark_queue_entry_completed(&global_completion_queue);

				if (entry.callback == export_notify_load_tile_completed) {
					viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*) entry.userdata;
					if (task->pixel_memory) {
						bool need_free_pixel_memory = true;
						tile_t* tile = get_tile_from_tile_index(image, task->scale, task->tile_index);
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
					}

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



		// Now we can proceed with constructing the new tiles from the source tiles,
		// and writing them to disk.
		// TODO: make this a multi-threaded task.

		for (i32 tile_index = start_tile_index; tile_index <= end_tile_index; ++tile_index) {
			i32 export_tile_x = tile_index % level_task->export_width_in_tiles;
			i32 export_tile_y = tile_index / level_task->export_width_in_tiles;

			i32 work_index = tile_index % batch_size;
			u8** jpeg_buffer = &jpeg_compressed_buffers[work_index];
			u32* jpeg_size = &jpeg_compressed_sizes[work_index];

			construct_new_tile_from_source_tiles(export_task, level_task, export_tile_x, export_tile_y, jpeg_buffer, jpeg_size);

		}

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


	}
	// level export completed

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

bool32 export_cropped_bigtiff(app_state_t* app_state, image_t* image, tiff_t* tiff, bounds2i level0_bounds, const char* filename,
                              u32 export_tile_width, u16 desired_photometric_interpretation, i32 quality) {
	if (!(tiff && tiff->main_image_ifd && (tiff->mpp_x > 0.0f) && (tiff->mpp_y > 0.0f))) {
		return false;
	}

	switch(desired_photometric_interpretation) {
		case TIFF_PHOTOMETRIC_YCBCR: break;
		case TIFF_PHOTOMETRIC_RGB: break;
		default: {
			console_print_error("Error exporting BigTIFF: unsupported photometric interpretation (%d)\n", desired_photometric_interpretation);
		} return false;
	}

	// TODO: make ASAP understand the resolution in the exported file

	tiff_ifd_t* source_level0_ifd = tiff->main_image_ifd;
	u32 tile_width = source_level0_ifd->tile_width;
	u32 tile_height = source_level0_ifd->tile_height;
	bool is_tile_aligned = ((level0_bounds.left % tile_width) == 0) && ((level0_bounds.top % tile_height) == 0);

	bool need_reuse_tiles = is_tile_aligned && (desired_photometric_interpretation == source_level0_ifd->color_space)
	                        && (export_tile_width == tile_width) && (tile_width == tile_height); // only allow square tiles for re-use

	export_task_data_t export_task = {0};
	export_task.source_tile_width = tile_width;
	export_task.export_tile_width = export_tile_width;
	export_task.quality = quality;
	export_task.use_rgb = (desired_photometric_interpretation == TIFF_PHOTOMETRIC_RGB);

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
		i32 source_ifd_index = 0;
		tiff_ifd_t* source_ifd = source_level0_ifd;
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
		raw_bigtiff_tag_t tag_tile_width = {TIFF_TAG_TILE_WIDTH, TIFF_UINT16, 1, .offset = export_tile_width};
		raw_bigtiff_tag_t tag_tile_length = {TIFF_TAG_TILE_LENGTH, TIFF_UINT16, 1, .offset = export_tile_width};
		// NOTE: chroma subsampling is used for YCbCr-encoded images, but not for RGB
		u16 chroma_subsampling[4] = {2, 2, 0, 0};
		raw_bigtiff_tag_t tag_chroma_subsampling = {TIFF_TAG_YCBCRSUBSAMPLING, TIFF_UINT16, 2, .offset = *(u64*)(chroma_subsampling)};

		bool reached_level_with_only_one_tile_in_it = false;
		for (i32 level = 0; level < image->level_count && !reached_level_with_only_one_tile_in_it; ++level) {

			export_level_task_data_t* level_task_data = export_task.level_task_datas + level;

			// Find an IFD for this downsampling level
			if (source_ifd->downsample_level != level) {
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
#if DO_DEBUG
					console_print("Warning: source TIFF does not contain level %d, will be skipped\n", level);
#endif
					continue;
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
				bool out_of_bounds = (abs_source_tile_y < 0 || abs_source_tile_y >= source_ifd->height_in_tiles);
				if (!out_of_bounds) {
					for (i32 rel_source_tile_x = 0; rel_source_tile_x < source_bounds_width_in_tiles; ++rel_source_tile_x) {
						i32 abs_source_tile_x = level_task_data->source_tile_bounds.left + rel_source_tile_x;
						out_of_bounds = (abs_source_tile_x < 0 || abs_source_tile_x >= source_ifd->width_in_tiles);
						if (!out_of_bounds) {
							tile_t* tile = get_tile(source_level_image, abs_source_tile_x, abs_source_tile_y);
							level_task_data->source_tiles[rel_source_tile_y * source_bounds_width_in_tiles + rel_source_tile_x] = tile;
						}
					}
				}

			}


#if 0
			for (i32 export_tile_y = 0; export_tile_y < export_height_in_tiles; ++export_tile_y) {
				for (i32 export_tile_x = 0; export_tile_x < export_width_in_tiles; ++export_tile_x) {
					encode_tile_task_t task = {
							.image = image,
							.tiff = tiff,
							.ifd = source_ifd,
							.level = level,
							.export_tile_width = export_tile_width,
							.export_tile_x = export_tile_x,
							.export_tile_y = export_tile_y,
							.pixel_bounds = pixel_bounds,
					};
					bool32 enqueued = add_work_queue_entry(&work_queue, encode_tile_func, &task);


				}
			}
#endif

			// Include the NewSubfileType tag in every IFD except the first one
			if (level > 0) {
				memrw_push_back(&tag_buffer, &tag_new_subfile_type, sizeof(raw_bigtiff_tag_t));
				++tag_count_for_ifd;
			}

			raw_bigtiff_tag_t tag_image_width = {TIFF_TAG_IMAGE_WIDTH, TIFF_UINT32, 1, .offset = export_width_in_pixels};
			raw_bigtiff_tag_t tag_image_length = {TIFF_TAG_IMAGE_LENGTH, TIFF_UINT32, 1, .offset = export_height_in_pixels};
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_width); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_length); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_bits_per_sample); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_compression); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_photometric_interpretation); ++tag_count_for_ifd;

			// Image description will be copied verbatim from the source
			add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer, TIFF_TAG_IMAGE_DESCRIPTION,
			                      TIFF_ASCII, source_ifd->image_description_length, source_ifd->image_description);
			++tag_count_for_ifd;
			// unused tag: strip offsets

			memrw_push_bigtiff_tag(&tag_buffer, &tag_orientation); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_samples_per_pixel); ++tag_count_for_ifd;

			// unused tag: rows per strip
			// unused tag: strip byte counts

			if (source_ifd->x_resolution.b > 0) {
				raw_bigtiff_tag_t tag_x_resolution = {TIFF_TAG_X_RESOLUTION, TIFF_RATIONAL, 1, .offset = *(u64*)(&source_ifd->x_resolution)};
				memrw_push_bigtiff_tag(&tag_buffer, &tag_x_resolution);
				++tag_count_for_ifd;
			}
			if (source_ifd->x_resolution.b > 0) {
				raw_bigtiff_tag_t tag_y_resolution = {TIFF_TAG_Y_RESOLUTION, TIFF_RATIONAL, 1, .offset = *(u64*) (&source_ifd->y_resolution)};
				memrw_push_bigtiff_tag(&tag_buffer, &tag_y_resolution);
				++tag_count_for_ifd;
			}
//			u64 software_offset = 0; // TODO
//			u64 software_length = 0; // TODO
//			raw_bigtiff_tag_t tag_software = {TIFF_TAG_SOFTWARE, TIFF_ASCII, software_length, software_offset};

			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_width); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_length); ++tag_count_for_ifd;

			// TODO: actually write the tiles...


			u64 tag_tile_offsets_write_offset = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_TILE_OFFSETS, TIFF_UINT64, export_tile_count, NULL);
			level_task_data->offset_of_tile_offsets = tag_tile_offsets_write_offset + offsetof(raw_bigtiff_tag_t, offset);
			if (export_tile_count == 1) {
				level_task_data->are_tile_offsets_inlined_in_tag = true; // no indirection (inlined, no offset in data buffer)
			}
			++tag_count_for_ifd;

			u64 tag_tile_bytecounts_write_offset = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_TILE_BYTE_COUNTS, TIFF_UINT64, export_tile_count, NULL);
			level_task_data->offset_of_tile_bytecounts = tag_tile_bytecounts_write_offset + offsetof(raw_bigtiff_tag_t, offset);


			++tag_count_for_ifd;

			// unused tag: SMinSampleValue
			// unused tag: SMaxSampleValue

			u8* tables_buffer = NULL;
			u32 tables_size = 0;
			jpeg_encode_tile(NULL, export_tile_width, export_tile_width, quality, &tables_buffer, &tables_size, NULL,
			                 NULL, 0);
			add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_JPEG_TABLES, TIFF_UNDEFINED, tables_size, tables_buffer);
			++tag_count_for_ifd;
			if (tables_buffer) libc_free(tables_buffer);

			if (desired_photometric_interpretation == TIFF_PHOTOMETRIC_YCBCR) {
				memrw_push_bigtiff_tag(&tag_buffer, &tag_chroma_subsampling);
				++tag_count_for_ifd;
			}

			// Update the tag count, which was written incorrectly as a placeholder at the beginning of the IFD
			*(u64*)(tag_buffer.data + tag_count_for_ifd_offset) = tag_count_for_ifd;

			/*if (need_reuse_tiles && level == 0) {
				u32 tile_offset_x = level0_pixel_bounds.left / tile_width;
				u32 tile_offset_y = level0_pixel_bounds.top / tile_height;
			}*/

		}

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

		for (i32 level = 0; level <= export_task.max_level; ++level) {
			console_print_verbose("TIFF export region: encoding level %d\n", level);
			export_bigtiff_encode_level(app_state, image, &export_task, level);
		}
		fclose(export_task.fp);

	}


	return success;
}

//TODO: This currently doesn't work properly. What is
typedef struct export_region_task_t {
	app_state_t* app_state;
	image_t* image;
	tiff_t* tiff;
	bounds2i level0_bounds;
	const char* filename;
	u32 export_tile_width;
	u16 desired_photometric_interpretation;
	i32 quality;
} export_region_task_t;

void export_cropped_bigtiff_func(i32 logical_thread_index, void* userdata) {
	export_region_task_t* task = (export_region_task_t*) userdata;
	bool success = export_cropped_bigtiff(task->app_state, task->image, task->tiff, task->level0_bounds,
	                                      task->filename, task->export_tile_width,
	                                      task->desired_photometric_interpretation, task->quality);
//	atomic_decrement(&task->isyntax->refcount); // TODO: release
}

void begin_export_cropped_bigtiff(app_state_t* app_state, image_t* image, tiff_t* tiff, bounds2i level0_bounds, const char* filename,
                                  u32 export_tile_width, u16 desired_photometric_interpretation, i32 quality) {
	export_region_task_t task = {0};
	task.app_state = app_state;
	task.image = image;
	task.tiff = tiff;
	task.level0_bounds = level0_bounds;
	task.filename = filename;
	task.export_tile_width = export_tile_width;
	task.desired_photometric_interpretation = desired_photometric_interpretation;
	task.quality = quality;

//	atomic_increment(&isyntax->refcount); // TODO: retain; don't destroy  while busy
	if (!add_work_queue_entry(&global_work_queue, export_cropped_bigtiff_func, &task, sizeof(task))) {
//		tile->is_submitted_for_loading = false; // chicken out
//		atomic_decrement(&isyntax->refcount);
	};
}
