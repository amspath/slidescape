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

void viewer_upload_already_cached_tile_to_gpu(int logical_thread_index, void* userdata) {
	load_tile_task_t* task = (load_tile_task_t*) userdata;
	tile_t* tile = task->tile;
	if (tile->is_cached && tile->pixels) {
		if (tile->need_gpu_residency) {
			// TODO: use PBO's instead
			u32 new_texture = load_texture(tile->pixels, TILE_DIM, TILE_DIM);
			tile->texture = new_texture;
		} else {
			ASSERT(!"viewer_only_upload_cached_tile() called but !tile->need_gpu_residency\n");
		}

		if (!task->need_keep_in_cache) {
			free(tile->pixels);
			tile->pixels = NULL;
			tile->is_cached = false;
		}
	} else {
		printf("Warning: viewer_only_upload_cached_tile() called on a non-cached tile\n");
	}
}

typedef struct viewer_notify_tile_completed_task_t {
	u8* pixel_memory;
	tile_t* tile;
	i32 tile_width;
} viewer_notify_tile_completed_task_t;

void viewer_notify_load_tile_completed(int logical_thread_index, void* userdata) {
	viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*) userdata;
	if (task->pixel_memory) {
		bool need_free_pixel_memory = true;
		if (task->tile) {
			tile_t* tile = task->tile;
			if (tile->need_gpu_residency) {
				// TODO: use PBO's instead
				u32 new_texture = load_texture(task->pixel_memory, TILE_DIM, TILE_DIM);
				tile->texture = new_texture;
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

//	printf("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, task_data->level, task_data->tile_x, task_data->tile_y);
	free(userdata);
}

void load_tile_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_t* task = (load_tile_task_t*) userdata;
	i32 level = task->level;
	i32 tile_x = task->tile_x;
	i32 tile_y = task->tile_y;
	image_t* image = task->image;
	level_image_t* level_image = image->level_images + level;
	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	float tile_world_pos_x_end = (tile_x + 1) * level_image->x_tile_side_in_um;
	float tile_world_pos_y_end = (tile_y + 1) * level_image->y_tile_side_in_um;
	float tile_x_excess = tile_world_pos_x_end - image->width_in_um;
	float tile_y_excess = tile_world_pos_y_end - image->height_in_um;

	// Note: when the thread started up we allocated a large blob of memory for the thread to use privately
	// TODO: better/more explicit allocator (instead of some setting some hard-coded pointers)
	thread_memory_t* thread_memory = (thread_memory_t*) thread_local_storage[logical_thread_index];
	u8* temp_memory = malloc(WSI_BLOCK_SIZE);//(u8*) thread_memory->aligned_rest_of_thread_memory;
	memset(temp_memory, 0xFF, WSI_BLOCK_SIZE);
	u8* compressed_tile_data = (u8*) thread_memory->aligned_rest_of_thread_memory;// + WSI_BLOCK_SIZE;


	if (image->type == IMAGE_TYPE_TIFF) {
		tiff_t* tiff = &image->tiff.tiff;
		tiff_ifd_t* level_ifd = tiff->level_images_ifd + level_image->pyramid_image_index;

		u64 tile_offset = level_ifd->tile_offsets[tile_index];
		u64 compressed_tile_size_in_bytes = level_ifd->tile_byte_counts[tile_index];
		// Some tiles apparently contain no data (not even an empty/dummy JPEG stream like some other tiles have).
		// We need to check for this situation and chicken out if this is the case.
		if (tile_offset == 0 || compressed_tile_size_in_bytes == 0) {
			printf("thread %d: tile level %d, tile %d (%d, %d) appears to be empty\n", logical_thread_index, level, tile_index, tile_x, tile_y);
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




	finish_up:;

	viewer_notify_tile_completed_task_t* completion_task = (viewer_notify_tile_completed_task_t*) calloc(1, sizeof(viewer_notify_tile_completed_task_t));
	completion_task->pixel_memory = temp_memory;
	completion_task->tile_width = TILE_DIM; // TODO: make tile width agnostic
	completion_task->tile = task->tile;

	//	printf("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);
	ASSERT(task->completion_callback);
	add_work_queue_entry(&thread_message_queue, task->completion_callback, completion_task);

	free(userdata);

}

void load_wsi(wsi_t* wsi, const char* filename) {
	if (!is_openslide_loading_done) {
#if DO_DEBUG
		printf("Waiting for OpenSlide to finish loading...\n");
#endif
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

bool32 load_generic_file(app_state_t *app_state, const char *filename) {
	const char* ext = get_file_extension(filename);
	if (strcasecmp(ext, "json") == 0) {
		reload_global_caselist(app_state, filename);
		show_slide_list_window = true;
		caselist_select_first_case(app_state, &app_state->caselist);
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
				memset(image.level_images, 0, sizeof(image.level_images));

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

					sb_push(app_state->loaded_images, image);
			result = true;

		}
	}
	return result;

}


void unload_wsi(wsi_t* wsi) {
	if (wsi->osr) {
		openslide.openslide_close(wsi->osr);
		wsi->osr = NULL;
	}

}
