/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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
#include "mathutils.h"

#include "tiff.h"
#include "viewer.h"


bool32 export_cropped_bigtiff(image_t* image, tiff_t* tiff, bounds2f bounds, const char* filename,
							  u32 export_tile_width, u16 desired_color_space) {
	if (!(tiff && tiff->main_image_ifd && (tiff->mpp_x > 0.0f) && (tiff->mpp_y > 0.0f))) {
		return false;
	}

	switch(desired_color_space) {
		case TIFF_PHOTOMETRIC_YCBCR: break;
		case TIFF_PHOTOMETRIC_RGB: break;
		default: {
			printf("Error exporting BigTIFF: unsupported color space (%d)\n", desired_color_space);
		} return false;
	}

	// pixel bounds
	bounds2i pixel_bounds = {};
	pixel_bounds.left = (i32) floorf(bounds.left / tiff->mpp_x);
	pixel_bounds.right = (i32) ceilf(bounds.right / tiff->mpp_x);
	pixel_bounds.top = (i32) floorf(bounds.top / tiff->mpp_y);
	pixel_bounds.bottom = (i32) ceilf(bounds.bottom / tiff->mpp_y);

	tiff_ifd_t* source_level0_ifd = tiff->main_image_ifd;
	u32 tile_width = source_level0_ifd->tile_width;
	u32 tile_height = source_level0_ifd->tile_height;
	bool is_tile_aligned = ((pixel_bounds.left % tile_width) == 0) && ((pixel_bounds.top % tile_height) == 0);

	bool need_reuse_tiles = is_tile_aligned && (desired_color_space == source_level0_ifd->color_space)
			&& (export_tile_width == tile_width) && (tile_width == tile_height); // only allow square tiles for re-use

	u32 dest_width_in_tiles = (pixel_bounds.right - pixel_bounds.left) / export_tile_width;
	u32 dest_height_in_tiles = (pixel_bounds.bottom - pixel_bounds.top) / export_tile_width;

	FILE* fp = fopen64(filename, "wb");
	bool32 success = false;
	if (fp) {
		tiff_header_t header = {};
		header.byte_order_indication = 0x4D4D; // little-endian
		header.filetype = 0x002B; // BigTIFF
		header.bigtiff.offset_size = 0x0008;
		header.bigtiff.always_zero = 0;
		header.bigtiff.first_ifd_offset = 16;

		i32 level = 0;
		i32 source_ifd_index = 0;
		tiff_ifd_t* source_ifd = source_level0_ifd;

		raw_bigtiff_tag_t tag_new_subfile_type = {TIFF_TAG_NEW_SUBFILE_TYPE, TIFF_UINT32, 1, TIFF_FILETYPE_REDUCEDIMAGE};

		for (; level < image->level_count; ++level) {

			raw_bigtiff_tag_t tag_image_width = {TIFF_TAG_IMAGE_WIDTH, TIFF_UINT32, 1, source_ifd->image_width};
			raw_bigtiff_tag_t tag_image_length = {TIFF_TAG_IMAGE_LENGTH, TIFF_UINT32, 1, source_ifd->image_height};
			u16 bits_per_sample[4] = {8, 8, 8, 0};
			raw_bigtiff_tag_t tag_bits_per_sample = {TIFF_TAG_BITS_PER_SAMPLE, TIFF_UINT16, 3, *(u64*)bits_per_sample};
			raw_bigtiff_tag_t tag_compression = {TIFF_TAG_COMPRESSION, TIFF_UINT16, 1, TIFF_COMPRESSION_JPEG};
			raw_bigtiff_tag_t tag_photometric_interpretation = {TIFF_TAG_PHOTOMETRIC_INTERPRETATION, TIFF_UINT16, 1, desired_color_space};
			u64 image_description_offset = 0; // TODO
			u64 image_description_length = source_ifd->image_description_length; // TODO
			raw_bigtiff_tag_t tag_image_description = {TIFF_TAG_IMAGE_DESCRIPTION, TIFF_ASCII, image_description_length, image_description_offset};
			// unused tag: strip offsets
			raw_bigtiff_tag_t tag_orientation = {TIFF_TAG_ORIENTATION, TIFF_UINT16, 1, TIFF_ORIENTATION_TOPLEFT};
			raw_bigtiff_tag_t tag_samples_per_pixel = {TIFF_TAG_SAMPLES_PER_PIXEL, TIFF_UINT16, 1, 3};
			// unused tag: rows per strip
			// unused tag: strip byte counts
			raw_bigtiff_tag_t tag_x_resolution = {TIFF_TAG_X_RESOLUTION, TIFF_RATIONAL, 1, *(u64*)(&source_ifd->x_resolution)};
			raw_bigtiff_tag_t tag_y_resolution = {TIFF_TAG_Y_RESOLUTION, TIFF_RATIONAL, 1, *(u64*)(&source_ifd->y_resolution)};
			raw_bigtiff_tag_t tag_planar_configuration = {TIFF_TAG_PLANAR_CONFIGURATION, TIFF_UINT16, 1, TIFF_PLANARCONFIG_CONTIG};
			raw_bigtiff_tag_t tag_resolution_unit = {TIFF_TAG_RESOLUTION_UNIT, TIFF_UINT16, 1, TIFF_RESUNIT_CENTIMETER};
			u64 software_offset = 0; // TODO
			u64 software_length = 0; // TODO
			raw_bigtiff_tag_t tag_software = {TIFF_TAG_SOFTWARE, TIFF_ASCII, software_length, software_offset};
			raw_bigtiff_tag_t tag_tile_width = {TIFF_TAG_TILE_WIDTH, TIFF_UINT16, 1, export_tile_width};
			raw_bigtiff_tag_t tag_tile_length = {TIFF_TAG_TILE_LENGTH, TIFF_UINT16, 1, export_tile_width};
			u64 tile_offsets_offset = 0; // TODO
			u64 tile_offsets_length = 0;
			raw_bigtiff_tag_t tag_tile_offsets = {TIFF_TAG_TILE_OFFSETS, TIFF_UINT32, tile_offsets_length, tile_offsets_offset};
			u64 tile_byte_counts_offset = 0; // TODO
			u64 tile_byte_counts_length = 0;
			raw_bigtiff_tag_t tag_tile_byte_counts = {TIFF_TAG_TILE_BYTE_COUNTS, TIFF_UINT32, tile_byte_counts_length, tile_byte_counts_offset};
			// unused tag: SMinSampleValue
			// unused tag: SMaxSampleValue
			u64 jpeg_tables_offset = 0; // TODO
			u64 jpeg_tables_length = 0;
			raw_bigtiff_tag_t tag_jpeg_tables = {TIFF_TAG_JPEG_TABLES, TIFF_UNDEFINED, jpeg_tables_length, jpeg_tables_offset};
			u16 chroma_subsampling[4] = {2, 2, 0, 0};
			raw_bigtiff_tag_t tag_chroma_subsampling = {TIFF_TAG_YCBCRSUBSAMPLING, TIFF_UINT16, 2, *(u64*)(chroma_subsampling)};


			if (need_reuse_tiles && level == 0) {
				u32 tile_offset_x = pixel_bounds.left / tile_width;
				u32 tile_offset_y = pixel_bounds.top / tile_height;
			}

		}

	}
	return success;
}
