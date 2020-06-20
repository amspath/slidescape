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

#include "common.h"

#include "win32_main.h"
#include "platform.h"
#include "intrinsics.h"
#include "stringutils.h"

#include "openslide_api.h"
#include <glad/glad.h>
#include <linmath.h>
#include <io.h>

#include "arena.h"
#include "arena.c"

#define VIEWER_IMPL
#include "viewer.h"

#define STBI_ASSERT(x) ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "render_group.h"
#include "render_group.c"

#include "tiff.h"
#include "jpeg_decoder.h"
#include "tlsclient.h"
#include "gui.h"
#include "caselist.h"
#include "annotation.h"


void reset_scene(image_t *image, scene_t *scene) {
	scene->current_level = ATLEAST(0, image->level_count-2);
	scene->zoom_position = (float)scene->current_level;
	scene->camera.x = image->width_in_um / 2.0f;
	scene->camera.y = image->height_in_um / 2.0f;

}

u32 load_texture(void* pixels, i32 width, i32 height) {
	u32 texture = 0; //gl_gen_texture();
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &texture);
//	printf("Generated texture %d\n", texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
//	gl_diagnostic("glTexImage2D");
	return texture;
}

tile_t* get_tile(level_image_t* image_level, i32 tile_x, i32 tile_y) {
	i32 tile_index = tile_y * image_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < image_level->tile_count);
	tile_t* result = image_level->tiles + tile_index;
	return result;
}

void tiff_load_tile_batch_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_batch_t* batch = (load_tile_task_batch_t*) userdata;
	load_tile_task_t* first_task = batch->tile_tasks;
	image_t* image = first_task->image;

	// Note: when the thread started up we allocated a large blob of memory for the thread to use privately
	// TODO: better/more explicit allocator (instead of some setting some hard-coded pointers)
	thread_memory_t* thread_memory = (thread_memory_t*) thread_local_storage[logical_thread_index];
	u8* temp_memory = (u8*) thread_memory->aligned_rest_of_thread_memory; //malloc(WSI_BLOCK_SIZE);
	memset(temp_memory, 0xFF, WSI_BLOCK_SIZE);


	if (image->type == IMAGE_TYPE_TIFF) {
		tiff_t* tiff = &image->tiff.tiff;

		if (tiff->is_remote) {

			i32 bytes_read = 0;
			i32 batch_size = batch->task_count;
			i64 chunk_offsets[TILE_LOAD_BATCH_MAX];
			i64 chunk_sizes[TILE_LOAD_BATCH_MAX];
			i64 total_read_size = 0;
			for (i32 i = 0; i < batch_size; ++i) {
				load_tile_task_t* task = batch->tile_tasks + i;

				i32 level = task->level;
				i32 tile_x = task->tile_x;
				i32 tile_y = task->tile_y;
				level_image_t* level_image = image->level_images + level;
				tile_t* tile = task->tile;
				i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
				tiff_ifd_t* level_ifd = tiff->level_images + level;
				u64 tile_offset = level_ifd->tile_offsets[tile_index];
				u64 chunk_size = level_ifd->tile_byte_counts[tile_index];

				// It doesn't make sense to ask for empty tiles, this should never happen!
				ASSERT(tile_offset != 0);
				ASSERT(chunk_size != 0);

				chunk_offsets[i] = tile_offset;
				chunk_sizes[i] = chunk_size;
				total_read_size += chunk_size;
			}


			u32 new_textures[TILE_LOAD_BATCH_MAX] = {0};

			// Note: First download everything, then decode and upload everything to the GPU.
			// It would be faster to pipeline this somehow.
			u8* read_buffer = download_remote_batch(tiff->location.hostname, tiff->location.portno,
			                                        tiff->location.filename,
			                                        chunk_offsets, chunk_sizes, batch_size, &bytes_read, logical_thread_index);
			if (read_buffer && bytes_read > 0) {
				i64 content_offset = find_end_of_http_headers(read_buffer, bytes_read);
				i64 content_length = bytes_read - content_offset;
				u8* content = read_buffer + content_offset;

				// TODO: better way to check the real content length?
				if (content_length >= total_read_size) {

					i64 chunk_offset_in_read_buffer = 0;
					for (i32 i = 0; i < batch_size; ++i) {
						u8* current_chunk = content + chunk_offset_in_read_buffer;
						chunk_offset_in_read_buffer += chunk_sizes[i];

						load_tile_task_t* task = batch->tile_tasks + i;
						tiff_ifd_t* level_ifd = tiff->level_images + task->level;
						u8* jpeg_tables = level_ifd->jpeg_tables;
						u64 jpeg_tables_length = level_ifd->jpeg_tables_length;

						if (content[0] == 0xFF && content[1] == 0xD9) {
							// JPEG stream is empty
						} else {
							if (decode_tile(jpeg_tables, jpeg_tables_length, current_chunk, chunk_sizes[i],
							                temp_memory, (level_ifd->color_space == TIFF_PHOTOMETRIC_YCBCR))) {
//		                    printf("thread %d: successfully decoded level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
							} else {
								printf("[thread %d] failed to decode level %d, tile (%d, %d)\n", logical_thread_index, task->level, task->tile_x, task->tile_y);
							}
						}

						new_textures[i] = load_texture(temp_memory, TILE_DIM, TILE_DIM);
					}

				}

			}

			// Note: setting task->tile->texture to the texture handle lets the main thread know that the texture
			// is ready for use. However, the texture may still not *actually* be available until OpenGL has done its
			// magic, so to be 100% sure we need to call glFinish() before setting task->tile->texture
			// We only want to call glFinish() once, so we store the new texture handles temporarily while the
			// rest of the batch is still loading.
			// Better would be to flag the texture as 'ready for use' as soon as it's done, but I don't know
			// how we could query this / be notified of this. (maybe an optimization for later?)
			glFinish();
			write_barrier;
			for (i32 i = 0; i < batch_size; ++i) {
				load_tile_task_t* task = batch->tile_tasks + i;
				task->tile->texture = new_textures[i];
			}

			free(read_buffer);
		}

	}

}

