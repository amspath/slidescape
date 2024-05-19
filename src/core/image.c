/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2024  Pieter Valkema

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
#include "platform.h"
#include "image.h"
#include "jpeg_decoder.h"

#define STBI_ASSERT(x) ASSERT(x)
#include "stb_image.h" // for stbi_image_free()

#include "viewer.h" // for unload_texture()

// TODO: refcount mechanism and eviction scheme, retain tiles for re-use?
void tile_release_cache(tile_t* tile) {
	ASSERT(tile);
	if (tile->pixels) free(tile->pixels);
	tile->pixels = NULL;
	tile->is_cached = false;
	tile->need_keep_in_cache = false;
}


const char* get_image_backend_name(image_t* image) {
    const char* result = "--";
    if (image->backend == IMAGE_BACKEND_TIFF) {
        result = "TIFF";
    } else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
        result = "OpenSlide";
    } else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
        result = "iSyntax";
    } else if (image->backend == IMAGE_BACKEND_DICOM) {
        result = "DICOM";
    } else if (image->backend == IMAGE_BACKEND_STBI) {
        result = "stb_image";
    }
    return result;
}

const char* get_image_descriptive_type_name(image_t* image) {
    const char* result = "--";
    if (image->type == IMAGE_TYPE_WSI) {
        if (image->backend == IMAGE_BACKEND_TIFF) {
            result = "WSI (TIFF)";
        } else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
            result = "WSI (OpenSlide)";
        } else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
            result = "WSI (iSyntax)";
        } else if (image->backend == IMAGE_BACKEND_DICOM) {
            result = "WSI (DICOM)";
        } else if (image->backend == IMAGE_BACKEND_STBI) {
            result = "Simple image";
        }
    } else {
        result = "Unknown";
    }
    return result;
}

