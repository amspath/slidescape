#include "common.h"

#include "win32_main.h"
#include "platform.h"
#include "intrinsics.h"
#include "stringutils.h"

#include "openslide_api.h"
#include <glad/glad.h>
#include <linmath.h>

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

void gl_diagnostic(const char* prefix) {
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("%s: failed with error code 0x%x\n", prefix, err);
	}
}


// TODO: remove? do we still need this>
rect2i clip_rect(rect2i* first, rect2i* second) {
	i32 x0 = MAX(first->x, second->x);
	i32 y0 = MAX(first->y, second->y);
	i32 x1 = MIN(first->x + first->w, second->x + second->w);
	i32 y1 = MIN(first->y + first->h, second->y + second->h);
	rect2i result = {
			.x = x0,
			.y = y0,
			.w = x1 - x0,
			.h = y1 - y0,
	};
	return result;
}

bool is_point_inside_rect(rect2i rect, v2i point) {
	bool result = true;
	if (point.x < rect.x || point.x >= (rect.x + rect.w) || point.y < rect.y || point.y >= (rect.y + rect.h)) {
		result = false;
	}
	return result;
}

v2i rect2i_center_point(rect2i* rect) {
	v2i result = {
			.x = rect->x + rect->w / 2,
			.y = rect->y + rect->h / 2,
	};
	return result;
}

#define FLOAT_TO_BYTE(x) ((u8)(255.0f * CLAMP((x), 0.0f, 1.0f)))
#define BYTE_TO_FLOAT(x) CLAMP(((float)((x & 0x0000ff))) /255.0f, 0.0f, 1.0f)
#define TO_BGRA(r,g,b,a) ((a) << 24 | (r) << 16 | (g) << 8 | (b) << 0)

#if 0
void fill_image(image_t* image, void* data, i32 width, i32 height) {
	*image = (image_t) { .data = data, .width = width, .height = height, .pitch = width * 4, };
}

bool32 load_image(image_t* image, const char* filename) {
	if (!image) panic();
	else {
		if (image->data != NULL) {
			free(image->data);
		}
		memset_zero(image);
		i64 time_start = get_clock();
		image->data = stbi_load(filename, &image->width, &image->height, &image->bpp, 4);
		float seconds_elapsed = get_seconds_elapsed(time_start, get_clock());
		printf("Loading the image took %f seconds.\n", seconds_elapsed);
		if (image->data) {
			image->pitch = image->width * 4;
			return true;
		}
	}
	return false;
}
#endif


bool32 load_image_from_file(image_t* image, const char* filename) {
	bool32 result = false;

	const char* ext = get_file_extension(filename);

	if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0) {
		// Load using stb_image
		image->type = IMAGE_TYPE_STBI_COMPATIBLE;
		image->stbi.channels = 4; // desired: RGBA
		image->stbi.pixels = stbi_load(filename, &image->stbi.width, &image->stbi.height, &image->stbi.channels_in_file, 4);
		if (image->stbi.pixels) {
			glEnable(GL_TEXTURE_2D);
			glGenTextures(1, &image->stbi.texture);
			//glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, image->stbi.texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->stbi.width, image->stbi.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->stbi.pixels);

			result = true;
			image->stbi.texture_initialized = true;
			//stbi_image_free(image->stbi.pixels);
		}
		return result;
#if  1 // TODO: The TIFF code is work in progress
	} else if (strcasecmp(ext, "tiff") == 0 || strcasecmp(ext, "tif") == 0) {
		tiff_t tiff = {};
		if (open_tiff_file(&tiff, filename)) {
			image->type = IMAGE_TYPE_TIFF_GENERIC;
			image->tiff.tiff = tiff;

			current_level = tiff.level_count-1;
			zoom_position = (float)current_level;

			camera_pos.x = (tiff.main_image->image_width * tiff.mpp_x) / 2.0f;
			camera_pos.y = (tiff.main_image->image_height * tiff.mpp_y) / 2.0f;

			result = true;
		} else {
			tiff_destroy(&tiff);
			result = false;
		}

		return result;

#endif
	} else {
		image->type = IMAGE_TYPE_WSI;
		wsi_t* wsi = &image->wsi.wsi;
		load_wsi(wsi, filename);
		result = wsi->osr != NULL;

		return result;
	}
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	gl_diagnostic("glTexImage2D");
	return texture;
}


wsi_tile_t* wsi_get_tile(wsi_level_t* wsi_level, i32 tile_x, i32 tile_y) {
	i32 tile_index = tile_y * wsi_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < wsi_level->num_tiles);
	wsi_tile_t* result = wsi_level->tiles + tile_index;
	return result;
}

