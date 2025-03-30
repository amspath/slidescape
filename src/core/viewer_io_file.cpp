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



// TODO: refactor
void viewer_upload_already_cached_tile_to_gpu(int logical_thread_index, void* userdata) {
	DUMMY_STATEMENT;
}


void viewer_notify_load_tile_completed(int logical_thread_index, void* userdata) {
	viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*)userdata;
	work_queue_submit_task(&global_completion_queue, viewer_notify_load_tile_completed, task, sizeof(*task));
}


void load_tile_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_t* task = (load_tile_task_t*) userdata;
	image_t* image = task->image;

	if (image->is_deleted) {
		// Early out to save time if the image was already closed/waiting for destruction
		atomic_subtract(&image->refcount, task->refcount_to_decrement);
		return;
	}

	i32 level = task->level;
	i32 tile_x = task->tile_x;
	i32 tile_y = task->tile_y;
	level_image_t* level_image = image->level_images + level;
	ASSERT(level_image->exists);
	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	ASSERT(level_image->x_tile_side_in_um > 0 && level_image->y_tile_side_in_um > 0);
	float tile_world_pos_x_end = (tile_x + 1) * level_image->x_tile_side_in_um;
	float tile_world_pos_y_end = (tile_y + 1) * level_image->y_tile_side_in_um;
	float tile_x_excess = tile_world_pos_x_end - image->width_in_um;
	float tile_y_excess = tile_world_pos_y_end - image->height_in_um;

	// Note: when the thread started up we allocated a large blob of memory for the thread to use privately
	// TODO: better/more explicit allocator (instead of some setting some hard-coded pointers)
	size_t pixel_memory_size = level_image->tile_width * level_image->tile_height * BYTES_PER_PIXEL;
	u8* temp_memory = (u8*)malloc(pixel_memory_size);

	u32 image_background_color = image->is_background_black ? 0 /*black*/ : 0xFFFFFFFF; // white
    u8 image_background_byte = image->is_background_black ? 0 /*black*/ : 0xFF; // white
	memset(temp_memory, image_background_byte, pixel_memory_size);

	bool failed = false;
	bool is_empty = false; // we might 'discover' that the tile is empty for OpenSlide backend (-> read_region() would return all zeroes)
	ASSERT(image->type == IMAGE_TYPE_WSI);
	if (image->backend == IMAGE_BACKEND_TIFF) {
		tiff_t* tiff = &image->tiff;
		tiff_ifd_t* level_ifd = tiff->level_images_ifd + level_image->pyramid_image_index;
		u8* pixels = tiff_decode_tile(logical_thread_index, tiff, level_ifd, tile_index, level, tile_x, tile_y);
		if (pixels) {
			free(temp_memory);
			temp_memory = pixels;
		} else {
			failed = true;
		}

		// Trim the tile (replace with transparent color) if it extends beyond the image size
		// TODO: anti-alias edge?
		// TODO: do this for other backends as well?
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
		wsi_t* wsi = &image->openslide_wsi;
		i32 wsi_file_level = level_image->pyramid_image_index;
		i64 x = (tile_x * level_image->tile_width) << level;
		i64 y = (tile_y * level_image->tile_height) << level;
		openslide.read_region(wsi->osr, (u32*)temp_memory, x, y, wsi_file_level, level_image->tile_width, level_image->tile_height);

		// Check for (partially) empty tiles
		u32 pixel_count = level_image->tile_width * level_image->tile_height;
		u32* pixels = (u32*)temp_memory;
		u32 nonempty_pixel_count = 0;
		for (i32 i = 0; i < pixel_count; ++i) {
			// Fill in any empty pixels with the background color.
			u32 p = pixels[i];
			nonempty_pixel_count += (p > 0);
			pixels[i] = p ? p : image_background_color;
		}
		if (nonempty_pixel_count == 0) {
			// Tile is entirely empty.
			console_print_verbose("thread %d: tile level %d, tile %d (%d, %d): openslide.read_region() returned zeroes (empty tile)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
			failed = true;
			is_empty = true;
		}
	} else if (image->backend == IMAGE_BACKEND_DICOM) {
		u8* pixels = dicom_wsi_decode_tile_to_bgra(&image->dicom, level, tile_index);
		if (pixels) {
			free(temp_memory);
			temp_memory = pixels;
		} else {
			failed = true;
		}
	} else if (image->backend == IMAGE_BACKEND_MRXS) {
		u8* pixels = mrxs_decode_tile_to_bgra(&image->mrxs, level, tile_index);
		if (pixels) {
			free(temp_memory);
			temp_memory = pixels;
		} else {
			failed = true;
		}
	} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
//		console_print_error("thread %d: tile level %d, tile %d (%d, %d): TYRING\n", logical_thread_index, level, tile_index, tile_x, tile_y);
		ASSERT(!"invalid code path");

	} else if (image->backend == IMAGE_BACKEND_STBI) {
		ASSERT(!"invalid code path");
	} else {
		console_print_error("thread %d: tile level %d, tile %d (%d, %d): unsupported image type\n", logical_thread_index, level, tile_index, tile_x, tile_y);
		failed = true;
	}


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
	completion_task.failed = true;
	completion_task.is_empty = is_empty;

	//	console_print("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);
	if (task->completion_callback) {
		task->completion_callback(logical_thread_index, &completion_task);
	}
	if (task->completion_queue) {
		work_queue_submit_task(task->completion_queue, dummy_work_queue_callback, &completion_task,
		                       sizeof(completion_task));
	}

