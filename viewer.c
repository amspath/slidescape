#include "common.h"

//#include "win32_main.h"
#include "platform.h"
#include "intrinsics.h"

#include "openslide_api.h"
#include <glad/glad.h>
#include <linmath.h>

#include "arena.h"
#include "arena.c"

#include "viewer.h"

#define STBI_ASSERT(x) ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "shader.c"

#include "render_group.h"
#include "render_group.c"


//#define MAX_ENTITIES 4096
//u32 entity_count = 1;
//entity_t entities[MAX_ENTITIES];

v2f camera_pos;


viewer_t global_viewer;

wsi_t global_wsi;
i32 current_level;
float zoom_position;

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


/*
bool32 load_texture_from_file(texture_t* texture, const char* filename) {
	bool32 result = false;
	i32 channels_in_file = 0;
	u8* pixels = stbi_load(filename, &texture->width, &texture->height, &channels_in_file, 4);
	if (pixels) {
		glGenTextures(1, &texture->texture);
//		texture->texture = gl_gen_texture();
		glBindTexture(GL_TEXTURE_2D, texture->texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture->width, texture->height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

		result = true;
		stbi_image_free(pixels);
	}
	return result;
}
*/



u32 load_texture(void* pixels, i32 width, i32 height) {
	u32 texture = 0; //gl_gen_texture();
	glGenTextures(1, &texture);
//	printf("Generated texture %d\n", texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	gl_diagnostic("glTexImage2D");
	return texture;
}

u32 alloc_wsi_block(slide_memory_t* slide_memory) {
	if (slide_memory->blocks_in_use == slide_memory->capacity_in_blocks) {
		panic(); // TODO: Need to free some stuff, we ran out of memory
	}
	u32 result = slide_memory->blocks_in_use++;
	return result;
}

void* get_wsi_block(slide_memory_t* slide_memory, u32 block_index) {
	u8* block = slide_memory->data + (block_index * WSI_BLOCK_SIZE);
	return block;
}

wsi_tile_t* get_tile(wsi_level_t* wsi_level, i32 tile_x, i32 tile_y) {
	i32 tile_index = tile_y * wsi_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < wsi_level->num_tiles);
	wsi_tile_t* result = wsi_level->tiles + tile_index;
	return result;
}



void load_tile_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_t* task_data = (load_tile_task_t*) userdata;
	wsi_t* wsi = task_data->wsi;
	i32 level = task_data->level;
	i32 tile_x = task_data->tile_x;
	i32 tile_y = task_data->tile_y;
	wsi_level_t* wsi_level = wsi->levels + level;
	wsi_tile_t* tile = get_tile(wsi_level, tile_x, tile_y);

	u32* temp_memory = malloc(WSI_BLOCK_SIZE);
	i64 x = (tile_x * TILE_DIM) << level;
	i64 y = (tile_y * TILE_DIM) << level;
	openslide.openslide_read_region(wsi->osr, temp_memory, x, y, level, TILE_DIM, TILE_DIM);

//	printf("[thread %d] Loaded tile: level=%d tile_x=%d tile_y=%d\n", logical_thread_index, level, tile_x, tile_y);

	task_data->cached_pixels = temp_memory;

	// do the submitting directly
	tile->texture = load_texture(task_data->cached_pixels, TILE_DIM, TILE_DIM);
	glFinish();
	free(task_data->cached_pixels);
	task_data->cached_pixels = NULL;
	free(task_data);
	write_barrier;
	tile->is_loading_complete = true;

}

extern work_queue_t work_queue;

void enqueue_load_tile(wsi_t* wsi, i32 level, i32 tile_x, i32 tile_y) {
	load_tile_task_t* task_data = malloc(sizeof(load_tile_task_t)); // should be freed after uploading the tile to the gpu
	*task_data = (load_tile_task_t){ .wsi = wsi, .level = level, .tile_x = tile_x, .tile_y = tile_y };

	add_work_queue_entry(&work_queue, load_tile_func, task_data);

}

