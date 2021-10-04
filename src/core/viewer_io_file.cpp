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

#include "tif_lzw.h"

// TODO: refactor
void viewer_upload_already_cached_tile_to_gpu(int logical_thread_index, void* userdata) {
	ASSERT(!"viewer_upload_already_cached_tile_to_gpu() is a dummy, it should not be called");
}


void viewer_notify_load_tile_completed(int logical_thread_index, void* userdata) {
	ASSERT(!"viewer_notify_load_tile_completed() is a dummy, it should not be called");
}

// Adapted from ASAP: see core/PathologyEnums.cpp
u32 lut[] = {
		MAKE_BGRA(0, 0, 0, 0),
		MAKE_BGRA(0, 224, 249, 255),
		MAKE_BGRA(0, 249, 50, 255),
		MAKE_BGRA(174, 249, 0, 255),
		MAKE_BGRA(249, 100, 0, 255),
		MAKE_BGRA(249, 0, 125, 255),
		MAKE_BGRA(149, 0, 249, 255),
		MAKE_BGRA(0, 0, 206, 255),
		MAKE_BGRA(0, 185, 206, 255),
		MAKE_BGRA(0, 206, 41, 255),
		MAKE_BGRA(143, 206, 0, 255),
		MAKE_BGRA(206, 82, 0, 255),
		MAKE_BGRA(206, 0, 103, 255),
		MAKE_BGRA(124, 0, 206, 255),
		MAKE_BGRA(0, 0, 162, 255),
		MAKE_BGRA(0, 145, 162, 255),
		MAKE_BGRA(0, 162, 32, 255),
		MAKE_BGRA(114, 162, 0, 255),
		MAKE_BGRA(162, 65, 0, 255),
		MAKE_BGRA(162, 0, 81, 255),
		MAKE_BGRA(97, 0, 162, 255),
		MAKE_BGRA(0, 0, 119, 255),
		MAKE_BGRA(0, 107, 119, 255),
		MAKE_BGRA(0, 119, 23, 255),
		MAKE_BGRA(83, 119, 0, 255),
		MAKE_BGRA(119, 47, 0, 255),
		MAKE_BGRA(119, 0, 59, 255),
		MAKE_BGRA(71, 0, 119, 255),
		MAKE_BGRA(100, 100, 249, 255),
		MAKE_BGRA(100, 234, 249, 255)
};

static inline u32 lookup_color_from_lut(u8 index) {
	if (index >= COUNT(lut)) {
		index = 0;
	}
	u32 color = lut[index];
	return color;
}

