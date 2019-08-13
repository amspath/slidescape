#define USE_MINIMAL_SYSTEM_HEADER
#include "common.h"

#include "platform.h"
#include "win32_platform.h"
#include "win32_multithreading.h"
#include "intrinsics.h"
#include "openslide_api.h"

#include <glad/glad.h>
#include <stdio.h>

#include "wsi.h"

void load_wsi(wsi_t* wsi, char* filename) {
	if (!is_openslide_loading_done) {
		printf("Waiting for OpenSlide to finish loading...\n");
		platform_wait_for_boolean_true(&is_openslide_loading_done);
	}

	read_barrier;
	if (!is_openslide_available) {
		char message[4096];
		snprintf(message, sizeof(message), "Could not open \"%s\":\nlibopenslide-0.dll is missing or broken.\n", filename);
		win32_message_box(NULL, message);
		return;
	}


	if (wsi->osr) {
		openslide.openslide_close(wsi->osr);
		wsi->osr = NULL;
	}


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



	}

}

void unload_wsi(wsi_t* wsi) {
	if (wsi->osr) {
		openslide.openslide_close(wsi->osr);
		wsi->osr = NULL;
	}
}