#endif

    // NOTE: we guarantee existence of image_t until the jobs submitted from the main thread are done.
    // However, we will NOT wait for the completion queues to also be finished (usually the responsibility of the main thread).
    // This means that when we receive the completion tasks on the main thread, we have to check if the image is still valid.
    atomic_subtract(&image->refcount, task->refcount_to_decrement);

//	console_print_verbose("[thread %d] tile load done\n", logical_thread_index);

}

void load_openslide_wsi(wsi_t* wsi, const char* filename) {
	if (!is_openslide_loading_done) {
#if DO_DEBUG
		console_print("Waiting for OpenSlide to finish loading...\n");
#endif
		while (work_queue_is_work_in_progress(&global_work_queue)) {
            work_queue_do_work(&global_work_queue, 0);
		}
	}

	// TODO: check if necessary anymore?
    unload_openslide_wsi(wsi);

	wsi->osr = openslide.open(filename);
	if (wsi->osr) {
		const char* error_string = openslide.get_error(wsi->osr);
		if (error_string != NULL) {
			console_print_error("OpenSlide error: %s\n", error_string);
            unload_openslide_wsi(wsi);
			return;
		}

		console_print_verbose("OpenSlide: opened '%s'\n", filename);

		wsi->level_count = openslide.get_level_count(wsi->osr);
		if (wsi->level_count == -1) {
			error_string = openslide.get_error(wsi->osr);
			console_print_error("OpenSlide error: %s\n", error_string);
            unload_openslide_wsi(wsi);
			return;
		}
		console_print_verbose("OpenSlide: WSI has %d levels\n", wsi->level_count);
		if (wsi->level_count > WSI_MAX_LEVELS) {
			fatal_error();
		}

		openslide.get_level0_dimensions(wsi->osr, &wsi->width, &wsi->height);
		ASSERT(wsi->width > 0);
		ASSERT(wsi->height > 0);

		wsi->tile_width = WSI_TILE_DIM;
		wsi->tile_height = WSI_TILE_DIM;


		const char* const* wsi_properties = openslide.get_property_names(wsi->osr);
		if (wsi_properties) {
			i32 property_index = 0;
			const char* property = wsi_properties[0];
			for (; property != NULL; property = wsi_properties[++property_index]) {
				const char* property_value = openslide.get_property_value(wsi->osr, property);
				console_print_verbose("%s = %s\n", property, property_value);

			}
		}

		wsi->mpp_x = 1.0f; // microns per pixel (default)
		wsi->mpp_y = 1.0f; // microns per pixel (default)
		wsi->is_mpp_known = false;
		const char* mpp_x_string = openslide.get_property_value(wsi->osr, "openslide.mpp-x");
		const char* mpp_y_string = openslide.get_property_value(wsi->osr, "openslide.mpp-y");
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

			openslide.get_level_dimensions(wsi->osr, i, &level->width, &level->height);
			ASSERT(level->width > 0);
			ASSERT(level->height > 0);
			i64 partial_block_x = level->width % WSI_TILE_DIM;
			i64 partial_block_y = level->height % WSI_TILE_DIM;
			level->width_in_tiles = (i32)(level->width / WSI_TILE_DIM) + (partial_block_x != 0);
			level->height_in_tiles = (i32)(level->height / WSI_TILE_DIM) + (partial_block_y != 0);
			level->tile_width = WSI_TILE_DIM;
			level->tile_height = WSI_TILE_DIM;

			float raw_downsample_factor = openslide.get_level_downsample(wsi->osr, i);
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

		const char* barcode = openslide.get_property_value(wsi->osr, "philips.PIM_DP_UFS_BARCODE");
		if (barcode) {
			wsi->barcode = barcode;
		}

		const char* const* wsi_associated_image_names = openslide.get_associated_image_names(wsi->osr);
		if (wsi_associated_image_names) {
			i32 name_index = 0;
			const char* name = wsi_associated_image_names[0];
			for (; name != NULL; name = wsi_associated_image_names[++name_index]) {
				i64 w = 0;
				i64 h = 0;
				openslide.get_associated_image_dimensions(wsi->osr, name, &w, &h);
				console_print_verbose("%s : w=%lld h=%lld\n", name, w, h);

			}
		}
	}
}