void load_tile_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_t* task = (load_tile_task_t*) userdata;
	i32 level = task->level;
	i32 tile_x = task->tile_x;
	i32 tile_y = task->tile_y;
	image_t* image = task->image;
	level_image_t* level_image = image->level_images + level;
	ASSERT(level_image->exists);
	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	ASSERT(level_image->x_tile_side_in_um > 0&& level_image->y_tile_side_in_um > 0);
	float tile_world_pos_x_end = (tile_x + 1) * level_image->x_tile_side_in_um;
	float tile_world_pos_y_end = (tile_y + 1) * level_image->y_tile_side_in_um;
	float tile_x_excess = tile_world_pos_x_end - image->width_in_um;
	float tile_y_excess = tile_world_pos_y_end - image->height_in_um;

	// Note: when the thread started up we allocated a large blob of memory for the thread to use privately
	// TODO: better/more explicit allocator (instead of some setting some hard-coded pointers)
	thread_memory_t* thread_memory = local_thread_memory;
	size_t pixel_memory_size = level_image->tile_width * level_image->tile_height * BYTES_PER_PIXEL;
	u8* temp_memory = (u8*)malloc(pixel_memory_size);//(u8*) thread_memory->aligned_rest_of_thread_memory;
	memset(temp_memory, 0xFF, pixel_memory_size);
	u8* compressed_tile_data = (u8*) thread_memory->aligned_rest_of_thread_memory;// + WSI_BLOCK_SIZE;

	bool failed = false;
	ASSERT(image->type == IMAGE_TYPE_WSI);
	if (image->backend == IMAGE_BACKEND_TIFF) {
		tiff_t* tiff = &image->tiff;
		tiff_ifd_t* level_ifd = tiff->level_images_ifd + level_image->pyramid_image_index;

		u64 tile_offset = level_ifd->tile_offsets[tile_index];
		u64 compressed_tile_size_in_bytes = level_ifd->tile_byte_counts[tile_index];

		u16 compression = level_ifd->compression;
		// Some tiles apparently contain no data (not even an empty/dummy JPEG stream like some other tiles have).
		// We need to check for this situation and chicken out if this is the case.
		if (tile_offset == 0 || compressed_tile_size_in_bytes == 0) {
#if DO_DEBUG
			console_print("thread %d: tile level %d, tile %d (%d, %d) appears to be empty\n", logical_thread_index, level, tile_index, tile_x, tile_y);
#endif
			goto finish_up;
		}
		u8* jpeg_tables = level_ifd->jpeg_tables;
		u64 jpeg_tables_length = level_ifd->jpeg_tables_length;

		if (tiff->is_remote) {
			console_print_verbose("[thread %d] remote tile requested: level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);


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
//		                    console_print("thread %d: successfully decoded level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
						} else {
							console_print_error("[thread %d] failed to decode level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
							failed = true;
						}
					}

				}



			}
			free(read_buffer);
		} else {
//			console_print_verbose("[thread %d] loading tile: level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
#if WINDOWS
			win32_overlapped_read(thread_memory, tiff->file_handle, compressed_tile_data, compressed_tile_size_in_bytes, tile_offset);
//			console_print_verbose("[thread %d] read succeeded\n", logical_thread_index);
#else
			size_t bytes_read = pread(tiff->file_handle, compressed_tile_data, compressed_tile_size_in_bytes, tile_offset);
#endif

			if (compression == TIFF_COMPRESSION_JPEG) {
				if (compressed_tile_data[0] == 0xFF && compressed_tile_data[1] == 0xD9) {
					// JPEG stream is empty
				} else {
					if (decode_tile(jpeg_tables, jpeg_tables_length, compressed_tile_data, compressed_tile_size_in_bytes,
					                temp_memory, (level_ifd->color_space == TIFF_PHOTOMETRIC_YCBCR))) {
//		            console_print_verbose("thread %d: successfully decoded level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
					} else {
						console_print_error("thread %d: failed to decode level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
						failed = true;
					}
				}
			} else if (level_ifd->compression == TIFF_COMPRESSION_LZW) {

				size_t decompressed_size = level_image->tile_width * level_image->tile_height * level_ifd->samples_per_pixel;
				u8* decompressed = (u8*)malloc(decompressed_size);

				PseudoTIFF tif = {};
				tif.tif_rawdata = compressed_tile_data;
				tif.tif_rawcp = compressed_tile_data;
				tif.tif_rawdatasize = compressed_tile_size_in_bytes;
				tif.tif_rawcc = compressed_tile_size_in_bytes;
				LZWSetupDecode(&tif);
				LZWPreDecode(&tif, 0);
				// Check for old bit-reversed codes.
				int decode_success = 0;
				if (tif.tif_rawcc >= 2 && tif.tif_rawdata[0] == 0 && (tif.tif_rawdata[1] & 0x1)) {
					decode_success = LZWDecodeCompat(&tif, decompressed, decompressed_size, 0);
				} else {
					decode_success = LZWDecode(&tif, decompressed, decompressed_size, 0);
				}
				if (!decode_success) {
					console_print_error("LZW decompression failed\n");
					failed = true;
				}

				// Convert RGB to BGRA
				if (level_ifd->samples_per_pixel == 4) {
					// TODO: convert RGBA to BGRA
					console_print_error("LZW decompression: RGBA to BGRA conversion not implemented, assuming already in BGRA\n");
					ASSERT(decompressed_size == pixel_memory_size);
					free(temp_memory);
					temp_memory = decompressed;
				} else if (level_ifd->samples_per_pixel == 3) {
					// TODO: vectorize: https://stackoverflow.com/questions/7194452/fast-vectorized-conversion-from-rgb-to-bgra
					u64 pixel_count = level_image->tile_width * level_image->tile_height;
					i32 source_pos = 0;
					u32* pixels = (u32*) temp_memory;
					for (u64 i = 0; i < pixel_count; ++i) {
						u8 r = decompressed[source_pos];
//						u8 g = decompressed[source_pos+1];
//						u8 b = decompressed[source_pos+2];
//						pixels[i]=(r<<16) | (g<<8) | b | (0xff << 24);
						u32 color = lookup_color_from_lut(r);
						color = BGRA_SET_ALPHA(color, 128); // TODO: make color lookup tables configurable
						pixels[i] = color;
						source_pos+=3;
					}
					free(decompressed);
				}


			} else {
				console_print_error("\"thread %d: failed to decode level %d, tile %d (%d, %d): unsupported TIFF compression method (compression=%d)\n", logical_thread_index, level, tile_index, tile_x, tile_y, compression);
				failed = true;
			}



		}


		// Trim the tile (replace with transparent color) if it extends beyond the image size
		// TODO: anti-alias edge?
		i32 new_tile_height = level_image->tile_height;
		i32 pitch = level_image->tile_width * BYTES_PER_PIXEL;
		if (tile_y_excess > 0) {
			i32 excess_rows = (int)((tile_y_excess / level_image->y_tile_side_in_um) * level_image->tile_height);
			ASSERT(excess_rows >= 0);
			new_tile_height = level_image->tile_height - excess_rows;
			memset(temp_memory + (new_tile_height * pitch), 0, excess_rows * pitch);
		}
		if (tile_x_excess > 0) {
			i32 excess_pixels = (int)((tile_x_excess / level_image->x_tile_side_in_um) * level_image->tile_width);
			ASSERT(excess_pixels >= 0);
			i32 new_tile_width = level_image->tile_width - excess_pixels;
			for (i32 row = 0; row < new_tile_height; ++row) {
				u8* write_pos = temp_memory + (row * pitch) + (new_tile_width * BYTES_PER_PIXEL);
				memset(write_pos, 0, excess_pixels * BYTES_PER_PIXEL);
			}
		}

	} else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
		wsi_t* wsi = &image->wsi.wsi;
		i32 wsi_file_level = level_image->pyramid_image_index;
		i64 x = (tile_x * level_image->tile_width) << level;
		i64 y = (tile_y * level_image->tile_height) << level;
		openslide.openslide_read_region(wsi->osr, (u32*)temp_memory, x, y, wsi_file_level, level_image->tile_width, level_image->tile_height);
	} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
