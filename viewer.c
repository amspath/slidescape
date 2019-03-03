#include "common.h"

#include "win32_main.h"
#include "platform.h"
#include "viewer.h"

#define STBI_ASSERT(x) ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "render_group.h"
#include "render_group.c"

#include "openslide_api.h"




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

typedef struct {
	bool32 active;
	i32 type;
	v2i pos;
	image_t* image;
} entity_t;

u32 entity_count;
entity_t entities[1024];

static image_t images[1024];
u32 image_count;

static bool32 viewer_initialized;
static v2i camera_pos;

void add_image_entity(image_t* image, v2i pos) {
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

void init_image(image_t* image, void* data, i32 width, i32 height) {
	if (image->data != NULL) free(image->data);
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

typedef struct {
	i64 width;
	i64 height;
	u32* data;
} wsi_level_t;

typedef struct {
	i32 num_levels;
	openslide_t* osr;
} wsi_t;


i64 total_wsi_size;
wsi_level_t wsi_levels[64];
i64 wsi_offsets[64];
u32* wsi_memory;
i32 level_count;
openslide_t* global_osr;
i32 current_level;

void load_wsi(char* filename) {
	if (global_osr) {
		openslide.openslide_close(global_osr);
		global_osr = NULL;
		if (wsi_memory){
			free(wsi_memory);
		}
	}

	global_osr = openslide.openslide_open(filename);
}

void load_wsi_level(openslide_t* osr, i32 level) {
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
}

void on_file_dragged(char* filename) {
#if 0
	current_image = loaded_images;
	load_image(&loaded_images[0], filename);
#else

	load_wsi(filename);
	openslide_t* osr = global_osr;
	if (osr) {
		if (wsi_memory) {
			free(wsi_memory);
		}
		total_wsi_size = 0;
		memset(wsi_offsets, 0, sizeof(wsi_offsets));

		printf("Openslide: opened %s\n", filename);
		i32 num_levels = openslide.openslide_get_level_count(osr);
		printf("Openslide: WSI has %d levels\n", num_levels);

		const char* const* wsi_properties = openslide.openslide_get_property_names(osr);
		if (wsi_properties) {
			i32 property_index = 0;
			const char* property = wsi_properties[0];
			for (; property != NULL; property = wsi_properties[++property_index]) {
				const char* property_value = openslide.openslide_get_property_value(osr, property);
				printf("%s = %s\n", property, property_value);

			}
		}



		for (i32 i = 0; i < num_levels; ++i) {
			i64 w = 0;
			i64 h = 0;
			openslide.openslide_get_level_dimensions(osr, i, &w, &h);
			i64 level_size = w * h * 4;
			total_wsi_size += level_size;
			wsi_offsets[i+1] = total_wsi_size;
			wsi_levels[i].width = w;
			wsi_levels[i].height= h;

			printf("level %d: w=%d, h=%d\n", i, w, h);
		}

		load_wsi_level(osr, num_levels-1);

	}
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

void viewer_update_and_render(surface_t* surface, viewer_t* viewer, input_t* input) {
	if (!viewer_initialized) {
		viewer_initialized = true;
//		load_image(&loaded_images, "data/evg.jpg");
	}

	if (input->mouse_z != 0) {
//		printf("mouse_z = %d\n", input->mouse_z);
		if (global_osr) {
			i32 dlevel = input->mouse_z > 0 ? -1 : 1;
			load_wsi_level(global_osr, current_level + dlevel);
		}
	}

	if (input->mouse_buttons[0].down) {
		// do the drag
		for (u32 entity_id = 0; entity_id < entity_count; ++entity_id) {
			entity_t* entity = entities + entity_id;
			if (!entity->active) continue;
			entity->pos.x += input->drag_vector.x;
			entity->pos.y -= input->drag_vector.y;
			input->drag_vector = (v2i){};
		}
		mouse_hide();
	} else {
		mouse_show();
		if (input->mouse_buttons[0].transition_count != 0) {
//			printf("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
		}
	}



	v4f clear_color = {0.7f, 0.7f, 0.7f, 1.0f};
	clear_surface(surface, &clear_color);

	for (u32 entity_id = 0; entity_id < entity_count; ++entity_id) {
		entity_t* entity = entities + entity_id;
		if (!entity->active) continue;

		image_t* image = entity->image;
		if (image && image->data) {


			rect2i viewport_rect = {-surface->width/2, -surface->height/2, surface->width, surface->height};
			rect2i screen_rect = {0, 0, surface->width, surface->height};
			v2i camera_offset = { viewport_rect.x - screen_rect.x, viewport_rect.y - screen_rect.y };

			rect2i image_rect = {entity->pos.x - image->width / 2,
			                     entity->pos.y - image->height / 2,
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

}

