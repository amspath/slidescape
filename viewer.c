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


#define MAX_ENTITIES 4096
u32 entity_count = 1;
entity_t entities[MAX_ENTITIES];

static image_t images[MAX_ENTITIES];
u32 image_count;

v2f camera_pos;


viewer_t global_viewer;

wsi_t global_wsi;
i32 current_level;



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

void add_tile_entity(image_t* image, v2i pos) {
	entity_t* entity = &entities[entity_count++];
	*entity = (entity_t) {
		.active = true,
		.pos = pos,
		.image = image,
	};
}

#define FLOAT_TO_BYTE(x) ((u8)(255.0f * CLAMP((x), 0.0f, 1.0f)))
#define BYTE_TO_FLOAT(x) CLAMP(((float)((x & 0x0000ff))) /255.0f, 0.0f, 1.0f)
#define TO_BGRA(r,g,b,a) ((a) << 24 | (r) << 16 | (g) << 8 | (b) << 0)

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
bool32 load_texture_from_file(texture_t* texture, const char* filename) {
	bool32 result = false;
	i32 channels_in_file = 0;
	u8* pixels = stbi_load(filename, &texture->width, &texture->height, &channels_in_file, 4);
	if (pixels) {
		glGenTextures(1, &texture->texture);
		glBindTexture(GL_TEXTURE_2D, texture->texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	GLenum format = GL_RGBA;
	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
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

v2i wsi_tile_world_position(wsi_level_t* wsi_level, u32 tile_x, u32 tile_y) {
	v2i result = { -wsi_level->width/2 + tile_x * TILE_DIM, -wsi_level->height/2 + tile_y * TILE_DIM };
	return result;
}

i32 wsi_load_tile(wsi_t* wsi, i32 level, i32 tile_x, i32 tile_y) {
	wsi_level_t* wsi_level = wsi->levels + level;

	i32 tile_index = tile_y * wsi_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < wsi_level->num_tiles);
	wsi_tile_t* tile = wsi_level->tiles + tile_index;

	if (tile->block != 0) {
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
		return 1;
	}
}

void wsi_load_blocks_in_region(wsi_t* wsi, i32 level, v2i xy_tile_min, v2i xy_tile_max) {

	i32 debug_tiles_loaded = 0;
	i64 debug_start = get_clock();

	for (i32 tile_y = xy_tile_min.y; tile_y <= xy_tile_max.y; ++tile_y) {
		for (i32 tile_x = xy_tile_min.x; tile_x <= xy_tile_max.x; ++tile_x) {
			debug_tiles_loaded += wsi_load_tile(wsi, level, tile_x, tile_y);
			if (debug_tiles_loaded > 1) {
				return;
			}
		}
	}

	if (debug_tiles_loaded > 0) {
		printf("Level %d: loaded %d tiles in %g seconds.\n", level, debug_tiles_loaded, get_seconds_elapsed(debug_start, get_clock()));
	}

}


void load_wsi(wsi_t* wsi, char* filename) {
	if (wsi->osr) {
		openslide.openslide_close(wsi->osr);
		wsi->osr = NULL;
	}

	global_viewer.slide_memory.blocks_in_use = 1;
	image_count = 0;
	entity_count = 1;

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
//		load_wsi_level(wsi->osr, current_level);

	}

}


void on_file_dragged(char* filename) {
#if 0
	current_image = loaded_images;
	load_image(&loaded_images[0], filename);
#else

	load_wsi(&global_wsi, filename);

#endif

}



#if 0
void render_weird_gradient(surface_t *surface, int x_offset, int y_offset) {
	u8* row = (u8*) surface->memory;
	for (int y = 0; y < surface->height; ++y) {
		u32* pixel = (u32*) row;
		for (int x = 0; x < surface->width; ++x) {
			// little endian: BB GG RR xx
			u8 blue = (u8) (y + y_offset); // blue channel
			u8 green = (u8) (-x / 2 + x_offset); // green channel
			u8 red = (u8) (x + x_offset); // red channel
			*pixel = (blue << 0 | (green ^ blue) << 8 | red << 16);
			pixel += 1;
		}
		row += surface->pitch;
	}
}
#endif


image_t debug_image;
texture_t debug_texture;

void first() {
	i64 wsi_memory_size = GIGABYTES(1);
	global_viewer.slide_memory.data = platform_alloc(wsi_memory_size);
	global_viewer.slide_memory.capacity = wsi_memory_size;
	global_viewer.slide_memory.capacity_in_blocks = wsi_memory_size / WSI_BLOCK_SIZE;
	global_viewer.slide_memory.blocks_in_use = 1;

/*	size_t transient_storage_size = MEGABYTES(8);
	global_viewer.transient_arena = (arena_t) {
		.base = platform_alloc(transient_storage_size),
		.size = transient_storage_size,
	};*/

	init_opengl_stuff();

	//debug
//	if (!load_texture_from_file(&debug_texture, "data/transparent_cat.png")) {
//		panic();
//	}

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

i32 debug_current_image_index = 1;

void viewer_update_and_render(input_t* input, i32 client_width, i32 client_height) {

	bool32 control_back = false;
	bool32 control_forward = false;
	if (input->mouse_buttons[4].down && input->mouse_buttons[4].transition_count > 0) {
		control_back = true;
	}
	if (input->mouse_buttons[5].down && input->mouse_buttons[5].transition_count > 0) {
		control_forward = true;
	}
//	i32 dlevel = 0 + control_forward - control_back;
	i32 dlevel = input->mouse_z != 0 ? (input->mouse_z > 0 ? -1 : 1) : 0;


//	i32 dimage_index = input->mouse_z != 0 ? (input->mouse_z > 0 ? -1 : 1) : 0;
	i32 dimage_index = 0;
	debug_current_image_index = CLAMP(debug_current_image_index + dimage_index, 1, entity_count-1);

	if (dlevel != 0) {
//		printf("mouse_z = %d\n", input->mouse_z);
		if (global_wsi.osr) {

			current_level += dlevel;
			current_level = CLAMP(current_level, 0, global_wsi.num_levels-1);

#if 0
			load_wsi_level(global_wsi.osr, current_level);
#endif
		}
	}

	float um_per_pixel_x = um_per_screen_pixel(global_wsi.mpp_x, current_level);
	float um_per_pixel_y = um_per_screen_pixel(global_wsi.mpp_y, current_level);

	if (input->mouse_buttons[0].down) {
		// do the drag
		camera_pos.x -= input->drag_vector.x * um_per_pixel_x;
		camera_pos.y += input->drag_vector.y * um_per_pixel_y;
		input->drag_vector = (v2i){};
		mouse_hide();
	} else {
		mouse_show();
		if (input->mouse_buttons[0].transition_count != 0) {
//			printf("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
		}
	}


#if 1


	if (global_wsi.osr) {
		wsi_load_tile(&global_wsi, current_level, 0, 0);
	}



	glClearColor(0.85f, 0.85f, 0.85f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	mat4x4 projection = {};
	{
		float r_minus_l = um_per_pixel_x * (float)client_width;
		float t_minus_b = um_per_pixel_y * (float)client_height;

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
	// define model matrix
	mat4x4_translate(T, 0.0f, 0.0f, 0.0f);
	mat4x4_scale_aniso(S, I, um_per_pixel_x * (float)TILE_DIM, um_per_pixel_y * (float)TILE_DIM, 1.0f);
	mat4x4_mul(M, T, S);

	// define view matrix
	mat4x4_translate(V, -camera_pos.x, -camera_pos.y, 0.0f);

	glUniformMatrix4fv(glGetUniformLocation(basic_shader, "model"), 1, GL_FALSE, &M[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(basic_shader, "view"), 1, GL_FALSE, &V[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(basic_shader, "projection"), 1, GL_FALSE, &projection[0][0]);

	draw_rect(0);


#elif 0
	v4f clear_color = {0.85f, 0.85f, 0.85f, 1.0f};
	clear_surface(surface, &clear_color);

//	rect2i client_rect = {-client_width/2, -client_height/2, client_width, client_height};
	rect2i viewport_rect = {camera_pos.x - client_width/2, camera_pos.y - client_height/2, client_width, client_height};

//	rect2i viewport_rect = {-client_width/2, -client_height/2, client_width, client_height};
//	rect2i screen_rect = {0, 0, client_width, client_height};
//	v2i camera_offset = { camer.x - screen_rect.x, viewport_rect.y - screen_rect.y };


	rect2i region = viewport_rect;
	region.x -= camera_pos.x;
	region.y -= camera_pos.y;

	rect2i clip_space;
	clip_space.x = LOWERBOUND(region.x << current_level, 0);
	clip_space.y = LOWERBOUND(region.y << current_level, 0);
	clip_space.w = viewport_rect.w << current_level;
	clip_space.h = viewport_rect.h << current_level;

	region = clip_rect(&region, &clip_space);


	wsi_level_t* wsi_level = global_wsi.levels + current_level;

	v2i xy_tile_min = { region.x / TILE_DIM, region.y / TILE_DIM };
	v2i xy_tile_max = { (region.x + (clip_space.w - 1) / TILE_DIM),
	                    (region.y + (clip_space.h - 1) / TILE_DIM) };

	ASSERT(xy_tile_min.x >= 0);
	ASSERT(xy_tile_min.y >= 0);
	xy_tile_max.x = UPPERBOUND(xy_tile_max.x, wsi_level->width_in_tiles-1);
	xy_tile_max.y = UPPERBOUND(xy_tile_max.y, wsi_level->height_in_tiles-1);

	wsi_load_blocks_in_region(&global_wsi, current_level, xy_tile_min, xy_tile_max);

	v2i camera_offset = { camera_pos.x - client_width/2, camera_pos.y - client_height/2 };

	for (i32 tile_y = xy_tile_min.y; tile_y <= xy_tile_max.y; ++tile_y) {
		for (i32 tile_x = xy_tile_min.x; tile_x <= xy_tile_max.x; ++tile_x) {
			i32 tile_index = tile_y * wsi_level->width_in_tiles + tile_x;
			ASSERT(tile_index >= 0 && tile_index < wsi_level->num_tiles);
			wsi_tile_t* tile = wsi_level->tiles + tile_index;

			if (tile->block != 0) {
				// memory is allocated
				u32* pixel_data = get_wsi_block(&global_viewer.slide_memory, tile->block);
				v2i pos = wsi_tile_world_position(wsi_level, tile_x, tile_y);

				rect2i screen_rect = {0, 0, client_width, client_height};

				rect2i image_rect = { pos.x - camera_offset.x, pos.y - camera_offset.y, TILE_DIM, TILE_DIM };
				rect2i clipped_rect = clip_rect(&image_rect, &screen_rect);
				v2i image_offset = { clipped_rect.x - image_rect.x, clipped_rect.y - image_rect.y };

				i32 start_x = clipped_rect.x;
				i32 start_y = clipped_rect.y;
				i32 end_x = clipped_rect.x + clipped_rect.w;// - camera_offset.x;
				i32 end_y = clipped_rect.y + clipped_rect.h;// - camera_offset.y;


//			printf("offset: %d %d\n", image_offset.x,image_offset.y);


				u8* surface_row = (u8*) surface->memory + (start_y * surface->pitch) + start_x * BYTES_PER_PIXEL;
				u8* image_row = (u8*)pixel_data;// + (TILE_DIM - 1) * TILE_PITCH; // start on the last row

				for (int y = start_y; y < end_y; ++y) {
					u32* surface_pixel = (u32*) surface_row;
					u32* image_pixel = (u32*) image_row + image_offset.y * TILE_DIM + image_offset.x;
					for (int x = start_x; x < end_x; ++x) {
						u32 color = *image_pixel++;
						u8 a = (u8) (color >> 24);
						u8 b = (u8) (color >> 16);
						u8 g = (u8) (color >> 8);
						u8 r = (u8) (color);

						*surface_pixel = (a << 24 | r << 16 | g << 8 | b << 0);
						surface_pixel += 1;
					}
					surface_row += surface->pitch;
					image_row += TILE_PITCH;
				}
			}
		}
	}
#else
	for (u32 entity_id = 1; entity_id < entity_count; ++entity_id) {
		entity_t* entity = entities + entity_id;
		if (!entity->active) continue;

		if (entity_id != debug_current_image_index) {
			continue;
		}

		image_t* image = entity->image;
		if (image && image->data) {


			rect2i image_rect = {entity->pos.x,
			                     entity->pos.y,
			                     image->width, image->height
			};
			rect2i clipped_rect = clip_rect(&image_rect, &viewport_rect);

			i32 start_x = clipped_rect.x - camera_offset.x;
			i32 start_y = clipped_rect.y - camera_offset.y;
			i32 end_x = clipped_rect.x + clipped_rect.w - camera_offset.x;
			i32 end_y = clipped_rect.y + clipped_rect.h - camera_offset.y;

			v2i image_offset = { clipped_rect.x - image_rect.x, clipped_rect.y - image_rect.y };
//			printf("offset: %d %d\n", image_offset.x,image_offset.y);


			u8* surface_row = (u8*) surface->memory + (start_y * surface->pitch) + start_x * BYTES_PER_PIXEL;
			u8* image_row = image->data + (image->height - 1) * image->pitch; // start on the last row

			for (int y = start_y; y < end_y; ++y) {
				u32* surface_pixel = (u32*) surface_row;
				u32* image_pixel = (u32*) image_row - image_offset.y * image->width + image_offset.x;
				for (int x = start_x; x < end_x; ++x) {
					u32 color = *image_pixel++;
					u8 a = (u8) (color >> 24);
					u8 b = (u8) (color >> 16);
					u8 g = (u8) (color >> 8);
					u8 r = (u8) (color);

					*surface_pixel = (a << 24 | r << 16 | g << 8 | b << 0);
					surface_pixel += 1;
				}
				surface_row += surface->pitch;
				image_row -= image->pitch;
			}
		}
	}
#endif

}

