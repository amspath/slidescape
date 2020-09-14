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


typedef struct encode_tile_task_t {
	image_t* image;
	tiff_t* tiff;
	tiff_ifd_t* ifd;
	i32 level;
	u32 export_tile_width;
	i32 export_tile_x;
	i32 export_tile_y;
	bounds2i pixel_bounds;
} encode_tile_task_t;

void encode_tile_func(i32 logical_thread_index, void* userdata) {
	encode_tile_task_t* task = (encode_tile_task_t*) userdata;
	image_t* image = task->image;
	tiff_t* tiff = task->tiff;
	tiff_ifd_t* ifd = task->ifd;
	i32 level = task->level;
	u32 export_tile_width = task->export_tile_width;
	i32 export_tile_x = task->export_tile_x;
	i32 export_tile_y = task->export_tile_y;
	bounds2i pixel_bounds = task->pixel_bounds;

	i32 export_width_in_pixels = pixel_bounds.right - pixel_bounds.left;
	i32 export_height_in_pixels = pixel_bounds.bottom - pixel_bounds.top;
	ASSERT(export_width_in_pixels > 0);
	ASSERT(export_height_in_pixels > 0);

	bounds2i source_tile_bounds = pixel_bounds;
	source_tile_bounds.left = div_floor(pixel_bounds.left, export_tile_width);
	source_tile_bounds.top = div_floor(pixel_bounds.top, export_tile_width);
	source_tile_bounds.right = div_floor(pixel_bounds.right, export_tile_width);
	source_tile_bounds.bottom = div_floor(pixel_bounds.bottom, export_tile_width);

	i32 source_bounds_width_in_tiles = source_tile_bounds.right - source_tile_bounds.left;
	i32 source_bounds_height_in_tiles = source_tile_bounds.bottom - source_tile_bounds.top;
	u32 source_tile_count = source_bounds_width_in_tiles * source_bounds_height_in_tiles;

	cached_tile_t* source_tiles = alloca(source_tile_count * sizeof(cached_tile_t));
	u8* dest_pixels = malloc(export_tile_width * export_tile_width * 4);

	for (i32 source_tile_y = 0; source_tile_y < source_bounds_height_in_tiles; ++source_tile_y) {
		for (i32 source_tile_x = 0; source_tile_x < source_bounds_width_in_tiles; ++source_tile_x) {

			// load the tile into memory


		}
	}



}

typedef struct offset_fixup_t {
	u64* offset_to_fix;
	u64 offset_from_unknown_base;
} offset_fixup_t;

static inline void add_fixup(offset_fixup_t* fixups, u64* offset_to_fix, u64 offset_from_unknown_base) {
	offset_fixup_t new_fixup = {offset_to_fix, offset_from_unknown_base};
	sb_push(fixups, new_fixup);
}

static inline raw_bigtiff_tag_t* memrw_push_bigtiff_tag(memrw_t* buffer, raw_bigtiff_tag_t* tag) {
	u64 write_offset = memrw_push(buffer, tag, sizeof(*tag));
	raw_bigtiff_tag_t* result = (raw_bigtiff_tag_t*) (buffer->data + write_offset);
	return result;
}

static raw_bigtiff_tag_t* add_large_bigtiff_tag(memrw_t* tag_buffer, memrw_t* data_buffer, offset_fixup_t* fixups, u16 tag_code, u16 tag_type, u64 tag_data_size, void* tag_data) {
	// NOTE: tag_data is allowed to be NULL, in that case we are only pushing placeholder data (zeroes)
	if (tag_data_size <= 8) {
		raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_size, 0};
		if (tag_data) {
			memcpy(tag.data, tag_data, tag_data_size);
		}
		raw_bigtiff_tag_t* stored_tag = memrw_push_bigtiff_tag(tag_buffer, &tag);
		return stored_tag;
	} else {
		u64 data_offset = memrw_push(data_buffer, tag_data, tag_data_size);
		raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_size, data_offset};
		raw_bigtiff_tag_t* stored_tag = memrw_push_bigtiff_tag(tag_buffer, &tag);
		add_fixup(fixups, &stored_tag->offset, data_offset);
		return stored_tag;
	}
}