void load_tile_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_t* task_data = (load_tile_task_t*) userdata;
	i32 level = task_data->level;
	i32 tile_x = task_data->tile_x;
	i32 tile_y = task_data->tile_y;
	image_t* image = task_data->image;
	level_image_t* level_image = image->level_images + level;
	tile_t* tile = get_tile(level_image, tile_x, tile_y);
	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	float tile_world_pos_x_end = (tile_x + 1) * level_image->x_tile_side_in_um;
	float tile_world_pos_y_end = (tile_y + 1) * level_image->y_tile_side_in_um;
	float tile_x_excess = tile_world_pos_x_end - image->width_in_um;
	float tile_y_excess = tile_world_pos_y_end - image->height_in_um;

	// Note: when the thread started up we allocated a large blob of memory for the thread to use privately
	// TODO: better/more explicit allocator (instead of some setting some hard-coded pointers)
	thread_memory_t* thread_memory = (thread_memory_t*) thread_local_storage[logical_thread_index];
	u8* temp_memory = (u8*) thread_memory->aligned_rest_of_thread_memory; //malloc(WSI_BLOCK_SIZE);
	memset(temp_memory, 0xFF, WSI_BLOCK_SIZE);
	u8* compressed_tile_data = (u8*) thread_memory->aligned_rest_of_thread_memory + WSI_BLOCK_SIZE;


	if (image->type == IMAGE_TYPE_TIFF) {
		tiff_t* tiff = &image->tiff.tiff;
		tiff_ifd_t* level_ifd = tiff->level_images + level;

		u64 tile_offset = level_ifd->tile_offsets[tile_index];
		u64 compressed_tile_size_in_bytes = level_ifd->tile_byte_counts[tile_index];
		// Some tiles apparently contain no data (not even an empty/dummy JPEG stream like some other tiles have).
		// We need to check for this situation and chicken out if this is the case.
		if (tile_offset == 0 || compressed_tile_size_in_bytes == 0) {
			printf("thread %d: tile level %d, tile %d (%d, %d) appears to be empty\n", logical_thread_index, level, tile_index, tile_x, tile_y);
			// TODO: Make one single 'empty' tile texture and simply reuse that
//		    memset(temp_memory, 0xFF, WSI_BLOCK_SIZE);
			goto finish_up;
		}
		u8* jpeg_tables = level_ifd->jpeg_tables;
		u64 jpeg_tables_length = level_ifd->jpeg_tables_length;

		// TODO: make async I/O code platform agnostic

		if (tiff->is_remote) {
			printf("[thread %d] remote tile requested: level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);


			i32 bytes_read = 0;
			u8* read_buffer = download_remote_chunk(tiff->location.hostname, tiff->location.portno, tiff->location.filename,
			                                        tile_offset, compressed_tile_size_in_bytes, &bytes_read, logical_thread_index);
			if (read_buffer && bytes_read > 0) {
				i64 content_offset = find_end_of_http_headers(read_buffer, bytes_read);
				i64 content_length = bytes_read - content_offset;
				u8* content = read_buffer + content_offset;

				// TODO: better way to check the real content length?
				if (content_length >= compressed_tile_size_in_bytes) {
					if (content[0] == 0xFF && content[1] == 0xD9) {
						// JPEG stream is empty
					} else {
						if (decode_tile(jpeg_tables, jpeg_tables_length, content, compressed_tile_size_in_bytes,
						                temp_memory, (level_ifd->color_space == TIFF_PHOTOMETRIC_YCBCR))) {
//		                    printf("thread %d: successfully decoded level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
						} else {
							printf("[thread %d] failed to decode level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
						}
					}

				}



			}
			free(read_buffer);
		} else {
			// To submit an async I/O request on Win32, we need to fill in an OVERLAPPED structure with the
			// offset in the file where we want to do the read operation
			LARGE_INTEGER offset = {.QuadPart = (i64)tile_offset};
			thread_memory->overlapped = (OVERLAPPED) {};
			thread_memory->overlapped.Offset = offset.LowPart;
			thread_memory->overlapped.OffsetHigh = (DWORD)offset.HighPart;
			thread_memory->overlapped.hEvent = thread_memory->async_io_event;
			ResetEvent(thread_memory->async_io_event); // reset the event to unsignaled state

			if (!ReadFile(tiff->win32_file_handle, compressed_tile_data,
			              compressed_tile_size_in_bytes, NULL, &thread_memory->overlapped)) {
				DWORD error = GetLastError();
				if (error != ERROR_IO_PENDING) {
					win32_diagnostic("ReadFile");
				}
			}

			// Wait for the result of the I/O operation (blocking, because we specify bWait=TRUE)
			DWORD bytes_read = 0;
			if (!GetOverlappedResult(tiff->win32_file_handle, &thread_memory->overlapped, &bytes_read, TRUE)) {
				win32_diagnostic("GetOverlappedResult");
			}
			// This should not be strictly necessary, but do it just in case GetOverlappedResult exits early (paranoia)
			if(WaitForSingleObject(thread_memory->overlapped.hEvent, INFINITE) != WAIT_OBJECT_0) {
				win32_diagnostic("WaitForSingleObject");
			}


			if (compressed_tile_data[0] == 0xFF && compressed_tile_data[1] == 0xD9) {
				// JPEG stream is empty
			} else {
				if (decode_tile(jpeg_tables, jpeg_tables_length, compressed_tile_data, compressed_tile_size_in_bytes,
				                temp_memory, (level_ifd->color_space == TIFF_PHOTOMETRIC_YCBCR))) {
//		            printf("thread %d: successfully decoded level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
				} else {
					printf("thread %d: failed to decode level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
				}
			}

		}

		// Trim the tile (replace with transparent color) if it extends beyond the image size
		// TODO: anti-alias edge?
		i32 new_tile_height = TILE_DIM;
		i32 pitch = TILE_DIM * BYTES_PER_PIXEL;
		if (tile_y_excess > 0) {
			i32 excess_rows = (int)(tile_y_excess / level_image->y_tile_side_in_um * TILE_DIM);
			ASSERT(excess_rows >= 0);
			new_tile_height = TILE_DIM - excess_rows;
			memset(temp_memory + (new_tile_height * pitch), 0, excess_rows * pitch);
		}
		if (tile_x_excess > 0) {
			i32 excess_pixels = (int)(tile_x_excess / level_image->x_tile_side_in_um * TILE_DIM);
			ASSERT(excess_pixels >= 0);
			i32 new_tile_width = TILE_DIM - excess_pixels;
			for (i32 row = 0; row < new_tile_height; ++row) {
				u8* write_pos = temp_memory + (row * pitch) + (new_tile_width * BYTES_PER_PIXEL);
				memset(write_pos, 0, excess_pixels * BYTES_PER_PIXEL);
			}
		}

	} else if (image->type == IMAGE_TYPE_WSI) {
		wsi_t* wsi = &image->wsi.wsi;
		i64 x = (tile_x * TILE_DIM) << level;
		i64 y = (tile_y * TILE_DIM) << level;
		openslide.openslide_read_region(wsi->osr, (u32*)temp_memory, x, y, level, TILE_DIM, TILE_DIM);
	} else {
		printf("thread %d: tile level %d, tile %d (%d, %d): unsupported image type\n", logical_thread_index, level, tile_index, tile_x, tile_y);

	}


//	printf("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);

	finish_up:;
	glEnable(GL_TEXTURE_2D);
	u32 new_texture = load_texture(temp_memory, TILE_DIM, TILE_DIM);
	glFinish(); // Block thread execution until all OpenGL operations have finished.
	write_barrier;
	tile->texture = new_texture;
	free(task_data);

}

bool32 enqueue_load_tile(image_t* image, i32 level, i32 tile_x, i32 tile_y) {
	load_tile_task_t* task_data = (load_tile_task_t*) malloc(sizeof(load_tile_task_t)); // should be freed after uploading the tile to the gpu
	*task_data = (load_tile_task_t){ .image = image, .tile = NULL, .level = level, .tile_x = tile_x, .tile_y = tile_y };

	return add_work_queue_entry(&work_queue, load_tile_func, task_data);

}

u32 get_texture_for_tile(image_t* image, i32 level, i32 tile_x, i32 tile_y) {
	level_image_t* level_image = image->level_images + level;

	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < level_image->tile_count);
	tile_t* tile = level_image->tiles + tile_index;

	return tile->texture;
}

void load_wsi(wsi_t* wsi, const char* filename) {
	if (!is_openslide_loading_done) {
		// TODO: hack! queue abused, may cause conflicts
		printf("Waiting for OpenSlide to finish loading...\n");
		while (is_queue_work_in_progress(&work_queue)) {
			do_worker_work(&work_queue, 0);
		}
	}

	if (!is_openslide_available) {
		char message[4096];
		snprintf(message, sizeof(message), "Could not open \"%s\":\nlibopenslide-0.dll is missing or broken.\n", filename);
		message_box(message);
		return;
	}

	// TODO: check if necessary anymore?
	unload_wsi(wsi);

	wsi->osr = openslide.openslide_open(filename);
	if (wsi->osr) {
		printf("Openslide: opened %s\n", filename);

		openslide.openslide_get_level0_dimensions(wsi->osr, &wsi->width, &wsi->height);
		ASSERT(wsi->width > 0);
		ASSERT(wsi->height > 0);

		wsi->level_count = openslide.openslide_get_level_count(wsi->osr);
		printf("Openslide: WSI has %d levels\n", wsi->level_count);
		if (wsi->level_count > WSI_MAX_LEVELS) {
			panic();
		}



		const char* const* wsi_properties = openslide.openslide_get_property_names(wsi->osr);
		if (wsi_properties) {
			i32 property_index = 0;
			const char* property = wsi_properties[0];
			for (; property != NULL; property = wsi_properties[++property_index]) {
				const char* property_value = openslide.openslide_get_property_value(wsi->osr, property);
				printf("%s = %s\n", property, property_value);

			}
		}

		wsi->mpp_x = 0.25f; // microns per pixel (default)
		wsi->mpp_y = 0.25f; // microns per pixel (default)
		const char* mpp_x_string = openslide.openslide_get_property_value(wsi->osr, "openslide.mpp-x");
		const char* mpp_y_string = openslide.openslide_get_property_value(wsi->osr, "openslide.mpp-y");
		if (mpp_x_string) {
			float mpp = atof(mpp_x_string);
			if (mpp > 0.0f) {
				wsi->mpp_x = mpp;
			}
		}
		if (mpp_y_string) {
			float mpp = atof(mpp_y_string);
			if (mpp > 0.0f) {
				wsi->mpp_y = mpp;
			}
		}

		for (i32 i = 0; i < wsi->level_count; ++i) {
			wsi_level_t* level = wsi->levels + i;

			openslide.openslide_get_level_dimensions(wsi->osr, i, &level->width, &level->height);
			ASSERT(level->width > 0);
			ASSERT(level->height > 0);
			i64 partial_block_x = level->width % TILE_DIM;
			i64 partial_block_y = level->height % TILE_DIM;
			level->width_in_tiles = (i32)(level->width / TILE_DIM) + (partial_block_x != 0);
			level->height_in_tiles = (i32)(level->height / TILE_DIM) + (partial_block_y != 0);
			level->um_per_pixel_x = (float)(1 << i) * wsi->mpp_x;
			level->um_per_pixel_y = (float)(1 << i) * wsi->mpp_y;
			level->x_tile_side_in_um = level->um_per_pixel_x * (float)TILE_DIM;
			level->y_tile_side_in_um = level->um_per_pixel_y * (float)TILE_DIM;
			level->tile_count = level->width_in_tiles * level->height_in_tiles;
			// Note: tiles are now managed by the format-agnostic image_t
//			level->tiles = calloc(1, level->num_tiles * sizeof(wsi_tile_t));
		}

		const char* barcode = openslide.openslide_get_property_value(wsi->osr, "philips.PIM_DP_UFS_BARCODE");
		if (barcode) {
			wsi->barcode = barcode;
		}

		const char* const* wsi_associated_image_names = openslide.openslide_get_associated_image_names(wsi->osr);
		if (wsi_associated_image_names) {
			i32 name_index = 0;
			const char* name = wsi_associated_image_names[0];
			for (; name != NULL; name = wsi_associated_image_names[++name_index]) {
				i64 w = 0;
				i64 h = 0;
				openslide.openslide_get_associated_image_dimensions(wsi->osr, name, &w, &h);
				printf("%s : w=%lld h=%lld\n", name, w, h);

			}
		}


	}

}

void unload_texture(u32 texture) {
	glDeleteTextures(1, &texture);
}

void unload_wsi(wsi_t* wsi) {
	if (wsi->osr) {
		openslide.openslide_close(wsi->osr);
		wsi->osr = NULL;
	}

}

void unload_image(image_t* image) {
	if (image) {
		if (image->type == IMAGE_TYPE_WSI) {
			unload_wsi(&image->wsi.wsi);
		} else if (image->type == IMAGE_TYPE_SIMPLE) {
			if (image->simple.pixels) {
				stbi_image_free(image->simple.pixels);
				image->simple.pixels = NULL;
			}
			if (image->simple.texture != 0) {
				unload_texture(image->simple.texture);
				image->simple.texture = 0;
			}
		} else if (image->type == IMAGE_TYPE_TIFF) {
			tiff_destroy(&image->tiff.tiff);
		}

		if (image->level_images) {
			for (i32 i = 0; i < image->level_count; ++i) {
				level_image_t* level_image = image->level_images + i;
				if (level_image->tiles) {
					for (i32 j = 0; j < level_image->tile_count; ++j) {
						tile_t* tile = level_image->tiles + j;
						if (tile->texture != 0) {
							unload_texture(tile->texture);
						}
					}
				}
				free(level_image->tiles);
				level_image->tiles = NULL;
			}
			free(image->level_images);
			image->level_images = NULL;
		}


	}
}

void unload_all_images(app_state_t *app_state) {
	i32 current_image_count = sb_count(app_state->loaded_images);
	if (current_image_count > 0) {
		ASSERT(app_state->loaded_images);
		for (i32 i = 0; i < current_image_count; ++i) {
			image_t* old_image = app_state->loaded_images + i;
			unload_image(old_image);
		}
		sb_free(app_state->loaded_images);
		app_state->loaded_images = NULL;
	}
	mouse_show();
}

void add_image_from_tiff(app_state_t* app_state, tiff_t tiff) {
	image_t new_image = (image_t){};
	new_image.type = IMAGE_TYPE_TIFF;
	new_image.tiff.tiff = tiff;
	new_image.is_freshly_loaded = true;
	new_image.mpp_x = tiff.mpp_x;
	new_image.mpp_y = tiff.mpp_y;
	ASSERT(tiff.main_image);
	new_image.width_in_pixels = tiff.main_image->image_width;
	new_image.width_in_um = tiff.main_image->image_width * tiff.mpp_x;
	new_image.height_in_pixels = tiff.main_image->image_height;
	new_image.height_in_um = tiff.main_image->image_height * tiff.mpp_y;
	// TODO: fix code duplication with tiff_deserialize()
	if (tiff.level_count > 0 && tiff.main_image->tile_width) {

		new_image.level_count = tiff.level_count;
		new_image.level_images = (level_image_t*) calloc(1, tiff.level_count * sizeof(level_image_t));

		for (i32 i = 0; i < tiff.level_count; ++i) {
			level_image_t* level_image = new_image.level_images + i;
			tiff_ifd_t* ifd = tiff.level_images + i;
			level_image->tile_count = ifd->tile_count;
			level_image->width_in_tiles = ifd->width_in_tiles;
			level_image->height_in_tiles = ifd->height_in_tiles;
			level_image->um_per_pixel_x = ifd->um_per_pixel_x;
			level_image->um_per_pixel_y = ifd->um_per_pixel_y;
			level_image->x_tile_side_in_um = ifd->x_tile_side_in_um;
			level_image->y_tile_side_in_um = ifd->y_tile_side_in_um;
			level_image->tiles = (tile_t*) calloc(1, ifd->tile_count * sizeof(tile_t));
			ASSERT(ifd->tile_byte_counts != NULL);
			ASSERT(ifd->tile_offsets != NULL);
			// mark the empty tiles, so that we can skip loading them later on
			for (i32 j = 0; j < level_image->tile_count; ++j) {
				tile_t* tile = level_image->tiles + j;
				u64 tile_byte_count = ifd->tile_byte_counts[j];
				if (tile_byte_count == 0) {
					tile->is_empty = true;
				}
			}
		}
	}
	reset_scene(&new_image, &app_state->scene);
	sb_push(app_state->loaded_images, new_image);
}

bool file_exists(const char* filename) {
	return (access(filename, F_OK) != -1);
}

bool32 load_generic_file(app_state_t *app_state, const char *filename) {
	const char* ext = get_file_extension(filename);
	if (strcasecmp(ext, "json") == 0) {
		reload_global_caselist(app_state, filename);
		show_slide_list_window = true;
		return true;
	} else if (strcasecmp(ext, "xml") == 0) {
		return load_asap_xml_annotations(app_state, filename);
	} else {
		// assume it is an image file?
		reset_global_caselist(app_state);
		if (load_image_from_file(app_state, filename)) {
			// Check if there is an associated ASAP XML annotations file
			size_t len = strlen(filename);
			size_t temp_size = len + 5; // add 5 so that we can always append ".xml\0"
			char* temp_filename = alloca(temp_size);
			strncpy(temp_filename, filename, temp_size);
			replace_file_extension(temp_filename, temp_size, "xml");
			if (file_exists(temp_filename)) {
				printf("Found XML annotations: %s\n", temp_filename);
				load_asap_xml_annotations(app_state, temp_filename);
			}
			return true;

		} else {
			printf("Could not load '%s'\n", filename);
			return false;
		}

	}
}

bool32 load_image_from_file(app_state_t* app_state, const char *filename) {
	unload_all_images(app_state);

	bool32 result = false;
	const char* ext = get_file_extension(filename);

	if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0) {
		// Load using stb_image
		image_t image = (image_t){};
		image.type = IMAGE_TYPE_SIMPLE;
		image.simple.channels = 4; // desired: RGBA
		image.simple.pixels = stbi_load(filename, &image.simple.width, &image.simple.height, &image.simple.channels_in_file, 4);
		if (image.simple.pixels) {
			glEnable(GL_TEXTURE_2D);
			glGenTextures(1, &image.simple.texture);
			//glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, image.simple.texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.simple.width, image.simple.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.simple.pixels);

			image.is_freshly_loaded = true;
			sb_push(app_state->loaded_images, image);
			result = true;

			//stbi_image_free(image->stbi.pixels);
		}

	} else if (app_state->use_builtin_tiff_backend && (strcasecmp(ext, "tiff") == 0 || strcasecmp(ext, "tif") == 0)) {
		tiff_t tiff = {0};
		if (open_tiff_file(&tiff, filename)) {
			add_image_from_tiff(app_state, tiff);
			result = true;
		} else {
			tiff_destroy(&tiff);
			result = false;
			printf("Opening %s failed\n", filename);
		}

	} else {
		// Try to load the file using OpenSlide
		if (!is_openslide_available) {
			printf("Can't try to load %s using OpenSlide, because OpenSlide is not available\n", filename);
			return false;
		}
		image_t image = (image_t){};

		image.type = IMAGE_TYPE_WSI;
		wsi_t* wsi = &image.wsi.wsi;
		load_wsi(wsi, filename);
		if (wsi->osr) {
			image.is_freshly_loaded = true;
			image.mpp_x = wsi->mpp_x;
			image.mpp_y = wsi->mpp_y;
			image.width_in_pixels = wsi->width;
			image.width_in_um = wsi->width * wsi->mpp_x;
			image.height_in_pixels = wsi->height;
			image.height_in_um = wsi->height * wsi->mpp_y;
			if (wsi->level_count > 0 && wsi->levels[0].x_tile_side_in_um > 0) {

				image.level_count = wsi->level_count;
				image.level_images = (level_image_t*) calloc(1, wsi->level_count * sizeof(level_image_t));

				for (i32 i = 0; i < wsi->level_count; ++i) {
					level_image_t* level_image = image.level_images + i;
					wsi_level_t* wsi_level = wsi->levels + i;
					level_image->tile_count = wsi_level->tile_count;
					level_image->width_in_tiles = wsi_level->width_in_tiles;
					level_image->height_in_tiles = wsi_level->height_in_tiles;
					level_image->um_per_pixel_x = wsi_level->um_per_pixel_x;
					level_image->um_per_pixel_y = wsi_level->um_per_pixel_y;
					level_image->x_tile_side_in_um = wsi_level->x_tile_side_in_um;
					level_image->y_tile_side_in_um = wsi_level->y_tile_side_in_um;
					level_image->tiles = (tile_t*) calloc(1, wsi_level->tile_count * sizeof(tile_t));
					// Note: OpenSlide doesn't allow us to quickly check if tiles are empty or not.
				}
			}

			reset_scene(&image, &app_state->scene);
			sb_push(app_state->loaded_images, image);
			result = true;

		}
	}
	return result;

}