tiff_tile_t* tiff_get_tile(tiff_ifd_t* image_level, i32 tile_x, i32 tile_y) {
	i32 tile_index = tile_y * image_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < image_level->tile_count);
	tiff_tile_t* result = image_level->tiles + tile_index;
	return result;
}

void load_tile_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_t* task_data = (load_tile_task_t*) userdata;
	wsi_t* wsi = task_data->wsi;
	i32 level = task_data->level;
	i32 tile_x = task_data->tile_x;
	i32 tile_y = task_data->tile_y;
	wsi_level_t* wsi_level = wsi->levels + level;
	wsi_tile_t* tile = wsi_get_tile(wsi_level, tile_x, tile_y);

	u32* temp_memory = thread_local_storage[logical_thread_index] ; //malloc(WSI_BLOCK_SIZE);
	i64 x = (tile_x * TILE_DIM) << level;
	i64 y = (tile_y * TILE_DIM) << level;
	openslide.openslide_read_region(wsi->osr, temp_memory, x, y, level, TILE_DIM, TILE_DIM);

//	printf("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);

	write_barrier;

	// do the submitting directly
	glEnable(GL_TEXTURE_2D);
	tile->texture = load_texture(temp_memory, TILE_DIM, TILE_DIM);
	free(task_data);
	glFinish(); // Block thread execution until all OpenGL operations have finished.

}

void tiff_load_tile_func(i32 logical_thread_index, void* userdata) {
	tiff_load_tile_task_t* task_data = (tiff_load_tile_task_t*) userdata;
	tiff_t* tiff = task_data->tiff;
	i32 level = task_data->level;
	i32 tile_x = task_data->tile_x;
	i32 tile_y = task_data->tile_y;
	tiff_ifd_t* level_image = tiff->level_images + level;
	tiff_tile_t* tile = tiff_get_tile(level_image, tile_x, tile_y);

	// Note: when the thread started up we allocated a large blob of memory for the thread to use privately
	// TODO: better/more explicit allocator (instead of some setting some hard-coded pointers)
	thread_memory_t* thread_memory = (thread_memory_t*) thread_local_storage[logical_thread_index];
	u8* temp_memory = thread_memory->aligned_rest_of_thread_memory; //malloc(WSI_BLOCK_SIZE);
	u8* compressed_tile_data = thread_memory->aligned_rest_of_thread_memory + WSI_BLOCK_SIZE;

	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	u64 tile_offset = level_image->tile_offsets[tile_index];
	u64 compressed_tile_size_in_bytes = level_image->tile_byte_counts[tile_index];
	// Some tiles apparently contain no data (not even an empty/dummy JPEG stream like some other tiles have).
	// We need to check for this situation and chicken out if this is the case.
	if (tile_offset == 0 || compressed_tile_size_in_bytes == 0) {
		//printf("thread %d: tile level %d, tile %d (%d, %d) appears to be empty\n", logical_thread_index, level, tile_index, tile_x, tile_y);
		// TODO: Make one single 'empty' tile texture and simply reuse that
		memset(temp_memory, 0xFF, WSI_BLOCK_SIZE);
		goto finish_up;
	}
	u8* jpeg_tables = level_image->jpeg_tables;
	u64 jpeg_tables_length = level_image->jpeg_tables_length;

	// TODO: make async I/O code platform agnostic

	// To submit an async I/O request on Win32, we need to fill in an OVERLAPPED structure with the
	// offset in the file where we want to do the read operation
	LARGE_INTEGER offset = {.QuadPart = tile_offset};
	thread_memory->overlapped = (OVERLAPPED) {
		.Offset = offset.LowPart,
		.OffsetHigh = offset.HighPart,
		.hEvent = thread_memory->async_io_event
	};
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
		memset(temp_memory, 0xFF, WSI_BLOCK_SIZE);
	} else {
		if (decode_tile(jpeg_tables, jpeg_tables_length, compressed_tile_data, compressed_tile_size_in_bytes,
				temp_memory, (level_image->color_space == TIFF_PHOTOMETRIC_YCBCR))) {
//		    printf("thread %d: successfully decoded level%d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
		} else {
			printf("thread %d: failed to decode level%d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
		}
	}

//	printf("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);

	finish_up:
	write_barrier;

	// do the submitting directly on the GPU
	glEnable(GL_TEXTURE_2D);
	tile->texture = load_texture(temp_memory, TILE_DIM, TILE_DIM);
	free(task_data);
	glFinish(); // Block thread execution until all OpenGL operations have finished.

}

extern work_queue_t work_queue;

