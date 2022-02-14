/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2022  Pieter Valkema

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

#if WINDOWS
#include "win32_platform.h"
#include <io.h>
#else
#include <sys/mman.h>
#endif
#include OPENGL_H

#include "platform.h"
#include "intrinsics.h"
#include "stringutils.h"

#include "openslide_api.h"
#include <linmath.h>

#include "arena.h"

#define VIEWER_IMPL
#include "viewer.h"

#define STBI_ASSERT(x) ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "tiff.h"
#include "isyntax.h"
#include "jpeg_decoder.h"
#include "remote.h"
#include "gui.h"
#include "caselist.h"
#include "annotation.h"
#include "shader.h"
#include "ini.h"

#include "viewer_opengl.cpp"
#include "viewer_io_file.cpp"
#include "viewer_io_remote.cpp"
#include "viewer_options.cpp"
#include "commandline.cpp"

tile_t* get_tile(level_image_t* image_level, i32 tile_x, i32 tile_y) {
	i32 tile_index = tile_y * image_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < image_level->tile_count);
	tile_t* result = image_level->tiles + tile_index;
	return result;
}

tile_t* get_tile_from_tile_index(image_t* image, i32 scale, i32 tile_index) {
	ASSERT(image);
	ASSERT(scale < image->level_count);
	level_image_t* level_image = image->level_images + scale;
	tile_t* tile = level_image->tiles + tile_index;
	return tile;
}

u32 get_texture_for_tile(image_t* image, i32 level, i32 tile_x, i32 tile_y) {
	level_image_t* level_image = image->level_images + level;

	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < level_image->tile_count);
	tile_t* tile = level_image->tiles + tile_index;

	return tile->texture;
}

void unload_image(image_t* image) {
	if (image) {
		if (image->type == IMAGE_TYPE_WSI) {
			if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
				unload_wsi(&image->wsi.wsi);
			} else if (image->backend == IMAGE_BACKEND_TIFF) {
				tiff_destroy(&image->tiff);
			} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
				isyntax_destroy(&image->isyntax);
			} else if (image->backend == IMAGE_BACKEND_STBI) {
				if (image->simple.pixels) {
					stbi_image_free(image->simple.pixels);
					image->simple.pixels = NULL;
				}
				if (image->simple.texture != 0) {
					unload_texture(image->simple.texture);
					image->simple.texture = 0;
				}
				image->simple.is_valid = false;
			} else {
				panic("invalid image backend");
			}
		} else {
			panic("invalid image type");
		}

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

		if (image->macro_image.is_valid) {
			if (image->macro_image.pixels) stbi_image_free(image->simple.pixels);
			if (image->macro_image.texture) unload_texture(image->macro_image.texture);
			memset(&image->macro_image, 0, sizeof(image->macro_image));
		}
		if (image->label_image.is_valid) {
			if (image->label_image.pixels) stbi_image_free(image->simple.pixels);
			if (image->label_image.texture) unload_texture(image->label_image.texture);
			memset(&image->label_image, 0, sizeof(image->label_image));
		}

	}
}

void add_image(app_state_t* app_state, image_t image, bool need_zoom_reset) {
	arrput(app_state->loaded_images, image);
	arrput(app_state->active_resources, image.resource_id);
	app_state->scene.active_layer = arrlen(app_state->loaded_images)-1;
	if (need_zoom_reset) {
		app_state->scene.need_zoom_reset = true;
	}
}

// TODO: make this based on scene (allow loading multiple images independently side by side)
void unload_all_images(app_state_t *app_state) {
	autosave(app_state, true); // save recent changes to annotations, if necessary

	i32 current_image_count = arrlen(app_state->loaded_images);
	if (current_image_count > 0) {
		ASSERT(app_state->loaded_images);
		for (i32 i = 0; i < current_image_count; ++i) {
			image_t* old_image = app_state->loaded_images + i;
			unload_image(old_image);
		}
		arrfree(app_state->loaded_images);
		arrfree(app_state->active_resources);
	}
	mouse_show();
	app_state->scene.is_cropped = false;
	app_state->scene.has_selection_box = false;
	viewer_switch_tool(app_state, TOOL_NONE);
}

void image_change_resolution(image_t* image, float mpp_x, float mpp_y) {
	image->mpp_x = mpp_x;
	image->mpp_y = mpp_y;
	image->width_in_um = image->width_in_pixels * mpp_x;
	image->height_in_um = image->height_in_pixels * mpp_y;

	if (image->type == IMAGE_TYPE_WSI) {
		// shorthand pointers for backend-specific data structure
		tiff_t* tiff = &image->tiff;
		wsi_t* openslide_image = &image->wsi.wsi;
		isyntax_t* isyntax = &image->isyntax;


		if (image->backend == IMAGE_BACKEND_TIFF) {
			tiff->mpp_x = mpp_x;
			tiff->mpp_y = mpp_y;
		} else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
			openslide_image->mpp_x = mpp_x;
			openslide_image->mpp_y = mpp_y;
		} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
			isyntax->mpp_x = mpp_x;
			isyntax->mpp_y = mpp_y;
		}

		for (i32 i = 0; i < image->level_count; ++i) {
			level_image_t* level_image = image->level_images + i;
			level_image->um_per_pixel_x = image->mpp_x * level_image->downsample_factor;
			level_image->um_per_pixel_y = image->mpp_y * level_image->downsample_factor;
			level_image->x_tile_side_in_um = level_image->tile_width * level_image->um_per_pixel_x;
			level_image->y_tile_side_in_um = level_image->tile_height * level_image->um_per_pixel_y;

			// If this downsampling level is 'backed' by a corresponding image pyramid level (not guaranteed),
			// then we also need to update the dimension info for the backend-specific data structure
			if (level_image->exists) {
				i32 pyramid_image_index = level_image->pyramid_image_index;
				if (image->backend == IMAGE_BACKEND_TIFF) {
					ASSERT(pyramid_image_index < tiff->ifd_count);
					tiff_ifd_t* ifd = tiff->ifds + pyramid_image_index;
					ifd->um_per_pixel_x = level_image->um_per_pixel_x;
					ifd->um_per_pixel_y = level_image->um_per_pixel_y;
					ifd->x_tile_side_in_um = level_image->x_tile_side_in_um;
					ifd->y_tile_side_in_um = level_image->y_tile_side_in_um;

//					TODO: ifd->x_resolution = create_tiff_rational(...);
//					ifd->y_resolution = create_tiff_rational(...);

				} else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
					wsi_level_t* wsi_level = openslide_image->levels + pyramid_image_index;
					wsi_level->um_per_pixel_x = level_image->um_per_pixel_x;
					wsi_level->um_per_pixel_y = level_image->um_per_pixel_y;
					wsi_level->x_tile_side_in_um = level_image->x_tile_side_in_um;
					wsi_level->y_tile_side_in_um = level_image->y_tile_side_in_um;
				} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
					// TODO: stub
				}
			}
		}


	}
}