i32 tile_pos_from_world_pos(float world_pos, float tile_side) {
	ASSERT(tile_side > 0);
	float tile_float = (world_pos / tile_side);
	float tile = (i32)floorf(tile_float);
	return tile;
}

bool32 was_button_pressed(button_state_t* button) {
	bool32 result = button->down && button->transition_count > 0;
	return result;
}

bool32 was_button_released(button_state_t* button) {
	bool32 result = (!button->down) && button->transition_count > 0;
	return result;
}

bool32 was_key_pressed(input_t* input, i32 keycode) {
	u8 key = keycode & 0xFF;
	button_state_t* button = &input->keyboard.keys[key];
	bool32 result = was_button_pressed(button);
	return result;
}

bool32 is_key_down(input_t* input, i32 keycode) {
	u8 key = keycode & 0xFF;
	bool32 result = input->keyboard.keys[key].down;
	return result;
}


i64 zoom_in_key_hold_down_start_time;
i64 zoom_in_key_times_zoomed_while_holding;
i64 zoom_out_key_hold_down_start_time;
i64 zoom_out_key_times_zoomed_while_holding;

// a 'frame of reference' and coordinate system for drawing 2d objects and making sense of the world
typedef struct {
	v2f view_pos; // camera position (at top left corner of the screen)
} layer_t;