void enqueue_load_tile(wsi_t* wsi, i32 level, i32 tile_x, i32 tile_y) {
	load_tile_task_t* task_data = malloc(sizeof(load_tile_task_t)); // should be freed after uploading the tile to the gpu
	*task_data = (load_tile_task_t){ .wsi = wsi, .level = level, .tile_x = tile_x, .tile_y = tile_y };

	add_work_queue_entry(&work_queue, load_tile_func, task_data);

}

void tiff_enqueue_load_tile(tiff_t* tiff, i32 level, i32 tile_x, i32 tile_y) {
	tiff_load_tile_task_t* task_data = malloc(sizeof(tiff_load_tile_task_t)); // should be freed after uploading the tile to the gpu
	*task_data = (tiff_load_tile_task_t){ .tiff = tiff, .level = level, .tile_x = tile_x, .tile_y = tile_y };

	add_work_queue_entry(&work_queue, tiff_load_tile_func, task_data);

}

i32 wsi_load_tile(wsi_t* wsi, i32 level, i32 tile_x, i32 tile_y) {
	wsi_level_t* wsi_level = wsi->levels + level;
	wsi_tile_t* tile = wsi_get_tile(wsi_level, tile_x, tile_y);

	if (tile->texture != 0) {
		// Yay, this tile is already loaded
		return 0;
	} else {
		read_barrier;
		if (!tile->is_submitted_for_loading) {
			tile->is_submitted_for_loading = true;
			enqueue_load_tile(wsi, level, tile_x, tile_y);
		} else {
			// already submitted for loading, there is nothing to do!
		}

		return 1;
	}
}

i32 tiff_load_tile(tiff_t* tiff, i32 level, i32 tile_x, i32 tile_y) {
	tiff_ifd_t* tiff_level = tiff->level_images + level;
	tiff_tile_t* tile = tiff_get_tile(tiff_level, tile_x, tile_y);

	if (tile->texture != 0) {
		// Yay, this tile is already loaded
		return 0;
	} else {
		read_barrier;
		if (!tile->is_submitted_for_loading) {
			tile->is_submitted_for_loading = true;
			tiff_enqueue_load_tile(tiff, level, tile_x, tile_y);
		} else {
			// already submitted for loading, there is nothing to do!
		}
		return 1;
	}
}

u32 get_texture_for_tile(wsi_t* wsi, i32 level, i32 tile_x, i32 tile_y) {
	wsi_level_t* wsi_level = wsi->levels + level;

	i32 tile_index = tile_y * wsi_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < wsi_level->num_tiles);
	wsi_tile_t* tile = wsi_level->tiles + tile_index;

	return tile->texture;
}

u32 tiff_get_texture_for_tile(tiff_t* tiff, i32 level, i32 tile_x, i32 tile_y) {
	tiff_ifd_t* level_image = tiff->level_images + level;

	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < level_image->tile_count);
	tiff_tile_t* tile = level_image->tiles + tile_index;

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


	unload_wsi(wsi);

	wsi->osr = openslide.openslide_open(filename);
	if (wsi->osr) {
		printf("Openslide: opened %s\n", filename);

		openslide.openslide_get_level0_dimensions(wsi->osr, &wsi->width, &wsi->height);
		ASSERT(wsi->width > 0);
		ASSERT(wsi->height > 0);

//		wsi->width_pow2 = next_pow2((u64)wsi->width);
//		wsi->height_pow2 = next_pow2((u64)wsi->height);

		wsi->num_levels = openslide.openslide_get_level_count(wsi->osr);
		printf("Openslide: WSI has %d levels\n", wsi->num_levels);
		if (wsi->num_levels > WSI_MAX_LEVELS) {
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

		for (i32 i = 0; i < wsi->num_levels; ++i) {
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
			level->num_tiles = level->width_in_tiles * level->height_in_tiles;
			level->tiles = calloc(1, level->num_tiles * sizeof(wsi_tile_t));
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
				printf("%s : w=%d h=%d\n", name, w, h);

			}
		}

		current_level = wsi->num_levels-1;
		zoom_position = (float)current_level;

		camera_pos.x = (wsi->width * wsi->mpp_x) / 2.0f;
		camera_pos.y = (wsi->height * wsi->mpp_y) / 2.0f;

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

	for (i32 i = 0; i < COUNT(wsi->levels); ++i) {
		wsi_level_t* level = wsi->levels + i;
		if (level->tiles) {
			for (i32 j = 0; j < level->num_tiles; ++j) {
				wsi_tile_t* tile = level->tiles + j;
				if (tile->texture != 0) {
					unload_texture(tile->texture);
				}
			}
		}
		free(level->tiles);
	}
}

