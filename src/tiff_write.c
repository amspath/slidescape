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

#include "jpeg_decoder.h"


enum export_region_format_enum {
	EXPORT_REGION_FORMAT_BIGTIFF = 0,
	EXPORT_REGION_FORMAT_JPEG = 1,
	EXPORT_REGION_FORMAT_PNG = 2,
};

typedef struct export_region_task_t {
	image_t* image;
	bounds2f bounds;
	u32 export_region_format;
} export_region_task_t;

typedef struct export_bigtiff_task_t {
	image_t* image;
	bounds2f bounds;
	tiff_t* tiff;
	const char* filename;
	u32 export_tile_width;
	u16 desired_photometric_interpretation;
} export_bigtiff_task_t;


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
	u64 offset_to_fix;
	u64 offset_from_unknown_base;
} offset_fixup_t;

static inline void add_fixup(memrw_t* fixups_buffer, u64 offset_to_fix, u64 offset_from_unknown_base) {
	// NOTE: 'offset_to_fix' can't be a direct pointer to the value that needs to be fixed up, because
	// pointers to the destination buffer might be unstable because the destination buffer might resize.
	// so instead we are storing it as an offset from the start of the destination buffer.
	offset_fixup_t new_fixup = {offset_to_fix, offset_from_unknown_base};
	memrw_push(fixups_buffer, &new_fixup, sizeof(new_fixup));
}

static inline raw_bigtiff_tag_t* memrw_push_bigtiff_tag(memrw_t* buffer, raw_bigtiff_tag_t* tag) {
	u64 write_offset = memrw_push(buffer, tag, sizeof(*tag));
	raw_bigtiff_tag_t* result = (raw_bigtiff_tag_t*) (buffer->data + write_offset);
	return result;
}

static raw_bigtiff_tag_t* add_large_bigtiff_tag(memrw_t* tag_buffer, memrw_t* data_buffer, memrw_t* fixups_buffer,
                                                u16 tag_code, u16 tag_type, u64 tag_data_count, void* tag_data) {
	// NOTE: tag_data is allowed to be NULL, in that case we are only pushing placeholder data (zeroes)
	u32 field_size = get_tiff_field_size(tag_type);
	u64 tag_data_size = field_size * tag_data_count;
	if (tag_data_size <= 8) {
		raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_count, .data_u64 = 0};
		if (tag_data) {
			memcpy(tag.data, tag_data, tag_data_size);
		}
		raw_bigtiff_tag_t* stored_tag = memrw_push_bigtiff_tag(tag_buffer, &tag);
		return stored_tag;
	} else {
		u64 data_offset = memrw_push(data_buffer, tag_data, tag_data_size);
		raw_bigtiff_tag_t tag = {tag_code, tag_type, tag_data_count, .offset = data_offset};
		raw_bigtiff_tag_t* stored_tag = memrw_push_bigtiff_tag(tag_buffer, &tag);
		// NOTE: we cannot store a raw pointer to the offset we need to fix later, because the buffer
		// might resize (pointer is unstable).
		add_fixup(fixups_buffer, (u64)((u8*)&stored_tag->offset - tag_buffer->data), data_offset);
		return stored_tag;
	}
}