//		console_print_error("thread %d: tile level %d, tile %d (%d, %d): TYRING\n", logical_thread_index, level, tile_index, tile_x, tile_y);
		ASSERT(!"invalid code path");

	} else if (image->backend == IMAGE_BACKEND_STBI) {
		ASSERT(!"invalid code path");
	} else {
		console_print_error("thread %d: tile level %d, tile %d (%d, %d): unsupported image type\n", logical_thread_index, level, tile_index, tile_x, tile_y);
		failed = true;
	}


	finish_up:;

	if (failed && temp_memory != NULL) {
		free(temp_memory);
		temp_memory = NULL;
	}

//	console_print_verbose("[thread %d] completing...\n", logical_thread_index);


#if USE_MULTIPLE_OPENGL_CONTEXTS
#if 1
	upload_tile_on_worker_thread(image, temp_memory, level, tile_index, level_image->tile_width, level_image->tile_height);
#else
	glEnable(GL_TEXTURE_2D);
	u32 texture = load_texture(temp_memory, level_image->tile_width, level_image->tile_height, GL_BGRA);
	glFinish(); // Block thread execution until all OpenGL operations have finished.
	write_barrier;
	task->tile->texture = texture;
#endif


#else//USE_MULTIPLE_OPENGL_CONTEXTS

	viewer_notify_tile_completed_task_t completion_task = {};
	completion_task.resource_id = task->resource_id;
	completion_task.pixel_memory = temp_memory;
	completion_task.tile_width = level_image->tile_width;
	completion_task.tile_height = level_image->tile_height;
	completion_task.scale = level;
	completion_task.tile_index = tile_index;
	completion_task.want_gpu_residency = true;

	//	console_print("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);
	ASSERT(task->completion_callback);
	add_work_queue_entry(&global_completion_queue, task->completion_callback, &completion_task, sizeof(completion_task));

#endif
//	console_print_verbose("[thread %d] tile load done\n", logical_thread_index);

}