static viewer_file_type_enum viewer_determine_file_type(file_info_t* file) {
	if (file->is_regular_file) {
        if (strcasecmp(file->filename_in_directory, "Slidedat.ini") == 0) {
            return VIEWER_FILE_TYPE_MRXS;
        } else if (strlen(file->ext) == 0) { // no extension
            if (is_file_a_dicom_file(file->header, MIN(file->filesize, sizeof(file->header)))) {
                return VIEWER_FILE_TYPE_DICOM;
            } else {
                return VIEWER_FILE_TYPE_UNKNOWN;
            }
        }
        if (strcasecmp(file->ext, "tiff") == 0 ||
            strcasecmp(file->ext, "tif") == 0 ||
            strcasecmp(file->ext, "ptif") == 0)
        {
			return VIEWER_FILE_TYPE_TIFF;
		} else if (strcasecmp(file->ext, "ndpi") == 0) {
            return VIEWER_FILE_TYPE_NDPI;
        } else if (strcasecmp(file->ext, "png") == 0 ||
		           strcasecmp(file->ext, "jpg") == 0 ||
		           strcasecmp(file->ext, "jpeg") == 0 ||
		           strcasecmp(file->ext, "bmp") == 0 ||
		           strcasecmp(file->ext, "ppm") == 0)
		{
			return VIEWER_FILE_TYPE_SIMPLE_IMAGE; // i.e. stb_image compatible
		} else if (strcasecmp(file->ext, "xml") == 0) {
			return VIEWER_FILE_TYPE_XML;
		} else if (strcasecmp(file->ext, "json") == 0) {
			return VIEWER_FILE_TYPE_JSON;
		} else if (strcasecmp(file->ext, "dcm") == 0) {
			return VIEWER_FILE_TYPE_DICOM;
		} else if (strcasecmp(file->ext, "isyntax") == 0 || strcasecmp(file->ext, "i2syntax") == 0) {
			return VIEWER_FILE_TYPE_ISYNTAX;
        } else if (strcasecmp(file->ext, "mrxs") == 0) {
            return VIEWER_FILE_TYPE_MRXS;
		} else {
			if (is_file_a_dicom_file(file->header, MIN(file->filesize, sizeof(file->header)))) {
				return VIEWER_FILE_TYPE_DICOM;
			} else {
				// TODO: this is a total guess, maybe flesh out more?
				return VIEWER_FILE_TYPE_OPENSLIDE_COMPATIBLE;
			}
		}
	}
	return VIEWER_FILE_TYPE_UNKNOWN;
}

