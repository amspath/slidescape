#include "common.h"

#include "win32_main.h"
#include "platform.h"

#include "openslide_api.h"

#include "viewer.h"

#define STBI_ASSERT(x) ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "arena.h"
#include "arena.c"

#include "render_group.h"
#include "render_group.c"


#define MAX_ENTITIES 4096
u32 entity_count = 1;
entity_t entities[MAX_ENTITIES];

static image_t images[MAX_ENTITIES];
u32 image_count;

v2i camera_pos;


viewer_t global_viewer;

u32* wsi_memory;
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

/*void init_image(image_t* image, void* data, i32 width, i32 height) {
	if (image->data != NULL) free(image->data);
	*image = (image_t) { .data = data, .width = width, .height = height, .pitch = width * 4, };
}*/

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






/*void load_wsi_level(openslide_t* osr, i32 level) {
	i32 loaded_level = CLAMP(level, 0, openslide.openslide_get_level_count(osr)-1);
	i64 w = 0, h = 0;
	openslide.openslide_get_level_dimensions(osr, loaded_level, &w, &h);
	u32* buf = calloc(1, w * h * 4);
	openslide.openslide_read_region(osr, buf, 0, 0, loaded_level, w, h);

	image_count = 1;
	image_t* image = images; // TODO: just keep reusing the same image for now. fix this later.
	init_image(image, buf, w, h);

	entity_count = 0;
	add_image_entity(image, (v2i){0, 0});

	current_level = loaded_level;

}*/

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