void load_wsi(wsi_t* wsi, const char* filename) {
	if (!is_openslide_loading_done) {
#if DO_DEBUG
		console_print("Waiting for OpenSlide to finish loading...\n");
#endif
		while (is_queue_work_in_progress(&global_work_queue)) {
			do_worker_work(&global_work_queue, 0);
		}
	}

	// TODO: check if necessary anymore?
	unload_wsi(wsi);

	wsi->osr = openslide.openslide_open(filename);
	if (wsi->osr) {
		const char* error_string = openslide.openslide_get_error(wsi->osr);
		if (error_string != NULL) {
			console_print_error("OpenSlide error: %s\n", error_string);
			unload_wsi(wsi);
			return;
		}

		console_print_verbose("OpenSlide: opened '%s'\n", filename);

		wsi->level_count = openslide.openslide_get_level_count(wsi->osr);
		if (wsi->level_count == -1) {
			error_string = openslide.openslide_get_error(wsi->osr);
			console_print_error("OpenSlide error: %s\n", error_string);
			unload_wsi(wsi);
			return;
		}
		console_print_verbose("OpenSlide: WSI has %d levels\n", wsi->level_count);
		if (wsi->level_count > WSI_MAX_LEVELS) {
			panic();
		}

		openslide.openslide_get_level0_dimensions(wsi->osr, &wsi->width, &wsi->height);
		ASSERT(wsi->width > 0);
		ASSERT(wsi->height > 0);

		wsi->tile_width = WSI_TILE_DIM;
		wsi->tile_height = WSI_TILE_DIM;


		const char* const* wsi_properties = openslide.openslide_get_property_names(wsi->osr);
		if (wsi_properties) {
			i32 property_index = 0;
			const char* property = wsi_properties[0];
			for (; property != NULL; property = wsi_properties[++property_index]) {
				const char* property_value = openslide.openslide_get_property_value(wsi->osr, property);
				console_print_verbose("%s = %s\n", property, property_value);

			}
		}

		wsi->mpp_x = 1.0f; // microns per pixel (default)
		wsi->mpp_y = 1.0f; // microns per pixel (default)
		wsi->is_mpp_known = false;
		const char* mpp_x_string = openslide.openslide_get_property_value(wsi->osr, "openslide.mpp-x");
		const char* mpp_y_string = openslide.openslide_get_property_value(wsi->osr, "openslide.mpp-y");
		if (mpp_x_string) {
			float mpp = atof(mpp_x_string);
			if (mpp > 0.0f) {
				wsi->mpp_x = mpp;
				wsi->is_mpp_known = true;
			}
		}
		if (mpp_y_string) {
			float mpp = atof(mpp_y_string);
			if (mpp > 0.0f) {
				wsi->mpp_y = mpp;
				wsi->is_mpp_known = true;
			}
		}

		for (i32 i = 0; i < wsi->level_count; ++i) {
			wsi_level_t* level = wsi->levels + i;

			openslide.openslide_get_level_dimensions(wsi->osr, i, &level->width, &level->height);
			ASSERT(level->width > 0);
			ASSERT(level->height > 0);
			i64 partial_block_x = level->width % WSI_TILE_DIM;
			i64 partial_block_y = level->height % WSI_TILE_DIM;
			level->width_in_tiles = (i32)(level->width / WSI_TILE_DIM) + (partial_block_x != 0);
			level->height_in_tiles = (i32)(level->height / WSI_TILE_DIM) + (partial_block_y != 0);
			level->tile_width = WSI_TILE_DIM;
			level->tile_height = WSI_TILE_DIM;

			float raw_downsample_factor = openslide.openslide_get_level_downsample(wsi->osr, i);
			float raw_downsample_level = log2f(raw_downsample_factor);
			i32 downsample_level = (i32) roundf(raw_downsample_level);

			level->downsample_level = downsample_level;
			level->downsample_factor = exp2f(level->downsample_level);
			wsi->max_downsample_level = MAX(level->downsample_level, wsi->max_downsample_level);
			level->um_per_pixel_x = level->downsample_factor * wsi->mpp_x;
			level->um_per_pixel_y = level->downsample_factor * wsi->mpp_y;
			level->x_tile_side_in_um = level->um_per_pixel_x * (float)WSI_TILE_DIM;
			level->y_tile_side_in_um = level->um_per_pixel_y * (float)WSI_TILE_DIM;
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
				console_print_verbose("%s : w=%lld h=%lld\n", name, w, h);

			}
		}


	}

}