bool32 export_cropped_bigtiff(image_t* image, tiff_t* tiff, bounds2f bounds, const char* filename,
							  u32 export_tile_width, u16 desired_photometric_interpretation, i32 quality) {
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
		// temporary buffer tracking the offsets that we need to fix after writing all the IFDs
		memrw_t fixups_buffer = memrw_create(1024);

		// Write the TIFF header (except the offset to the first IFD, which we will push when iterating over the IFDs)
		tiff_header_t header = {};
		header.byte_order_indication = 0x4949; // little-endian
		header.filetype = 0x002B; // BigTIFF
		header.bigtiff.offset_size = 0x0008;
		header.bigtiff.always_zero = 0;
		memrw_push(&tag_buffer, &header, 8);

		// NOTE: the downsampling level does not necessarily equal the ifd index.
		i32 level = 0;
		i32 source_ifd_index = 0;
		tiff_ifd_t* source_ifd = source_level0_ifd;

		// Tags that are the same for all image levels
		raw_bigtiff_tag_t tag_new_subfile_type = {TIFF_TAG_NEW_SUBFILE_TYPE, TIFF_UINT32, 1, .offset = TIFF_FILETYPE_REDUCEDIMAGE};
		u16 bits_per_sample[4] = {8, 8, 8, 0};
		raw_bigtiff_tag_t tag_bits_per_sample = {TIFF_TAG_BITS_PER_SAMPLE, TIFF_UINT16, 3, .offset = *(u64*)bits_per_sample};
		raw_bigtiff_tag_t tag_compression = {TIFF_TAG_COMPRESSION, TIFF_UINT16, 1, .offset = TIFF_COMPRESSION_JPEG};
		raw_bigtiff_tag_t tag_photometric_interpretation = {TIFF_TAG_PHOTOMETRIC_INTERPRETATION, TIFF_UINT16, 1, .offset = desired_photometric_interpretation};
		raw_bigtiff_tag_t tag_orientation = {TIFF_TAG_ORIENTATION, TIFF_UINT16, 1, .offset = TIFF_ORIENTATION_TOPLEFT};
		raw_bigtiff_tag_t tag_samples_per_pixel = {TIFF_TAG_SAMPLES_PER_PIXEL, TIFF_UINT16, 1, .offset = 3};
		raw_bigtiff_tag_t tag_tile_width = {TIFF_TAG_TILE_WIDTH, TIFF_UINT16, 1, .offset = export_tile_width};
		raw_bigtiff_tag_t tag_tile_length = {TIFF_TAG_TILE_LENGTH, TIFF_UINT16, 1, .offset = export_tile_width};
		u16 chroma_subsampling[4] = {2, 2, 0, 0};
		raw_bigtiff_tag_t tag_chroma_subsampling = {TIFF_TAG_YCBCRSUBSAMPLING, TIFF_UINT16, 2, .offset = *(u64*)(chroma_subsampling)};

		bool reached_level_with_only_one_tile_in_it = false;
		for (; level < image->level_count && !reached_level_with_only_one_tile_in_it; ++level) {


			// Find an IFD for this downsampling level
			if (source_ifd->downsample_level != level) {
				++source_ifd_index;
				bool found = false;
				for (i32 i = source_ifd_index; i < tiff->level_image_ifd_count; ++i) {
					tiff_ifd_t* ifd = tiff->level_images_ifd + i;
					if (ifd->downsample_level == level) {
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


			// Offset to the beginning of the next IFD (= 8 bytes directly after the current offset)
			u64 next_ifd_offset = tag_buffer.used_size + sizeof(u64);
			memrw_push(&tag_buffer, &next_ifd_offset, sizeof(u64));

			u64 tag_count_for_ifd = 0;
			u64 tag_count_for_ifd_offset = memrw_push(&tag_buffer, &tag_count_for_ifd, sizeof(u64));

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
#if 0
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
#endif

			// Include the NewSubfileType tag in every IFD except the first one
			if (level > 0) {
				memrw_push(&tag_buffer, &tag_new_subfile_type, sizeof(raw_bigtiff_tag_t));
				++tag_count_for_ifd;
			}

			raw_bigtiff_tag_t tag_image_width = {TIFF_TAG_IMAGE_WIDTH, TIFF_UINT32, 1, .offset = export_width_in_pixels};
			raw_bigtiff_tag_t tag_image_length = {TIFF_TAG_IMAGE_LENGTH, TIFF_UINT32, 1, .offset = export_height_in_pixels};
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_width); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_image_length); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_bits_per_sample); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_compression); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_photometric_interpretation); ++tag_count_for_ifd;

			// Image description will be copied verbatim from the source
			add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer, TIFF_TAG_IMAGE_DESCRIPTION,
			                      TIFF_ASCII, source_ifd->image_description_length, source_ifd->image_description);
			++tag_count_for_ifd;
			// unused tag: strip offsets

			memrw_push_bigtiff_tag(&tag_buffer, &tag_orientation); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_samples_per_pixel); ++tag_count_for_ifd;

			// unused tag: rows per strip
			// unused tag: strip byte counts

			if (source_ifd->x_resolution.b > 0) {
				raw_bigtiff_tag_t tag_x_resolution = {TIFF_TAG_X_RESOLUTION, TIFF_RATIONAL, 1, .offset = *(u64*)(&source_ifd->x_resolution)};
				memrw_push_bigtiff_tag(&tag_buffer, &tag_x_resolution);
				++tag_count_for_ifd;
			}
			if (source_ifd->x_resolution.b > 0) {
				raw_bigtiff_tag_t tag_y_resolution = {TIFF_TAG_Y_RESOLUTION, TIFF_RATIONAL, 1, .offset = *(u64*) (&source_ifd->y_resolution)};
				memrw_push_bigtiff_tag(&tag_buffer, &tag_y_resolution);
				++tag_count_for_ifd;
			}