void unload_image(image_t* image) {
	if (image) {
		if (image->type == IMAGE_TYPE_WSI) {
			unload_wsi(&image->wsi.wsi);
		} else if (image->type == IMAGE_TYPE_STBI_COMPATIBLE) {
			if (image->stbi.pixels) {
				stbi_image_free(image->stbi.pixels);
			}
			if (image->stbi.texture != 0) {
				unload_texture(image->stbi.texture);
			}
		} else if (image->type == IMAGE_TYPE_TIFF_GENERIC) {
			tiff_destroy(&image->tiff.tiff);
		}
	}
}

void on_file_dragged(char* filename) {
	i32 current_image_count = sb_count(loaded_images);
	if (current_image_count > 0) {
		ASSERT(loaded_images);
		for (i32 i = 0; i < current_image_count; ++i) {
			image_t* old_image = loaded_images + i;
			unload_image(old_image);
		}
		sb_free(loaded_images);
		loaded_images = NULL;
	}

	image_t new_image = {0};

	if (load_image_from_file(&new_image, filename)) {
		new_image.is_freshly_loaded = true;
		sb_push(loaded_images, new_image);
	}
}

void first(i32 client_width, i32 client_height) {
	init_opengl_stuff(client_width, client_height);

	// Load a slide from the command line or through the OS (double-click / drag on executable, etc.)
	if (g_argc > 1) {
		char* filename = g_argv[1];
		on_file_dragged(filename);
	}
}

i32 tile_pos_from_world_pos(float world_pos, float tile_side) {
	ASSERT(tile_side > 0);
	float tile_float = (world_pos / tile_side);
	float tile = (i32)floorf(tile_float);
	return tile;
}