bool32 export_cropped_bigtiff(image_t* image, tiff_t* tiff, bounds2f bounds, const char* filename,
							  u32 export_tile_width, u16 desired_photometric_interpretation) {
	if (!(tiff && tiff->main_image_ifd && (tiff->mpp_x > 0.0f) && (tiff->mpp_y > 0.0f))) {
		return false;
	}

	switch(desired_photometric_interpretation) {
		case TIFF_PHOTOMETRIC_YCBCR: break;
		case TIFF_PHOTOMETRIC_RGB: break;
		default: {
			printf("Error exporting BigTIFF: unsupported photometric interpretation (%d)\n", desired_photometric_interpretation);
		} return false;
	}

	// Calculate the export dimensions for the base level
	bounds2i level0_pixel_bounds = {};
	level0_pixel_bounds.left = (i32) floorf(bounds.left / tiff->mpp_x);
	level0_pixel_bounds.right = (i32) ceilf(bounds.right / tiff->mpp_x);
	level0_pixel_bounds.top = (i32) floorf(bounds.top / tiff->mpp_y);
	level0_pixel_bounds.bottom = (i32) ceilf(bounds.bottom / tiff->mpp_y);

	tiff_ifd_t* source_level0_ifd = tiff->main_image_ifd;
	u32 tile_width = source_level0_ifd->tile_width;
	u32 tile_height = source_level0_ifd->tile_height;
	bool is_tile_aligned = ((level0_pixel_bounds.left % tile_width) == 0) && ((level0_pixel_bounds.top % tile_height) == 0);

	bool need_reuse_tiles = is_tile_aligned && (desired_photometric_interpretation == source_level0_ifd->color_space)
	                        && (export_tile_width == tile_width) && (tile_width == tile_height); // only allow square tiles for re-use



	FILE* fp = fopen64(filename, "wb");
	bool32 success = false;
	if (fp) {

		// We will prepare all the tags, and push them into a temporary buffer, to be written to file later.
		// For non-inlined tags, the 'offset' field gets a placeholder offset because we don't know yet
		// where the tag data will be located in the file. For such tags we will:
		// - Push the data into a separate buffer, and remember the relative offset within that buffer
		// - Create a 'fixup', so that we can later substitute the offset once we know the base offset
		//   where we will store the separate data buffer in the output file.

		// temporary buffer for only the TIFF header + IFD tags
		memrw_t tag_buffer = memrw_create(KILOBYTES(64));
		// temporary buffer for all data >8 bytes not fitting in the raw TIFF tags (leaving out the pixel data)
		memrw_t small_data_buffer = memrw_create(MEGABYTES(1));

		offset_fixup_t* fixups = NULL; // sb

		tiff_header_t header = {};
		header.byte_order_indication = 0x4D4D; // little-endian
		header.filetype = 0x002B; // BigTIFF
		header.bigtiff.offset_size = 0x0008;
		header.bigtiff.always_zero = 0;
		header.bigtiff.first_ifd_offset = 16;
		memrw_push(&tag_buffer, &header, 16);

		// NOTE: the downsampling level does not necessarily equal the ifd index.
		i32 level = 0;
		i32 source_ifd_index = 0;
		tiff_ifd_t* source_ifd = source_level0_ifd;

		// Tags that are the same for all image levels
		raw_bigtiff_tag_t tag_new_subfile_type = {TIFF_TAG_NEW_SUBFILE_TYPE, TIFF_UINT32, 1, TIFF_FILETYPE_REDUCEDIMAGE};
		u16 bits_per_sample[4] = {8, 8, 8, 0};
		raw_bigtiff_tag_t tag_bits_per_sample = {TIFF_TAG_BITS_PER_SAMPLE, TIFF_UINT16, 3, *(u64*)bits_per_sample};
		raw_bigtiff_tag_t tag_compression = {TIFF_TAG_COMPRESSION, TIFF_UINT16, 1, TIFF_COMPRESSION_JPEG};
		raw_bigtiff_tag_t tag_photometric_interpretation = {TIFF_TAG_PHOTOMETRIC_INTERPRETATION, TIFF_UINT16, 1, desired_photometric_interpretation};
		raw_bigtiff_tag_t tag_orientation = {TIFF_TAG_ORIENTATION, TIFF_UINT16, 1, TIFF_ORIENTATION_TOPLEFT};
		raw_bigtiff_tag_t tag_samples_per_pixel = {TIFF_TAG_SAMPLES_PER_PIXEL, TIFF_UINT16, 1, 3};
		raw_bigtiff_tag_t tag_tile_width = {TIFF_TAG_TILE_WIDTH, TIFF_UINT16, 1, export_tile_width};
		raw_bigtiff_tag_t tag_tile_length = {TIFF_TAG_TILE_LENGTH, TIFF_UINT16, 1, export_tile_width};
		u16 chroma_subsampling[4] = {2, 2, 0, 0};
		raw_bigtiff_tag_t tag_chroma_subsampling = {TIFF_TAG_YCBCRSUBSAMPLING, TIFF_UINT16, 2, *(u64*)(chroma_subsampling)};

		bool reached_level_with_only_one_tile_in_it = false;
		for (; level < image->level_count && !reached_level_with_only_one_tile_in_it; ++level) {

			// Find an IFD for this downsampling level
			if (source_ifd->downsample_level != level) {
				++source_ifd_index;
				bool found = false;
				for (i32 i = source_ifd_index; i < tiff->level_image_ifd_count; ++i) {
					tiff_ifd_t* ifd = tiff->level_images_ifd + i;
					if (source_ifd->downsample_level == level) {
						found = true;
						source_ifd_index = i;
						source_ifd = ifd;
						break;
					}
				}
				if (!found) {
#if DO_DEBUG
					printf("Warning: source TIFF does not contain level %d, will be skipped\n", level);
#endif
					continue;
				}
			}

			// Calculate dimensions for the current downsampling level
			bounds2i pixel_bounds = level0_pixel_bounds;
			pixel_bounds.left >>= level;
			pixel_bounds.top >>= level;
			pixel_bounds.right >>= level;
			pixel_bounds.bottom >>= level;

			i32 export_width_in_pixels = pixel_bounds.right - pixel_bounds.left;
			i32 export_height_in_pixels = pixel_bounds.bottom - pixel_bounds.top;

			u32 export_width_in_tiles = (export_width_in_pixels + (export_tile_width - 1)) / export_tile_width;
			u32 export_height_in_tiles = (export_height_in_pixels + (export_tile_width - 1)) / export_tile_width;
			u64 export_tile_count = export_width_in_tiles * export_height_in_tiles;
			ASSERT(export_tile_count > 0);
			if (export_tile_count <= 1) {
				reached_level_with_only_one_tile_in_it = true; // should be the last level, no point in further downsampling
			}

			// TODO: Create the encoding jobs
			for (i32 export_tile_y = 0; export_tile_y < export_height_in_tiles; ++export_tile_y) {
				for (i32 export_tile_x = 0; export_tile_x < export_width_in_tiles; ++export_tile_x) {
					encode_tile_task_t task = {
							.image = image,
							.tiff = tiff,
							.ifd = source_ifd,
							.level = level,
							.export_tile_width = export_tile_width,
							.export_tile_x = export_tile_x,
							.export_tile_y = export_tile_y,
							.pixel_bounds = pixel_bounds,
					};
					bool32 enqueued = add_work_queue_entry(&work_queue, encode_tile_func, &task);


				}
			}

			// Include the NewSubfileType tag in every IFD except the first one
			if (level > 0) {
				memrw_push(&tag_buffer, &tag_new_subfile_type, sizeof(raw_bigtiff_tag_t));
			}

			raw_bigtiff_tag_t tag_image_width = {TIFF_TAG_IMAGE_WIDTH, TIFF_UINT32, 1, export_width_in_pixels};
			raw_bigtiff_tag_t tag_image_length = {TIFF_TAG_IMAGE_LENGTH, TIFF_UINT32, 1, export_height_in_pixels};
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_width);
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_length);
			memrw_push_bigtiff_tag(&tag_buffer, &tag_bits_per_sample);
			memrw_push_bigtiff_tag(&tag_buffer, &tag_compression);
			memrw_push_bigtiff_tag(&tag_buffer, &tag_photometric_interpretation);

			// Image description will be copied verbatim from the source
			add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, fixups, TIFF_TAG_IMAGE_DESCRIPTION,
			                      TIFF_ASCII, source_ifd->image_description_length, source_ifd->image_description);
			/*{
				void* tag_data = (void*) source_ifd->image_description;
				u64 tag_data_size = source_ifd->image_description_length;
				u16 tag_code = TIFF_TAG_IMAGE_DESCRIPTION;
				u16 tag_type = TIFF_ASCII;
				if (tag_data_size <= tag_data_max_inlined_size) {
					raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_size, 0};
					memcpy(tag.data, tag_data, tag_data_size);
					memrw_push(&tiff_tags_buffer, &tag, sizeof(raw_bigtiff_tag_t));
				} else {
					u64 data_offset = memrw_push(&tiff_small_data_buffer, tag_data, tag_data_size);
					raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_size, data_offset};
					u64 tag_offset = memrw_push(&tiff_tags_buffer, &tag, sizeof(raw_bigtiff_tag_t));
					raw_bigtiff_tag_t* stored_tag = (raw_bigtiff_tag_t*) (tiff_tags_buffer.data + tag_offset);
					add_fixup(fixups, &stored_tag->offset, data_offset);
				}
			}*/



			// unused tag: strip offsets

			memrw_push_bigtiff_tag(&tag_buffer, &tag_orientation);
			memrw_push_bigtiff_tag(&tag_buffer, &tag_samples_per_pixel);

			// unused tag: rows per strip
			// unused tag: strip byte counts

			raw_bigtiff_tag_t tag_x_resolution = {TIFF_TAG_X_RESOLUTION, TIFF_RATIONAL, 1, *(u64*)(&source_ifd->x_resolution)};
			raw_bigtiff_tag_t tag_y_resolution = {TIFF_TAG_Y_RESOLUTION, TIFF_RATIONAL, 1, *(u64*)(&source_ifd->y_resolution)};
			memrw_push_bigtiff_tag(&tag_buffer, &tag_x_resolution);
			memrw_push_bigtiff_tag(&tag_buffer, &tag_y_resolution);