file_info_t viewer_get_file_info(const char* filename) {
	file_info_t file = {};
	size_t filename_len = strlen(filename);
	if (filename_len >= sizeof(file.full_filename)) {
		console_print_error("viewer_get_file_info(): filename too long (length=%u): '%s'\n", filename_len, filename);
		return file;
	}
	memcpy(file.full_filename, filename, filename_len);
	const char* ext = get_file_extension(filename);
	strncpy(file.ext, ext, sizeof(file.ext) - 1);
    file.filename_in_directory = one_past_last_slash(file.full_filename, filename_len);
	i64 prefix_len = file.filename_in_directory - file.full_filename;
    ASSERT(prefix_len >= 0 && prefix_len < filename_len);
	if (prefix_len > 0 && prefix_len < filename_len) {
		strncpy(file.filename_prefix, filename, prefix_len);
	}

	struct stat st;
	if (platform_stat(filename, &st) == 0) {
		file.is_valid = true;
		file.is_directory = S_ISDIR(st.st_mode);
		file.is_regular_file = S_ISREG(st.st_mode);
		if (file.is_regular_file) {
			file.filesize = st.st_size;
			file_stream_t fp = file_stream_open_for_reading(filename);
			if (fp) {
				size_t bytes_to_read = MIN(file.filesize, sizeof(file.header));
				size_t bytes_read = file_stream_read(file.header, bytes_to_read, fp);
				if (bytes_read == bytes_to_read) {
					file.type = viewer_determine_file_type(&file);
					switch(file.type) {
						default: break;
                        case VIEWER_FILE_TYPE_TIFF:
                        case VIEWER_FILE_TYPE_NDPI:
                        case VIEWER_FILE_TYPE_MRXS:
                        case VIEWER_FILE_TYPE_OPENSLIDE_COMPATIBLE:
                            file.is_openslide_compatible = true;
                            // fallthrough
                        case VIEWER_FILE_TYPE_SIMPLE_IMAGE:
                        case VIEWER_FILE_TYPE_DICOM:
                        case VIEWER_FILE_TYPE_ISYNTAX:
                            file.is_image = true;
							break;
					}
				} else {
					console_print_error("viewer_get_file_info(): read header failed (tried to read %d bytes, but read %d)\n", bytes_to_read, bytes_read);
					file.is_valid = false;
				}
				file_stream_close(fp);
			} else {
				file.is_valid = false;
			}
		}
	}
	return file;
}

void viewer_directory_info_destroy(directory_info_t* info) {
	arrfree(info->dicom_files);
	arrfree(info->nondicom_files);
    if (info->directories) {
        for (i32 i = 0; i < arrlen(info->directories); ++i) {
            viewer_directory_info_destroy(info->directories + i);
        }
    }
	arrfree(info->directories);
	info->is_valid = false;
}

directory_info_t viewer_get_directory_info(const char* path) {
	directory_info_t directory = {};
	directory_listing_t* listing = create_directory_listing_and_find_first_file(path, NULL);
	if (listing) {
		directory.is_valid = true;
		do {
			char* current_filename = get_current_filename_from_directory_listing(listing);
			char full_filename[512];
			snprintf(full_filename, sizeof(full_filename), "%s" PATH_SEP "%s", path, current_filename);
			file_info_t file = viewer_get_file_info(full_filename);
			if (file.is_valid) {
				if (file.is_directory) {
					directory_info_t subdir_info = viewer_get_directory_info(full_filename);
                    arrput(directory.directories, subdir_info);
				} else if (file.is_regular_file) {
					if (file.type == VIEWER_FILE_TYPE_DICOM) {
						directory.contains_dicom_files = true;
						arrput(directory.dicom_files, file);
					} else {
                        arrput(directory.nondicom_files, file);
						if (file.is_image) {
							directory.contains_nondicom_images = true;
						}
                        if (file.type == VIEWER_FILE_TYPE_MRXS) {
                            directory.contains_mrxs_files = true;
                        }
					}
				}
			}

//			console_print("File: %s\n", full_filename);


//				FILE* fp = fopen(full_filename, "rb");
		} while (find_next_file(listing));
		close_directory_listing(listing);
	}

	return directory;
}