static void image_change_resolution(image_t* image, float mpp_x, float mpp_y) {
    image->mpp_x = mpp_x;
    image->mpp_y = mpp_y;
    image->width_in_um = image->width_in_pixels * mpp_x;
    image->height_in_um = image->height_in_pixels * mpp_y;

    if (image->type == IMAGE_TYPE_WSI) {
        // shorthand pointers for backend-specific data structure
        tiff_t* tiff = &image->tiff;
        wsi_t* openslide_image = &image->openslide_wsi;
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


// TODO: write 'drivers' / interfaces to be queried, instead of this copy-pasta

bool init_image_from_tiff(image_t* image, tiff_t tiff, bool is_overlay, image_t* parent_image) {
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
                fatal_error();
            }
            if (image->level_count > WSI_MAX_LEVELS) {
                fatal_error();
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
                    level_image->width_in_pixels = ifd->image_width;
                    level_image->height_in_pixels = ifd->image_height;
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
        } else if (tiff.is_ndpi) {
            DUMMY_STATEMENT;
        } else {
            // The image is NOT tiled
            memset(image->level_images, 0, sizeof(image->level_images));
            image->level_count = 1;
            level_image_t* level_image = image->level_images + 0;
            tiff_ifd_t* ifd = tiff.main_image_ifd;
            level_image->exists = true;
            level_image->pyramid_image_index = 0;
            level_image->downsample_factor = ifd->downsample_factor;
            level_image->width_in_pixels = ifd->image_width;
            level_image->height_in_pixels = ifd->image_height;
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
    if (is_overlay && parent_image) {
        ASSERT(parent_image->mpp_x > 0.0f && parent_image->mpp_y > 0.0f);
        image_change_resolution(image, parent_image->mpp_x, parent_image->mpp_y);
    }


    image->is_valid = true;
    image->is_freshly_loaded = true;
    return image->is_valid;
}

u8* decode_associated_image_from_isyntax(isyntax_t* isyntax, isyntax_image_t* image) {
	u32 jpeg_size = 0;
	u8* jpeg_compressed = isyntax_get_associated_image_jpeg(isyntax, image, &jpeg_size);

	// TODO: Why does this crash?
	// Apparently, there is a bug in the libjpeg-turbo implementation of jsimd_can_h2v2_fancy_upsample() when using SIMD.
	// jsimd_h2v2_fancy_upsample_avx2 writes memory out of bounds.
	// This causes the program to crash eventually when trying to free memory in free_pool().
	// When using a hardware watchpoint on the corrupted memory, the overwiting occurs in x86_64/jdsample-avx2.asm at line 358:
	//     vmovdqu     YMMWORD [rdi+3*SIZEOF_YMMWORD], ymm6
	// WORKAROUND: disabled SIMD in jsimd_can_h2v2_fancy_upsample().
	i32 channels_in_file = 0;
	u8* pixels = jpeg_decode_image(jpeg_compressed, jpeg_size, &image->width, &image->height, &channels_in_file);
	return pixels;

}

bool init_image_from_isyntax(image_t* image, isyntax_t* isyntax, bool is_overlay) {
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
            fatal_error();
        }

        for (i32 level_index = 0; level_index < image->level_count; ++level_index) {
            level_image_t* level_image = image->level_images + level_index;
            isyntax_level_t* isyntax_level = wsi_image->levels + level_index;

            level_image->exists = true;
            level_image->pyramid_image_index = level_index; // not used
            level_image->downsample_factor = exp2f((float)level_index);
            level_image->width_in_pixels = isyntax_level->width_in_tiles * isyntax->tile_width; // TODO: check that this is right...
            level_image->height_in_pixels = isyntax_level->height_in_tiles * isyntax->tile_height; // TODO: check that this is right...
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

	// TODO (pvalkema): defer this/load lazily
	isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
	if (macro_image->image_type == ISYNTAX_IMAGE_TYPE_MACROIMAGE) {
		u8* pixels = decode_associated_image_from_isyntax(isyntax, macro_image);
        if (pixels) {
            image->macro_image.pixels = pixels;
            image->macro_image.width = macro_image->width;
            image->macro_image.height = macro_image->height;
            image->macro_image.mpp = 0.0315f * 1000.0f; // apparently, always this value
            image->macro_image.world_pos.x = -((float)(wsi_image->offset_x + wsi_image->level0_padding) * isyntax->mpp_x);
            image->macro_image.world_pos.y = -((float)(wsi_image->offset_y + wsi_image->level0_padding) * isyntax->mpp_y);
            image->macro_image.is_valid = true;
        }
    }
    isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
    if (label_image->image_type == ISYNTAX_IMAGE_TYPE_LABELIMAGE) {
		u8* pixels = decode_associated_image_from_isyntax(isyntax, label_image);
        if (pixels) {
            image->label_image.pixels = pixels;
            image->label_image.width = label_image->width;
            image->label_image.height = label_image->height;
            image->label_image.mpp = 0.0315f * 1000.0f; // apparently, always this value
			if (image->macro_image.is_valid) {
				image->label_image.world_pos.x = image->macro_image.world_pos.x // macro image left side (origin)
					+ image->macro_image.width * image->macro_image.mpp // macro image right side
					+ image->label_image.width * image->label_image.mpp; // add label height (will rotate 90 degrees to the right to 'fit' in place)
				image->label_image.world_pos.y = image->macro_image.world_pos.y;
			}
	        image->label_image.is_valid = true;
        }
    }


    image->is_valid = true;
    image->is_freshly_loaded = true;
    return image->is_valid;
}

bool init_image_from_dicom(image_t* image, dicom_series_t* dicom, bool is_overlay) {
    image->type = IMAGE_TYPE_WSI;
    image->backend = IMAGE_BACKEND_DICOM;
    image->dicom = *dicom;
    dicom = &image->dicom;
    image->is_freshly_loaded = true;

    dicom_instance_t* base_level_instance = dicom->wsi.level_instances[0];
    ASSERT(base_level_instance);
    if (!base_level_instance) return false;

    image->mpp_x = dicom->wsi.mpp_x;
    image->mpp_y = dicom->wsi.mpp_y;
    image->is_mpp_known = dicom->wsi.is_mpp_known;
    if (image->mpp_x <= 0.0f || image->mpp_y <= 0.0f) {
        image->is_mpp_known = false;
        image->mpp_x = 1.0f;
        image->mpp_y = 1.0f;
    }

    image->tile_width = base_level_instance->columns;
    image->tile_height = base_level_instance->rows;
    image->width_in_pixels = base_level_instance->total_pixel_matrix_columns;
    image->width_in_um = base_level_instance->total_pixel_matrix_columns * image->mpp_x;
    image->height_in_pixels = base_level_instance->total_pixel_matrix_rows;
    image->height_in_um = base_level_instance->total_pixel_matrix_rows * image->mpp_y;
    // TODO: fix code duplication with tiff_deserialize()
    if (dicom->wsi.level_count > 0 && image->tile_width) {

        memset(image->level_images, 0, sizeof(image->level_images));
        image->level_count = dicom->wsi.level_count;

        if (image->level_count > WSI_MAX_LEVELS) {
            fatal_error();
        }

        for (i32 level_index = 0; level_index < image->level_count; ++level_index) {
            level_image_t* level_image = image->level_images + level_index;
            dicom_instance_t* level_instance = dicom->wsi.level_instances[level_index];

            level_image->exists = true;
            level_image->needs_indexing = level_instance->is_pixel_data_encapsulated && !level_instance->are_all_offsets_read;
            level_image->pyramid_image_index = level_index; // not used
            level_image->downsample_factor = exp2f((float)level_index);
            level_image->width_in_pixels = level_instance->total_pixel_matrix_columns; // TODO: check that this is right
            level_image->height_in_pixels = level_instance->total_pixel_matrix_rows; // TODO: check that this is right
            level_image->width_in_tiles = level_instance->width_in_tiles;
            ASSERT(level_image->width_in_tiles > 0);
            level_image->height_in_tiles = level_instance->height_in_tiles;
            ASSERT(level_image->height_in_tiles > 0);
            level_image->tile_count = level_instance->tile_count;
            level_image->tile_width = level_instance->columns;
            level_image->tile_height = level_instance->rows;
            if (level_instance->rows != image->tile_width) {
                ASSERT(!"tile width is not equal across all levels");
                return false;
            }
            if (level_instance->columns != image->tile_height) {
                ASSERT(!"tile height is not equal across all levels");
                return false;
            }
            level_image->um_per_pixel_x = level_image->downsample_factor * dicom->wsi.mpp_x;
            level_image->um_per_pixel_y = level_image->downsample_factor * dicom->wsi.mpp_y;
            level_image->x_tile_side_in_um = level_image->um_per_pixel_x * level_instance->columns;
            level_image->y_tile_side_in_um = level_image->um_per_pixel_x * level_instance->rows;
            ASSERT(level_image->x_tile_side_in_um > 0);
            ASSERT(level_image->y_tile_side_in_um > 0);
            level_image->origin_offset = level_instance->origin_offset;
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

                dicom_tile_t* dicom_tile = level_instance->tiles + tile_index;
                if (!dicom_tile->exists) {
                    tile->is_empty = true;
                }
            }
            DUMMY_STATEMENT;
        }
    }

    /*isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
    if (macro_image->image_type == ISYNTAX_IMAGE_TYPE_MACROIMAGE) {
        if (macro_image->pixels) {
            image->macro_image.pixels = macro_image->pixels;
            macro_image->pixels = NULL; // transfer ownership
            image->macro_image.width = macro_image->width;
            image->macro_image.height = macro_image->height;
            image->macro_image.mpp = 0.0315f * 1000.0f; // apparently, always this value
            image->macro_image.world_pos.x = -((float)base_level_instance->offset_x * isyntax->mpp_x);
            image->macro_image.world_pos.y = -((float)base_level_instance->offset_y * isyntax->mpp_y);
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
    }*/


    image->is_valid = true;
    image->is_freshly_loaded = true;
    return image->is_valid;
}

bool init_image_from_stbi(image_t* image, simple_image_t* simple, bool is_overlay) {
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
    level_image->width_in_pixels = image->width_in_pixels;
    level_image->height_in_pixels = image->height_in_pixels;
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
    level_image->origin_offset = (v2f){};
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

// TODO: solve issue on macOS with OpenSlide backend, not all tiles displaying properly (Retina screen related maybe?)

void init_image_from_openslide(image_t* image, wsi_t* wsi, bool is_overlay) {
    image->type = IMAGE_TYPE_WSI;
    image->backend = IMAGE_BACKEND_OPENSLIDE;
    image->openslide_wsi = *wsi;
    image->is_freshly_loaded = true;
    image->mpp_x = wsi->mpp_x;
    image->mpp_y = wsi->mpp_y;
    image->is_mpp_known = wsi->is_mpp_known;
    image->tile_width = wsi->tile_width;
    image->tile_height = wsi->tile_height;
    image->width_in_pixels = wsi->width;
    image->width_in_um = (float)wsi->width * wsi->mpp_x;
    image->height_in_pixels = wsi->height;
    image->height_in_um = (float)wsi->height * wsi->mpp_y;
    ASSERT(wsi->levels[0].x_tile_side_in_um > 0);
    if (wsi->level_count > 0 && wsi->levels[0].x_tile_side_in_um > 0) {
        ASSERT(wsi->max_downsample_level >= 0);

        memset(image->level_images, 0, sizeof(image->level_images));
        image->level_count = wsi->max_downsample_level + 1;

        // TODO: check against downsample level, see init_image_from_tiff()
        if (wsi->level_count > image->level_count) {
            fatal_error();
        }
        if (image->level_count > WSI_MAX_LEVELS) {
            fatal_error();
        }

        i32 wsi_level_index = 0;
        i32 next_wsi_level_index_to_check_for_match = 0;
        wsi_level_t *wsi_file_level = wsi->levels + wsi_level_index;
        for (i32 downsample_level = 0; downsample_level < image->level_count; ++downsample_level) {
            level_image_t *downsample_level_image = image->level_images + downsample_level;
            i32 wanted_downsample_level = downsample_level;
            bool found_wsi_level_for_downsample_level = false;
            for (wsi_level_index = next_wsi_level_index_to_check_for_match;
                wsi_level_index < wsi->level_count; ++wsi_level_index) {
                wsi_file_level = wsi->levels + wsi_level_index;
                if (wsi_file_level->downsample_level == wanted_downsample_level) {
                    // match!
                    found_wsi_level_for_downsample_level = true;
                    next_wsi_level_index_to_check_for_match =
                            wsi_level_index + 1; // next iteration, don't reuse the same WSI level!
                    break;
                }
            }

            if (found_wsi_level_for_downsample_level) {
                // The current downsampling level is backed by a corresponding IFD level image in the TIFF.
                downsample_level_image->exists = true;
                downsample_level_image->pyramid_image_index = wsi_level_index;
                downsample_level_image->downsample_factor = wsi_file_level->downsample_factor;
                downsample_level_image->tile_count = wsi_file_level->tile_count;
                downsample_level_image->width_in_pixels = wsi_file_level->width;
                downsample_level_image->height_in_pixels = wsi_file_level->height;
                downsample_level_image->width_in_tiles = wsi_file_level->width_in_tiles;
                ASSERT(downsample_level_image->width_in_tiles > 0);
                downsample_level_image->height_in_tiles = wsi_file_level->height_in_tiles;
                downsample_level_image->tile_width = wsi_file_level->tile_width;
                downsample_level_image->tile_height = wsi_file_level->tile_height;
#if DO_DEBUG
                if (downsample_level_image->tile_width != image->tile_width) {
                        console_print("Warning: level image %d (WSI level #%d) tile width (%d) does not match base level (%d)\n", downsample_level, wsi_level_index, downsample_level_image->tile_width, image->tile_width);
                    }
                    if (downsample_level_image->tile_height != image->tile_height) {
                        console_print("Warning: level image %d (WSI level #%d) tile width (%d) does not match base level (%d)\n", downsample_level, wsi_level_index, downsample_level_image->tile_width, image->tile_width);
                    }
#endif
                downsample_level_image->um_per_pixel_x = wsi_file_level->um_per_pixel_x;
                downsample_level_image->um_per_pixel_y = wsi_file_level->um_per_pixel_y;
                downsample_level_image->x_tile_side_in_um = wsi_file_level->x_tile_side_in_um;
                downsample_level_image->y_tile_side_in_um = wsi_file_level->y_tile_side_in_um;
                ASSERT(downsample_level_image->x_tile_side_in_um > 0);
                ASSERT(downsample_level_image->y_tile_side_in_um > 0);
                downsample_level_image->tiles = (tile_t *) calloc(1, wsi_file_level->tile_count * sizeof(tile_t));
                // Note: OpenSlide doesn't allow us to quickly check if tiles are empty or not.
                for (i32 tile_index = 0; tile_index < downsample_level_image->tile_count; ++tile_index) {
                    tile_t *tile = downsample_level_image->tiles + tile_index;
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
                downsample_level_image->downsample_factor = exp2f((float) wanted_downsample_level);
                // Just in case anyone tries to divide by zero:
                downsample_level_image->tile_width = image->tile_width;
                downsample_level_image->tile_height = image->tile_height;
                downsample_level_image->um_per_pixel_x = image->mpp_x * downsample_level_image->downsample_factor;
                downsample_level_image->um_per_pixel_y = image->mpp_y * downsample_level_image->downsample_factor;
                downsample_level_image->x_tile_side_in_um =
                        downsample_level_image->um_per_pixel_x * (float) wsi->levels[0].tile_width;
                downsample_level_image->y_tile_side_in_um =
                        downsample_level_image->um_per_pixel_y * (float) wsi->levels[0].tile_height;
            }

        }

    }
    ASSERT(image->level_count > 0);
    image->is_valid = true;

}

// TODO: optimize?
float f32_rgb_to_f32_y(float R, float G, float B) {
    float Co  = R - B;
    float tmp = B + Co/2;
    float Cg  = G - tmp;
    float Y   = tmp + Cg/2;
    return Y;
}

// TODO: optimize?
void image_convert_u8_rgba_to_f32_y(u8* src, float* dest, i32 w, i32 h, i32 components) {
    if (components == 3 || components == 4) {
        i32 row_elements = w * components;
        for (i32 y = 0; y < h; ++y) {
            u8* src_pixel = src + y * row_elements;
            float* dst_pixel = dest + y * w;
            for (i32 x = 0; x < w; ++x) {
                float r = (float)(src_pixel[0]) * (1.0f/255.0f);
                float g = (float)(src_pixel[1]) * (1.0f/255.0f);
                float b = (float)(src_pixel[2]) * (1.0f/255.0f);
                float luminance = f32_rgb_to_f32_y(r, g, b);
                *dst_pixel++ = luminance;
                src_pixel += components;
            }
        }
    } else {
        printf("number of components (%d) not supported\n", components);
        exit(1);
    }
}

bool image_read_region(image_t* image, i32 level, i32 x, i32 y, i32 w, i32 h, void* dest, pixel_format_enum desired_pixel_format) {
    ASSERT(dest != NULL);

	if (w <= 0 || h <= 0) {
		return false;
	}
	if (level < 0 || level >= image->level_count) {
		console_print_error("image_read_region(): level %d out of bounds (valid range 0-%d)\n", level, image->level_count-1);
		return false;
	}

    pixel_format_enum intermediate_pixel_format = PIXEL_FORMAT_UNDEFINED;
    void* intermediate_pixel_buffer = NULL;

    switch (image->backend) {
        default: {
            console_print_error("image_read_region(): not implemented for backend '%s'\n", get_image_backend_name(image));
            return false;
        } break;
        case IMAGE_BACKEND_OPENSLIDE: {
            intermediate_pixel_format = PIXEL_FORMAT_U8_BGRA;
            if (desired_pixel_format == intermediate_pixel_format) {
                intermediate_pixel_buffer = (uint32_t*) dest;
            } else {
                intermediate_pixel_buffer = malloc(w * h * sizeof(uint32_t));
            }
            openslide.read_region(image->openslide_wsi.osr, intermediate_pixel_buffer, x, y, level, w, h);
        } break;
		case IMAGE_BACKEND_TIFF:
		case IMAGE_BACKEND_DICOM:
		case IMAGE_BACKEND_STBI:{
			level_image_t* level_image = image->level_images + level;

			bounds2i level_tiles_bounds = BOUNDS2I(0, 0, (i32)level_image->width_in_tiles, (i32)level_image->height_in_tiles);
			i32 local_x = x << level;
			i32 local_y = y << level;
			i32 tile_width = level_image->tile_width;
			i32 tile_height = level_image->tile_height;
			i32 tile_x0 = local_x / tile_width;
			i32 tile_y0 = local_y / tile_height;
			i32 tile_x1 = (local_x + w - 1) / tile_width + 1;
			i32 tile_y1 = (local_y + h - 1) / tile_height + 1;
			bounds2i region_tiles = BOUNDS2I(tile_x0, tile_y0, tile_x1, tile_y1);
			bounds2i tiles_within_level_bounds = clip_bounds2i(region_tiles, level_tiles_bounds);

			i32 width_in_tiles = tiles_within_level_bounds.right - tiles_within_level_bounds.left;
			i32 height_in_tiles = tiles_within_level_bounds.bottom - tiles_within_level_bounds.top;

			if (width_in_tiles > 0 && height_in_tiles > 0) {
				load_tile_task_t* wishlist = calloc(width_in_tiles * height_in_tiles, sizeof(load_tile_task_t));
				i32 tiles_to_load = 0;

				work_queue_t read_completion_queue = work_queue_create("/imagereadregionsem",
                                                                       width_in_tiles * height_in_tiles);

				// request tiles
				benaphore_lock(&image->lock);
				for (i32 tile_y = tiles_within_level_bounds.min.y; tile_y < tiles_within_level_bounds.max.y; ++tile_y) {
					for (i32 tile_x = tiles_within_level_bounds.min.x; tile_x < tiles_within_level_bounds.max.x; ++tile_x) {
						tile_t* tile = get_tile(level_image, tile_x, tile_y);
						if (tile->is_empty) continue; // no need to load empty tiles
						if (tile->is_cached && tile->pixels) {
							//TODO: retain
							continue; // already cached
						}
						tile->need_keep_in_cache = true;
						wishlist[tiles_to_load++] = (load_tile_task_t){
							.resource_id = image->resource_id,
							.image = image, .tile = tile, .level = level,
							.tile_x = tile->tile_x,
							.tile_y = tile->tile_y,
							.need_gpu_residency = tile->need_gpu_residency,
							.need_keep_in_cache = true,
							.completion_queue = &read_completion_queue,
                            .refcount_to_decrement = 1, // refcount will be decremented at end of thread proc load_tile_func()
						};

					}
				}

				request_tiles(image, wishlist, tiles_to_load);
				benaphore_unlock(&image->lock);

				// retrieve all requested tiles
				while (read_completion_queue.completion_count < tiles_to_load) {
					if (work_queue_is_work_in_progress(&read_completion_queue)) {
						work_queue_entry_t entry = work_queue_get_next_entry(&read_completion_queue);
						if (entry.is_valid) {
							benaphore_lock(&image->lock);
                            work_queue_mark_entry_completed(&read_completion_queue);
							viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*) entry.userdata;
							if (task->pixel_memory) {
								tile_t* tile = get_tile_from_tile_index(image, task->scale, task->tile_index);
								ASSERT(tile->pixels == NULL);
								if (task->pixel_memory) {
									tile->pixels = task->pixel_memory; // TODO: retain
									tile->is_cached = true;
								}
							}
							benaphore_unlock(&image->lock);
						}
					} else if (work_queue_is_work_waiting_to_start(&global_work_queue)) {
                        work_queue_do_work(&global_work_queue, 0);
					} else {
						platform_sleep(1);
					}
				}
                work_queue_destroy(&read_completion_queue);
			}


			intermediate_pixel_format = PIXEL_FORMAT_U8_BGRA;
			if (desired_pixel_format == intermediate_pixel_format) {
				intermediate_pixel_buffer = (uint32_t*) dest;
			} else {
				intermediate_pixel_buffer = malloc(w * h * sizeof(uint32_t));
			}


			// reconstruct the tiles into the requested region
			i32 x0_tile_offset = local_x % tile_width;
			i32 y0_tile_offset = local_y % tile_height;
			i32 x1_tile_offset = ((local_x + w - 1) % tile_width) + 1;
			i32 y1_tile_offset = ((local_y + h - 1) % tile_height) + 1;
			int bg_value = 0xFF; // for memset() (white)
			for (i32 tile_y = region_tiles.min.y; tile_y < region_tiles.max.y; ++tile_y) {
				i32 dest_y = (tile_y - region_tiles.min.y) * tile_height;
				if (tile_y > region_tiles.min.y) {
					dest_y -= y0_tile_offset;
				}

				i32 copy_height = tile_height;
				i32 copy_y0 = 0;
				i32 copy_y1 = tile_height;
				if (tile_y == region_tiles.min.y) {
					copy_height -= y0_tile_offset;
					copy_y0 = y0_tile_offset;
				}
				if (tile_y == region_tiles.max.y-1) {
					copy_height -= (tile_height - y1_tile_offset);
					copy_y1 = y1_tile_offset;
				}

				for (i32 tile_x = region_tiles.min.x; tile_x < region_tiles.max.x; ++tile_x) {
					i32 dest_x = (tile_x - region_tiles.min.x) * tile_width;
					if (tile_x > region_tiles.min.x) {
						dest_x -= x0_tile_offset;
					}

					i32 copy_width = tile_width;
					i32 copy_x0 = 0;
					i32 copy_x1 = tile_width;
					if (tile_x == region_tiles.min.x) {
						copy_width -= x0_tile_offset;
						copy_x0 = x0_tile_offset;
					}
					if (tile_x == region_tiles.max.x-1) {
						copy_width -= (tile_width - x1_tile_offset);
						copy_x1 = x1_tile_offset;
					}

					bool copied = false;
					if (tile_x >= 0 && tile_y >= 0 && tile_x < level_image->width_in_tiles && tile_y < level_image->height_in_tiles) {
						// fill the area covered by this tile (if it exists)
						tile_t* tile = get_tile(level_image, tile_x, tile_y);
						if (!tile->is_empty && tile->is_cached && tile->pixels) {
							uint32_t* row = (uint32_t*)intermediate_pixel_buffer + dest_y * w + dest_x;
							for (i32 src_y = copy_y0; src_y < copy_y1; ++src_y) {
								memcpy(row, (uint32_t*)tile->pixels + src_y * tile_width + copy_x0, copy_width * sizeof(uint32_t));
								row += w;
							}
							copied = true;
						}
					}
					if (!copied) {
						// if the tile does not exist, fill the area with white
						uint32_t* row = (uint32_t*)intermediate_pixel_buffer + dest_y * w + dest_x;
						for (i32 src_y = copy_y0; src_y < copy_y1; ++src_y) {
							memset(row, bg_value, copy_width * sizeof(uint32_t));
							row += w;
						}
					}
				}
			}

			// release tiles
			benaphore_lock(&image->lock);
			for (i32 tile_y = tiles_within_level_bounds.min.y; tile_y < tiles_within_level_bounds.max.y; ++tile_y) {
				for (i32 tile_x = tiles_within_level_bounds.min.x; tile_x < tiles_within_level_bounds.max.x; ++tile_x) {
					tile_t* tile = get_tile(level_image, tile_x, tile_y);
					if (tile->is_empty) continue; // no need to load empty tiles
					if (tile->is_cached && tile->pixels) {
						free(tile->pixels);
						tile->pixels = NULL;
						tile->is_cached = false;
					}
					tile->need_keep_in_cache = false;
				}
			}
			benaphore_unlock(&image->lock);



		} break;
    }

    bool success = true;
    if (intermediate_pixel_format == desired_pixel_format) {
        return true; // we're already done!
    } else {
        // need to convert between pixel formats
        if (intermediate_pixel_format == PIXEL_FORMAT_U8_BGRA) {
            if (desired_pixel_format == PIXEL_FORMAT_F32_Y) {
                image_convert_u8_rgba_to_f32_y(intermediate_pixel_buffer, dest, w, h, 4);
            } else {
                console_print_error("image_read_region(): pixel conversion (%d to %d) not implemented\n", intermediate_pixel_format, desired_pixel_format);
                success = false;
            }
        } else {
            console_print_error("image_read_region(): pixel conversion (%d to %d) not implemented\n", intermediate_pixel_format, desired_pixel_format);
            success = false;
        }
        if (intermediate_pixel_buffer) free(intermediate_pixel_buffer);
    }
    return success;
}

void do_level_image_indexing(image_t* image, level_image_t* level_image, i32 scale) {
    if (image->backend == IMAGE_BACKEND_DICOM) {
        if (dicom_instance_index_pixel_data(image->dicom.wsi.level_instances[scale])) {
            level_image->needs_indexing = false;
        }
    }
}

typedef struct level_indexing_task_t {
	image_t* image;
	level_image_t* level_image;
	i32 scale;
} level_indexing_task_t;


void level_image_indexing_task_func(i32 logical_thread_index, void* userdata) {
	level_indexing_task_t* task = (level_indexing_task_t*) userdata;
	do_level_image_indexing(task->image, task->level_image, task->scale);
	atomic_decrement(&task->image->refcount); // release
}

void begin_level_image_indexing(image_t* image, level_image_t* level_image, i32 scale) {
	ASSERT(!level_image->indexing_job_submitted);
	level_indexing_task_t task = {0};
	task.image = image;
	task.level_image = level_image;
	task.scale = scale;

	level_image->indexing_job_submitted = true;
	atomic_increment(&image->refcount); // retain
	if (!work_queue_submit_task(&global_work_queue, level_image_indexing_task_func, &task, sizeof(task))) {
		atomic_decrement(&image->refcount); // chicken out
		level_image->indexing_job_submitted = false;
	};
}

void image_destroy(image_t* image) {
    image->is_deleted = true;
    while (image->refcount > 0) {
//		console_print_error("refcount = %d\n", image->refcount);
        platform_sleep(1);
        work_queue_do_work(&global_work_queue, 0);
    }
	if (image) {
		if (image->type == IMAGE_TYPE_WSI) {
			if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
				unload_openslide_wsi(&image->openslide_wsi);
			} else if (image->backend == IMAGE_BACKEND_TIFF) {
				tiff_destroy(&image->tiff);
			} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
				isyntax_destroy(&image->isyntax);
			} else if (image->backend == IMAGE_BACKEND_DICOM) {
				dicom_destroy(&image->dicom);
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
				fatal_error("invalid image backend");
			}
		} else {
			fatal_error("invalid image type");
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