layer_t basic_image_layer;


int priority_cmp_func (const void* a, const void* b) {
	return ( (*(load_tile_task_t*)b).priority - (*(load_tile_task_t*)a).priority );
}

void init_scene(app_state_t *app_state, scene_t *scene) {
	memset(scene, 0, sizeof(scene_t));
	scene->clear_color = app_state->clear_color;
	scene->entity_count = 1; // NOTE: entity 0 = null entity, so start from 1
	scene->camera = (v2f){0.0f, 0.0f}; // center camera at origin
	scene->pixel_width = 1.0f;
	scene->pixel_height = 1.0f;
	scene->initialized = true;
}

void init_app_state(app_state_t* app_state) {
	memset(app_state, 0, sizeof(app_state_t));
	app_state->clear_color = (v4f){0.95f, 0.95f, 0.95f, 1.00f};
	app_state->black_level = 0.10f;
	app_state->white_level = 0.95f;
	// If disabled, revert to OpenSlide when loading TIFF files.
	app_state->use_builtin_tiff_backend = true;
	app_state->initialized = true;
}

void autosave(app_state_t* app_state, bool force_ignore_delay) {
	annotation_set_t* annotation_set = &app_state->scene.annotation_set;
	autosave_annotations(app_state, annotation_set, force_ignore_delay);
}