bool viewer_load_new_image(app_state_t* app_state, file_info_t* file, directory_info_t* directory, u32 filetype_hint) {
	// assume it is an image file?
	reset_global_caselist(app_state);
	bool is_base_image = filetype_hint != FILETYPE_HINT_OVERLAY;
	if (is_base_image) {
		unload_all_images(app_state);
		// Unload any old annotations if necessary
		unload_and_reinit_annotations(&app_state->scene.annotation_set);
	}
	load_next_image_as_overlay = false; // reset after use (don't keep stacking on more overlays unintendedly)
	image_t* image = load_image_from_file(app_state, file, directory, filetype_hint);
	if (image->is_valid) {
		add_image(app_state, image, is_base_image, !is_base_image);

        if (is_base_image) {
            annotation_set_t* annotation_set = &app_state->scene.annotation_set;
//            unload_and_reinit_annotations(annotation_set); // no need, already unloaded!
            annotation_set->mpp = V2F(image->mpp_x, image->mpp_y);

            // Check if there is an associated ASAP XML or COCO JSON annotations file
            char temp_filename[512];
            temp_filename[0] = '\0';
            const char* prefix = (app_state->annotation_directory[0] != '\0') ? app_state->annotation_directory : file->filename_prefix;
            snprintf(temp_filename, sizeof(temp_filename), "%s%s", prefix, file->filename_in_directory);
            bool were_annotations_loaded = false;

            // Load JSON first
#if 0
            replace_file_extension(temp_filename, temp_size, "json");
			annotation_set->coco_filename = strdup(temp_filename); // TODO: do this somewhere else
			if (file_exists(temp_filename)) {
				console_print("Found JSON annotations: '%s'\n", temp_filename);
				coco_t coco = {};
				load_coco_from_file(&coco, temp_filename);
				coco_transfer_annotations_to_annotation_set(&coco, annotation_set);
				coco_destroy(&coco);
				were_annotations_loaded = true;

				// Enable export as XML (make sure XML annotations do not get out of date!)
				annotation_set->export_as_asap_xml = true;
				replace_file_extension(temp_filename, temp_size, "xml");
				strncpy(annotation_set->asap_xml_filename, temp_filename, sizeof(annotation_set->asap_xml_filename)-1);


			} else {
				coco_init_main_image(&annotation_set->coco, &image);
			}
#else
            // TODO: remove?
            coco_init_main_image(&annotation_set->coco, image);
#endif

            // TODO: use most recently updated annotations?
            replace_file_extension(temp_filename, sizeof(temp_filename), "xml");
            if (file_exists(temp_filename)) {
                console_print("Found XML annotations: '%s'\n", temp_filename);
                if (!were_annotations_loaded) {
                    load_asap_xml_annotations(app_state, temp_filename);
                    were_annotations_loaded = true;
	                // Don't hide annotations when first loading the slide, that might lead the user to believe that there are none!
					app_state->scene.enable_annotations = true;
                }
            }

            if (app_state->remember_annotation_groups_as_template && !were_annotations_loaded && app_state->scene.annotation_set_template.is_valid) {
                annotation_set_init_from_template(annotation_set, &app_state->scene.annotation_set_template);
            }

            // TODO: only save/convert COCO, not the XML as well!
            if (annotation_set->export_as_asap_xml) {
//				annotation_set->modified = true; // to force export in COCO as well
            }
        }

		console_print("Loaded '%s'\n", file->full_filename);
		if (image->backend == IMAGE_BACKEND_ISYNTAX) {
			console_print("   iSyntax: loading took %g seconds\n", image->isyntax.loading_time);
		}
		return true;

	} else {
		return false;
	}
}