bool32 load_generic_file(app_state_t* app_state, const char* filename, u32 filetype_hint) {
	const char* ext = get_file_extension(filename);
	if (strcasecmp(ext, "json") == 0) {
		// TODO: disambiguate between COCO annotations and case lists
		reload_global_caselist(app_state, filename);
		show_slide_list_window = true;
		caselist_select_first_case(app_state, &app_state->caselist);
		return true;
	} else if (strcasecmp(ext, "xml") == 0) {
		// TODO: how to get the correct scale factor for the annotations?
		// Maybe a placeholder value, which gets updated based on the scale of the scene image?
		annotation_set_t* annotation_set = &app_state->scene.annotation_set;
		unload_and_reinit_annotations(annotation_set);
		annotation_set->mpp = (v2f){0.25f, 0.25f};
		return load_asap_xml_annotations(app_state, filename);
	} else {
		// assume it is an image file?
		reset_global_caselist(app_state);
		bool is_base_image = filetype_hint != FILETYPE_HINT_OVERLAY;
		if (is_base_image) {
			unload_all_images(app_state);
			// Unload any old annotations if necessary
			unload_and_reinit_annotations(&app_state->scene.annotation_set);
		}
		load_next_image_as_overlay = false; // reset after use (don't keep stacking on more overlays unintendedly)
		image_t image = load_image_from_file(app_state, filename, filetype_hint);
		if (image.is_valid) {
			add_image(app_state, image, is_base_image);

			annotation_set_t* annotation_set = &app_state->scene.annotation_set;
			unload_and_reinit_annotations(annotation_set);
			annotation_set->mpp = (v2f){image.mpp_x, image.mpp_y};

			// Check if there is an associated ASAP XML or COCO JSON annotations file
			size_t filename_len = strlen(filename);
			size_t temp_size = filename_len + 6; // add 5 so that we can always append ".xml\0" or ".json\0"
			char* temp_filename = (char*) alloca(temp_size);
			strncpy(temp_filename, filename, temp_size);
			bool were_annotations_loaded = false;

			// Load JSON first
			replace_file_extension(temp_filename, temp_size, "json");
			annotation_set->export_as_coco = true;
			annotation_set->coco_filename = strdup(temp_filename); // TODO: do this somewhere else
			if (file_exists(temp_filename)) {
				console_print("Found JSON annotations: %s\n", temp_filename);
				coco_t coco = {};
				load_coco_from_file(&coco, temp_filename);
				coco_transfer_annotations_to_annotation_set(&coco, annotation_set);
				coco_destroy(&coco);
				were_annotations_loaded = true;

				// Enable export as XML (make sure XML annotations do not get out of date!)
				annotation_set->export_as_asap_xml = true;
				replace_file_extension(temp_filename, temp_size, "xml");
				annotation_set->asap_xml_filename = strdup(temp_filename); // TODO: fix leak

			} else {
				coco_init_main_image(&annotation_set->coco, &image);
			}

			// TODO: use most recently updated annotations?
			replace_file_extension(temp_filename, temp_size, "xml");
			if (file_exists(temp_filename)) {
				console_print("Found XML annotations: %s\n", temp_filename);
				if (!were_annotations_loaded) {
					load_asap_xml_annotations(app_state, temp_filename);
				}
			}


			// TODO: only save/convert COCO, not the XML as well!
			if (annotation_set->export_as_asap_xml) {
//				annotation_set->modified = true; // to force export in COCO as well
			}

			console_print("Loaded '%s'\n", filename);
			if (image.backend == IMAGE_BACKEND_ISYNTAX) {
				console_print("   iSyntax: loading took %g seconds\n", image.isyntax.loading_time);
			}
			return true;

		} else {
			console_print_error("Could not load '%s'\n", filename);
			gui_add_modal_popup("Error##load_generic_file", "Could not load '%s'.\n", filename);
			return false;
		}

	}
}