i32 wsi_load_tile(wsi_t* wsi, i32 level, i32 tile_x, i32 tile_y) {
	wsi_level_t* wsi_level = wsi->levels + level;
	wsi_tile_t* tile = get_tile(wsi_level, tile_x, tile_y);

	if (tile->texture != 0) {
		// Yay, this tile is already loaded
		return 0;
	} else {
		read_barrier;
		if (tile->is_submitted_for_loading) {
			DUMMY_STATEMENT;
#if 0
			load_tile_task_t* task_data = tile->load_task_data;
//			printf("encountered a pre-cached tile: level=%d tile_x=%d tile_y=%d\n", level, tile_x, tile_y);

			tile->texture = load_texture(task_data->cached_pixels, TILE_DIM, TILE_DIM);
			free(task_data->cached_pixels);
			task_data->cached_pixels = NULL;
			free(task_data);
			tile->load_task_data = NULL;
#endif
		} else {
			tile->is_submitted_for_loading = true;
			enqueue_load_tile(wsi, level, tile_x, tile_y);

		}


//		printf("Loaded tile: level=%d tile_x=%d tile_y=%d block=%u texture=%u\n", level, tile_x, tile_y, tile->block, tile->texture);
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

void load_wsi(wsi_t* wsi, char* filename) {
	if (!is_openslide_loading_done) {
		printf("Waiting for OpenSlide to finish loading...\n");
		platform_wait_for_boolean_true(&is_openslide_loading_done);
	}

	read_barrier;
	if (!is_openslide_available) {
		char message[4096];
		snprintf(message, sizeof(message), "Could not open \"%s\":\nlibopenslide-0.dll is missing or broken.\n", filename);
		message_box(message);
		return;
	}


	if (wsi->osr) {
		openslide.openslide_close(wsi->osr);
		wsi->osr = NULL;
	}

	global_viewer.slide_memory.blocks_in_use = 1;

	wsi->osr = openslide.openslide_open(filename);
	if (wsi->osr) {
		printf("Openslide: opened %s\n", filename);

		openslide.openslide_get_level0_dimensions(wsi->osr, &wsi->width, &wsi->height);
		ASSERT(wsi->width > 0);
		ASSERT(wsi->height > 0);

		wsi->width_pow2 = next_pow2((u64)wsi->width);
		wsi->height_pow2 = next_pow2((u64)wsi->height);

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

	global_viewer.slide_memory.blocks_in_use = 1;

//	for (int i = 0; i < texture_count; ++i) {
//		unload_texture();
//	}
}


void on_file_dragged(char* filename) {
#if 0
	current_image = loaded_images;
	load_image(&loaded_images[0], filename);
#else
	// TODO: allow loading either a normal image or a WSI (we should be able to function as a normal image viewer!)
	load_wsi(&global_wsi, filename);
#endif
}

void first() {
	// TODO: remove or change this.
	i64 wsi_memory_size = GIGABYTES(1);
	global_viewer.slide_memory.data = platform_alloc(wsi_memory_size);
	global_viewer.slide_memory.capacity = wsi_memory_size;
	global_viewer.slide_memory.capacity_in_blocks = wsi_memory_size / WSI_BLOCK_SIZE;
	global_viewer.slide_memory.blocks_in_use = 1;

	init_opengl_stuff();

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

bool32 use_image_adjustments;
float black_level = (10.0f / 255.0f);
float white_level = (230.0f / 255.0f);
i64 zoom_in_key_hold_down_start_time;
i64 zoom_in_key_times_zoomed_while_holding;
i64 zoom_out_key_hold_down_start_time;
i64 zoom_out_key_times_zoomed_while_holding;

void viewer_update_and_render(input_t* input, i32 client_width, i32 client_height) {
	glViewport(0, 0, client_width, client_height);
	glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	if (!global_wsi.osr) {
		return;
	} else {

		i32 old_level = current_level;
		i32 center_offset_x = 0;
		i32 center_offset_y = 0;

		i32 max_level = global_wsi.num_levels - 1;
		wsi_level_t* wsi_level = global_wsi.levels + current_level;

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
				current_level = CLAMP(current_level + dlevel, 0, global_wsi.num_levels-1);
				wsi_level = global_wsi.levels + current_level;

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





		// Spring/bounce effect
		float d_zoom = (float)current_level - zoom_position;
		float abs_d_zoom = fabs(d_zoom);
		float sign_d_zoom = signbit(d_zoom) ? -1.0f : 1.0f;
		float linear_catch_up_speed = 0.15f;
		float exponential_catch_up_speed = 0.3f;
		if (abs_d_zoom > linear_catch_up_speed) {
			d_zoom = (linear_catch_up_speed + (abs_d_zoom - linear_catch_up_speed)*exponential_catch_up_speed) * sign_d_zoom;
		}
		zoom_position += d_zoom;

		float screen_um_per_pixel_x = powf(2.0f, zoom_position) * global_wsi.mpp_x;
		float screen_um_per_pixel_y = powf(2.0f, zoom_position) * global_wsi.mpp_y;

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

			// NOTE: temporary experimentation with basic image adjustments.
			if (is_key_down(input, 'J')) {
				black_level -= 0.1f;
				black_level = CLAMP(black_level, 0.0f, white_level - 0.1f);
			}

			if (is_key_down(input, 'U')) {
				black_level += 0.1f;
				black_level = CLAMP(black_level, 0.0f, white_level - 0.1f);
			}

			if (is_key_down(input, 'L')) {
				white_level -= 0.1f;
				white_level = CLAMP(white_level, black_level + 0.1f, 1.0f);
			}

			if (is_key_down(input, 'O')) {
				white_level += 0.1f;
				white_level = CLAMP(white_level, black_level + 0.1f, 1.0f);
			}

			// Experimental code for exporting regions of the wsi to a raw image file.
			if (input->mouse_buttons[1].down && input->mouse_buttons[1].transition_count > 0) {
				DUMMY_STATEMENT;
				i32 click_x = (camera_rect_x1 + input->mouse_xy.x * wsi_level->um_per_pixel_x) / global_wsi.mpp_x;
				i32 click_y = (camera_rect_y2 - input->mouse_xy.y * wsi_level->um_per_pixel_y) / global_wsi.mpp_y;
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
					openslide.openslide_read_region(global_wsi.osr, temp_memory, p1.x, p1.y, 0, w, h);
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
			float panning_speed = 15.0f * panning_multiplier;
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

			if (input->mouse_buttons[0].down) {
				// Mouse drag.
				camera_pos.x -= input->drag_vector.x * wsi_level->um_per_pixel_x * panning_multiplier;
				camera_pos.y += input->drag_vector.y * wsi_level->um_per_pixel_y * panning_multiplier;
				input->drag_vector = (v2i){};
				mouse_hide();
			} else {
				mouse_show();
				if (input->mouse_buttons[0].transition_count != 0) {
//			    printf("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
				}
			}

			i32 max_tiles_to_load_at_once = 5;
			i32 tiles_loaded = 0;
			for (i32 tile_y = camera_tile_y1; tile_y < camera_tile_y2; ++tile_y) {
				for (i32 tile_x = camera_tile_x1; tile_x < camera_tile_x2; ++tile_x) {
					if (tiles_loaded >= max_tiles_to_load_at_once) {
//						 TODO: implement queue reprioritization and remove this performance hack.
						break;
					} else {
						tiles_loaded += wsi_load_tile(&global_wsi, current_level, tile_x, tile_y);
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

		mat4x4 M, V, I, T, S;
		mat4x4_identity(I);

		// define view matrix
		mat4x4_translate(V, -camera_pos.x, -camera_pos.y, 0.0f);

		glUniformMatrix4fv(glGetUniformLocation(basic_shader, "view"), 1, GL_FALSE, &V[0][0]);
		glUniformMatrix4fv(glGetUniformLocation(basic_shader, "projection"), 1, GL_FALSE, &projection[0][0]);


		if (use_image_adjustments) {
			glUniform1f(glGetUniformLocation(basic_shader, "black_level"), black_level);
			glUniform1f(glGetUniformLocation(basic_shader, "white_level"), white_level);
		} else {
			glUniform1f(glGetUniformLocation(basic_shader, "black_level"), 0.0f);
			glUniform1f(glGetUniformLocation(basic_shader, "white_level"), 1.0f);
		}

		i32 num_levels_above_current = global_wsi.num_levels - current_level - 1;
		ASSERT(num_levels_above_current>=0);

		// Draw all levels within the viewport, up to the current zoom factor
		for (i32 level = global_wsi.num_levels - 1; level >= current_level; --level) {
			wsi_level_t* drawn_level = global_wsi.levels + level;

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

					wsi_tile_t* tile = get_tile(drawn_level, tile_x, tile_y);
					// TODO: better synchronization between worker threads and main thread!!
					read_barrier;
					if (tile->is_loading_complete) {
						u32 texture = get_texture_for_tile(&global_wsi, level, tile_x, tile_y);

						float tile_pos_x = drawn_level->x_tile_side_in_um * tile_x;
						float tile_pos_y = drawn_level->y_tile_side_in_um * tile_y;

						// define model matrix
						mat4x4_translate(T, tile_pos_x, tile_pos_y, 0.0f);
						mat4x4_scale_aniso(S, I, drawn_level->x_tile_side_in_um, drawn_level->y_tile_side_in_um, 1.0f);
						mat4x4_mul(M, T, S);
						glUniformMatrix4fv(glGetUniformLocation(basic_shader, "model"), 1, GL_FALSE, &M[0][0]);

						draw_rect(texture);
					}

				}
			}

		}

	}



}