bool load_generic_file(app_state_t* app_state, const char* filename, u32 filetype_hint) {
	file_info_t file = viewer_get_file_info(filename);
	bool success = false;
	if (file.is_valid) {
		if (file.is_regular_file) {
			if (file.type == VIEWER_FILE_TYPE_DICOM) {
				// TODO: load the rest of the directory
				dicom_series_t dicom = {};
				dicom_open_from_file(&dicom, &file);
				success = true;
			} else if (file.is_image) {
				success = viewer_load_new_image(app_state, &file, NULL, filetype_hint);
			} else {
				if (file.type == VIEWER_FILE_TYPE_XML) {
					// TODO: how to get the correct scale factor for the annotations?
					// Maybe a placeholder value, which gets updated based on the scale of the scene image?
					annotation_set_t* annotation_set = &app_state->scene.annotation_set;
					unload_and_reinit_annotations(annotation_set);
					if (arrlen(app_state->loaded_images) > 0) {
						image_t* image = app_state->loaded_images[0];
						annotation_set->mpp = V2F(image->mpp_x, image->mpp_y);
					} else {
						annotation_set->mpp = V2F(0.25f, 0.25f);
					}
					success = load_asap_xml_annotations(app_state, filename);
				} else if (file.type == VIEWER_FILE_TYPE_JSON) {
					// TODO: disambiguate between COCO annotations and case lists
					reload_global_caselist(app_state, filename);
					show_slide_list_window = true;
					success = caselist_select_first_case(app_state, &app_state->caselist);
				}
			}
		} else if (file.is_directory) {
			directory_info_t directory = viewer_get_directory_info(filename);
			if (directory.is_valid) {
                console_print("Trying to open a directory '%s'\n", filename);
                if (directory.contains_dicom_files) {
                    file.type = VIEWER_FILE_TYPE_DICOM;
					success = viewer_load_new_image(app_state, &file, &directory, filetype_hint);
				} else if (directory.contains_mrxs_files) {
                    file.type = VIEWER_FILE_TYPE_MRXS;
                    success = viewer_load_new_image(app_state, &file, &directory, filetype_hint);
                }
			}
			viewer_directory_info_destroy(&directory); // TODO: transfer ownership of directory structure info?
		}
	}

	if (!success) {
		console_print_error("Could not load '%s'\n", filename);
		gui_add_modal_message_popup("Error##load_generic_file", "Could not load '%s'.\n", filename);
	}
	return success;

}

const char* get_active_directory(app_state_t* app_state) {
	if (arrlen(app_state->loaded_images) > 0) {
		for (i32 i = 0; i < arrlen(app_state->loaded_images); ++i) {
			image_t* image = app_state->loaded_images[i];
			if (image->is_local) {
				return image->directory;
			}
		}
	} else {
        if (strlen(app_state->last_active_directory) > 0) {
            return app_state->last_active_directory;
        }
    }
	return get_default_save_directory();
}

const char* get_annotation_directory(app_state_t* app_state) {
    if (app_state->is_annotation_directory_set) {
        return app_state->annotation_directory;
    } else {
        return get_active_directory(app_state);
    }
}

void set_annotation_directory(app_state_t* app_state, const char* path) {
	strncpy(app_state->annotation_directory, path, COUNT(app_state->annotation_directory)-2);
	i32 prefix_len = (i32)strlen(app_state->annotation_directory);
	if (prefix_len > 0) {
		// discard folder names "." and ".."
		char* folder_name = (char*)one_past_last_slash(app_state->annotation_directory, prefix_len);
		if (strcmp(folder_name, ".") == 0) {
			folder_name[0] = '\0';
			prefix_len -= 1;
		} else if (strcmp(folder_name, "..") == 0) {
			folder_name[0] = '\0';
			prefix_len -= 2;
		}
	}
	if (prefix_len > 0) {
		// add trailing slash
		if (app_state->annotation_directory[prefix_len-1] != '/' && app_state->annotation_directory[prefix_len-1] != '\\') {
			app_state->annotation_directory[prefix_len++] = PATH_SEP[0];
		}
	}
	app_state->is_annotation_directory_set = true;
}