bool init_image_from_tiff(app_state_t* app_state, image_t* image, tiff_t tiff, bool is_overlay) {
	image->type = IMAGE_TYPE_WSI;
	image->backend = IMAGE_BACKEND_TIFF;
	image->tiff = tiff;
	image->is_freshly_loaded = true;

	image->mpp_x = tiff.mpp_x;
	image->mpp_y = tiff.mpp_y;
	image->is_mpp_known = tiff.is_mpp_known;
	ASSERT(tiff.main_image_ifd);
	image->tile_width = tiff.main_image_ifd->tile_width;
	image->tile_height = tiff.main_image_ifd->tile_height;
	image->width_in_pixels = tiff.main_image_ifd->image_width;
	image->width_in_um = tiff.main_image_ifd->image_width * tiff.mpp_x;
	image->height_in_pixels = tiff.main_image_ifd->image_height;
	image->height_in_um = tiff.main_image_ifd->image_height * tiff.mpp_y;
	// TODO: fix code duplication with tiff_deserialize()
	if (tiff.level_image_ifd_count > 0 && tiff.main_image_ifd->tile_width) {

		if (tiff.main_image_ifd->is_tiled) {

			// This is a tiled image (as we expect most WSIs to be).
			// We need to fill out the level_image_t structures to initialize the backend-agnostic image_t

			memset(image->level_images, 0, sizeof(image->level_images));
			image->level_count = tiff.max_downsample_level + 1;

			if (tiff.level_image_ifd_count > image->level_count) {
				panic();
			}
			if (image->level_count > WSI_MAX_LEVELS) {
				panic();
			}

			i32 ifd_index = 0;
			i32 next_ifd_index_to_check_for_match = 0;
			tiff_ifd_t* ifd = tiff.level_images_ifd + ifd_index;
			for (i32 level_index = 0; level_index < image->level_count; ++level_index) {
				level_image_t* level_image = image->level_images + level_index;

				i32 wanted_downsample_level = level_index;
				bool found_ifd = false;
				for (ifd_index = next_ifd_index_to_check_for_match; ifd_index < tiff.level_image_ifd_count; ++ifd_index) {
					ifd = tiff.level_images_ifd + ifd_index;
					if (ifd->downsample_level == wanted_downsample_level) {
						// match!
						found_ifd = true;
						next_ifd_index_to_check_for_match = ifd_index + 1; // next iteration, don't reuse the same IFD!
						break;
					}
				}

				if (found_ifd) {
					// The current downsampling level is backed by a corresponding IFD level image in the TIFF.
					level_image->exists = true;
					level_image->pyramid_image_index = ifd_index;
					level_image->downsample_factor = ifd->downsample_factor;
					level_image->tile_count = ifd->tile_count;
					level_image->width_in_tiles = ifd->width_in_tiles;
					ASSERT(level_image->width_in_tiles > 0);
					level_image->height_in_tiles = ifd->height_in_tiles;
					level_image->tile_width = ifd->tile_width;
					level_image->tile_height = ifd->tile_height;
#if DO_DEBUG
					if (level_image->tile_width != image->tile_width) {
						console_print("Warning: level image %d (ifd #%d) tile width (%d) does not match base level (%d)\n", level_index, ifd_index, level_image->tile_width, image->tile_width);
					}
					if (level_image->tile_height != image->tile_height) {
						console_print("Warning: level image %d (ifd #%d) tile width (%d) does not match base level (%d)\n", level_index, ifd_index, level_image->tile_width, image->tile_width);
					}
#endif
					level_image->um_per_pixel_x = ifd->um_per_pixel_x;
					level_image->um_per_pixel_y = ifd->um_per_pixel_y;
					level_image->x_tile_side_in_um = ifd->x_tile_side_in_um;
					level_image->y_tile_side_in_um = ifd->y_tile_side_in_um;
					ASSERT(level_image->x_tile_side_in_um > 0);
					ASSERT(level_image->y_tile_side_in_um > 0);
					level_image->tiles = (tile_t*) calloc(1, ifd->tile_count * sizeof(tile_t));
					ASSERT(ifd->tile_byte_counts != NULL);
					ASSERT(ifd->tile_offsets != NULL);
					// mark the empty tiles, so that we can skip loading them later on
					for (i32 tile_index = 0; tile_index < level_image->tile_count; ++tile_index) {
						tile_t* tile = level_image->tiles + tile_index;
						u64 tile_byte_count = ifd->tile_byte_counts[tile_index];
						if (tile_byte_count == 0) {
							tile->is_empty = true;
						}
						// Facilitate some introspection by storing self-referential information
						// in the tile_t struct. This is needed for some specific cases where we
						// pass around pointers to tile_t structs without caring exactly where they
						// came from.
						// (Specific example: we use this when exporting a selected region as BigTIFF)
						tile->tile_index = tile_index;
						tile->tile_x = tile_index % level_image->width_in_tiles;
						tile->tile_y = tile_index / level_image->width_in_tiles;
					}
				} else {
					// The current downsampling level has no corresponding IFD level image :(
					// So we need only some placeholder information.
					level_image->exists = false;
					level_image->downsample_factor = exp2f((float)wanted_downsample_level);
					// Just in case anyone tries to divide by zero:
					level_image->tile_width = image->tile_width;
					level_image->tile_height = image->tile_height;
					level_image->um_per_pixel_x = image->mpp_x * level_image->downsample_factor;
					level_image->um_per_pixel_y = image->mpp_y * level_image->downsample_factor;
					level_image->x_tile_side_in_um = level_image->um_per_pixel_x * (float)tiff.main_image_ifd->tile_width;
					level_image->y_tile_side_in_um = level_image->um_per_pixel_y * (float)tiff.main_image_ifd->tile_height;
				}
				DUMMY_STATEMENT;
			}
		} else {
			// The image is NOT tiled
			memset(image->level_images, 0, sizeof(image->level_images));
			image->level_count = 1;
			level_image_t* level_image = image->level_images + 0;
			tiff_ifd_t* ifd = tiff.main_image_ifd;
			level_image->exists = true;
			level_image->pyramid_image_index = 0;
			level_image->downsample_factor = ifd->downsample_factor;
			level_image->tile_count = 1;
			level_image->width_in_tiles = 1;
			ASSERT(level_image->width_in_tiles > 0);
			level_image->height_in_tiles = 1;
			level_image->tile_width = ifd->tile_width;
			level_image->tile_height = ifd->tile_height;
			level_image->um_per_pixel_x = ifd->um_per_pixel_x;
			level_image->um_per_pixel_y = ifd->um_per_pixel_y;
			level_image->x_tile_side_in_um = ifd->x_tile_side_in_um;
			level_image->y_tile_side_in_um = ifd->y_tile_side_in_um;
			ASSERT(level_image->x_tile_side_in_um > 0);
			ASSERT(level_image->y_tile_side_in_um > 0);
			level_image->tiles = (tile_t*) calloc(1, sizeof(tile_t)); // only 1 tile in this case
			ASSERT(ifd->strip_byte_counts != NULL);
			ASSERT(ifd->strip_offsets != NULL);
			tile_t* tile = level_image->tiles + 0;
			tile->tile_index = 0;
			tile->tile_x = 0;
			tile->tile_y = 0;
		}


	}

	// TODO: establish the concept of a 'parent image' / fix dimensions not being exactly right
	// TODO: automatically register (translate/stretch) image
	// For now, we shall assume that the first loaded image is the parent image, and that the resolution of the overlay
	// is identical to the parent (although this is sometimes not strictly true, e.g. the TIFF resolution tags in the
	// Kaggle challenge prostate biopsies are *slightly* different in the base and mask images. But if we take those
	// resolution tags at face value, the images will not be correctly aligned!)
	if (is_overlay && arrlen(app_state->loaded_images)) {
		image_t* parent_image = app_state->loaded_images + 0;
		ASSERT(parent_image->mpp_x > 0.0f && parent_image->mpp_y > 0.0f);
		image_change_resolution(image, parent_image->mpp_x, parent_image->mpp_y);
	}


	image->is_valid = true;
	image->is_freshly_loaded = true;
	return image->is_valid;
}

bool init_image_from_isyntax(app_state_t* app_state, image_t* image, isyntax_t* isyntax, bool is_overlay) {
	image->type = IMAGE_TYPE_WSI;
	image->backend = IMAGE_BACKEND_ISYNTAX;
	image->isyntax = *isyntax;
	isyntax = &image->isyntax;
	image->is_freshly_loaded = true;

	image->mpp_x = isyntax->mpp_x;
	image->mpp_y = isyntax->mpp_y;
	image->is_mpp_known = isyntax->is_mpp_known;
	isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;
	image->tile_width = isyntax->tile_width;
	image->tile_height = isyntax->tile_height;
	image->width_in_pixels = wsi_image->width;
	image->width_in_um = wsi_image->width * isyntax->mpp_x;
	image->height_in_pixels = wsi_image->height;
	image->height_in_um = wsi_image->height * isyntax->mpp_y;
	// TODO: fix code duplication with tiff_deserialize()
	if (wsi_image->level_count > 0 && isyntax->tile_width) {

		memset(image->level_images, 0, sizeof(image->level_images));
		image->level_count = wsi_image->level_count;

		if (image->level_count > WSI_MAX_LEVELS) {
			panic();
		}

		for (i32 level_index = 0; level_index < image->level_count; ++level_index) {
			level_image_t* level_image = image->level_images + level_index;
			isyntax_level_t* isyntax_level = wsi_image->levels + level_index;

			level_image->exists = true;
			level_image->pyramid_image_index = level_index; // not used
			level_image->downsample_factor = exp2f((float)level_index);
			level_image->tile_count = isyntax_level->tile_count;
			level_image->width_in_tiles = isyntax_level->width_in_tiles;
			ASSERT(level_image->width_in_tiles > 0);
			level_image->height_in_tiles = isyntax_level->height_in_tiles;
			level_image->tile_width = isyntax->tile_width;
			level_image->tile_height = isyntax->tile_height;
			level_image->um_per_pixel_x = level_image->downsample_factor * isyntax->mpp_x;
			level_image->um_per_pixel_y = level_image->downsample_factor * isyntax->mpp_y;
			level_image->x_tile_side_in_um = level_image->um_per_pixel_x * isyntax->tile_width;
			level_image->y_tile_side_in_um = level_image->um_per_pixel_x * isyntax->tile_height;
			ASSERT(level_image->x_tile_side_in_um > 0);
			ASSERT(level_image->y_tile_side_in_um > 0);
			level_image->origin_offset = isyntax_level->origin_offset;
			level_image->tiles = (tile_t*) calloc(1, level_image->tile_count * sizeof(tile_t));
			for (i32 tile_index = 0; tile_index < level_image->tile_count; ++tile_index) {
				tile_t* tile = level_image->tiles + tile_index;
				// Facilitate some introspection by storing self-referential information
				// in the tile_t struct. This is needed for some specific cases where we
				// pass around pointers to tile_t structs without caring exactly where they
				// came from.
				// (Specific example: we use this when exporting a selected region as BigTIFF)
				tile->tile_index = tile_index;
				tile->tile_x = tile_index % level_image->width_in_tiles;
				tile->tile_y = tile_index / level_image->width_in_tiles;

				isyntax_tile_t* isyntax_tile = isyntax_level->tiles + tile_index;
				if (!isyntax_tile->exists) {
					tile->is_empty = true;
				}
			}
			DUMMY_STATEMENT;
		}
	}

	isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
	if (macro_image->image_type == ISYNTAX_IMAGE_TYPE_MACROIMAGE) {
		if (macro_image->pixels) {
			image->macro_image.pixels = macro_image->pixels;
			macro_image->pixels = NULL; // transfer ownership
			image->macro_image.width = macro_image->width;
			image->macro_image.height = macro_image->height;
			image->macro_image.mpp = 0.0315f * 1000.0f; // apparently, always this value
			image->macro_image.world_pos.x = -((float)wsi_image->offset_x * isyntax->mpp_x);
			image->macro_image.world_pos.y = -((float)wsi_image->offset_y * isyntax->mpp_y);
			image->macro_image.is_valid = true;
		}
	}
	isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
	if (label_image->image_type == ISYNTAX_IMAGE_TYPE_LABELIMAGE) {
		if (label_image->pixels) {
			image->label_image.pixels = label_image->pixels;
			label_image->pixels = NULL; // transfer ownership
			image->label_image.width = label_image->width;
			image->label_image.height = label_image->height;
			image->label_image.mpp = 0.0315f * 1000.0f; // apparently, always this value
//			image->label_image.world_pos.x = -((float)wsi_image->offset_x * isyntax->mpp_x);
//			image->label_image.world_pos.y = -((float)wsi_image->offset_y * isyntax->mpp_y);
			image->label_image.is_valid = true;
		}
	}


	image->is_valid = true;
	image->is_freshly_loaded = true;
	return image->is_valid;
}