image_t load_image_from_file(app_state_t* app_state, const char* filename, u32 filetype_hint) {

	image_t image = (image_t){};
	image.resource_id = global_next_resource_id++;

	bool is_overlay = (filetype_hint == FILETYPE_HINT_OVERLAY);

	size_t filename_len = strlen(filename);
	const char* name = one_past_last_slash(filename, filename_len);
	strncpy(image.name, name, sizeof(image.name)-1);
	const char* ext = get_file_extension(filename);

	if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
		// Load using stb_image

		image.type = IMAGE_TYPE_WSI;
		image.backend = IMAGE_BACKEND_STBI;
		image.simple.channels = 4; // desired: RGBA
		image.simple.pixels = stbi_load(filename, &image.simple.width, &image.simple.height, &image.simple.channels_in_file, 4);
		if (image.simple.pixels) {

			image.is_freshly_loaded = true;
			image.is_valid = true;
			init_image_from_stbi(app_state, &image, &image.simple, is_overlay);
			return image;

			//stbi_image_free(image->stbi.pixels);
		}

	} else if (app_state->use_builtin_tiff_backend && (strcasecmp(ext, "tiff") == 0 || strcasecmp(ext, "tif") == 0 || strcasecmp(ext, "ptif") == 0)) {
		// Try to open as TIFF, using the built-in backend
		tiff_t tiff = {0};
		if (open_tiff_file(&tiff, filename)) {
			init_image_from_tiff(app_state, &image, tiff, is_overlay);
			return image;
		} else {
			tiff_destroy(&tiff);
			image.is_valid = false;
			return image;
		}
	} else if (strcasecmp(ext, "isyntax") == 0) {
		// Try to open as iSyntax
		isyntax_t isyntax = {0};
		if (isyntax_open(&isyntax, filename)) {
			init_image_from_isyntax(app_state, &image, &isyntax, is_overlay);
			return image;
		}
	} else {
		// Try to load the file using OpenSlide
		if (!is_openslide_available) {
			if (!is_openslide_loading_done) {
#if DO_DEBUG
				console_print("Waiting for OpenSlide to finish loading...\n");
#endif
				while (is_queue_work_in_progress(&global_work_queue)) {
					do_worker_work(&global_work_queue, 0);
				}
			}
			if (!is_openslide_available) {
				console_print("Can't try to load %s using OpenSlide, because OpenSlide is not available\n", filename);
				image.is_valid = false;
				return image;
			}
		}

		// TODO: fix code duplication from init_image_from_tiff()
		image.type = IMAGE_TYPE_WSI;
		image.backend = IMAGE_BACKEND_OPENSLIDE;
		wsi_t* wsi = &image.wsi.wsi;
		load_wsi(wsi, filename);
		if (wsi->osr) {
			image.is_freshly_loaded = true;
			image.mpp_x = wsi->mpp_x;
			image.mpp_y = wsi->mpp_y;
			image.is_mpp_known = wsi->is_mpp_known;
			image.tile_width = wsi->tile_width;
			image.tile_height = wsi->tile_height;
			image.width_in_pixels = wsi->width;
			image.width_in_um = wsi->width * wsi->mpp_x;
			image.height_in_pixels = wsi->height;
			image.height_in_um = wsi->height * wsi->mpp_y;
			ASSERT(wsi->levels[0].x_tile_side_in_um > 0);
			if (wsi->level_count > 0 && wsi->levels[0].x_tile_side_in_um > 0) {
				ASSERT(wsi->max_downsample_level >= 0);

				memset(image.level_images, 0, sizeof(image.level_images));
				image.level_count = wsi->max_downsample_level + 1;

				// TODO: check against downsample level, see init_image_from_tiff()
				if (wsi->level_count > image.level_count) {
					panic();
				}
				if (image.level_count > WSI_MAX_LEVELS) {
					panic();
				}

				i32 wsi_level_index = 0;
				i32 next_wsi_level_index_to_check_for_match = 0;
				wsi_level_t* wsi_file_level = wsi->levels + wsi_level_index;
				for (i32 downsample_level = 0; downsample_level < image.level_count; ++downsample_level) {
					level_image_t* downsample_level_image = image.level_images + downsample_level;
					i32 wanted_downsample_level = downsample_level;
					bool found_wsi_level_for_downsample_level = false;
					for (wsi_level_index = next_wsi_level_index_to_check_for_match; wsi_level_index < wsi->level_count; ++wsi_level_index) {
						wsi_file_level = wsi->levels + wsi_level_index;
						if (wsi_file_level->downsample_level == wanted_downsample_level) {
							// match!
							found_wsi_level_for_downsample_level = true;
							next_wsi_level_index_to_check_for_match = wsi_level_index + 1; // next iteration, don't reuse the same WSI level!
							break;
						}
					}

					if (found_wsi_level_for_downsample_level) {
						// The current downsampling level is backed by a corresponding IFD level image in the TIFF.
						downsample_level_image->exists = true;
						downsample_level_image->pyramid_image_index = wsi_level_index;
						downsample_level_image->downsample_factor = wsi_file_level->downsample_factor;
						downsample_level_image->tile_count = wsi_file_level->tile_count;
						downsample_level_image->width_in_tiles = wsi_file_level->width_in_tiles;
						ASSERT(downsample_level_image->width_in_tiles > 0);
						downsample_level_image->height_in_tiles = wsi_file_level->height_in_tiles;
						downsample_level_image->tile_width = wsi_file_level->tile_width;
						downsample_level_image->tile_height = wsi_file_level->tile_height;
#if DO_DEBUG
						if (downsample_level_image->tile_width != image.tile_width) {
							console_print("Warning: level image %d (WSI level #%d) tile width (%d) does not match base level (%d)\n", downsample_level, wsi_level_index, downsample_level_image->tile_width, image.tile_width);
						}
						if (downsample_level_image->tile_height != image.tile_height) {
							console_print("Warning: level image %d (WSI level #%d) tile width (%d) does not match base level (%d)\n", downsample_level, wsi_level_index, downsample_level_image->tile_width, image.tile_width);
						}
#endif
						downsample_level_image->um_per_pixel_x = wsi_file_level->um_per_pixel_x;
						downsample_level_image->um_per_pixel_y = wsi_file_level->um_per_pixel_y;
						downsample_level_image->x_tile_side_in_um = wsi_file_level->x_tile_side_in_um;
						downsample_level_image->y_tile_side_in_um = wsi_file_level->y_tile_side_in_um;
						ASSERT(downsample_level_image->x_tile_side_in_um > 0);
						ASSERT(downsample_level_image->y_tile_side_in_um > 0);
						downsample_level_image->tiles = (tile_t*) calloc(1, wsi_file_level->tile_count * sizeof(tile_t));
						// Note: OpenSlide doesn't allow us to quickly check if tiles are empty or not.
						for (i32 tile_index = 0; tile_index < downsample_level_image->tile_count; ++tile_index) {
							tile_t* tile = downsample_level_image->tiles + tile_index;
							// Facilitate some introspection by storing self-referential information
							// in the tile_t struct. This is needed for some specific cases where we
							// pass around pointers to tile_t structs without caring exactly where they
							// came from.
							// (Specific example: we use this when exporting a selected region as BigTIFF)
							tile->tile_index = tile_index;
							tile->tile_x = tile_index % downsample_level_image->width_in_tiles;
							tile->tile_y = tile_index / downsample_level_image->width_in_tiles;
						}
					} else {
						// The current downsampling level has no corresponding IFD level image :(
						// So we need only some placeholder information.
						downsample_level_image->exists = false;
						downsample_level_image->downsample_factor = exp2f((float)wanted_downsample_level);
						// Just in case anyone tries to divide by zero:
						downsample_level_image->tile_width = image.tile_width;
						downsample_level_image->tile_height = image.tile_height;
						downsample_level_image->um_per_pixel_x = image.mpp_x * downsample_level_image->downsample_factor;
						downsample_level_image->um_per_pixel_y = image.mpp_y * downsample_level_image->downsample_factor;
						downsample_level_image->x_tile_side_in_um = downsample_level_image->um_per_pixel_x * (float)wsi->levels[0].tile_width;
						downsample_level_image->y_tile_side_in_um = downsample_level_image->um_per_pixel_y * (float)wsi->levels[0].tile_height;
					}

				}

			}
			ASSERT(image.level_count > 0);
			image.is_valid = true;

		}
	}
	return image;

}


void unload_wsi(wsi_t* wsi) {
	if (wsi->osr) {
		openslide.openslide_close(wsi->osr);
		wsi->osr = NULL;
	}

}

void tile_release_cache(tile_t* tile) {
	ASSERT(tile);
	if (tile->pixels) free(tile->pixels);
	tile->pixels = NULL;
	tile->is_cached = false;
	tile->need_keep_in_cache = false;
}