//TODO: refactor
image_t* load_image_from_file(app_state_t* app_state, file_info_t* file, directory_info_t* directory, u32 filetype_hint) {

	image_t* image = (image_t*)calloc(1, sizeof(image_t));
	image->is_local = true;
	image->resource_id = global_next_resource_id++;

	bool is_overlay = (filetype_hint == FILETYPE_HINT_OVERLAY);
    image_t* parent_image = NULL;
    if (is_overlay && arrlen(app_state->loaded_images)) {
        parent_image = app_state->loaded_images[0];
    }

	const char* filename = file->full_filename;
	size_t filename_len = strlen(filename);
	const char* name = one_past_last_slash(filename, filename_len);
	strncpy(image->name, name, sizeof(image->name)-1);

	if (name > filename) {
		size_t directory_len = (u64)name - (u64)filename;
		memcpy(image->directory, filename, ATMOST(directory_len, sizeof(image->directory)));
	}

	const char* ext = get_file_extension(filename);

	if (file->type == VIEWER_FILE_TYPE_SIMPLE_IMAGE) {
		// Load using stb_image

		image->type = IMAGE_TYPE_WSI;
		image->backend = IMAGE_BACKEND_STBI;
		image->simple.channels = 4; // desired: RGBA
		image->simple.pixels = stbi_load(filename, &image->simple.width, &image->simple.height, &image->simple.channels_in_file, 4);
		if (image->simple.pixels) {

			image->is_freshly_loaded = true;
			image->is_valid = true;
			init_image_from_stbi(image, &image->simple, is_overlay);
			return image;

			//stbi_image_free(image->stbi.pixels);
		}

	} else if (app_state->use_builtin_tiff_backend && (file->type == VIEWER_FILE_TYPE_TIFF /*|| file->type == VIEWER_FILE_TYPE_NDPI*/)) {
		// Try to open as TIFF, using the built-in backend
		tiff_t tiff = {0};
		if (open_tiff_file(&tiff, filename)) {
			init_image_from_tiff(image, tiff, is_overlay, parent_image);
			return image;
		} else {
			tiff_destroy(&tiff);
			image->is_valid = false;
			return image;
		}
	} else if (file->type == VIEWER_FILE_TYPE_ISYNTAX) {
		// Try to open as iSyntax
		isyntax_t isyntax = {0};
		isyntax_set_work_queue(&isyntax, &global_work_queue);
		if (isyntax_open(&isyntax, filename, true)) {
			init_image_from_isyntax(image, &isyntax, is_overlay);
			return image;
		}
	} else if (file->type == VIEWER_FILE_TYPE_DICOM) {

        if (file->is_regular_file) {
            // TODO: load the rest of the directory
            dicom_series_t dicom = {};
            if (dicom_open_from_file(&dicom, file)) {

            }
        } else if (file->is_directory && directory) {
            dicom_series_t dicom = {};
            if (dicom_open_from_directory(&dicom, directory)) {
                init_image_from_dicom(image, &dicom, is_overlay);
                return image;
            } else {
                dicom_destroy(&dicom);
            }
        }
    } else if (debug_use_native_mrxs_backend && file->type == VIEWER_FILE_TYPE_MRXS) {
		mrxs_t mrxs = {0};
		mrxs_set_work_queue(&mrxs, &global_work_queue);
		bool opened_successfully = false;
        if (file->is_regular_file) {
			// Strip .mrxs extension to get the name of the corresponding slide folder
	        char basename[512];
			strncpy(basename, filename, sizeof(basename));
	        char* end = basename + filename_len;
	        for (char* pos = end - 1; pos >= basename; --pos) {
		        if (*pos == '.') {
			        *pos = '\0';
					break;
		        }
		        if (*pos == '/' || *pos == '\\') {
			        break;
		        }
	        }
	        file_info_t slide_dir_file_info = viewer_get_file_info(basename);
			if (slide_dir_file_info.is_directory) {
				opened_successfully = mrxs_open_from_directory(&mrxs, &slide_dir_file_info, NULL);
			}

        } else if (file->is_directory && directory) {
			// We directly opened the slide folder instead of the .mrxs
	        opened_successfully = mrxs_open_from_directory(&mrxs, file, NULL);
        }
		if (opened_successfully) {
			init_image_from_mrxs(image, &mrxs, is_overlay);
			return image;
		} else {
			mrxs_destroy(&mrxs);
		}
	} else {
		// Try to load the file using OpenSlide
		if (!is_openslide_available) {
			if (!is_openslide_loading_done) {
#if DO_DEBUG
				console_print("Waiting for OpenSlide to finish loading...\n");
#endif
				while (work_queue_is_work_in_progress(&global_work_queue)) {
                    work_queue_do_work(&global_work_queue, 0);
				}
			}
			if (!is_openslide_available) {
				console_print("Can't try to load %s using OpenSlide, because OpenSlide is not available\n", filename);
				image->is_valid = false;
				return image;
			}
		}

		// TODO: fix code duplication from init_image_from_tiff()
		image->type = IMAGE_TYPE_WSI;
		image->backend = IMAGE_BACKEND_OPENSLIDE;
		wsi_t wsi = {};
        load_openslide_wsi(&wsi, filename);
        if (wsi.osr) {
            init_image_from_openslide(image, &wsi, is_overlay);
            return image;
        }
	}
	return image;

}


void unload_openslide_wsi(wsi_t* wsi) {
	if (wsi->osr) {
		openslide.close(wsi->osr);
		wsi->osr = NULL;
	}

}
