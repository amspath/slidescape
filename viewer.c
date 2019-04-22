#include "common.h"

//#include "win32_main.h"
#include "platform.h"

#include "openslide_api.h"
#include <glad/glad.h>
#include <linmath.h>

#include "arena.h"
#include "arena.c"

#include "viewer.h"

#define STBI_ASSERT(x) ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "render_group.h"
#include "render_group.c"


//#define MAX_ENTITIES 4096
//u32 entity_count = 1;
//entity_t entities[MAX_ENTITIES];

v2f camera_pos;


viewer_t global_viewer;

wsi_t global_wsi;
i32 current_level;


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

bool32 load_texture_from_file(texture_t* texture, const char* filename) {
	bool32 result = false;
	i32 channels_in_file = 0;
	u8* pixels = stbi_load(filename, &texture->width, &texture->height, &channels_in_file, 4);
	if (pixels) {
		glGenTextures(1, &texture->texture);
		glBindTexture(GL_TEXTURE_2D, texture->texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		GLenum format = GL_RGBA;
		glTexImage2D(GL_TEXTURE_2D, 0, format, texture->width, texture->height, 0, format, GL_UNSIGNED_BYTE, pixels);

		result = true;
		stbi_image_free(pixels);
	}
	return result;
}


u32 load_texture(void* pixels, i32 width, i32 height) {
	u32 texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	GLenum format = GL_RGBA;
	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
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

i32 wsi_load_tile(wsi_t* wsi, i32 level, i32 tile_x, i32 tile_y) {
	wsi_level_t* wsi_level = wsi->levels + level;
	wsi_tile_t* tile = get_tile(wsi_level, tile_x, tile_y);

	if (tile->texture != 0) {
		// Yay, this tile is already loaded
		return 0;
	} else {
		tile->block = alloc_wsi_block(&global_viewer.slide_memory);

		u32* pixel_data = get_wsi_block(&global_viewer.slide_memory, tile->block);
		ASSERT(pixel_data);

		// OpenSlide still counts the coordinates of downsampled images as if they're the full level 0 image.
		i64 x = (tile_x * TILE_DIM) << level;
		i64 y = (tile_y * TILE_DIM) << level;
		openslide.openslide_read_region(wsi->osr, pixel_data, x, y, level, TILE_DIM, TILE_DIM);

		tile->texture = load_texture(pixel_data, TILE_DIM, TILE_DIM);

		// TODO: remove this hack once we have a better strategy for memory management
		global_viewer.slide_memory.blocks_in_use--; // deallocate

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

		for (i32 i = 0; i < wsi->num_levels; ++i) {
			wsi_level_t* level = wsi->levels + i;

			openslide.openslide_get_level_dimensions(wsi->osr, i, &level->width, &level->height);
			ASSERT(level->width > 0);
			ASSERT(level->height > 0);
			i64 partial_block_x = level->width % TILE_DIM;
			i64 partial_block_y = level->height % TILE_DIM;
			level->width_in_tiles = (i32)(level->width / TILE_DIM) + (partial_block_x != 0);
			level->height_in_tiles = (i32)(level->height / TILE_DIM) + (partial_block_y != 0);
			level->num_tiles = level->width_in_tiles * level->height_in_tiles;
			level->tiles = calloc(1, level->num_tiles * sizeof(wsi_tile_t));
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

		camera_pos.x = (wsi->width * wsi->mpp_x) / 2.0f;
		camera_pos.y = (wsi->height * wsi->mpp_y) / 2.0f;

	}

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

float um_per_screen_pixel(float mpp, i32 level) {
	float result = (float)(1 << level) * mpp;
	return result;
}

i32 tile_pos_from_world_pos(float world_pos, float tile_side) {
	ASSERT(tile_side > 0);
	float tile_float = (world_pos / tile_side);
	float tile = (i32)floorf(tile_float);
	return tile;
}

i32 debug_current_image_index = 1;

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
	// TODO: fix: should return true if key is held down continuously
	bool32 result = input->keyboard.keys[key].down;
	return result;
}

bool32 use_image_adjustments;
float black_level = (10.0f / 255.0f);
float white_level = (230.0f / 255.0f);

void viewer_update_and_render(input_t* input, i32 client_width, i32 client_height) {
	glViewport(0, 0, client_width, client_height);
	glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	if (!global_wsi.osr) {
		return;
	} else {

		if (input) {

//	        i32 dlevel = 0 + control_forward - control_back;
			i32 dlevel = input->mouse_z != 0 ? (input->mouse_z > 0 ? -1 : 1) : 0;


			if (dlevel != 0) {
//		        printf("mouse_z = %d\n", input->mouse_z);
				if (global_wsi.osr) {

					current_level += dlevel;
					current_level = CLAMP(current_level, 0, global_wsi.num_levels-1);
				}
			}
		}

		wsi_level_t* wsi_level = global_wsi.levels + current_level;
		float um_per_pixel_x = um_per_screen_pixel(global_wsi.mpp_x, current_level);
		float um_per_pixel_y = um_per_screen_pixel(global_wsi.mpp_y, current_level);

		float x_tile_side_in_um = um_per_pixel_x * (float)TILE_DIM;
		float y_tile_side_in_um = um_per_pixel_y * (float)TILE_DIM;

		float r_minus_l = um_per_pixel_x * (float)client_width;
		float t_minus_b = um_per_pixel_y * (float)client_height;

		float camera_rect_x1 = camera_pos.x - r_minus_l * 0.5f;
		float camera_rect_x2 = camera_pos.x + r_minus_l * 0.5f;
		float camera_rect_y1 = camera_pos.y - t_minus_b * 0.5f;
		float camera_rect_y2 = camera_pos.y + t_minus_b * 0.5f;

		i32 camera_tile_x1 = tile_pos_from_world_pos(camera_rect_x1, x_tile_side_in_um);
		i32 camera_tile_x2 = tile_pos_from_world_pos(camera_rect_x2, x_tile_side_in_um) + 1;
		i32 camera_tile_y1 = tile_pos_from_world_pos(camera_rect_y1, y_tile_side_in_um);
		i32 camera_tile_y2 = tile_pos_from_world_pos(camera_rect_y2, y_tile_side_in_um) + 1;

		camera_tile_x1 = CLAMP(camera_tile_x1, 0, wsi_level->width_in_tiles);
		camera_tile_x2 = CLAMP(camera_tile_x2, 0, wsi_level->width_in_tiles);
		camera_tile_y1 = CLAMP(camera_tile_y1, 0, wsi_level->height_in_tiles);
		camera_tile_y2 = CLAMP(camera_tile_y2, 0, wsi_level->height_in_tiles);

		if (input) {

			if (was_key_pressed(input, 'P')) {
				use_image_adjustments = !use_image_adjustments;
			}

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

			if (input->mouse_buttons[1].down) {
				DUMMY_STATEMENT;
			}

			if (input->mouse_buttons[0].down) {
				// do the drag
				camera_pos.x -= input->drag_vector.x * um_per_pixel_x;
				camera_pos.y += input->drag_vector.y * um_per_pixel_y;
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
						// TODO: remove this performance hack after background loading (multithreaded) is implemented.
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

		for (i32 tile_y = camera_tile_y1; tile_y < camera_tile_y2; ++tile_y) {
			for (i32 tile_x = camera_tile_x1; tile_x < camera_tile_x2; ++tile_x) {

				wsi_tile_t* tile = get_tile(wsi_level, tile_x, tile_y);
				if (tile->texture) {
					u32 texture = get_texture_for_tile(&global_wsi, current_level, tile_x, tile_y);

					float tile_pos_x = x_tile_side_in_um * tile_x;
					float tile_pos_y = y_tile_side_in_um * tile_y;

					// define model matrix
					mat4x4_translate(T, tile_pos_x, tile_pos_y, 0.0f);
					mat4x4_scale_aniso(S, I, x_tile_side_in_um, y_tile_side_in_um, 1.0f);
					mat4x4_mul(M, T, S);
					glUniformMatrix4fv(glGetUniformLocation(basic_shader, "model"), 1, GL_FALSE, &M[0][0]);

					draw_rect(texture);
				}

			}
		}
	}



}