//			u64 software_offset = 0; // TODO
//			u64 software_length = 0; // TODO
//			raw_bigtiff_tag_t tag_software = {TIFF_TAG_SOFTWARE, TIFF_ASCII, software_length, software_offset};

			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_width);
			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_length);

			// TODO: actually write the tiles...


			u64 tile_offsets_length = export_tile_count * sizeof(u64);
			raw_bigtiff_tag_t* stored_tile_offsets_tag = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, fixups,
			                      TIFF_TAG_TILE_OFFSETS, TIFF_UINT64, tile_offsets_length, NULL);
//			raw_bigtiff_tag_t tag_tile_offsets = {TIFF_TAG_TILE_OFFSETS, TIFF_UINT64, tile_offsets_length, tile_offsets_offset};
//			pushed_tag = memrw_push(&tiff_tags_buffer, &tag_tile_offsets, sizeof(raw_bigtiff_tag_t));
//			add_fixup(fixups, &pushed_tag->offset, tile_offsets_offset);

			raw_bigtiff_tag_t* stored_tile_bytecounts_tag = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, fixups,
			                      TIFF_TAG_TILE_BYTE_COUNTS, TIFF_UINT64, tile_offsets_length, NULL);

//			u64 tile_byte_counts_offset = 0; // TODO
//			u64 tile_byte_counts_length = 0;
//			raw_bigtiff_tag_t tag_tile_byte_counts = {TIFF_TAG_TILE_BYTE_COUNTS, TIFF_UINT64, tile_byte_counts_length, tile_byte_counts_offset};
//			pushed_tag = memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_byte_counts);
//			add_fixup(fixups, &pushed_tag->offset, tile_byte_counts_offset);

			// unused tag: SMinSampleValue
			// unused tag: SMaxSampleValue
			u64 jpeg_tables_offset = 0; // TODO
			u64 jpeg_tables_length = 0;
			raw_bigtiff_tag_t tag_jpeg_tables = {TIFF_TAG_JPEG_TABLES, TIFF_UNDEFINED, jpeg_tables_length, jpeg_tables_offset};


			memrw_push_bigtiff_tag(&tag_buffer, &tag_chroma_subsampling);


			if (need_reuse_tiles && level == 0) {
				u32 tile_offset_x = level0_pixel_bounds.left / tile_width;
				u32 tile_offset_y = level0_pixel_bounds.top / tile_height;
			}

		}

	}
	return success;
}