bool init_image_from_stbi(app_state_t* app_state, image_t* image, simple_image_t* simple, bool is_overlay) {
	image->type = IMAGE_TYPE_WSI;
	image->backend = IMAGE_BACKEND_STBI;
	image->simple = *simple;
	simple = &image->simple;
	image->is_freshly_loaded = true;

	image->mpp_x = 1.0f;
	image->mpp_y = 1.0f;
	image->is_mpp_known = false;
	image->tile_width = simple->width;
	image->tile_height = simple->height;
	image->width_in_pixels = simple->width;
	image->width_in_um = (float)simple->width * image->mpp_x;
	image->height_in_pixels = simple->height;
	image->height_in_um = (float)simple->height * image->mpp_y;

	image->level_count = 1;
	level_image_t* level_image = image->level_images + 0;
	memset(level_image, 0, sizeof(*level_image));

	level_image->exists = true;
	level_image->pyramid_image_index = 0; // not used
	level_image->downsample_factor = 1.0f;
	level_image->tile_count = 1;
	level_image->width_in_tiles = 1;
	ASSERT(level_image->width_in_tiles > 0);
	level_image->height_in_tiles = 1;
	level_image->tile_width = image->width_in_pixels;
	level_image->tile_height = image->height_in_pixels;
	level_image->um_per_pixel_x = level_image->downsample_factor * image->mpp_x;
	level_image->um_per_pixel_y = level_image->downsample_factor * image->mpp_y;
	level_image->x_tile_side_in_um = level_image->um_per_pixel_x * image->tile_width;
	level_image->y_tile_side_in_um = level_image->um_per_pixel_x * image->tile_height;
	ASSERT(level_image->x_tile_side_in_um > 0);
	ASSERT(level_image->y_tile_side_in_um > 0);
	level_image->origin_offset = {};
	level_image->tiles = (tile_t*) calloc(1, level_image->tile_count * sizeof(tile_t));
	for (i32 tile_index = 0; tile_index < level_image->tile_count; ++tile_index) {
		tile_t* tile = level_image->tiles + tile_index;
		// Facilitate some introspection by storing self-referential information
		// in the tile_t struct. This is needed for some specific cases where we
		// pass around pointers to tile_t structs without caring exactly where they
		// came from.
		// (Specific example: we use this when exporting a selected region as BigTIFF)
		tile->tile_index = tile_index;
		tile->tile_x = tile_index % level_image->width_in_tiles;
		tile->tile_y = tile_index / level_image->width_in_tiles;
	}

	image->is_valid = true;
	image->is_freshly_loaded = true;
	return image->is_valid;
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

int priority_cmp_func (const void* a, const void* b) {
	return ( (*(load_tile_task_t*)b).priority - (*(load_tile_task_t*)a).priority );
}

void init_app_state(app_state_t* app_state, app_command_t command) {
	ASSERT(!app_state->initialized); // check sanity
	ASSERT(app_state->temp_storage_memory == NULL);
//	memset(app_state, 0, sizeof(app_state_t));

	app_state->command = command;
	app_state->headless = command.headless;

	if (app_state->display_points_per_pixel == 0.0f) {
		app_state->display_points_per_pixel = 1.0f;
	}
	if (app_state->display_scale_factor == 0.0f) {
		app_state->display_scale_factor = 1.0f;
	}

	// TODO: remove
	size_t temp_storage_size = MEGABYTES(16); // Note: what is a good size to use here?
	app_state->temp_storage_memory = platform_alloc(temp_storage_size);
	init_arena(&app_state->temp_arena, temp_storage_size, app_state->temp_storage_memory);

	app_state->clear_color = V4F(1.0f, 1.0f, 1.0f, 1.00f);
	app_state->black_level = 0.10f;
	app_state->white_level = 0.95f;
	app_state->use_builtin_tiff_backend = true; // If disabled, revert to OpenSlide when loading TIFF files.

	app_state->keyboard_base_panning_speed = 10.0f;
	app_state->mouse_sensitivity = 10.0f;
	app_state->enable_autosave = true;

	init_scene(app_state, &app_state->scene);

	unload_and_reinit_annotations(&app_state->scene.annotation_set);

	app_state->initialized = true;
}

void autosave(app_state_t* app_state, bool force_ignore_delay) {
	annotation_set_t* annotation_set = &app_state->scene.annotation_set;
	if (app_state->enable_autosave) {
		save_annotations(app_state, annotation_set, force_ignore_delay);
	}
}

void request_tiles(app_state_t* app_state, image_t* image, load_tile_task_t* wishlist, i32 tiles_to_load) {
	if (tiles_to_load > 0){
		app_state->allow_idling_next_frame = false;

		if (image->backend == IMAGE_BACKEND_TIFF && image->tiff.is_remote) {
			// For remote slides, only send out a batch request every so often, instead of single tile requests every frame.
			// (to reduce load on the server)
			static u32 intermittent = 0;
			++intermittent;
			u32 intermittent_interval = 1;
			intermittent_interval = 5; // reduce load on remote server; can be tweaked
			if (intermittent % intermittent_interval == 0) {
				load_tile_task_batch_t batch = {};
				batch.task_count = ATMOST(COUNT(batch.tile_tasks), tiles_to_load);
				memcpy(batch.tile_tasks, wishlist, batch.task_count * sizeof(load_tile_task_t));
				if (add_work_queue_entry(&global_work_queue, tiff_load_tile_batch_func, &batch, sizeof(batch))) {
					// success
					for (i32 i = 0; i < batch.task_count; ++i) {
						load_tile_task_t* task = batch.tile_tasks + i;
						tile_t* tile = task->tile;
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task->need_gpu_residency;
						tile->need_keep_in_cache = task->need_keep_in_cache;
					}
				}
			}
		} else {
			// regular file loading
			for (i32 i = 0; i < tiles_to_load; ++i) {
				load_tile_task_t task = wishlist[i];
				tile_t* tile = task.tile;
				if (tile->is_cached && tile->texture == 0 && task.need_gpu_residency) {
					// only GPU upload needed
					if (add_work_queue_entry(&global_completion_queue, viewer_upload_already_cached_tile_to_gpu, &task, sizeof(task))) {
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task.need_gpu_residency;
						tile->need_keep_in_cache = task.need_keep_in_cache;
					}
				} else {
					if (add_work_queue_entry(&global_work_queue, load_tile_func, &task, sizeof(task))) {
						// TODO: should we even allow this to fail?
						// success
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task.need_gpu_residency;
						tile->need_keep_in_cache = task.need_keep_in_cache;
					}
				}
			}
		}
	}
}

bool is_resource_valid(app_state_t* app_state, i32 resource_id) {
	for (i32 i = 0; i < arrlen(app_state->active_resources); ++i) {
		if (app_state->active_resources[i] == resource_id) {
			return true;
		}
	}
	return false;
}

image_t* get_image_from_resource_id(app_state_t* app_state, i32 resource_id) {
	i32 image_index = 0;
	for (; image_index < arrlen(app_state->loaded_images); ++image_index) {
		image_t* image = app_state->loaded_images + image_index;
		if (image->resource_id == resource_id) {
			return image;
		}
	}
	return NULL;
}


void viewer_process_completion_queue(app_state_t* app_state) {
	float max_texture_load_time = 0.007f; // TODO: pin to frame time
#if 1
	if (!finalize_textures_immediately) {
		// Finalize textures that were uploaded via PBO the previous frame
		for (i32 transfer_index = 0; transfer_index < COUNT(app_state->pixel_transfer_states); ++transfer_index) {
			pixel_transfer_state_t* transfer_state = app_state->pixel_transfer_states + transfer_index;
			if (transfer_state->need_finalization) {
				finalize_texture_upload_using_pbo(transfer_state);
				tile_t* tile = (tile_t*) transfer_state->userdata;  // TODO: think of something more elegant?
				tile->texture = transfer_state->texture;
			}
			float time_elapsed = get_seconds_elapsed(app_state->last_frame_start, get_clock());
			if (time_elapsed > max_texture_load_time) {
//			    	console_print("Warning: texture finalization is taking too much time\n");
				break;
			}
		}
	}

	/*time_elapsed = get_seconds_elapsed(last_section, get_clock());
	if (time_elapsed > 0.005f) {
		console_print("Warning: texture finalization took %g ms\n", time_elapsed * 1000.0f);
	}*/

//	last_section = profiler_end_section(last_section, "viewer_update_and_render: texture finalization", 7.0f);
#endif

	// Retrieve completed tasks from the worker threads
	i32 pixel_transfer_index_start = app_state->next_pixel_transfer_to_submit;
	while (is_queue_work_in_progress(&global_completion_queue)) {
		work_queue_entry_t entry = get_next_work_queue_entry(&global_completion_queue);
		if (entry.is_valid) {
			if (!entry.callback) panic();
			mark_queue_entry_completed(&global_completion_queue);

			if (entry.callback == viewer_notify_load_tile_completed) {
				viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*) entry.userdata;
				image_t* image = get_image_from_resource_id(app_state, task->resource_id);
				if (!image) {
					// Image doesn't exist anymore (was unloaded?)
					if (task->pixel_memory) free(task->pixel_memory);
				} else {
					// Upload the tile to the GPU
					tile_t* tile = get_tile_from_tile_index(image, task->scale, task->tile_index);
					ASSERT(tile);
					tile->is_submitted_for_loading = false;

					if (task->pixel_memory) {
						bool need_free_pixel_memory = true;
						if (task->want_gpu_residency) {
							pixel_transfer_state_t* transfer_state =
									submit_texture_upload_via_pbo(app_state, task->tile_width, task->tile_height,
									                              4, task->pixel_memory, finalize_textures_immediately);
							if (finalize_textures_immediately) {
								tile->texture = transfer_state->texture;
							} else {
								transfer_state->userdata = (void*) tile;
								tile->is_submitted_for_loading = true; // stuff still needs to happen, don't resubmit!
							}
						}
						if (tile->need_keep_in_cache) {
							need_free_pixel_memory = false;
							tile->pixels = task->pixel_memory;
							tile->is_cached = true;
						}
						if (need_free_pixel_memory) {
							free(task->pixel_memory);
						}
					} else {
						tile->is_empty = true; // failed; don't resubmit!
					}
				}

			} else if (entry.callback == viewer_upload_already_cached_tile_to_gpu) {
				load_tile_task_t* task = (load_tile_task_t*) entry.userdata;
				if (!is_resource_valid(app_state, task->resource_id)) {
					// Image no longer exists
				} else {
					tile_t* tile = task->tile;
					ASSERT(tile);
					tile->is_submitted_for_loading = false;
					if (tile->is_cached && tile->pixels) {
						if (tile->need_gpu_residency) {
							pixel_transfer_state_t* transfer_state = submit_texture_upload_via_pbo(app_state,
							                                                                       task->image->tile_width,
							                                                                       task->image->tile_height,
							                                                                       4,
							                                                                       tile->pixels,
							                                                                       finalize_textures_immediately);
							tile->texture = transfer_state->texture;
						} else {
							ASSERT(!"viewer_only_upload_cached_tile() called but !tile->need_gpu_residency\n");
						}

						if (!task->need_keep_in_cache) {
							tile_release_cache(tile);
						}
					} else {
						console_print("Warning: viewer_only_upload_cached_tile() called on a non-cached tile\n");
					}
				}

			}
		}

		float time_elapsed = get_seconds_elapsed(app_state->last_frame_start, get_clock());
		if (time_elapsed > max_texture_load_time) {
//				console_print("Warning: texture submission is taking too much time\n");
			break;
		}

		if (pixel_transfer_index_start == app_state->next_pixel_transfer_to_submit) {
//				console_print("Warning: not enough PBO's to do all the pixel transfers\n");
			break;
		}
	}
}