bool32 was_key_pressed(input_t* input, i32 keycode) {
	u8 key = keycode & 0xFF;
	if (input->keyboard.keys[key].down && input->keyboard.keys[key].transition_count > 0) {
		return true;
	} else {
		return false;
	}
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


void viewer_update_and_render(input_t* input, i32 client_width, i32 client_height, float delta_t) {
	glViewport(0, 0, client_width, client_height);
	glClearColor(clear_color.r, clear_color.g, clear_color.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	// Determine the image to view;
	for (i32 i = 0; i < 9; ++i) {
		if (input->keyboard.keys['1' + i].down) {
			displayed_image = MIN(sb_count(loaded_images)-1, i);
		}
	}

	image_t* image = loaded_images + displayed_image;

	if (!image) {
		return; // nothing to draw
	}

	// todo: process even more of the mouse/keyboard input here?
	v2i current_drag_vector = {};
	if (input) {
		if (input->mouse_buttons[0].down) {
			// Mouse drag.
			if (input->mouse_buttons[0].transition_count != 0) {
				// Don't start dragging if clicked outside the window
				rect2i valid_drag_start_rect = {0, 0, client_width, client_height};
				if (is_point_inside_rect(valid_drag_start_rect, input->mouse_xy)) {
					is_dragging = true; // drag start
//						printf("Drag started: x=%d y=%d\n", input->mouse_xy.x, input->mouse_xy.y);
				}
			} else if (is_dragging) {
				// already started dragging on a previous frame
				current_drag_vector = input->drag_vector;
			}
			input->drag_vector = (v2i){};
			mouse_hide();
		} else {
			mouse_show();
			if (input->mouse_buttons[0].transition_count != 0) {
				is_dragging = false;
//			        printf("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
			}
		}
	}

	if (image->type == IMAGE_TYPE_STBI_COMPATIBLE) {
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
		if (is_dragging) {
			obj_pos.x += current_drag_vector.x * pan_multiplier;
			obj_pos.y += current_drag_vector.y * pan_multiplier;
		}

		mat4x4 model_matrix;
		mat4x4_identity(model_matrix);
		mat4x4_translate_in_place(model_matrix, obj_pos.x, obj_pos.y, 0.0f);
		mat4x4_scale_aniso(model_matrix, model_matrix, image->stbi.width * 2, image->stbi.height * 2, 1.0f);


		glUseProgram(basic_shader);

		if (use_image_adjustments) {
			glUniform1f(basic_shader_u_black_level, black_level);
			glUniform1f(basic_shader_u_white_level, white_level);
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

		draw_rect(image->stbi.texture);
	}
	// TODO: refactor so that this code is TIFF/OpenSlide agnostic
#if 1
	else if (image->type == IMAGE_TYPE_TIFF_GENERIC) {
		tiff_t *tiff = &image->tiff.tiff;

		if (!tiff) {
			printf("Error: tried to draw a WSI that hasn't been loaded.\n");
			return;
		}


		i32 old_level = current_level;
		i32 center_offset_x = 0;
		i32 center_offset_y = 0;

		i32 max_level = tiff->level_count - 1;
		tiff_ifd_t* level_image = tiff->level_images + current_level;

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
				current_level = CLAMP(current_level + dlevel, 0, tiff->level_count - 1);
				level_image = tiff->level_images + current_level;

				if (current_level != old_level && used_mouse_to_zoom) {
#if 1
					center_offset_x = input->mouse_xy.x - client_width / 2;
					center_offset_y = -(input->mouse_xy.y - client_height / 2);

					if (current_level < old_level) {
						// Zoom in, while keeping the area around the mouse cursor in the same place on the screen.
						camera_pos.x += center_offset_x * level_image->um_per_pixel_x;
						camera_pos.y += center_offset_y * level_image->um_per_pixel_y;
					} else if (current_level > old_level) {
						// Zoom out, while keeping the area around the mouse cursor in the same place on the screen.
						camera_pos.x -= center_offset_x * level_image->um_per_pixel_x * 0.5f;
						camera_pos.y -= center_offset_y * level_image->um_per_pixel_y * 0.5f;
					}
#endif
				}
			}


		}




		// TODO: fix/rewrite
		// Spring/bounce effect
		float d_zoom = (float) current_level - zoom_position;
		float abs_d_zoom = fabs(d_zoom);
		float sign_d_zoom = signbit(d_zoom) ? -1.0f : 1.0f;
		float linear_catch_up_speed = 10.0f * delta_t;
		float exponential_catch_up_speed = 18.0f * delta_t;
		if (abs_d_zoom > linear_catch_up_speed) {
			d_zoom = (linear_catch_up_speed + (abs_d_zoom - linear_catch_up_speed) * exponential_catch_up_speed) *
			         sign_d_zoom;
		}
		zoom_position += d_zoom;

		float screen_um_per_pixel_x = powf(2.0f, zoom_position) * tiff->mpp_x;
		float screen_um_per_pixel_y = powf(2.0f, zoom_position) * tiff->mpp_y;

		float r_minus_l = screen_um_per_pixel_x * (float) client_width;
		float t_minus_b = screen_um_per_pixel_y * (float) client_height;

		float camera_rect_x1 = camera_pos.x - r_minus_l * 0.5f;
		float camera_rect_x2 = camera_pos.x + r_minus_l * 0.5f;
		float camera_rect_y1 = camera_pos.y - t_minus_b * 0.5f;
		float camera_rect_y2 = camera_pos.y + t_minus_b * 0.5f;

		i32 camera_tile_x1 = tile_pos_from_world_pos(camera_rect_x1, level_image->x_tile_side_in_um);
		i32 camera_tile_x2 = tile_pos_from_world_pos(camera_rect_x2, level_image->x_tile_side_in_um) + 1;
		i32 camera_tile_y1 = tile_pos_from_world_pos(camera_rect_y1, level_image->y_tile_side_in_um);
		i32 camera_tile_y2 = tile_pos_from_world_pos(camera_rect_y2, level_image->y_tile_side_in_um) + 1;

		camera_tile_x1 = CLAMP(camera_tile_x1, 0, level_image->width_in_tiles);
		camera_tile_x2 = CLAMP(camera_tile_x2, 0, level_image->width_in_tiles);
		camera_tile_y1 = CLAMP(camera_tile_y1, 0, level_image->height_in_tiles);
		camera_tile_y2 = CLAMP(camera_tile_y2, 0, level_image->height_in_tiles);

		if (input) {

			if (was_key_pressed(input, 'P')) {
				use_image_adjustments = !use_image_adjustments;
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
			float panning_multiplier = 1.0f + 3.0f * ((float) max_level - zoom_position) / (float) max_level;
			if (is_key_down(input, KEYCODE_SHIFT)) {
				panning_multiplier *= 0.25f;
			}

			// Panning using the arrow or WASD keys.
			float panning_speed = 900.0f * delta_t * panning_multiplier;
			if (input->keyboard.action_down.down || is_key_down(input, 'S')) {
				camera_pos.y -= level_image->um_per_pixel_y * panning_speed;
			}
			if (input->keyboard.action_up.down || is_key_down(input, 'W')) {
				camera_pos.y += level_image->um_per_pixel_y * panning_speed;
			}
			if (input->keyboard.action_right.down || is_key_down(input, 'D')) {
				camera_pos.x += level_image->um_per_pixel_x * panning_speed;
			}
			if (input->keyboard.action_left.down || is_key_down(input, 'A')) {
				camera_pos.x -= level_image->um_per_pixel_x * panning_speed;
			}

			if (is_dragging) {
				camera_pos.x -= current_drag_vector.x * level_image->um_per_pixel_x * panning_multiplier;
				camera_pos.y += current_drag_vector.y * level_image->um_per_pixel_y * panning_multiplier;
			}


			// TODO: fix multithreaded IO problems
			i32 max_tiles_to_load_at_once = 5;
			i32 tiles_loaded = 0;
			for (i32 tile_y = camera_tile_y1; tile_y < camera_tile_y2; ++tile_y) {
				for (i32 tile_x = camera_tile_x1; tile_x < camera_tile_x2; ++tile_x) {
					if (tiles_loaded >= max_tiles_to_load_at_once) {
//						 TODO: remove this performance/stability hack after better async IO (multithreaded) is implemented.
						break;
					} else {
						tiles_loaded += tiff_load_tile(tiff, current_level, tile_x, tile_y);
					}
				}
			}
		}

		mat4x4 projection = {};
		{
			float l = -0.5f * r_minus_l;
			float r = +0.5f * r_minus_l;
			float b = -0.5f * t_minus_b;
			float t = +0.5f * t_minus_b;
			float n = 100.0f;
			float f = -100.0f;
			mat4x4_ortho(projection, l, r, b, t, n, f);
		}

		mat4x4 I;
		mat4x4_identity(I);

		// define view matrix
		mat4x4 view_matrix;
		mat4x4_translate(view_matrix, -camera_pos.x, -camera_pos.y, 0.0f);

		mat4x4 projection_view_matrix;
		mat4x4_mul(projection_view_matrix, projection, view_matrix);

		glUseProgram(basic_shader);
		glUniformMatrix4fv(basic_shader_u_projection_view_matrix, 1, GL_FALSE, &projection_view_matrix[0][0]);

		glUniform3fv(basic_shader_u_background_color, 1, (GLfloat *) &clear_color);
		if (use_image_adjustments) {
			glUniform1f(basic_shader_u_black_level, black_level);
			glUniform1f(basic_shader_u_white_level, white_level);
		} else {
			glUniform1f(basic_shader_u_black_level, 0.0f);
			glUniform1f(basic_shader_u_white_level, 1.0f);
		}

		i32 num_levels_above_current = tiff->level_count - current_level - 1;
		ASSERT(num_levels_above_current >= 0);

		// Draw all levels within the viewport, up to the current zoom factor
		for (i32 level = tiff->level_count - 1; level >= current_level; --level) {
			tiff_ifd_t *drawn_level = tiff->level_images + level;

			i32 level_camera_tile_x1 = tile_pos_from_world_pos(camera_rect_x1, drawn_level->x_tile_side_in_um);
			i32 level_camera_tile_x2 = tile_pos_from_world_pos(camera_rect_x2, drawn_level->x_tile_side_in_um) + 1;
			i32 level_camera_tile_y1 = tile_pos_from_world_pos(camera_rect_y1, drawn_level->y_tile_side_in_um);
			i32 level_camera_tile_y2 = tile_pos_from_world_pos(camera_rect_y2, drawn_level->y_tile_side_in_um) + 1;

			level_camera_tile_x1 = CLAMP(level_camera_tile_x1, 0, drawn_level->width_in_tiles);
			level_camera_tile_x2 = CLAMP(level_camera_tile_x2, 0, drawn_level->width_in_tiles);
			level_camera_tile_y1 = CLAMP(level_camera_tile_y1, 0, drawn_level->height_in_tiles);
			level_camera_tile_y2 = CLAMP(level_camera_tile_y2, 0, drawn_level->height_in_tiles);

			for (i32 tile_y = level_camera_tile_y1; tile_y < level_camera_tile_y2; ++tile_y) {
				for (i32 tile_x = level_camera_tile_x1; tile_x < level_camera_tile_x2; ++tile_x) {

					tiff_tile_t *tile = tiff_get_tile(drawn_level, tile_x, tile_y);
					if (tile->texture) {
						u32 texture = tiff_get_texture_for_tile(tiff, level, tile_x, tile_y);

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
#endif
	else if (image->type == IMAGE_TYPE_WSI) {
		wsi_t* wsi = &image->wsi.wsi;

		if (!wsi->osr) {
			printf("Error: tried to draw a WSI that hasn't been loaded.\n");
			return;
		}


		i32 old_level = current_level;
		i32 center_offset_x = 0;
		i32 center_offset_y = 0;

		i32 max_level = wsi->num_levels - 1;
		wsi_level_t* wsi_level = wsi->levels + current_level;

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
				current_level = CLAMP(current_level + dlevel, 0, wsi->num_levels-1);
				wsi_level = wsi->levels + current_level;

				if (current_level != old_level && used_mouse_to_zoom) {
#if 1
					center_offset_x = input->mouse_xy.x - client_width / 2;
					center_offset_y = -(input->mouse_xy.y - client_height / 2);

					if (current_level < old_level) {
						// Zoom in, while keeping the area around the mouse cursor in the same place on the screen.
						camera_pos.x += center_offset_x * wsi_level->um_per_pixel_x;
						camera_pos.y += center_offset_y * wsi_level->um_per_pixel_y;
					} else if (current_level > old_level) {
						// Zoom out, while keeping the area around the mouse cursor in the same place on the screen.
						camera_pos.x -= center_offset_x * wsi_level->um_per_pixel_x * 0.5f;
						camera_pos.y -= center_offset_y * wsi_level->um_per_pixel_y * 0.5f;
					}
#endif
				}
			}


		}




		// TODO: fix/rewrite
		// Spring/bounce effect
		float d_zoom = (float)current_level - zoom_position;
		float abs_d_zoom = fabs(d_zoom);
		float sign_d_zoom = signbit(d_zoom) ? -1.0f : 1.0f;
		float linear_catch_up_speed = 10.0f * delta_t;
		float exponential_catch_up_speed = 18.0f * delta_t;
		if (abs_d_zoom > linear_catch_up_speed) {
			d_zoom = (linear_catch_up_speed + (abs_d_zoom - linear_catch_up_speed)*exponential_catch_up_speed) * sign_d_zoom;
		}
		zoom_position += d_zoom;

		float screen_um_per_pixel_x = powf(2.0f, zoom_position) * wsi->mpp_x;
		float screen_um_per_pixel_y = powf(2.0f, zoom_position) * wsi->mpp_y;

		float r_minus_l = screen_um_per_pixel_x * (float)client_width;
		float t_minus_b = screen_um_per_pixel_y * (float)client_height;

		float camera_rect_x1 = camera_pos.x - r_minus_l * 0.5f;
		float camera_rect_x2 = camera_pos.x + r_minus_l * 0.5f;
		float camera_rect_y1 = camera_pos.y - t_minus_b * 0.5f;
		float camera_rect_y2 = camera_pos.y + t_minus_b * 0.5f;

		i32 camera_tile_x1 = tile_pos_from_world_pos(camera_rect_x1, wsi_level->x_tile_side_in_um);
		i32 camera_tile_x2 = tile_pos_from_world_pos(camera_rect_x2, wsi_level->x_tile_side_in_um) + 1;
		i32 camera_tile_y1 = tile_pos_from_world_pos(camera_rect_y1, wsi_level->y_tile_side_in_um);
		i32 camera_tile_y2 = tile_pos_from_world_pos(camera_rect_y2, wsi_level->y_tile_side_in_um) + 1;

		camera_tile_x1 = CLAMP(camera_tile_x1, 0, wsi_level->width_in_tiles);
		camera_tile_x2 = CLAMP(camera_tile_x2, 0, wsi_level->width_in_tiles);
		camera_tile_y1 = CLAMP(camera_tile_y1, 0, wsi_level->height_in_tiles);
		camera_tile_y2 = CLAMP(camera_tile_y2, 0, wsi_level->height_in_tiles);

		if (input) {

			if (was_key_pressed(input, 'P')) {
				use_image_adjustments = !use_image_adjustments;
			}
#if 0

			// Experimental code for exporting regions of the wsi to a raw image file.
			if (input->mouse_buttons[1].down && input->mouse_buttons[1].transition_count > 0) {
				DUMMY_STATEMENT;
				i32 click_x = (camera_rect_x1 + input->mouse_xy.x * wsi_level->um_per_pixel_x) / wsi->mpp_x;
				i32 click_y = (camera_rect_y2 - input->mouse_xy.y * wsi_level->um_per_pixel_y) / wsi->mpp_y;
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
			float panning_multiplier = 1.0f + 3.0f * ((float)max_level - zoom_position) / (float)max_level;
			if (is_key_down(input, KEYCODE_SHIFT)) {
				panning_multiplier *= 0.25f;
			}

			// Panning using the arrow or WASD keys.
			float panning_speed = 900.0f * delta_t * panning_multiplier;
			if (input->keyboard.action_down.down || is_key_down(input, 'S')) {
				camera_pos.y -= wsi_level->um_per_pixel_y * panning_speed;
			}
			if (input->keyboard.action_up.down  || is_key_down(input, 'W')) {
				camera_pos.y += wsi_level->um_per_pixel_y * panning_speed;
			}
			if (input->keyboard.action_right.down  || is_key_down(input, 'D')) {
				camera_pos.x += wsi_level->um_per_pixel_x * panning_speed;
			}
			if (input->keyboard.action_left.down  || is_key_down(input, 'A')) {
				camera_pos.x -= wsi_level->um_per_pixel_x * panning_speed;
			}

			if (is_dragging) {
				camera_pos.x -= current_drag_vector.x * wsi_level->um_per_pixel_x * panning_multiplier;
				camera_pos.y += current_drag_vector.y * wsi_level->um_per_pixel_y * panning_multiplier;
			}



			i32 max_tiles_to_load_at_once = 10;
			i32 tiles_loaded = 0;
			for (i32 tile_y = camera_tile_y1; tile_y < camera_tile_y2; ++tile_y) {
				for (i32 tile_x = camera_tile_x1; tile_x < camera_tile_x2; ++tile_x) {
					if (tiles_loaded >= max_tiles_to_load_at_once) {
//						 TODO: remove this performance hack after background loading (multithreaded) is implemented.
						break;
					} else {
						tiles_loaded += wsi_load_tile(wsi, current_level, tile_x, tile_y);
					}
				}
			}
		}

		mat4x4 projection = {};
		{
			float l = -0.5f*r_minus_l;
			float r = +0.5f*r_minus_l;
			float b = -0.5f*t_minus_b;
			float t = +0.5f*t_minus_b;
			float n = 100.0f;
			float f = -100.0f;
			mat4x4_ortho(projection, l, r, b, t, n, f);
		}

		mat4x4 I;
		mat4x4_identity(I);

		// define view matrix
		mat4x4 view_matrix;
		mat4x4_translate(view_matrix, -camera_pos.x, -camera_pos.y, 0.0f);

		mat4x4 projection_view_matrix;
		mat4x4_mul(projection_view_matrix, projection, view_matrix);

		glUseProgram(basic_shader);
		glUniformMatrix4fv(basic_shader_u_projection_view_matrix, 1, GL_FALSE, &projection_view_matrix[0][0]);

		glUniform3fv(basic_shader_u_background_color, 1, (GLfloat*)&clear_color);
		if (use_image_adjustments) {
			glUniform1f(basic_shader_u_black_level, black_level);
			glUniform1f(basic_shader_u_white_level, white_level);
		} else {
			glUniform1f(basic_shader_u_black_level, 0.0f);
			glUniform1f(basic_shader_u_white_level, 1.0f);
		}

		i32 num_levels_above_current = wsi->num_levels - current_level - 1;
		ASSERT(num_levels_above_current>=0);

		// Draw all levels within the viewport, up to the current zoom factor
		for (i32 level = wsi->num_levels - 1; level >= current_level; --level) {
			wsi_level_t* drawn_level = wsi->levels + level;

			i32 level_camera_tile_x1 = tile_pos_from_world_pos(camera_rect_x1, drawn_level->x_tile_side_in_um);
			i32 level_camera_tile_x2 = tile_pos_from_world_pos(camera_rect_x2, drawn_level->x_tile_side_in_um) + 1;
			i32 level_camera_tile_y1 = tile_pos_from_world_pos(camera_rect_y1, drawn_level->y_tile_side_in_um);
			i32 level_camera_tile_y2 = tile_pos_from_world_pos(camera_rect_y2, drawn_level->y_tile_side_in_um) + 1;

			level_camera_tile_x1 = CLAMP(level_camera_tile_x1, 0, drawn_level->width_in_tiles);
			level_camera_tile_x2 = CLAMP(level_camera_tile_x2, 0, drawn_level->width_in_tiles);
			level_camera_tile_y1 = CLAMP(level_camera_tile_y1, 0, drawn_level->height_in_tiles);
			level_camera_tile_y2 = CLAMP(level_camera_tile_y2, 0, drawn_level->height_in_tiles);

			for (i32 tile_y = level_camera_tile_y1; tile_y < level_camera_tile_y2; ++tile_y) {
				for (i32 tile_x = level_camera_tile_x1; tile_x < level_camera_tile_x2; ++tile_x) {

					wsi_tile_t* tile = wsi_get_tile(drawn_level, tile_x, tile_y);
					if (tile->texture) {
						u32 texture = get_texture_for_tile(wsi, level, tile_x, tile_y);

						float tile_pos_x = drawn_level->x_tile_side_in_um * tile_x;
						float tile_pos_y = drawn_level->y_tile_side_in_um * tile_y;

						// define model matrix
						mat4x4 model_matrix;
						mat4x4_translate(model_matrix, tile_pos_x, tile_pos_y, 0.0f);
						mat4x4_scale_aniso(model_matrix, model_matrix, drawn_level->x_tile_side_in_um, drawn_level->y_tile_side_in_um, 1.0f);
						glUniformMatrix4fv(basic_shader_u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);

						draw_rect(texture);
					}

				}
			}

		}

	}



}