// TODO: refactor delta_t
// TODO: think about having access to both current and old input. (for comparing); is transition count necessary?
void viewer_update_and_render(app_state_t *app_state, input_t *input, i32 client_width, i32 client_height, float delta_t) {

	if (!app_state->initialized) init_app_state(app_state);
	// Note: the window might get resized, so need to update this every frame
	app_state->client_viewport = (rect2i){0, 0, client_width, client_height};

	scene_t* scene = &app_state->scene;
	if (!scene->initialized) init_scene(app_state, scene);

	// Note: could be changed to allow e.g. multiple scenes side by side
	scene->viewport = app_state->client_viewport;


	// TODO: this is part of rendering and doesn't belong here
	gui_new_frame();

	// Set up rendering state for the next frame
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, client_width, client_height);
	glClearColor(app_state->clear_color.r, app_state->clear_color.g, app_state->clear_color.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);



	// Determine the image to view;
	for (i32 i = 0; i < 9; ++i) {
		if (input->keyboard.keys['1' + i].down) {
			app_state->displayed_image = MIN(sb_count(app_state->loaded_images)-1, i);
		}
	}

	image_t* image = app_state->loaded_images + app_state->displayed_image;

	if (!image) {
		return; // nothing to draw
	}


	// TODO: mutate state here

	// todo: process even more of the mouse/keyboard input here?
	v2i current_drag_vector = {};
	float mouse_x = 0.0f;
	float mouse_y = 0.0f;
	bool32 scene_clicked = false;
	float click_x = 0.0f;
	float click_y = 0.0f;
	if (input) {
		if (was_key_pressed(input, 'W') && is_key_down(input, KEYCODE_CONTROL)) {
			menu_close_file(app_state);
			return;
		}

		if (was_button_released(&input->mouse_buttons[0])) {
			float drag_distance = v2i_distance(scene->cumulative_drag_vector);
			if (drag_distance < 2.0f) {
				scene_clicked = true;
			}
		}

		if (input->mouse_buttons[0].down) {
			// Mouse drag.
			if (input->mouse_buttons[0].transition_count != 0) {
				// Don't start dragging if clicked outside the window
				rect2i valid_drag_start_rect = {0, 0, client_width, client_height};
				if (is_point_inside_rect2i(valid_drag_start_rect, input->mouse_xy)) {
					scene->is_dragging = true; // drag start
					scene->cumulative_drag_vector = (v2i){};
//						printf("Drag started: x=%d y=%d\n", input->mouse_xy.x, input->mouse_xy.y);
				}
			} else if (scene->is_dragging) {
				// already started dragging on a previous frame
				current_drag_vector = input->drag_vector;
				scene->cumulative_drag_vector.x += current_drag_vector.x;
				scene->cumulative_drag_vector.y += current_drag_vector.y;
			}
			input->drag_vector = (v2i){};
			mouse_hide();
		} else {
			if (input->mouse_buttons[0].transition_count != 0) {
				mouse_show();
				scene->is_dragging = false;
//			        printf("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
			}
		}
	}

	if (image->type == IMAGE_TYPE_SIMPLE) {
		// Display a basic image

		float display_pos_x = 0.0f;
		float display_pos_y = 0.0f;

		float L = display_pos_x;
		float R = display_pos_x + client_width;
		float T = display_pos_y;
		float B = display_pos_y + client_height;
		mat4x4 ortho_projection =
				{
						{ 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
						{ 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
						{ 0.0f,         0.0f,        -1.0f,   0.0f },
						{ (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
				};

		// Set up model matrix: scale and translate to the correct world position
		static v2f obj_pos;
		if (image->is_freshly_loaded) {
			obj_pos = (v2f) {50, 100};
			image->is_freshly_loaded = false;
		}
		float pan_multiplier = 2.0f;
		if (scene->is_dragging) {
			obj_pos.x += current_drag_vector.x * pan_multiplier;
			obj_pos.y += current_drag_vector.y * pan_multiplier;
		}

		mat4x4 model_matrix;
		mat4x4_identity(model_matrix);
		mat4x4_translate_in_place(model_matrix, obj_pos.x, obj_pos.y, 0.0f);
		mat4x4_scale_aniso(model_matrix, model_matrix, image->simple.width * 2, image->simple.height * 2, 1.0f);


		glUseProgram(basic_shader);

		if (app_state->use_image_adjustments) {
			glUniform1f(basic_shader_u_black_level, app_state->black_level);
			glUniform1f(basic_shader_u_white_level, app_state->white_level);
		} else {
			glUniform1f(basic_shader_u_black_level, 0.0f);
			glUniform1f(basic_shader_u_white_level, 1.0f);
		}

		glUniformMatrix4fv(basic_shader_u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);

		v2f* view_pos = &basic_image_layer.view_pos;

		mat4x4 view_matrix;
		mat4x4_identity(view_matrix);
		mat4x4_translate_in_place(view_matrix, -view_pos->x, -view_pos->y, 0.0f);
		mat4x4_scale_aniso(view_matrix, view_matrix, 0.5f, 0.5f, 1.0f);

		mat4x4 projection_view_matrix;
		mat4x4_mul(projection_view_matrix, ortho_projection, view_matrix);

		glUniformMatrix4fv(basic_shader_u_projection_view_matrix, 1, GL_FALSE, &projection_view_matrix[0][0]);

		// todo: bunch up vertex and index uploads

		draw_rect(image->simple.texture);
	}
	else if (image->type == IMAGE_TYPE_TIFF || image->type == IMAGE_TYPE_WSI) {

		i32 old_level = scene->current_level;
		i32 center_offset_x = 0;
		i32 center_offset_y = 0;

		i32 max_level = image->level_count - 1;
		level_image_t* level_image = image->level_images + scene->current_level;

		// TODO: move all input handling code together
		if (input) {

			i32 dlevel = 0;
			bool32 used_mouse_to_zoom = false;

			// Zoom in or out using the mouse wheel.
			if (input->mouse_z != 0) {
				dlevel = (input->mouse_z > 0 ? -1 : 1);
				used_mouse_to_zoom = true;
			}

			float key_repeat_interval = 0.2f; // in seconds

			// Zoom out using Z or /
			if (is_key_down(input, 'Z') || is_key_down(input, KEYCODE_OEM_2 /* '/' */)) {

				if (was_key_pressed(input, 'Z') || was_key_pressed(input, KEYCODE_OEM_2 /* '/' */)) {
					dlevel += 1;
					zoom_in_key_hold_down_start_time = get_clock();
					zoom_in_key_times_zoomed_while_holding = 0;
				} else {
					float time_elapsed = get_seconds_elapsed(zoom_in_key_hold_down_start_time, get_clock());
					int zooms = (int) (time_elapsed / key_repeat_interval);
					if ((zooms - zoom_in_key_times_zoomed_while_holding) == 1) {
						zoom_in_key_times_zoomed_while_holding = zooms;
						dlevel += 1;
					}
				}
			}

			// Zoom in using X or .
			if (is_key_down(input, 'X') || is_key_down(input, KEYCODE_OEM_PERIOD)) {


				if (was_key_pressed(input, 'X') || was_key_pressed(input, KEYCODE_OEM_PERIOD)) {
					dlevel -= 1;
					zoom_out_key_hold_down_start_time = get_clock();
					zoom_out_key_times_zoomed_while_holding = 0;
				} else {
					float time_elapsed = get_seconds_elapsed(zoom_out_key_hold_down_start_time, get_clock());
					int zooms = (int) (time_elapsed / key_repeat_interval);
					if ((zooms - zoom_out_key_times_zoomed_while_holding) == 1) {
						zoom_out_key_times_zoomed_while_holding = zooms;
						dlevel -= 1;
					}
				}
			}


			if (dlevel != 0) {
//		        printf("mouse_z = %d\n", input->mouse_z);
				scene->current_level = CLAMP(scene->current_level + dlevel, 0, image->level_count - 1);
				level_image = image->level_images + scene->current_level;

				if (scene->current_level != old_level && used_mouse_to_zoom) {
#if 1
					center_offset_x = input->mouse_xy.x - client_width / 2;
					center_offset_y = (input->mouse_xy.y - client_height / 2);

					if (scene->current_level < old_level) {
						// Zoom in, while keeping the area around the mouse cursor in the same place on the screen.
						scene->camera.x += center_offset_x * level_image->um_per_pixel_x;
						scene->camera.y += center_offset_y * level_image->um_per_pixel_y;
					} else if (scene->current_level > old_level) {
						// Zoom out, while keeping the area around the mouse cursor in the same place on the screen.
						scene->camera.x -= center_offset_x * level_image->um_per_pixel_x * 0.5f;
						scene->camera.y -= center_offset_y * level_image->um_per_pixel_y * 0.5f;
					}
#endif
				}
			}

		}




		// TODO: fix/rewrite
		// Spring/bounce effect
		float d_zoom = (float) scene->current_level - scene->zoom_position;
		float abs_d_zoom = fabsf(d_zoom);
		float sign_d_zoom = signbit(d_zoom) ? -1.0f : 1.0f;
		float linear_catch_up_speed = 10.0f * delta_t;
		float exponential_catch_up_speed = 18.0f * delta_t;
		if (abs_d_zoom > linear_catch_up_speed) {
			d_zoom = (linear_catch_up_speed + (abs_d_zoom - linear_catch_up_speed) * exponential_catch_up_speed) *
			         sign_d_zoom;
		}
		scene->zoom_position += d_zoom;

		scene->pixel_width = powf(2.0f, scene->zoom_position) * image->mpp_x;
		scene->pixel_height = powf(2.0f, scene->zoom_position) * image->mpp_y;

		float r_minus_l = scene->pixel_width * (float) client_width;
		float t_minus_b = scene->pixel_height * (float) client_height;



		v2f camera_min = {
				.x = scene->camera.x - r_minus_l * 0.5f,
				.y = scene->camera.y - t_minus_b * 0.5f,
		};
		v2f camera_max = {
				.x = scene->camera.x + r_minus_l * 0.5f,
				.y = scene->camera.y + t_minus_b * 0.5f,
		};


		draw_annotations(&scene->annotation_set, camera_min, scene->pixel_width);

		i32 camera_tile_x1 = tile_pos_from_world_pos(camera_min.x, level_image->x_tile_side_in_um);
		i32 camera_tile_x2 = tile_pos_from_world_pos(camera_max.x, level_image->x_tile_side_in_um) + 1;
		i32 camera_tile_y1 = tile_pos_from_world_pos(camera_min.y, level_image->y_tile_side_in_um);
		i32 camera_tile_y2 = tile_pos_from_world_pos(camera_max.y, level_image->y_tile_side_in_um) + 1;

		camera_tile_x1 = CLAMP(camera_tile_x1, 0, level_image->width_in_tiles);
		camera_tile_x2 = CLAMP(camera_tile_x2, 0, level_image->width_in_tiles);
		camera_tile_y1 = CLAMP(camera_tile_y1, 0, level_image->height_in_tiles);
		camera_tile_y2 = CLAMP(camera_tile_y2, 0, level_image->height_in_tiles);

		scene->mouse = scene->camera;
		if (input) {

			scene->mouse.x = camera_min.x + (float)input->mouse_xy.x * scene->pixel_width;
			scene->mouse.y = camera_min.y + (float)input->mouse_xy.y * scene->pixel_height;


			if (was_key_pressed(input, 'P')) {
				app_state->use_image_adjustments = !app_state->use_image_adjustments;
			}
#if 0

			// Experimental code for exporting regions of the wsi to a raw image file.
			if (input->mouse_buttons[1].down && input->mouse_buttons[1].transition_count > 0) {
				DUMMY_STATEMENT;
				i32 click_x = (camera_rect_x1 + input->mouse_xy.x * level_image->um_per_pixel_x) / wsi->mpp_x;
				i32 click_y = (camera_rect_y2 - input->mouse_xy.y * level_image->um_per_pixel_y) / wsi->mpp_y;
				printf("Clicked screen x=%d y=%d; image x=%d y=%d\n",
						input->mouse_xy.x, input->mouse_xy.y, click_x, click_y);
			}

			{
				button_state_t* button = &input->keyboard.keys['E'];
				if (button->down && button->transition_count > 0) {
					v2i p1 = { 101456, 30736 };
					v2i p2 = { 134784
				, 61384 };
					i64 w = p2.x - p1.x;
					i64 h = p2.y - p1.y;
					size_t export_size = w * h * BYTES_PER_PIXEL;
					u32* temp_memory = malloc(export_size);
					openslide.openslide_read_region(wsi->osr, temp_memory, p1.x, p1.y, 0, w, h);
					FILE* fp = fopen("export.raw", "wb");
					fwrite(temp_memory, export_size, 1, fp);
					fclose(fp);
					free(temp_memory);
					printf("Exported region, width %d height %d\n", w, h);

				}
			}
#endif

			// Panning should be faster when zoomed in very far.
			float panning_multiplier = 1.0f + 3.0f * ((float) max_level - scene->zoom_position) / (float) max_level;
			if (is_key_down(input, KEYCODE_SHIFT)) {
				panning_multiplier *= 0.25f;
			}

			// Panning using the arrow or WASD keys.
			float panning_speed = 900.0f * delta_t * panning_multiplier;
			if (input->keyboard.action_down.down || is_key_down(input, 'S')) {
				scene->camera.y += level_image->um_per_pixel_y * panning_speed;
				mouse_hide();
			}
			if (input->keyboard.action_up.down || is_key_down(input, 'W')) {
				scene->camera.y -= level_image->um_per_pixel_y * panning_speed;
				mouse_hide();
			}
			if (input->keyboard.action_right.down || is_key_down(input, 'D')) {
				scene->camera.x += level_image->um_per_pixel_x * panning_speed;
				mouse_hide();
			}
			if (input->keyboard.action_left.down || is_key_down(input, 'A')) {
				scene->camera.x -= level_image->um_per_pixel_x * panning_speed;
				mouse_hide();
			}

			if (scene->is_dragging) {
				scene->camera.x -= current_drag_vector.x * level_image->um_per_pixel_x * panning_multiplier;
				scene->camera.y -= current_drag_vector.y * level_image->um_per_pixel_y * panning_multiplier;
			}

			// try to select an annotation
			if (scene->annotation_set.annotation_count > 0) {
				if (was_key_pressed(input, 'Q') || (!gui_want_capture_mouse && scene_clicked)) {
					i64 select_begin = get_clock();
					select_annotation(scene, is_key_down(input, KEYCODE_CONTROL));
					float selection_ms = get_seconds_elapsed(select_begin, get_clock()) * 1000.0f;
//					printf("Selecting took %g ms.\n", selection_ms);
				}
			}

			if (!gui_want_capture_keyboard && was_key_pressed(input, KEYCODE_DELETE)) {
				delete_selected_annotations(&scene->annotation_set);
			}

		}


		// IO

		// Create a 'wishlist' of tiles to request
		load_tile_task_t tile_wishlist[32];
		i32 num_tasks_on_wishlist = 0;
		float screen_radius = ATLEAST(1.0f, sqrtf(SQUARE(client_width/2) + SQUARE(client_height/2)));

		for (i32 level = image->level_count - 1; level >= scene->current_level; --level) {
			level_image_t *drawn_level = image->level_images + level;

			i32 base_priority = (image->level_count - level) * 100; // highest priority for the most zoomed in levels

			i32 level_camera_tile_x1 = tile_pos_from_world_pos(camera_min.x, drawn_level->x_tile_side_in_um);
			i32 level_camera_tile_x2 = tile_pos_from_world_pos(camera_max.x, drawn_level->x_tile_side_in_um) + 1;
			i32 level_camera_tile_y1 = tile_pos_from_world_pos(camera_min.y, drawn_level->y_tile_side_in_um);
			i32 level_camera_tile_y2 = tile_pos_from_world_pos(camera_max.y, drawn_level->y_tile_side_in_um) + 1;

			level_camera_tile_x1 = CLAMP(level_camera_tile_x1, 0, drawn_level->width_in_tiles);
			level_camera_tile_x2 = CLAMP(level_camera_tile_x2, 0, drawn_level->width_in_tiles);
			level_camera_tile_y1 = CLAMP(level_camera_tile_y1, 0, drawn_level->height_in_tiles);
			level_camera_tile_y2 = CLAMP(level_camera_tile_y2, 0, drawn_level->height_in_tiles);


			for (i32 tile_y = level_camera_tile_y1; tile_y < level_camera_tile_y2; ++tile_y) {
				for (i32 tile_x = level_camera_tile_x1; tile_x < level_camera_tile_x2; ++tile_x) {



					tile_t* tile = get_tile(drawn_level, tile_x, tile_y);
					if (tile->texture != 0 || tile->is_empty || tile->is_submitted_for_loading) {
						continue; // nothing needs to be done with this tile
					}

					float tile_distance_from_center_of_screen_x =
							(scene->camera.x - ((tile_x + 0.5f) * drawn_level->x_tile_side_in_um)) / drawn_level->um_per_pixel_x;
					float tile_distance_from_center_of_screen_y =
							(scene->camera.y - ((tile_y + 0.5f) * drawn_level->y_tile_side_in_um)) / drawn_level->um_per_pixel_y;
					float tile_distance_from_center_of_screen =
							sqrtf(SQUARE(tile_distance_from_center_of_screen_x) + SQUARE(tile_distance_from_center_of_screen_y));
					tile_distance_from_center_of_screen /= screen_radius;
					// prioritize tiles close to the center of the screen
					float priority_bonus = (1.0f - tile_distance_from_center_of_screen) * 300.0f; // can be tweaked.
					i32 tile_priority = base_priority + (i32)priority_bonus;



					if (num_tasks_on_wishlist >= COUNT(tile_wishlist)) {
						break;
					}
					tile_wishlist[num_tasks_on_wishlist++] = (load_tile_task_t){
							.image = image, .tile = tile, .level = level, .tile_x = tile_x, .tile_y = tile_y,
							.priority = tile_priority,
					};

				}
			}

		}
//		printf("Num tiles on wishlist = %d\n", num_tasks_on_wishlist);

		qsort(tile_wishlist, num_tasks_on_wishlist, sizeof(load_tile_task_t), priority_cmp_func);



		if (num_tasks_on_wishlist > 0){


			if (image->type == IMAGE_TYPE_TIFF && image->tiff.tiff.is_remote) {
				// For remote slides, only send out a batch request every so often, instead of single tile requests every frame.
				// (to reduce load on the server)
				static u32 intermittent = 0;
				++intermittent;
				u32 intermittent_interval = 1;
				intermittent_interval = 5; // reduce load on remote server; can be tweaked
				if (intermittent % intermittent_interval == 0) {
					i32 max_tiles_to_load = ATMOST(num_tasks_on_wishlist, 3); // can be tweaked

					load_tile_task_batch_t* batch = (load_tile_task_batch_t*) calloc(1, sizeof(load_tile_task_batch_t));
					batch->task_count = ATMOST(COUNT(batch->tile_tasks), max_tiles_to_load);
					memcpy(batch->tile_tasks, tile_wishlist, batch->task_count * sizeof(load_tile_task_t));
					if (add_work_queue_entry(&work_queue, tiff_load_tile_batch_func, batch)) {
						// success
						for (i32 i = 0; i < batch->task_count; ++i) {
							load_tile_task_t* task = batch->tile_tasks + i;
							task->tile->is_submitted_for_loading = true;
						}
					}
				}
			} else {
				// regular file loading
				i32 max_tiles_to_load = ATMOST(num_tasks_on_wishlist, 10);
				for (i32 i = 0; i < max_tiles_to_load; ++i) {
					load_tile_task_t* the_task = &tile_wishlist[i];
					load_tile_task_t* task_data = (load_tile_task_t*) malloc(sizeof(load_tile_task_t)); // should be freed after uploading the tile to the gpu
					*task_data = *the_task;
					if (add_work_queue_entry(&work_queue, load_tile_func, task_data)) {
						// success
						task_data->tile->is_submitted_for_loading = true;
					}
				}
			}


		}



		// RENDERING


		mat4x4 projection = {};
		{
			float l = -0.5f * r_minus_l;
			float r = +0.5f * r_minus_l;
			float b = +0.5f * t_minus_b;
			float t = -0.5f * t_minus_b;
			float n = 100.0f;
			float f = -100.0f;
			mat4x4_ortho(projection, l, r, b, t, n, f);
		}

		mat4x4 I;
		mat4x4_identity(I);

		// define view matrix
		mat4x4 view_matrix;
		mat4x4_translate(view_matrix, -scene->camera.x, -scene->camera.y, 0.0f);

		mat4x4 projection_view_matrix;
		mat4x4_mul(projection_view_matrix, projection, view_matrix);

		glUseProgram(basic_shader);
		glUniformMatrix4fv(basic_shader_u_projection_view_matrix, 1, GL_FALSE, &projection_view_matrix[0][0]);

		glUniform3fv(basic_shader_u_background_color, 1, (GLfloat *) &app_state->clear_color);
		if (app_state->use_image_adjustments) {
			glUniform1f(basic_shader_u_black_level, app_state->black_level);
			glUniform1f(basic_shader_u_white_level, app_state->white_level);
		} else {
			glUniform1f(basic_shader_u_black_level, 0.0f);
			glUniform1f(basic_shader_u_white_level, 1.0f);
		}

		i32 num_levels_above_current = image->level_count - scene->current_level - 1;
		ASSERT(num_levels_above_current >= 0);

		// Draw all levels within the viewport, up to the current zoom factor
		for (i32 level = scene->current_level; level < image->level_count; ++level) {
//		for (i32 level = image->level_count - 1; level >= scene->current_level; --level) {
			level_image_t *drawn_level = image->level_images + level;

			i32 level_camera_tile_x1 = tile_pos_from_world_pos(camera_min.x, drawn_level->x_tile_side_in_um);
			i32 level_camera_tile_x2 = tile_pos_from_world_pos(camera_max.x, drawn_level->x_tile_side_in_um) + 1;
			i32 level_camera_tile_y1 = tile_pos_from_world_pos(camera_min.y, drawn_level->y_tile_side_in_um);
			i32 level_camera_tile_y2 = tile_pos_from_world_pos(camera_max.y, drawn_level->y_tile_side_in_um) + 1;

			level_camera_tile_x1 = CLAMP(level_camera_tile_x1, 0, drawn_level->width_in_tiles);
			level_camera_tile_x2 = CLAMP(level_camera_tile_x2, 0, drawn_level->width_in_tiles);
			level_camera_tile_y1 = CLAMP(level_camera_tile_y1, 0, drawn_level->height_in_tiles);
			level_camera_tile_y2 = CLAMP(level_camera_tile_y2, 0, drawn_level->height_in_tiles);

			for (i32 tile_y = level_camera_tile_y1; tile_y < level_camera_tile_y2; ++tile_y) {
				for (i32 tile_x = level_camera_tile_x1; tile_x < level_camera_tile_x2; ++tile_x) {

					tile_t *tile = get_tile(drawn_level, tile_x, tile_y);
					if (tile->texture) {
						u32 texture = get_texture_for_tile(image, level, tile_x, tile_y);

						float tile_pos_x = drawn_level->x_tile_side_in_um * tile_x;
						float tile_pos_y = drawn_level->y_tile_side_in_um * tile_y;

						// define model matrix
						mat4x4 model_matrix;
						mat4x4_translate(model_matrix, tile_pos_x, tile_pos_y, 0.0f);
						mat4x4_scale_aniso(model_matrix, model_matrix, drawn_level->x_tile_side_in_um,
						                   drawn_level->y_tile_side_in_um, 1.0f);
						glUniformMatrix4fv(basic_shader_u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);

						draw_rect(texture);
					}

				}
			}

		}
	}


}