void update_and_render_image(app_state_t* app_state, input_t *input, float delta_t, image_t* image) {
	scene_t* scene = &app_state->scene;

	i32 client_width = app_state->client_viewport.w;
	i32 client_height = app_state->client_viewport.h;

	if (image->type == IMAGE_TYPE_WSI) {

//		last_section = profiler_end_section(last_section, "viewer_update_and_render: process input (2)", 5.0f);

		// IO

		// Upload macro and label images (just-in-time)
		simple_image_t* macro_image = &image->macro_image;
		simple_image_t* label_image = &image->label_image;
		if (macro_image->is_valid) {
			if (macro_image->texture == 0 && macro_image->pixels != NULL) {
				macro_image->texture = load_texture(macro_image->pixels, macro_image->width, macro_image->height, GL_RGBA);
				stbi_image_free(macro_image->pixels);
				macro_image->pixels = NULL;
			}
		}
		if (label_image->is_valid) {
			if (label_image->texture == 0 && label_image->pixels != NULL) {
				label_image->texture = load_texture(label_image->pixels, label_image->width, label_image->height, GL_RGBA);
				stbi_image_free(label_image->pixels);
				label_image->pixels = NULL;
			}
		}

		// Determine the highest and lowest levels with image data that need to be loaded and rendered.
		// The lowest needed level might be lower than the actual current downsampling level,
		// because some levels may not have image data available (-> need to fall back to lower level).
		ASSERT(image->level_count >= 0);
		i32 highest_visible_scale = ATLEAST(image->level_count - 1, 0);
		i32 lowest_visible_scale = ATLEAST(scene->zoom.level, 0);
		lowest_visible_scale = ATMOST(highest_visible_scale, lowest_visible_scale);
		for (; lowest_visible_scale > 0; --lowest_visible_scale) {
			if (image->level_images[lowest_visible_scale].exists) {
				break; // done, no need to go lower
			}
		}

		if (image->backend == IMAGE_BACKEND_ISYNTAX) {
			isyntax_t* isyntax = &image->isyntax;
			isyntax_image_t* wsi = isyntax->images + isyntax->wsi_image_index;
			if (!wsi->first_load_complete && !wsi->first_load_in_progress) {
				wsi->first_load_in_progress = true;
				isyntax_begin_first_load(image->resource_id, isyntax, wsi);
			} else if (wsi->first_load_complete) {
				tile_streamer_t tile_streamer = {};
				tile_streamer.image = image;
				tile_streamer.origin_offset = image->origin_offset; // TODO: superfluous?
				if (!scene->restrict_load_bounds) {
					tile_streamer.camera_bounds = scene->camera_bounds;
				} else {
					tile_streamer.camera_bounds = scene->tile_load_bounds;
				}
				tile_streamer.camera_center = scene->camera;
				tile_streamer.scene = scene;
				tile_streamer.crop_bounds = scene->crop_bounds;
				tile_streamer.is_cropped = scene->is_cropped;
				tile_streamer.zoom = scene->zoom;
				isyntax_begin_stream_image_tiles(&tile_streamer);
			}
		} else if (image->backend == IMAGE_BACKEND_STBI) {
			simple_image_t* simple = &image->simple;
			if (image->simple.texture == 0 && image->simple.pixels != NULL) {
				image->simple.texture = load_texture(image->simple.pixels, image->simple.width, image->simple.height, GL_RGBA);
//			    image->origin_offset = (v2f) {50, 100};
				image->is_freshly_loaded = false;
				level_image_t* level_image = image->level_images + 0;
				ASSERT(level_image->tiles && level_image->tile_count > 0);
				tile_t* tile = level_image->tiles + 0;
				tile->texture = image->simple.texture;
			}

		} else {

			// Create a 'wishlist' of tiles to request
			load_tile_task_t tile_wishlist[32];
			i32 num_tasks_on_wishlist = 0;
			float screen_radius = ATLEAST(1.0f, sqrtf(SQUARE(client_width/2) + SQUARE(client_height/2)));

			for (i32 scale = highest_visible_scale; scale >= lowest_visible_scale; --scale) {
				ASSERT(scale >= 0 && scale < COUNT(image->level_images));
				level_image_t *drawn_level = image->level_images + scale;
				if (!drawn_level->exists) {
					continue; // no image data
				}

				bounds2i level_tiles_bounds = BOUNDS2I(0, 0, (i32)drawn_level->width_in_tiles, (i32)drawn_level->height_in_tiles);

				bounds2i visible_tiles = world_bounds_to_tile_bounds(&scene->camera_bounds, drawn_level->x_tile_side_in_um,
				                                                     drawn_level->y_tile_side_in_um, image->origin_offset);
				visible_tiles = clip_bounds2i(visible_tiles, level_tiles_bounds);

				if (scene->is_cropped) {
					bounds2i crop_tile_bounds = world_bounds_to_tile_bounds(&scene->crop_bounds,
					                                                        drawn_level->x_tile_side_in_um,
					                                                        drawn_level->y_tile_side_in_um, image->origin_offset);
					visible_tiles = clip_bounds2i(visible_tiles, crop_tile_bounds);
				}

				i32 base_priority = (image->level_count - scale) * 100; // highest priority for the most zoomed in levels


				for (i32 tile_y = visible_tiles.min.y; tile_y < visible_tiles.max.y; ++tile_y) {
					for (i32 tile_x = visible_tiles.min.x; tile_x < visible_tiles.max.x; ++tile_x) {

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
						load_tile_task_t task = {
								.resource_id = image->resource_id,
								.image = image, .tile = tile, .level = scale, .tile_x = tile_x, .tile_y = tile_y,
								.priority = tile_priority,
								.need_gpu_residency = true,
								.need_keep_in_cache = tile->need_keep_in_cache,
								.completion_callback = viewer_notify_load_tile_completed,
						};
						tile_wishlist[num_tasks_on_wishlist++] = task;
					}
				}
			}
//			if (num_tasks_on_wishlist > 0) {
//				console_print_verbose("Num tiles on wishlist = %d\n", num_tasks_on_wishlist);
//			}

			qsort(tile_wishlist, num_tasks_on_wishlist, sizeof(load_tile_task_t), priority_cmp_func);

//		    last_section = profiler_end_section(last_section, "viewer_update_and_render: create tiles wishlist", 5.0f);

			i32 max_tiles_to_load = (image->backend == IMAGE_BACKEND_TIFF && image->tiff.is_remote) ? 3 : 10;
			i32 tiles_to_load = ATMOST(num_tasks_on_wishlist, max_tiles_to_load);

			request_tiles(app_state, image, tile_wishlist, tiles_to_load);
		}

//		last_section = profiler_end_section(last_section, "viewer_update_and_render: load tiles", 5.0f);

		// RENDERING
		mat4x4 projection = {};
		{
			float l = -0.5f * scene->r_minus_l;
			float r = +0.5f * scene->r_minus_l;
			float b = +0.5f * scene->t_minus_b;
			float t = -0.5f * scene->t_minus_b;
			float n = 100.0f;
			float f = -100.0f;
			mat4x4_ortho(projection, l, r, b, t, n, f);
		}

		mat4x4 I;
		mat4x4_identity(I);

		// define view matrix
		mat4x4 view_matrix;
		mat4x4_translate(view_matrix,
						 -scene->camera.x + image->origin_offset.x,
						 -scene->camera.y + image->origin_offset.y,
						 0.0f);

		mat4x4 projection_view_matrix;
		mat4x4_mul(projection_view_matrix, projection, view_matrix);

		glUseProgram(basic_shader.program);
		glActiveTexture(GL_TEXTURE0);
		glUniform1i(basic_shader.u_tex, 0);

		glUniformMatrix4fv(basic_shader.u_projection_view_matrix, 1, GL_FALSE, &projection_view_matrix[0][0]);

		glUniform3fv(basic_shader.u_background_color, 1, (GLfloat *) &app_state->clear_color);
		if (app_state->use_image_adjustments) {
			glUniform1f(basic_shader.u_black_level, app_state->black_level);
			glUniform1f(basic_shader.u_white_level, app_state->white_level);
		} else {
			glUniform1f(basic_shader.u_black_level, 0.0f);
			glUniform1f(basic_shader.u_white_level, 1.0f);
		}
		glUniform1i(basic_shader.u_use_transparent_filter, scene->use_transparent_filter);
		if (scene->use_transparent_filter) {
			glUniform3fv(basic_shader.u_transparent_color, 1, (GLfloat *) &app_state->scene.transparent_color);
			glUniform1f(basic_shader.u_transparent_tolerance, app_state->scene.transparent_tolerance);
		}

//		last_section = profiler_end_section(last_section, "viewer_update_and_render: render (1)", 5.0f);

		// Render label and macro images
		if (draw_macro_image_in_background) {
			glDisable(GL_STENCIL_TEST);
			if (macro_image->is_valid && macro_image->texture != 0) {
				v2f pmax = {};
				pmax.x = macro_image->width * macro_image->mpp;
				pmax.y = macro_image->height * macro_image->mpp;

				mat4x4 model_matrix;
				mat4x4_translate(model_matrix,
				                 image->origin_offset.x + macro_image->world_pos.x,
				                 image->origin_offset.y + macro_image->world_pos.y,
				                 10.0f);
				mat4x4_scale_aniso(model_matrix, model_matrix, pmax.x, pmax.y, 1.0f);
				glUniformMatrix4fv(basic_shader.u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);

				draw_rect(macro_image->texture);
			}
		}

		{
			// Set up the stencil buffer to prevent rendering outside the image area
			bounds2f stencil_bounds = {0, 0, image->width_in_um, image->height_in_um};
			if (scene->is_cropped) {
				stencil_bounds = clip_bounds2f(stencil_bounds, scene->crop_bounds);
			}

			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_ALWAYS, 1, 0xFF);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
//			glStencilMask(0xFF);
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // don't actually draw the stencil rectangle
			glDepthMask(GL_FALSE); // don't write to depth buffer
			{
				mat4x4 model_matrix;
				mat4x4_translate(model_matrix, stencil_bounds.left, stencil_bounds.top, 0.0f);
				mat4x4_scale_aniso(model_matrix, model_matrix,
				                   stencil_bounds.right - stencil_bounds.left,
				                   stencil_bounds.bottom - stencil_bounds.top,
				                   1.0f);
				glUniformMatrix4fv(basic_shader.u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);
				draw_rect(dummy_texture);
			}

			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDepthMask(GL_TRUE);
//			glStencilMask(0xFF);
			glStencilFunc(GL_EQUAL, 1, 0xFF);
//			glDisable(GL_STENCIL_TEST);

		}

		// If a background image has already been rendered, we need to blend the tiles on top
		// while taking into account transparency.
		if (draw_macro_image_in_background) {
			glEnable(GL_BLEND);
			glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
		} else {
			glDisable(GL_BLEND);
		}

		// Draw tiles
		// Draw all levels within the viewport, up to the current zoom factor
		for (i32 level = lowest_visible_scale; level <= highest_visible_scale; ++level) {
			level_image_t *drawn_level = image->level_images + level;
			if (!drawn_level->exists) {
				continue;
			}

			bounds2i level_tiles_bounds = BOUNDS2I(0, 0, (i32)drawn_level->width_in_tiles, (i32)drawn_level->height_in_tiles);

			bounds2i visible_tiles = world_bounds_to_tile_bounds(&scene->camera_bounds, drawn_level->x_tile_side_in_um,
			                                                     drawn_level->y_tile_side_in_um, image->origin_offset);
			visible_tiles = clip_bounds2i(visible_tiles, level_tiles_bounds);

			if (scene->is_cropped) {
				bounds2i crop_tile_bounds = world_bounds_to_tile_bounds(&scene->crop_bounds,
				                                                        drawn_level->x_tile_side_in_um,
				                                                        drawn_level->y_tile_side_in_um, image->origin_offset);
				visible_tiles = clip_bounds2i(visible_tiles, crop_tile_bounds);
			}

			i32 missing_tiles_on_this_level = 0;
			for (i32 tile_y = visible_tiles.min.y; tile_y < visible_tiles.max.y; ++tile_y) {
				for (i32 tile_x = visible_tiles.min.x; tile_x < visible_tiles.max.x; ++tile_x) {

					tile_t *tile = get_tile(drawn_level, tile_x, tile_y);
					if (tile->texture) {
						tile->time_last_drawn = app_state->frame_counter;
						u32 texture = get_texture_for_tile(image, level, tile_x, tile_y);

						float tile_pos_x = drawn_level->origin_offset.x + drawn_level->x_tile_side_in_um * tile_x;
						float tile_pos_y = drawn_level->origin_offset.y + drawn_level->y_tile_side_in_um * tile_y;

						// define model matrix
						mat4x4 model_matrix;
						mat4x4_translate(model_matrix, tile_pos_x, tile_pos_y, 0.0f);
						mat4x4_scale_aniso(model_matrix, model_matrix, drawn_level->x_tile_side_in_um,
						                   drawn_level->y_tile_side_in_um, 1.0f);
						glUniformMatrix4fv(basic_shader.u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);

						draw_rect(texture);
					} else {
						++missing_tiles_on_this_level;
					}
				}
			}

			if (missing_tiles_on_this_level == 0) {
				break; // don't need to bother drawing the next level, there are no gaps left to fill in!
			}

		}

		// restore OpenGL state
		glDisable(GL_STENCIL_TEST);

//		last_section = profiler_end_section(last_section, "viewer_update_and_render: render (2)", 5.0f);

	}
}