//			u64 software_offset = 0; // TODO
//			u64 software_length = 0; // TODO
//			raw_bigtiff_tag_t tag_software = {TIFF_TAG_SOFTWARE, TIFF_ASCII, software_length, software_offset};

			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_width); ++tag_count_for_ifd;
			memrw_push_bigtiff_tag(&tag_buffer, &tag_tile_length); ++tag_count_for_ifd;

			// TODO: actually write the tiles...


			raw_bigtiff_tag_t* stored_tile_offsets_tag = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_TILE_OFFSETS, TIFF_UINT64, export_tile_count, NULL);
			++tag_count_for_ifd;

			raw_bigtiff_tag_t* stored_tile_bytecounts_tag = add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_TILE_BYTE_COUNTS, TIFF_UINT64, export_tile_count, NULL);
			++tag_count_for_ifd;

			// unused tag: SMinSampleValue
			// unused tag: SMaxSampleValue

			u64 jpeg_tables_length = 0;
			u8* tables_buffer = NULL;
			u32 tables_size = 0;
			encode_tile(NULL, export_tile_width, export_tile_width, quality, &tables_buffer, &tables_size, NULL, NULL);
			add_large_bigtiff_tag(&tag_buffer, &small_data_buffer, &fixups_buffer,
			                      TIFF_TAG_JPEG_TABLES, TIFF_UNDEFINED, jpeg_tables_length, tables_buffer);
			++tag_count_for_ifd;
			if (tables_buffer) free(tables_buffer);

			memrw_push_bigtiff_tag(&tag_buffer, &tag_chroma_subsampling);
			++tag_count_for_ifd;

			// Update the tag count, which was written incorrectly as a placeholder at the beginning of the IFD
			*(u64*)(tag_buffer.data + tag_count_for_ifd_offset) = tag_count_for_ifd;

			/*if (need_reuse_tiles && level == 0) {
				u32 tile_offset_x = level0_pixel_bounds.left / tile_width;
				u32 tile_offset_y = level0_pixel_bounds.top / tile_height;
			}*/

		}

		u64 next_ifd_offset_terminator = 0;
		memrw_push(&tag_buffer, &next_ifd_offset_terminator, sizeof(u64));

		// TODO: macro/label images

		// Adjust the offsets in the TIFF tags, so that they are counted from the beginning of the file.
		u64 data_buffer_base_offset = tag_buffer.used_size;
		offset_fixup_t* fixups = (offset_fixup_t*)fixups_buffer.data;
		u64 fixup_count = fixups_buffer.used_count;
		for (i32 i = 0; i < fixup_count; ++i) {
			offset_fixup_t* fixup = fixups + i;
			u64 fixed_offset = fixup->offset_from_unknown_base + data_buffer_base_offset;
			*(u64*)(tag_buffer.data + fixup->offset_to_fix) = fixed_offset;
		}

		fwrite(tag_buffer.data, tag_buffer.used_size, 1, fp);
		fwrite(small_data_buffer.data, small_data_buffer.used_size, 1,fp);
		u64 image_data_base_offset = tag_buffer.used_size + small_data_buffer.used_size;


		fclose(fp);

		memrw_destroy(&tag_buffer);
		memrw_destroy(&small_data_buffer);
		memrw_destroy(&fixups_buffer);

	}

	return success;
}