void wsi_load_blocks_in_region(wsi_t* wsi, i32 level, v2i xy_tile_min, v2i xy_tile_max) {


//	v2i image_origin = { -wsi_level->width/2, -wsi_level->height/2 };


	wsi_level_t* wsi_level = global_wsi.levels + level;

	for (i32 tile_y = xy_tile_min.y; tile_y <= xy_tile_max.y; ++tile_y) {
		for (i32 tile_x = xy_tile_min.x; tile_x <= xy_tile_max.x; ++tile_x) {
			i32 tile_index = tile_y * wsi_level->width_in_tiles + tile_x;
			ASSERT(tile_index >= 0 && tile_index < wsi_level->num_tiles);
			wsi_tile_t* tile = wsi_level->tiles + tile_index;

			if (tile->block != 0) {
				// Yay, this tile is already loaded

			} else {
				tile->block = alloc_wsi_block(&global_viewer.slide_memory);

				u32* pixel_data = get_wsi_block(&global_viewer.slide_memory, tile->block);
				ASSERT(pixel_data);

				i64 x = (tile_x * TILE_DIM) << level;
				i64 y = (tile_y * TILE_DIM) << level;
				openslide.openslide_read_region(wsi->osr, pixel_data, x, y, level, TILE_DIM, TILE_DIM);

				/*const char* error = openslide.openslide_get_error(wsi->osr);
				if (error) {
					printf("%s\n", error);
				}*/

//				image_t* image = images + image_count++;
//				fill_image(image, pixel_data, TILE_DIM, TILE_DIM);

//				 TODO: world coorinates vs image coordinates
//				v2i xy_world = {0,0};//V2i(image_origin.x + tile_x * TILE_DIM, image_origin.y + tile_y * TILE_DIM);
//				add_tile_entity(image, xy_world);
			}
		}
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

void clear_surface(surface_t* surface, v4f* color) {
	u32 pixel_value = TO_BGRA(FLOAT_TO_BYTE(color->r), FLOAT_TO_BYTE(color->g), FLOAT_TO_BYTE(color->b), FLOAT_TO_BYTE(color->a));
	u32* pixel = (u32*) surface->memory;
	i32 num_pixels = surface->width * surface->height;
	for (int i = 0; i < num_pixels; ++i) {
		*pixel++ = pixel_value;
	}
}

void first() {
	i64 wsi_memory_size = GIGABYTES(1);
	global_viewer.slide_memory.data = platform_alloc(wsi_memory_size);
	global_viewer.slide_memory.capacity = wsi_memory_size;
	global_viewer.slide_memory.capacity_in_blocks = wsi_memory_size / WSI_BLOCK_SIZE;
	global_viewer.slide_memory.blocks_in_use = 1;

	// Load a slide from the command line or through the OS (double-click / drag on executable, etc.)
	if (g_argc > 1) {
		char* filename = g_argv[1];
		on_file_dragged(filename);
	}
}

i32 debug_current_image_index = 1;

void viewer_update_and_render(surface_t* surface, input_t* input) {

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

	if (input->mouse_buttons[0].down) {
		// do the drag
#if 0
		for (u32 entity_id = 0; entity_id < entity_count; ++entity_id) {
			entity_t* entity = entities + entity_id;
			if (!entity->active) continue;
			entity->pos.x += input->drag_vector.x;
			entity->pos.y -= input->drag_vector.y;
			input->drag_vector = (v2i){};
		}
#else
		camera_pos.x -= input->drag_vector.x;
		camera_pos.y += input->drag_vector.y;
		input->drag_vector = (v2i){};
#endif
		mouse_hide();
	} else {
		mouse_show();
		if (input->mouse_buttons[0].transition_count != 0) {
//			printf("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
		}
	}



	v4f clear_color = {0.85f, 0.85f, 0.85f, 85.0f};
	clear_surface(surface, &clear_color);

//	rect2i client_rect = {-surface->width/2, -surface->height/2, surface->width, surface->height};
	rect2i viewport_rect = {camera_pos.x - surface->width/2, camera_pos.y - surface->height/2, surface->width, surface->height};

//	rect2i viewport_rect = {-surface->width/2, -surface->height/2, surface->width, surface->height};
//	rect2i screen_rect = {0, 0, surface->width, surface->height};
//	v2i camera_offset = { camer.x - screen_rect.x, viewport_rect.y - screen_rect.y };


	rect2i region = viewport_rect;
	region.x -= camera_pos.x;
	region.y -= camera_pos.y;

	rect2i clip_space;
	clip_space.x = LOWERBOUND(region.x, 0);
	clip_space.y = LOWERBOUND(region.y, 0);
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

	v2i camera_offset = { camera_pos.x - surface->width/2, camera_pos.y - surface->height/2 };

#if 1
	for (i32 tile_y = xy_tile_min.y; tile_y <= xy_tile_max.y; ++tile_y) {
		for (i32 tile_x = xy_tile_min.x; tile_x <= xy_tile_max.x; ++tile_x) {
			i32 tile_index = tile_y * wsi_level->width_in_tiles + tile_x;
			ASSERT(tile_index >= 0 && tile_index < wsi_level->num_tiles);
			wsi_tile_t* tile = wsi_level->tiles + tile_index;

			if (tile->block != 0) {
				// memory is allocated
				u32* pixel_data = get_wsi_block(&global_viewer.slide_memory, tile->block);
				v2i pos = wsi_tile_world_position(wsi_level, tile_x, tile_y);

				rect2i screen_rect = {0, 0, surface->width, surface->height};

				rect2i image_rect = { pos.x - camera_offset.x, pos.y - camera_offset.y, TILE_DIM, TILE_DIM };
				rect2i clipped_rect = clip_rect(&image_rect, &screen_rect);
				v2i image_offset = { clipped_rect.x - image_rect.x, clipped_rect.y - image_rect.y };

				i32 start_x = clipped_rect.x;
				i32 start_y = clipped_rect.y;
				i32 end_x = clipped_rect.x + clipped_rect.w;// - camera_offset.x;
				i32 end_y = clipped_rect.y + clipped_rect.h;// - camera_offset.y;


//			printf("offset: %d %d\n", image_offset.x,image_offset.y);


				u8* surface_row = (u8*) surface->memory + (start_y * surface->pitch) + start_x * BYTES_PER_PIXEL;
				u8* image_row = pixel_data;// + (TILE_DIM - 1) * TILE_PITCH; // start on the last row

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