void viewer_clear_and_set_up_framebuffer(v4f clear_color, i32 client_width, i32 client_height) {
//	glDrawBuffer(GL_BACK);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilMask(0xFF);
	glViewport(0, 0, client_width, client_height);
	glClearColor(clear_color.r, clear_color.g, clear_color.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}


v2f viewer_do_2d_control(v2f velocity, v2f control, float dt, float time_since_start_moving, bool is_shift_pressed) {
	float old_speed = v2f_length(velocity);
	float control_force = v2f_length(control);
	float max_force = 120.0f;
	if (time_since_start_moving < 0.20f) {
		control_force *= (0.25f + 0.75f * time_since_start_moving * (1.0f / 0.2f)) * max_force;
	} else {
		control_force *= max_force;
	}
	float friction = 15.0f;
	friction += control_force * 0.5f;
	if (is_shift_pressed && control_force > 0.0f) {
		friction *= 0.25f;
	}
	float net_force = control_force - ((1.0f + old_speed * old_speed) * friction);
	float dv = net_force * dt;
	float new_speed = ATLEAST(0.0f, old_speed + dv);

#if 0
	static i32 times_zero;
	if (new_speed != 0.0f || times_zero < 5) {
		if (new_speed == 0.0f) {
			times_zero++;
		} else times_zero = 0;
		console_print("old_speed = %g, control_force = %g, friction = %g, net_force = %g, dv = %g, new_speed = %g\n", old_speed, control_force, friction, net_force, dv, new_speed);
	}
#endif

	v2f new_velocity = {};
	if (control_force > 0.01f) {
		new_velocity = v2f_scale(new_speed, control);
	} else if (old_speed > 0.01f) {
		new_velocity = v2f_scale(new_speed / old_speed, velocity);
	}
	return new_velocity;
}

v2f get_2d_control_from_input(input_t* input) {
	v2f control = {};
	if (input) {
		if (!input->keyboard.key_ctrl.down) {
			if (input->keyboard.action_down.down || is_key_down(input, KEY_S) || is_key_down(input, KEY_Down)) {
				control.y += 1.0f;
			}
			if (input->keyboard.action_up.down || is_key_down(input, KEY_W) || is_key_down(input, KEY_Up)) {
				control.y += -1.0f;
			}
			if (input->keyboard.action_right.down || is_key_down(input, KEY_D) || is_key_down(input, KEY_Right)) {
				control.x += 1.0f;
			}
			if (input->keyboard.action_left.down || is_key_down(input, KEY_A) || is_key_down(input, KEY_Left)) {
				control.x += -1.0f;
			}
		}
		// Normalize
		float length_squared = v2f_length_squared(control);
		if (length_squared > 1.0f) {
			float length = sqrtf(length_squared);
			control = v2f_scale(1.0f / length, control);
		}
	}
	return control;
}

static inline void scene_update_camera_bounds(scene_t* scene) {
	scene->camera_bounds = bounds_from_center_point(scene->camera, scene->r_minus_l, scene->t_minus_b);
}

void scene_update_camera_pos(scene_t* scene, v2f pos) {
	scene->camera = pos;
	scene_update_camera_bounds(scene);
}

static void scene_update_mouse_pos(app_state_t* app_state, scene_t* scene, v2f client_mouse_xy) {
	if (client_mouse_xy.x >= 0 && client_mouse_xy.y < app_state->client_viewport.w * app_state->display_scale_factor &&
			client_mouse_xy.y >= 0 && client_mouse_xy.y < app_state->client_viewport.h * app_state->display_scale_factor) {
		scene->mouse.x = scene->camera_bounds.min.x + client_mouse_xy.x * scene->zoom.screen_point_width;
		scene->mouse.y = scene->camera_bounds.min.y + client_mouse_xy.y * scene->zoom.screen_point_width;
	} else {
		scene->mouse = scene->camera;
	}
}

void viewer_switch_tool(app_state_t* app_state, placement_tool_enum tool) {
	if (app_state->mouse_tool == tool) {
		// no action needed
		return;
	}

	// TODO: finalize in-progress annotations?

	switch(tool) {
		default: case TOOL_NONE: {
			if (app_state->mouse_mode != MODE_VIEW) {
				app_state->mouse_mode = MODE_VIEW;
				set_cursor_default();
				// TODO: reset edit functionality?
			}
		} break;
		case TOOL_CREATE_OUTLINE: // TODO: how to handle creation of selection boxes; different from annotations?
		case TOOL_CREATE_POINT:
		case TOOL_CREATE_LINE:
		case TOOL_CREATE_ARROW:
		case TOOL_CREATE_FREEFORM:
		case TOOL_CREATE_ELLIPSE:
		case TOOL_CREATE_RECTANGLE:
		case TOOL_CREATE_TEXT: {
			app_state->mouse_mode = MODE_INSERT;
			set_cursor_crosshair();
		} break;
	}
	app_state->mouse_tool = tool;
	app_state->scene.annotation_set.editing_annotation_index = -1;
}

#define CLICK_DRAG_TOLERANCE 8.0f

void viewer_update_and_render(app_state_t *app_state, input_t *input, i32 client_width, i32 client_height, float delta_t) {

	i64 last_section = get_clock(); // start profiler section

	// Release the temporary memory that was allocated the previous frame.
	app_state->temp_arena.used = 0;

//	if (!app_state->initialized) init_app_state(app_state);
	// Note: the window might get resized, so need to update this every frame
	app_state->client_viewport = RECT2I(0, 0, client_width, client_height);

	scene_t* scene = &app_state->scene;
	ASSERT(app_state->initialized);
	ASSERT(scene->initialized);
	annotation_set_t* annotation_set = &scene->annotation_set;

	// Note: could be changed to allow e.g. multiple scenes side by side
	{
		rect2f old_viewport = scene->viewport;
		rect2f new_viewport = RECT2F(
				(float)app_state->client_viewport.x * app_state->display_points_per_pixel,
				(float)app_state->client_viewport.y * app_state->display_points_per_pixel,
				(float)app_state->client_viewport.w * app_state->display_points_per_pixel,
				(float)app_state->client_viewport.h * app_state->display_points_per_pixel
		);
		if (new_viewport.x != old_viewport.x || old_viewport.y != new_viewport.y || old_viewport.w != new_viewport.w || old_viewport.h != new_viewport.h) {
			scene->viewport_changed = true;
		} else {
			scene->viewport_changed = false;
		}
		scene->viewport = new_viewport;
	}

	scene->clicked = false;
	scene->right_clicked = false;
	scene->drag_started = false;
	scene->drag_ended = false;

	app_state->input = input;

	// Set up rendering state for the next frame
	viewer_clear_and_set_up_framebuffer(app_state->clear_color, client_width, client_height);

	last_section = profiler_end_section(last_section, "viewer_update_and_render: new frame", 20.0f);

	app_state->allow_idling_next_frame = true; // but we might set it to false later

	i32 image_count = arrlen(app_state->loaded_images);
	ASSERT(image_count >= 0);
	app_state->is_any_image_loaded = (image_count > 0);

	if (!app_state->is_export_in_progress) {
		viewer_process_completion_queue(app_state);
	}

	if (image_count == 0) {
		if (app_state->is_window_title_set_for_image) {
			reset_window_title(app_state->main_window);
			app_state->is_window_title_set_for_image = false;
		}
		//load_generic_file(app_state, "test.jpeg");
		do_after_scene_render(app_state, input);
		return;
	}

	image_t* displayed_image = app_state->loaded_images + app_state->displayed_image;
	ASSERT(displayed_image->type == IMAGE_TYPE_WSI);

	// Do actions that need to performed only once, when a new image has been loaded
	if (displayed_image->is_freshly_loaded) {
		set_window_title(app_state->main_window, displayed_image->name);
		app_state->is_window_title_set_for_image = true;
		// Workaround for drag onto window being registered as a click
		input->mouse_buttons[0].down = false;
		input->mouse_buttons[0].transition_count = 0;
		displayed_image->is_freshly_loaded = false;
	}

	if (input) {
		if (input->are_any_buttons_down) app_state->allow_idling_next_frame = false;

		if (input->mouse_moved) {
			app_state->seconds_without_mouse_movement = 0.0f;
		} else {
			app_state->seconds_without_mouse_movement += delta_t;
		}

		if (was_key_pressed(input, KEY_W) && input->keyboard.key_ctrl.down) {
			menu_close_file(app_state);
			do_after_scene_render(app_state, input);
			return;
		}

		if (gui_want_capture_mouse) {
			// ignore mouse input
		} else {
			if (was_button_released(&input->mouse_buttons[0]) && !scene->suppress_next_click) {
				float drag_distance = v2f_length(scene->cumulative_drag_vector);
				if (drag_distance < CLICK_DRAG_TOLERANCE) {
					scene->clicked = true;
				}
			}
			if (was_button_released(&input->mouse_buttons[1])) {
				// Right click doesn't drag the scene, so we can be a bit more tolerant without confusing drags with clicks.
				scene->right_clicked = true;
//				scene->right_clicked_xy = input->mouse_xy;
				/*float drag_distance = v2f_length(scene->cumulative_drag_vector);
				if (drag_distance < 30.0f) {

				}*/
			}

			if (input->mouse_buttons[0].down) {
				// Mouse drag.
				rect2i valid_drag_start_rect = {0, 0, (i32)(client_width * app_state->display_scale_factor), (i32)(client_height * app_state->display_scale_factor)};
				if (input->mouse_buttons[0].transition_count != 0) {
					// Don't start dragging if clicked outside the window
					if (is_point_inside_rect2i(valid_drag_start_rect, V2I((i32)input->mouse_xy.x, (i32)input->mouse_xy.y))) {
						scene->is_dragging = true; // drag start
						scene->drag_started = true;
						scene->cumulative_drag_vector = v2f();
						mouse_hide();
//					    console_print("Drag started: x=%d y=%d\n", input->mouse_xy.x, input->mouse_xy.y);
					}
				} else if (scene->is_dragging) {
					// already started dragging on a previous frame
					scene->drag_vector = input->drag_vector;
					scene->cumulative_drag_vector.x += scene->drag_vector.x;
					scene->cumulative_drag_vector.y += scene->drag_vector.y;
				}
				input->drag_vector = v2f();
			} else {
				if (input->mouse_buttons[0].transition_count != 0) {
					mouse_show();
					scene->is_dragging = false;
					scene->drag_ended = true;
//			        console_print("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
				}
				scene->suppress_next_click = false;
			}
		}
	}

	if (displayed_image->type == IMAGE_TYPE_WSI) {
		if (scene->need_zoom_reset) {
			float times_larger_x = (float)displayed_image->width_in_pixels / (float)client_width;
			float times_larger_y = (float)displayed_image->height_in_pixels / (float)client_height;
			float times_larger = MAX(times_larger_x, times_larger_y);
			float desired_zoom_pos = ceilf(log2f(times_larger * 1.1f));

			// By default, allow zooming in up to 2x native resolution
			viewer_min_level = -1;
			// If the image is small, allow zooming in further
			if (desired_zoom_pos < 2.0f) {
				viewer_min_level = (i32)desired_zoom_pos - 3;
			}
			// Don't set the initial zoom level past native resolution for small images
			if (desired_zoom_pos < 0.0f) {
				desired_zoom_pos = 0.0f;
			}

			init_zoom_state(&scene->zoom, desired_zoom_pos, 1.0f, displayed_image->mpp_x, displayed_image->mpp_y);
			scene->camera.x = displayed_image->width_in_um / 2.0f;
			scene->camera.y = displayed_image->height_in_um / 2.0f;

			scene->need_zoom_reset = false;
		}
		scene->is_mpp_known = displayed_image->is_mpp_known;

		zoom_state_t old_zoom = scene->zoom;

		scene->r_minus_l = scene->zoom.pixel_width * (float) client_width;
		scene->t_minus_b = scene->zoom.pixel_height * (float) client_height;

		scene_update_camera_bounds(scene);
		scene->mouse = scene->camera;

		if (input) {

			scene_update_mouse_pos(app_state, scene, input->mouse_xy);

			if (scene->right_clicked) {
				scene->right_clicked_pos = scene->mouse;
			}

			i32 dlevel = 0;
			bool32 used_mouse_to_zoom = false;

			// Zoom in or out using the mouse wheel.
			if (!gui_want_capture_mouse && input->mouse_z != 0) {
				dlevel = (input->mouse_z > 0 ? -1 : 1);
				used_mouse_to_zoom = true;
			}

			float key_repeat_interval = 0.15f; // in seconds

			scene->control = v2f();

			if (!gui_want_capture_keyboard) {

				scene->control = get_2d_control_from_input(input);
				float control_length = v2f_length(scene->control);
				if (control_length > 0.0f) {
					scene->time_since_control_start += delta_t;
				} else {
					scene->time_since_control_start = 0.0f;
				}

				scene->panning_velocity = viewer_do_2d_control(scene->panning_velocity, scene->control, delta_t, scene->time_since_control_start, input->keyboard.key_shift.down);

				// Zoom out using Z or /
				if (is_key_down(input, KEY_Z) || is_key_down(input, KEY_Slash)) {

					if (was_key_pressed(input, KEY_Z) || was_key_pressed(input, KEY_Slash)) {
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
				if (is_key_down(input, KEY_X) || is_key_down(input, KEY_Period)) {

					if (was_key_pressed(input, KEY_X) || was_key_pressed(input, KEY_Period)) {
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
			}

			if (dlevel != 0) {
//		        console_print("mouse_z = %d\n", input->mouse_z);

				i32 new_level = scene->zoom.level + dlevel;
				if (scene->need_zoom_animation) {
					i32 residual_dlevel = scene->zoom_target_state.level - scene->zoom.level;
					new_level += residual_dlevel;
				}
				new_level = CLAMP(new_level, viewer_min_level, viewer_max_level);
				zoom_state_t new_zoom = scene->zoom;
				zoom_update_pos(&new_zoom, (float) new_level);

				if (new_zoom.level != old_zoom.level) {
					if (used_mouse_to_zoom) {
						scene->zoom_pivot = scene->mouse;
						scene->zoom_pivot.x = CLAMP(scene->zoom_pivot.x, 0, displayed_image->width_in_um);
						scene->zoom_pivot.y = CLAMP(scene->zoom_pivot.y, 0, displayed_image->height_in_um);
					} else {
						scene->zoom_pivot = scene->camera;
					}
					scene->zoom_target_state = new_zoom;
					scene->need_zoom_animation = true;
				}

			}

			if (scene->need_zoom_animation) {
				float d_zoom = scene->zoom_target_state.pos - scene->zoom.pos;

				float abs_d_zoom = fabsf(d_zoom);
				if (abs_d_zoom < 1e-5f) {
					scene->need_zoom_animation = false;
				}
				float sign_d_zoom = signbit(d_zoom) ? -1.0f : 1.0f;
				float linear_catch_up_speed = 12.0f * delta_t;
				float exponential_catch_up_speed = 15.0f * delta_t;
				if (abs_d_zoom > linear_catch_up_speed) {
					d_zoom = (linear_catch_up_speed + (abs_d_zoom - linear_catch_up_speed) * exponential_catch_up_speed) *
					         sign_d_zoom;
				}

				zoom_update_pos(&scene->zoom, scene->zoom.pos + d_zoom);

				// get the relative position of the pivot point on the screen (with x and y between 0 and 1)
				v2f pivot_relative_to_screen = scene->zoom_pivot;
				pivot_relative_to_screen.x -= scene->camera_bounds.min.x;
				pivot_relative_to_screen.y -= scene->camera_bounds.min.y;
				pivot_relative_to_screen.x /= (float)scene->r_minus_l;
				pivot_relative_to_screen.y /= (float)scene->t_minus_b;

				// recalculate the camera position
				scene->r_minus_l = scene->zoom.pixel_width * (float) client_width;
				scene->t_minus_b = scene->zoom.pixel_height * (float) client_height;
				scene->camera_bounds = bounds_from_pivot_point(scene->zoom_pivot, pivot_relative_to_screen, scene->r_minus_l, scene->t_minus_b);
				scene->camera.x = (scene->camera_bounds.right + scene->camera_bounds.left) / 2.0f;
				scene->camera.y = (scene->camera_bounds.top + scene->camera_bounds.bottom) / 2.0f;

				// camera updated, need to updated mouse position
				scene_update_mouse_pos(app_state, scene, input->mouse_xy);

			}

			if (scene->need_zoom_animation) {
				app_state->allow_idling_next_frame = false;
			}

			// Panning should be faster when zoomed in very far.
			float panning_multiplier = 1.0f + 3.0f * ((float) viewer_max_level - scene->zoom.pos) / (float) viewer_max_level;
            panning_multiplier *= app_state->display_scale_factor;

			// Panning using the arrow or WASD keys.
			float panning_speed = app_state->keyboard_base_panning_speed * 100.0f * delta_t * panning_multiplier;
			bool panning = false;
			if (scene->panning_velocity.y != 0.0f) {
				scene->camera.y += scene->zoom.pixel_height * panning_speed * scene->panning_velocity.y;
				panning = true;
			}
			if (scene->panning_velocity.x != 0.0f) {
				scene->camera.x += scene->zoom.pixel_height * panning_speed * scene->panning_velocity.x;
				panning = true;
			}
			if (panning && app_state->seconds_without_mouse_movement > 0.25f) {
				mouse_hide();
			}

			// camera has been updated (now we need to recalculate some things)
			scene->r_minus_l = scene->zoom.pixel_width * (float) client_width;
			scene->t_minus_b = scene->zoom.pixel_height * (float) client_height;
			scene_update_camera_bounds(scene);
			scene_update_mouse_pos(app_state, scene, input->mouse_xy);

			u32 key_modifiers_without_shift = input->keyboard.modifiers & ~KMOD_SHIFT;
			if (was_key_pressed(input, KEY_G) && key_modifiers_without_shift == KMOD_CTRL) {
				scene->enable_grid = !scene->enable_grid;
			}
			if (was_key_pressed(input, KEY_B) && key_modifiers_without_shift == KMOD_CTRL) {
				scene->scale_bar.enabled = !scene->scale_bar.enabled;
			}

#if ENABLE_INSERT_TOOLS
			if (was_key_pressed(input, KEY_Q) && key_modifiers_without_shift == 0) {
				viewer_switch_tool(app_state, TOOL_CREATE_POINT);
			} else if (was_key_pressed(input, KEY_M) && key_modifiers_without_shift == 0) {
				viewer_switch_tool(app_state, TOOL_CREATE_LINE);
			} else if (was_key_pressed(input, KEY_F) && key_modifiers_without_shift == 0) {
				viewer_switch_tool(app_state, TOOL_CREATE_FREEFORM);
//			} else if (was_key_pressed(input, KEY_E) && key_modifiers_without_shift == 0) {
//				viewer_switch_tool(app_state, TOOL_CREATE_ELLIPSE);
			} else if (was_key_pressed(input, KEY_R) && key_modifiers_without_shift == 0) {
				viewer_switch_tool(app_state, TOOL_CREATE_RECTANGLE);
//			} else if (was_key_pressed(input, KEY_T) && key_modifiers_without_shift == 0) {
//				viewer_switch_tool(app_state, TOOL_CREATE_TEXT);
			}
#endif

			/*if (was_key_pressed(input, KEY_O)) {
				app_state->mouse_mode = MODE_CREATE_SELECTION_BOX;
//				console_print("switching to creation mode\n");
			}*/

			// Debug feature: view 'frozen' outline of camera bounds
#if DO_DEBUG
			if (!gui_want_capture_keyboard && was_key_pressed(input, KEY_F8)) {
				if (scene->restrict_load_bounds) {
					scene->restrict_load_bounds = false;
				} else {
					scene->tile_load_bounds = scene->camera_bounds;
					scene->restrict_load_bounds = true;
				}
			}
			if (scene->restrict_load_bounds) {
				gui_draw_bounds_in_scene(scene->tile_load_bounds, RGBA(0,0,0,128), 2.0f, scene);
			}
#endif

			if (!gui_want_capture_keyboard && was_key_pressed(input, KEY_P)) {
				app_state->use_image_adjustments = !app_state->use_image_adjustments;
			}
			update_scale_bar(scene, &scene->scale_bar);

			if (app_state->mouse_mode == MODE_VIEW) {
				v2f mouse = input->mouse_xy;
				if (scene->drag_started && v2f_between_points(mouse, scene->scale_bar.pos, scene->scale_bar.pos_max)) {
					scene->scale_bar.drag_start_offset = V2F(mouse.x - scene->scale_bar.pos.x,
															 mouse.y - scene->scale_bar.pos.y);
					app_state->mouse_mode = MODE_DRAG_SCALE_BAR;
				}
			}

			if (app_state->mouse_mode == MODE_VIEW) {
				if (scene->is_dragging && v2f_length(scene->cumulative_drag_vector) >= CLICK_DRAG_TOLERANCE) {
					float final_multiplier = panning_multiplier * app_state->mouse_sensitivity * 0.1f;
					scene->camera.x -= scene->drag_vector.x * scene->zoom.pixel_width * final_multiplier;
					scene->camera.y -= scene->drag_vector.y * scene->zoom.pixel_height * final_multiplier;

					// camera has been updated (now we need to recalculate some things)
					scene_update_camera_bounds(scene);
					scene_update_mouse_pos(app_state, scene, input->mouse_xy);
				}

				if (!gui_want_capture_mouse) {
					// try to hover over / select an annotation
					if (scene->annotation_set.stored_annotation_count > 0) {
						interact_with_annotations(app_state, scene, input);
//				    	    float selection_ms = get_seconds_elapsed(select_begin, get_clock()) * 1000.0f;
//			    	    	console_print("Selecting took %g ms.\n", selection_ms);
					}

				}

			} else if (app_state->mouse_mode == MODE_CREATE_SELECTION_BOX) {
				if (!gui_want_capture_mouse) {
					if (scene->drag_started) {
						scene->selection_box = RECT2F(scene->mouse.x, scene->mouse.y, 0.0f, 0.0f);
						scene->has_selection_box = true;
					} else if (scene->is_dragging) {
						scene->selection_box.w = scene->mouse.x - scene->selection_box.x;
						scene->selection_box.h = scene->mouse.y - scene->selection_box.y;
					} else if (scene->drag_ended) {
						app_state->mouse_mode = MODE_VIEW;

					}

				}
			} else if (app_state->mouse_mode == MODE_INSERT) {
				if (!gui_want_capture_mouse) {
					if (app_state->mouse_tool == TOOL_CREATE_POINT) {
						if (scene->clicked) {
							create_point_annotation(&scene->annotation_set, scene->mouse);
//							console_print("Creating a point\n");
							viewer_switch_tool(app_state, TOOL_NONE);
						}
					} else if (app_state->mouse_tool == TOOL_CREATE_LINE) {
						do_mouse_tool_create_line(app_state, input, scene, annotation_set);
					} else if (app_state->mouse_tool == TOOL_CREATE_FREEFORM) {
						do_mouse_tool_create_freeform(app_state, input, scene, annotation_set);
					} else if (app_state->mouse_tool == TOOL_CREATE_ELLIPSE) {
						if (scene->drag_started) {
							create_ellipse_annotation(annotation_set, scene->mouse);
						} else if (scene->is_dragging) {
							if (annotation_set->editing_annotation_index >= 0) {
								annotation_t* ellipse = get_active_annotation(annotation_set, annotation_set->editing_annotation_index);
								ellipse->p1 = scene->mouse;
							}
						} else if (scene->drag_ended) {
							// finalize ellipse
							viewer_switch_tool(app_state, TOOL_NONE);
						}
					} else if (app_state->mouse_tool == TOOL_CREATE_RECTANGLE) {
						do_mouse_tool_create_rectangle(app_state, input, scene, annotation_set);
					} else if (app_state->mouse_tool == TOOL_CREATE_TEXT) {
						if (scene->clicked) {
							// create text
							viewer_switch_tool(app_state, TOOL_NONE);
						}
					}
				}
			}

			// Determine whether exporting a region is possible, and precalculate the (level 0) pixel bounds for exporting.
			ASSERT(displayed_image->mpp_x > 0.0f && displayed_image->mpp_y > 0.0f);
			if (displayed_image->backend == IMAGE_BACKEND_TIFF) {
				if (scene->has_selection_box) {
					rect2f recanonicalized_selection_box = rect2f_recanonicalize(&scene->selection_box);
					bounds2f selection_bounds = rect2f_to_bounds(recanonicalized_selection_box);
					scene->crop_bounds = selection_bounds;
					scene->selection_pixel_bounds = world_bounds_to_pixel_bounds(&selection_bounds, displayed_image->mpp_x, displayed_image->mpp_y);
					scene->can_export_region = true;
				} else if (scene->is_cropped) {
					scene->selection_pixel_bounds = world_bounds_to_pixel_bounds(&scene->crop_bounds, displayed_image->mpp_x, displayed_image->mpp_y);
					scene->can_export_region = true;
				} else {
					scene->can_export_region = false;
				}
			} else {
				scene->can_export_region = false;
			}


			// Update dragging of objects
			if (app_state->mouse_mode == MODE_DRAG_ANNOTATION_NODE) {
				if (scene->is_dragging) {
					do_drag_annotation_node(scene);
				} else if (scene->drag_ended) {
					app_state->mouse_mode = MODE_VIEW;
//					scene->annotation_set.is_edit_mode = false;

				}

			} else if (app_state->mouse_mode == MODE_DRAG_SCALE_BAR) {
				scale_bar_t* scale_bar = &scene->scale_bar;
				if (scene->is_dragging && v2f_length(scene->cumulative_drag_vector) >= CLICK_DRAG_TOLERANCE) {
					// Update the position of the scale bar while dragging the mouse.
#if WINDOWS
					scale_bar->pos.x = input->mouse_xy.x - scale_bar->drag_start_offset.x;
					scale_bar->pos.y = input->mouse_xy.y - scale_bar->drag_start_offset.y;
#else
					// TODO: figure out why on macOS, input->mouse_xy is {0,0} while dragging
					scale_bar->pos.x += scene->drag_vector.x;
					scale_bar->pos.y += scene->drag_vector.y;
#endif
//					console_print_verbose("mouse = %d, %d\n", input->mouse_xy.x, input->mouse_xy.y);
					update_scale_bar(scene, scale_bar);
				} else if (scene->drag_ended) {
					app_state->mouse_mode = MODE_VIEW;
					update_scale_bar(scene, scale_bar);
				}
			}


			// Ctrl+S: save annotations manually
			if (scene->annotation_set.modified) {
				if (was_key_pressed(input, KEY_S) && input->keyboard.key_ctrl.down) {
					save_annotations(app_state, &scene->annotation_set, true);
				}
			}

			/*if (scene->clicked && !gui_want_capture_mouse) {
				scene->has_selection_box = false; // deselect selection box
			}*/

		}

		draw_grid(scene);
		draw_annotations(app_state, scene, &scene->annotation_set, scene->camera_bounds.min);
		draw_selection_box(scene);
		draw_scale_bar(&scene->scale_bar);
	}


	if (was_key_pressed(input, KEY_F5) || was_key_pressed(input, KEY_Tab)) {
		scene->active_layer++;
		if (scene->active_layer == image_count) {
			scene->active_layer = 0;
		}
		if (scene->active_layer == 0) {
			target_layer_t = 0.0f;
		} else if (scene->active_layer == 1) {
			target_layer_t = 1.0f;
		}
	}
	{
		float adjust_speed = 8.0f * delta_t;
		if (layer_t < target_layer_t) {
			float delta = MIN((target_layer_t - layer_t), adjust_speed);
			layer_t += delta;
		} else if (layer_t > target_layer_t) {
			float delta = MIN((layer_t - target_layer_t), adjust_speed);
			layer_t -= delta;
		}
	}



	if (image_count <= 1) {
		// Render everything at once
//		glBindFramebuffer(GL_FRAMEBUFFER, 0); // Redundant
		viewer_clear_and_set_up_framebuffer(app_state->clear_color, client_width, client_height);
		image_t* image = app_state->loaded_images + 0;
		update_and_render_image(app_state, input, delta_t, image);
	} else {
		// We are rendering the scene in two passes.
		// 1: render to framebuffer
		// 2: blit framebuffer to screen

		if (!layer_framebuffers_initialized) {
			init_layer_framebuffers(app_state);
		}

		for (i32 image_index = 0; image_index < image_count; ++image_index) {

			framebuffer_t* framebuffer = layer_framebuffers + image_index;
			maybe_resize_overlay(framebuffer, client_width, client_height);

			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer->framebuffer);
			viewer_clear_and_set_up_framebuffer(app_state->clear_color, client_width, client_height);

//			if (image_index == scene->active_layer) {
				image_t* image = app_state->loaded_images + image_index;
				update_and_render_image(app_state, input, delta_t, image);
//			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		// Second pass
//		glBindFramebuffer(GL_FRAMEBUFFER, 0); // Redundant
		viewer_clear_and_set_up_framebuffer(app_state->clear_color, client_width, client_height);

		glUseProgram(finalblit_shader.program);
		glUniform1f(finalblit_shader.u_t, layer_t);
		glBindVertexArray(vao_screen);
		glDisable(GL_DEPTH_TEST); // because we want to make sure the quad always renders in front of everything else
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, layer_framebuffers[0].texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, layer_framebuffers[1].texture);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);
	}


	do_after_scene_render(app_state, input);
}

void do_after_scene_render(app_state_t* app_state, input_t* input) {
	if (was_key_pressed(input, KEY_F1)) {
		show_demo_window = !show_demo_window;
	}
	if (was_key_pressed(input, KEY_F3) || was_key_pressed(input, KEY_Grave)) {
		show_console_window = !show_console_window;
	}
	// TODO: fix 'sticky' Alt key after Alt+Enter
	if (was_key_pressed(input, KEY_F12) && input->keyboard.key_alt.down) {
		show_menu_bar = !show_menu_bar;
	}
	if (was_key_pressed(input, KEY_F6)) {
		// Load the next image dragged on top of the window as a new layer/overlay instead of a base image.
		if (arrlen(app_state->loaded_images) >= 1) {
			load_next_image_as_overlay = true;
		}
	}
	if (!gui_want_capture_keyboard) {
		if (was_key_pressed(input, KEY_L)) {
			show_layers_window = !show_layers_window;
		}
		if (was_key_pressed(input, KEY_H)) {
			app_state->scene.enable_annotations = !app_state->scene.enable_annotations;
		}
	}


	gui_draw(app_state, curr_input, app_state->client_viewport.w, app_state->client_viewport.h);
//	last_section = profiler_end_section(last_section, "gui draw", 10.0f);

	autosave(app_state, false);
//	last_section = profiler_end_section(last_section, "autosave", 10.0f);

	if (need_quit) {
		if (!app_state->enable_autosave && app_state->scene.annotation_set.modified) {
			show_save_quit_prompt = true;
		} else {
			is_program_running = false;
		}
	}

	//glFinish();

	float update_and_render_time = get_seconds_elapsed(app_state->last_frame_start, get_clock());
//	console_print("Frame time: %g ms\n", update_and_render_time * 1000.0f);

	++app_state->frame_counter;
}