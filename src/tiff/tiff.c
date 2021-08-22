/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2021  Pieter Valkema

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

#ifndef IS_SERVER
#include <glad/glad.h>
#endif

#include "lz4.h"

#include "tiff.h"

u32 get_tiff_field_size(u16 data_type) {
	u32 size = 0;
	switch(data_type) {
		default:
			console_print("Warning: encountered a TIFF field with an unrecognized data type (%d)\n", data_type);
			break;
		case TIFF_UINT8: case TIFF_INT8: case TIFF_ASCII: case TIFF_UNDEFINED: size = 1; break;
		case TIFF_UINT16: case TIFF_INT16:                                     size = 2; break;
		case TIFF_UINT32: case TIFF_INT32: case TIFF_IFD: case TIFF_FLOAT:     size = 4; break;
		case TIFF_RATIONAL:	case TIFF_SRATIONAL:                               size = 8; break; // note: actually 2x4
		case TIFF_DOUBLE: case TIFF_UINT64: case TIFF_INT64: case TIFF_IFD8:   size = 8; break;
	}
	return size;
}

void maybe_swap_tiff_field(void* field, u16 data_type, bool32 is_big_endian) {
	if (is_big_endian) {
		u32 field_size = get_tiff_field_size(data_type);
		if (field_size > 1) {
			// Some fields consist of two smaller field (RATIONAL, SRATIONAL), their components need to be swapped individually
			i32 sub_count = (data_type == TIFF_RATIONAL || data_type == TIFF_SRATIONAL) ? 2 : 1;
			u8* pos = (u8*) field;
			for (i32 i = 0; i < sub_count; ++i, pos += field_size) {
				switch(field_size) {
					case 2: *(u16*)pos = bswap_16(*(u16*)pos); break;
					case 4: *(u32*)pos = bswap_32(*(u32*)pos); break;
					case 8: *(u64*)pos = bswap_64(*(u64*)pos); break;
					default: ASSERT(!"This field size should not exist");
				}
			}
		}
	}
}

const char* get_tiff_tag_name(u32 tag) {
	const char* result = "unrecognized tag";
	switch(tag) {
		case TIFF_TAG_NEW_SUBFILE_TYPE: result = "NewSubfileType"; break;
		case TIFF_TAG_IMAGE_WIDTH: result = "ImageWidth"; break;
		case TIFF_TAG_IMAGE_LENGTH: result = "ImageLength"; break;
		case TIFF_TAG_BITS_PER_SAMPLE: result = "BitsPerSample"; break;
		case TIFF_TAG_COMPRESSION: result = "Compression"; break;
		case TIFF_TAG_PHOTOMETRIC_INTERPRETATION: result = "PhotometricInterpretation"; break;
		case TIFF_TAG_IMAGE_DESCRIPTION: result = "ImageDescription"; break;
		case TIFF_TAG_STRIP_OFFSETS: result = "StripOffsets"; break;
		case TIFF_TAG_ORIENTATION: result = "Orientation"; break;
		case TIFF_TAG_SAMPLES_PER_PIXEL: result = "SamplesPerPixel"; break;
		case TIFF_TAG_ROWS_PER_STRIP: result = "RowsPerStrip"; break;
		case TIFF_TAG_STRIP_BYTE_COUNTS: result = "StripByteCounts"; break;
		case TIFF_TAG_X_RESOLUTION: result = "XResolution"; break;
		case TIFF_TAG_Y_RESOLUTION: result = "YResolution"; break;
		case TIFF_TAG_PLANAR_CONFIGURATION: result = "PlanarConfiguration"; break;
		case TIFF_TAG_RESOLUTION_UNIT: result = "ResolutionUnit"; break;
		case TIFF_TAG_SOFTWARE: result = "Software"; break;
		case TIFF_TAG_TILE_WIDTH: result = "TileWidth"; break;
		case TIFF_TAG_TILE_LENGTH: result = "TileLength"; break;
		case TIFF_TAG_TILE_OFFSETS: result = "TileOffsets"; break;
		case TIFF_TAG_TILE_BYTE_COUNTS: result = "TileByteCounts"; break;
		case TIFF_TAG_SAMPLE_FORMAT: result = "SampleFormat"; break;
		case TIFF_TAG_S_MIN_SAMPLE_VALUE: result = "SMinSampleValue"; break;
		case TIFF_TAG_S_MAX_SAMPLE_VALUE: result = "SMaxSampleValue"; break;
		case TIFF_TAG_JPEG_TABLES: result = "JPEGTables"; break;
		case TIFF_TAG_YCBCRSUBSAMPLING: result = "YCbCrSubSampling"; break;
		case TIFF_TAG_REFERENCEBLACKWHITE: result = "ReferenceBlackWhite"; break;
		default: break;
	}
	return result;
}

char* tiff_read_field_ascii(tiff_t* tiff, tiff_tag_t* tag) {
	size_t description_length = tag->data_count;
	char* result = (char*) calloc(ATLEAST(8, description_length + 1), 1);
	if (tag->data_is_offset) {
		file_read_at_offset(result, tiff->fp, tag->offset, tag->data_count);
	} else {
		memcpy(result, tag->data, description_length);
	}
	return result;
}

static inline void* tiff_read_field_undefined(tiff_t* tiff, tiff_tag_t* tag) {
	return (void*) tiff_read_field_ascii(tiff, tag);
}

// Read integer values in a TIFF tag (either 8, 16, 32, or 64 bits wide) + convert them to little-endian u64 if needed
u64* tiff_read_field_integers(tiff_t* tiff, tiff_tag_t* tag) {
	u64* integers = NULL;

	if (tag->data_is_offset) {
		u64 bytesize = get_tiff_field_size(tag->data_type);
		void* temp_integers = calloc(bytesize, tag->data_count);
		u64 read_size = tag->data_count * bytesize;
		if (file_read_at_offset(temp_integers, tiff->fp, tag->offset, read_size) != read_size) {
			free(temp_integers);
			return NULL; // failed
		}

		if (bytesize == 8) {
			// the numbers are already 64-bit, no need to widen
			integers = (u64*) temp_integers;
			if (tiff->is_big_endian) {
				for (i32 i = 0; i < tag->data_count; ++i) {
					integers[i] = bswap_64(integers[i]);
				}
			}
		} else {
			// offsets are 32-bit or less -> widen to 64-bit offsets
			integers = (u64*) malloc(tag->data_count * sizeof(u64));
			switch(bytesize) {
				case 4: {
					for (i32 i = 0; i < tag->data_count; ++i) {
						integers[i] = maybe_swap_32(((u32*) temp_integers)[i], tiff->is_big_endian);
					}
				} break;
				case 2: {
					for (i32 i = 0; i < tag->data_count; ++i) {
						integers[i] = maybe_swap_16(((u16*) temp_integers)[i], tiff->is_big_endian);
					}
				} break;
				case 1: {
					for (i32 i = 0; i < tag->data_count; ++i) {
						integers[i] = ((u8*) temp_integers)[i];
					}
				} break;
				default: {
					free(temp_integers);
					free(integers);
					return NULL; // failed (other bytesizes than the above shouldn't exist)
				}
			}
			free(temp_integers);
		}
		// all done!

	} else {
		// data is inlined
		integers = (u64*) malloc(sizeof(u64));
		integers[0] = tag->data_u64;
	}

	return integers;
}


tiff_rational_t* tiff_read_field_rationals(tiff_t* tiff, tiff_tag_t* tag) {
	tiff_rational_t* rationals = (tiff_rational_t*) calloc(ATLEAST(8, tag->data_count * sizeof(tiff_rational_t)), 1);

	if (tag->data_is_offset) {
		file_read_at_offset(rationals, tiff->fp, tag->offset, tag->data_count * sizeof(tiff_rational_t));
	} else {
		// data is inlined
		rationals[0] = *(tiff_rational_t*) &tag->data_u64;
	}

	if (tiff->is_big_endian) {
		for (i32 i = 0; i < tag->data_count; ++i) {
			tiff_rational_t* rational = rationals + i;
			rational->a = bswap_32(rational->a);
			rational->b = bswap_32(rational->b);
		}
	}

	return rationals;
}

// read only one rational
tiff_rational_t tiff_read_field_rational(tiff_t* tiff, tiff_tag_t* tag) {
	tiff_rational_t result = {};
	if (tag->data_is_offset) {
		tiff_rational_t* rational = tiff_read_field_rationals(tiff, tag);
		ASSERT(rational);
		result = *rational;
		free(rational);
	} else {
		result = *(tiff_rational_t*) &tag->data_u64;
	}
	return result;
}

float tiff_rational_to_float(tiff_rational_t rational) {
	float result = (float)((double)(u32)rational.a) / ((double)(u32)rational.b);
	return result;
}

bool32 tiff_read_ifd(tiff_t* tiff, tiff_ifd_t* ifd, u64* next_ifd_offset) {
	bool32 is_bigtiff = tiff->is_bigtiff;
	bool32 is_big_endian = tiff->is_big_endian;

	// By default, assume RGB color space.
	// (although TIFF files are always required to specify this in the PhotometricInterpretation tag)
	ifd->color_space = TIFF_PHOTOMETRIC_RGB;

	// Set the file position to the start of the IFD
	if (!(next_ifd_offset != NULL && file_stream_set_pos(tiff->fp, *next_ifd_offset))) {
		return false; // failed
	}

	u64 tag_count = 0;
	u64 tag_count_num_bytes = is_bigtiff ? 8 : 2;
	if (file_stream_read(&tag_count, tag_count_num_bytes, tiff->fp) != tag_count_num_bytes) return false;
	if (is_big_endian) {
		tag_count = is_bigtiff ? bswap_64(tag_count) : bswap_16(tag_count);
	}

	// Read the tags
	u64 tag_size = is_bigtiff ? 20 : 12;
	u64 bytes_to_read = tag_count * tag_size;
	u8* raw_tags = (u8*) malloc(bytes_to_read);
	if (file_stream_read(raw_tags, bytes_to_read, tiff->fp) != bytes_to_read) {
		free(raw_tags);
		return false; // failed
	}

	// Restructure the fields so we don't have to worry about the memory layout, endianness, etc
	tiff_tag_t* tags = (tiff_tag_t*) calloc(sizeof(tiff_tag_t) * tag_count, 1);
	for (i32 i = 0; i < tag_count; ++i) {
		tiff_tag_t* tag = tags + i;
		if (is_bigtiff) {
			raw_bigtiff_tag_t* raw = (raw_bigtiff_tag_t*)raw_tags + i;
			tag->code = maybe_swap_16(raw->code, is_big_endian);
			tag->data_type = maybe_swap_16(raw->data_type, is_big_endian);
			tag->data_count = maybe_swap_64(raw->data_count, is_big_endian);

			u32 field_size = get_tiff_field_size(tag->data_type);
			u64 data_size = field_size * tag->data_count;
			if (data_size <= 8) {
				// Data fits in the tag so it is inlined
				memcpy(tag->data, raw->data, 8);
				maybe_swap_tiff_field(tag->data, tag->data_type, is_big_endian);
				tag->data_is_offset = false;
			} else {
				// Data doesn't fit in the tag itself, so it's an offset
				tag->offset = maybe_swap_64(raw->offset, is_big_endian);
				tag->data_is_offset = true;
			}
		} else {
			// Standard TIFF
			raw_tiff_tag_t* raw = (raw_tiff_tag_t*)raw_tags + i;
			tag->code = maybe_swap_16(raw->code, is_big_endian);
			tag->data_type = maybe_swap_16(raw->data_type, is_big_endian);
			tag->data_count = maybe_swap_32(raw->data_count, is_big_endian);

			u32 field_size = get_tiff_field_size(tag->data_type);
			u64 data_size = field_size * tag->data_count;
			if (data_size <= 4) {
				// Data fits in the tag so it is inlined
				memcpy(tag->data, raw->data, 4);
				maybe_swap_tiff_field(tag->data, tag->data_type, is_big_endian);
				tag->data_is_offset = false;
			} else {
				// Data doesn't fit in the tag itself, so it's an offset
				tag->offset = maybe_swap_32(raw->offset, is_big_endian);
				tag->data_is_offset = true;
			}
		}
	}
	free(raw_tags);

	// Read and interpret the entries in the IFD
	for (i32 tag_index = 0; tag_index < tag_count; ++tag_index) {
		tiff_tag_t* tag = tags + tag_index;
		if (is_verbose_mode) {
			console_print_verbose("tag %2d: %30s - code=%d, data_type=%2d, count=%5llu, offset=%llu\n",
						 tag_index, get_tiff_tag_name(tag->code), tag->code, tag->data_type, tag->data_count, tag->offset);
		}
		switch(tag->code) {
			case TIFF_TAG_NEW_SUBFILE_TYPE: {
				ifd->tiff_subfiletype = tag->data_u32;
			} break;
			// Note: the data type of many tags (E.g. ImageWidth) can actually be either SHORT or LONG,
			// but because we already converted the byte order to native (=little-endian) with enough
			// padding in the tag struct, we can get away with treating them as if they are always LONG.
			case TIFF_TAG_IMAGE_WIDTH: {
				ifd->image_width = tag->data_u32;
			} break;
			case TIFF_TAG_IMAGE_LENGTH: {
				ifd->image_height = tag->data_u32;
			} break;
			case TIFF_TAG_BITS_PER_SAMPLE: {
				// TODO: Fix this for regular TIFF
				if (!tag->data_is_offset) {
					for (i32 i = 0; i < tag->data_count; ++i) {
						u16 bits = *(u16*)&tag->data[i*2];
						console_print_verbose("   channel %d: BitsPerSample=%d\n", i, bits); // expected to be 8
					}
				}
			} break;
			case TIFF_TAG_COMPRESSION: {
				ifd->compression = tag->data_u16;
			} break;
			case TIFF_TAG_PHOTOMETRIC_INTERPRETATION: {
				ifd->color_space = tag->data_u16;
			} break;
			case TIFF_TAG_IMAGE_DESCRIPTION: {
				ifd->image_description = tiff_read_field_ascii(tiff, tag);
				ifd->image_description_length = tag->data_count;
				console_print_verbose("%.500s\n", ifd->image_description);
			} break;
			case TIFF_TAG_SAMPLES_PER_PIXEL: {
				ifd->samples_per_pixel = tag->data_u16;
			} break;
			case TIFF_TAG_X_RESOLUTION: {
				tiff_rational_t resolution = tiff_read_field_rational(tiff, tag);
				ifd->x_resolution = resolution;
				console_print_verbose("   %g\n", tiff_rational_to_float(resolution));
			} break;
			case TIFF_TAG_Y_RESOLUTION: {
				tiff_rational_t resolution = tiff_read_field_rational(tiff, tag);
				ifd->y_resolution = resolution;
				console_print_verbose("   %g\n", tiff_rational_to_float(resolution));
			} break;
			case TIFF_TAG_RESOLUTION_UNIT: {
				ifd->resolution_unit = tag->data_u16; //
			} break;
			case TIFF_TAG_TILE_WIDTH: {
				ifd->tile_width = tag->data_u32;
			} break;
			case TIFF_TAG_TILE_LENGTH: {
				ifd->tile_height = tag->data_u32;
			} break;
			case TIFF_TAG_TILE_OFFSETS: {
				// TODO: to be sure, need check PlanarConfiguration==1 to check how to interpret the data count?
				ifd->tile_count = tag->data_count;
				ifd->tile_offsets = tiff_read_field_integers(tiff, tag);
				if (ifd->tile_offsets == NULL) {
					free(tags);
					return false; // failed
				}
			} break;
			case TIFF_TAG_TILE_BYTE_COUNTS: {
				// Note: is it OK to assume that the TileByteCounts will always come after the TileOffsets?
				if (tag->data_count != ifd->tile_count) {
					ASSERT(tag->data_count != 0);
					console_print("Error: mismatch in the TIFF tile count reported by TileByteCounts and TileOffsets tags\n");
					free(tags);
					return false; // failed;
				}
				ifd->tile_byte_counts = tiff_read_field_integers(tiff, tag);
				if (ifd->tile_byte_counts == NULL) {
					free(tags);
					return false; // failed
				}
			} break;
			case TIFF_TAG_JPEG_TABLES: {
				ifd->jpeg_tables = (u8*) tiff_read_field_undefined(tiff, tag);
				ifd->jpeg_tables_length = tag->data_count;
			} break;
			case TIFF_TAG_YCBCRSUBSAMPLING: {
				// https://www.awaresystems.be/imaging/tiff/tifftags/ycbcrsubsampling.html
				ifd->chroma_subsampling_horizontal = *(u16*)&tag->data[0];
				ifd->chroma_subsampling_vertical = *(u16*)&tag->data[2];
				console_print_verbose("   YCbCrSubsampleHoriz = %d, YCbCrSubsampleVert = %d\n", ifd->chroma_subsampling_horizontal, ifd->chroma_subsampling_vertical);

			} break;
			case TIFF_TAG_REFERENCEBLACKWHITE: {
				ifd->reference_black_white_rational_count = tag->data_count;
				ifd->reference_black_white = tiff_read_field_rationals(tiff, tag); //TODO: add to serialized format
				if (ifd->reference_black_white == NULL) {
					free(tags);
					return false; // failed
				}
				for (i32 i = 0; i < tag->data_count; ++i) {
					tiff_rational_t* reference_black_white = ifd->reference_black_white + i;
					console_print_verbose("    [%d] = %d / %d\n", i, reference_black_white->a, reference_black_white->b);
				}
			} break;
			default: {
			} break;
		}


	}

	free(tags);

	if (ifd->tile_width > 0) {
		ifd->width_in_tiles = (ifd->image_width + ifd->tile_width - 1) / ifd->tile_width;
	}
	if (ifd->tile_height > 0) {
		ifd->height_in_tiles = (ifd->image_height + ifd->tile_height - 1) / ifd->tile_height;
	}

	// Try to deduce what type of image this is (level, macro, or label).
	// Unfortunately this does not seem to be very consistently specified in the TIFF files, so in part we have to guess.
	if (ifd->image_description) {
		if (strncmp(ifd->image_description, "Macro", 5) == 0) {
			ifd->subimage_type = TIFF_MACRO_SUBIMAGE;
			tiff->macro_image = ifd;
			tiff->macro_image_index = ifd->ifd_index;
		} else if (strncmp(ifd->image_description, "Label", 5) == 0) {
			ifd->subimage_type = TIFF_LABEL_SUBIMAGE;
			tiff->label_image = ifd;
			tiff->label_image_index = ifd->ifd_index;
		} else if (strncmp(ifd->image_description, "level", 5) == 0) {
			ifd->subimage_type = TIFF_LEVEL_SUBIMAGE;
		}
	}
	// Guess that it must be a level image if it's not explicitly said to be something else
	if (ifd->subimage_type == TIFF_UNKNOWN_SUBIMAGE /*0*/ && ifd->tile_width > 0) {
		if (ifd->ifd_index == 0 /* main image */ || ifd->tiff_subfiletype & TIFF_FILETYPE_REDUCEDIMAGE) {
			ifd->subimage_type = TIFF_LEVEL_SUBIMAGE;
		}
	}



	// Read the next IFD
	if (file_stream_read(next_ifd_offset, tiff->bytesize_of_offsets, tiff->fp) != tiff->bytesize_of_offsets) return false;
	console_print_verbose("next ifd offset = %lld\n", *next_ifd_offset);
	return true; // success
}

// Calculate various derived values (better name for this procedure??)
void tiff_post_init(tiff_t* tiff) {
	// TODO: make more robust
	// Assume the first IFD is the main image, and also level 0.
	// (Are there any counterexamples out there?)
	tiff->main_image_ifd = tiff->ifds;
	tiff->main_image_ifd_index = 0;
	tiff->level_images_ifd = tiff->main_image_ifd;
	tiff->level_images_ifd_index = 0;

	// Determine the resolution of the base level
	tiff->mpp_x = tiff->mpp_y = 0.25f;
	tiff_ifd_t* main_image = tiff->main_image_ifd;
	if (main_image->x_resolution.b > 0 && main_image->y_resolution.b > 0) {
		if (main_image->resolution_unit == TIFF_RESUNIT_CENTIMETER) {
			float pixels_per_centimeter_x = tiff_rational_to_float(main_image->x_resolution);
			float pixels_per_centimeter_y = tiff_rational_to_float(main_image->y_resolution);
			tiff->mpp_x = 10000.0f / pixels_per_centimeter_x;
			tiff->mpp_y = 10000.0f / pixels_per_centimeter_y;
		}
	}

	float main_image_width = (float) main_image->image_width;
	float main_image_height = (float) main_image->image_height;
	tiff->max_downsample_level = 0;
	i32 last_downsample_level = 0;
	tiff->level_image_ifd_count = 0;
	for (i32 ifd_index = tiff->level_images_ifd_index; ifd_index < tiff->ifd_count; ++ifd_index) {
		tiff_ifd_t* ifd = tiff->ifds + ifd_index;
		if (ifd->tile_count == 0) {
			break; // not a tiled image, so cannot be part of the pyramid (could be macro or label image)
		}
		if (ifd_index == 0 || ifd->subimage_type == TIFF_LEVEL_SUBIMAGE) {
			++tiff->level_image_ifd_count;
		}

		float level_width = (float)ifd->image_width;
		float raw_downsample_factor = main_image_width / level_width;
		float raw_downsample_level = log2f(raw_downsample_factor);
		i32 downsample_level = (i32) roundf(raw_downsample_level);

		// Some TIFF files have the width/height set to an integer multiple of the tile size.
		// For the most zoomed out levels, this makes it harder to calculate the actual downsampling level
		// (because we might underestimate it). So we need to do extra work to deduce the downsampling
		// level in these corner cases.
		if (ifd->image_width % ifd->tile_width == 0) {
			if (ifd->width_in_tiles >= 1 && ifd->height_in_tiles >= 1) {
				u32 min_possible_width = ifd->tile_width * (ifd->width_in_tiles-1) + 1;
				u32 max_possible_width = ifd->tile_width * (ifd->width_in_tiles) ;
				float downsample_factor_upper_bound = main_image_width / (float)min_possible_width;
				float downsample_factor_lower_bound = main_image_width / (float)max_possible_width;

				if (ifd->image_height % ifd->tile_height == 0) {
					// constrain further based on the vertical tile count
					u32 min_possible_height = ifd->tile_height * (ifd->height_in_tiles-1) + 1;
					u32 max_possible_height = ifd->tile_height * (ifd->height_in_tiles);

					float downsample_factor_y_upper_bound = main_image_height / (float)min_possible_height;
					float downsample_factor_y_lower_bound = main_image_height / (float)max_possible_height;

					downsample_factor_upper_bound = MIN(downsample_factor_upper_bound, downsample_factor_y_upper_bound);
					downsample_factor_lower_bound = MAX(downsample_factor_lower_bound, downsample_factor_y_lower_bound);
				}

				float level_lower_bound = log2f(downsample_factor_lower_bound);
				float level_upper_bound = log2f(downsample_factor_upper_bound);

				i32 discrete_level_lower_bound = (i32) ceilf(level_lower_bound);
				i32 discrete_level_upper_bound = (i32) floorf(level_upper_bound);

				if (discrete_level_lower_bound == discrete_level_upper_bound) {
					downsample_level = discrete_level_lower_bound;
				} else {
					// ambiguity could not be resolved. Use last resort.
					downsample_level = MIN(discrete_level_lower_bound, last_downsample_level + 1);
				}
				DUMMY_STATEMENT;
			}
		}

		ifd->downsample_level = last_downsample_level = downsample_level;
		ifd->downsample_factor = exp2f((float)ifd->downsample_level);
		tiff->max_downsample_level = MAX(ifd->downsample_level, tiff->max_downsample_level);
		ifd->um_per_pixel_x = tiff->mpp_x * ifd->downsample_factor;
		ifd->um_per_pixel_y = tiff->mpp_y * ifd->downsample_factor;
		ifd->x_tile_side_in_um = ifd->um_per_pixel_x * (float)ifd->tile_width;
		ifd->y_tile_side_in_um = ifd->um_per_pixel_y * (float)ifd->tile_height;
		DUMMY_STATEMENT;
	}
}

bool32 open_tiff_file(tiff_t* tiff, const char* filename) {
	console_print_verbose("Opening TIFF file %s\n", filename);
	ASSERT(tiff);
	int ret = 0; (void)ret; // for checking return codes from fgetpos, fsetpos, etc
	file_stream_t fp = file_stream_open_for_reading(filename);
	bool32 success = false;
	if (fp) {
		tiff->fp = fp;
		tiff->filesize = file_stream_get_filesize(fp);
		if (tiff->filesize > 8) {
			// read the 8-byte TIFF header / 16-byte BigTIFF header
			tiff_header_t tiff_header = {};
			ret = file_stream_read(&tiff_header, sizeof(tiff_header_t) /*16*/, fp);
			if (ret != sizeof(tiff_header_t)) goto fail;
			bool32 is_big_endian;
			switch(tiff_header.byte_order_indication) {
				case TIFF_BIG_ENDIAN: is_big_endian = true; break;
				case TIFF_LITTLE_ENDIAN: is_big_endian = false; break;
				default: goto fail;
			}
			tiff->is_big_endian = is_big_endian;
			u16 filetype = maybe_swap_16(tiff_header.filetype, is_big_endian);
			bool32 is_bigtiff;
			switch(filetype) {
				case 0x2A: is_bigtiff = false; break;
				case 0x2B: is_bigtiff = true; break;
				default: goto fail;
			}
			tiff->is_bigtiff = is_bigtiff;
			u32 bytesize_of_offsets;
			u64 next_ifd_offset = 0;
			if (is_bigtiff) {
				bytesize_of_offsets = maybe_swap_16(tiff_header.bigtiff.offset_size, is_big_endian);
				if (bytesize_of_offsets != 8) goto fail;
				if (tiff_header.bigtiff.always_zero != 0) goto fail;
				next_ifd_offset = maybe_swap_64(tiff_header.bigtiff.first_ifd_offset, is_big_endian);
			} else {
				bytesize_of_offsets = 4;
				next_ifd_offset = maybe_swap_32(tiff_header.tiff.first_ifd_offset, is_big_endian);
			}
			ASSERT((bytesize_of_offsets == 4 && !is_bigtiff) || (bytesize_of_offsets == 8 && is_bigtiff));
			tiff->bytesize_of_offsets = bytesize_of_offsets;

			// Read and process the IFDs
			while (next_ifd_offset != 0) {
				console_print_verbose("Reading IFD #%llu\n", tiff->ifd_count);
				tiff_ifd_t ifd = { .ifd_index = tiff->ifd_count };
				if (!tiff_read_ifd(tiff, &ifd, &next_ifd_offset)) goto fail;
				arrput(tiff->ifds, ifd);
				tiff->ifd_count += 1;
			}

			tiff_post_init(tiff);

			success = true;

			// cleanup
			file_stream_close(fp);
			tiff->fp = NULL;

			// Prepare for Async I/O in the worker threads
#if !IS_SERVER
#if WINDOWS
			// TODO: make async I/O platform agnostic
			// TODO: set FILE_FLAG_NO_BUFFERING for maximum performance (but: need to align read requests to page size...)
			// http://vec3.ca/using-win32-asynchronous-io/
			tiff->file_handle = win32_open_overlapped_file_handle(filename);
#else
			tiff->file_handle = open(filename, O_RDONLY);
			if (tiff->file_handle == -1) {
				console_print_error("Error: Could not reopen %s for asynchronous I/O\n");
				return false;
			} else {
				// success
			}

#endif
#endif
		}

		// TODO: better error handling than this crap
		fail:;
		// Note: we need async i/o in the worker threads...
		// so for now we close and reopen the file using platform-native APIs to make that possible.
		if (tiff->fp) {
			file_stream_close(fp);
			tiff->fp = NULL;
		}
	}
	return success;
}


void memrw_push_tiff_block(memrw_t* buffer, u32 block_type, u32 index, u64 block_length) {
	serial_block_t block = { .block_type = block_type, .index = index, .length = block_length };
	memrw_push_back(buffer, (u8*) &block, sizeof(block));
}

#define INCLUDE_IMAGE_DESCRIPTION 1

memrw_t* tiff_serialize(tiff_t* tiff, memrw_t* buffer) {

	u64 uncompressed_size = 0;

	// block: general TIFF header / meta
	uncompressed_size += sizeof(serial_block_t);
	tiff_serial_header_t serial_header = (tiff_serial_header_t){
			.filesize = tiff->filesize,
			.ifd_count = tiff->ifd_count,
			.main_image_index = tiff->main_image_ifd_index,
			.macro_image_index = tiff->macro_image_index,
			.label_image_index = tiff->label_image_index,
			.level_image_ifd_count = tiff->level_image_ifd_count,
			.level_image_index = tiff->level_images_ifd_index,
			.bytesize_of_offsets = tiff->bytesize_of_offsets,
			.is_bigtiff = tiff->is_bigtiff,
			.is_big_endian = tiff->is_big_endian,
			.mpp_x = tiff->mpp_x,
			.mpp_y = tiff->mpp_y,
	};
	uncompressed_size += sizeof(serial_header);

	// block: IFD's
	uncompressed_size += sizeof(serial_block_t);
	u64 serial_ifds_block_size = tiff->ifd_count * sizeof(tiff_serial_ifd_t);
	tiff_serial_ifd_t* serial_ifds = (tiff_serial_ifd_t*) alloca(serial_ifds_block_size);
	for (i32 i = 0; i < tiff->ifd_count; ++i) {
		tiff_ifd_t* ifd = tiff->ifds + i;
		tiff_serial_ifd_t* serial_ifd = serial_ifds + i;
		*serial_ifd = (tiff_serial_ifd_t) {
			.image_width = ifd->image_width,
			.image_height = ifd->image_height,
			.tile_width = ifd->tile_width,
			.tile_height = ifd->tile_height,
			.tile_count = ifd->tile_count,
			.image_description_length = ifd->image_description_length,
			.jpeg_tables_length = ifd->jpeg_tables_length,
			.compression = ifd->compression,
			.color_space = ifd->color_space,
			.level_magnification = ifd->level_magnification,
			.width_in_tiles = ifd->width_in_tiles,
			.height_in_tiles = ifd->height_in_tiles,
			.um_per_pixel_x = ifd->um_per_pixel_x,
			.um_per_pixel_y = ifd->um_per_pixel_y,
			.x_tile_side_in_um = ifd->x_tile_side_in_um,
			.y_tile_side_in_um = ifd->y_tile_side_in_um,
			.chroma_subsampling_horizontal = ifd->chroma_subsampling_horizontal,
			.chroma_subsampling_vertical = ifd->chroma_subsampling_vertical,
			.subimage_type = ifd->subimage_type,
		};
#if INCLUDE_IMAGE_DESCRIPTION
		uncompressed_size += ifd->image_description_length;
#endif
		uncompressed_size += ifd->jpeg_tables_length;
		uncompressed_size += ifd->tile_count * sizeof(ifd->tile_offsets[0]);
		uncompressed_size += ifd->tile_count * sizeof(ifd->tile_byte_counts[0]);
	}
	uncompressed_size += tiff->ifd_count * sizeof(tiff_serial_ifd_t);

	// blocks: need separate blocks for each IFD's image descriptions, tile offsets, tile byte counts, jpeg tables
#if INCLUDE_IMAGE_DESCRIPTION
	uncompressed_size += tiff->ifd_count * sizeof(serial_block_t);
#endif
	uncompressed_size += tiff->ifd_count * sizeof(serial_block_t);
	uncompressed_size += tiff->ifd_count * sizeof(serial_block_t);
	uncompressed_size += tiff->ifd_count * sizeof(serial_block_t);

	// block: terminator (end of stream marker)
	uncompressed_size += sizeof(serial_block_t);

	// Allocate space, and start pushing the data onto the buffer
	memrw_maybe_grow(buffer, uncompressed_size);

	memrw_push_tiff_block(buffer, SERIAL_BLOCK_TIFF_HEADER_AND_META, 0, sizeof(serial_block_t));
	memrw_push_back(buffer, &serial_header, sizeof(serial_header));

	memrw_push_tiff_block(buffer, SERIAL_BLOCK_TIFF_IFDS, 0, serial_ifds_block_size);
	memrw_push_back(buffer, serial_ifds, serial_ifds_block_size);

	for (i32 i = 0; i < tiff->ifd_count; ++i) {
		tiff_ifd_t* ifd = tiff->ifds + i;
#if INCLUDE_IMAGE_DESCRIPTION
		memrw_push_tiff_block(buffer, SERIAL_BLOCK_TIFF_IMAGE_DESCRIPTION, i, ifd->image_description_length);
		memrw_push_back(buffer, ifd->image_description, ifd->image_description_length);
#endif
		u64 tile_offsets_size = ifd->tile_count * sizeof(ifd->tile_offsets[0]);
		memrw_push_tiff_block(buffer, SERIAL_BLOCK_TIFF_TILE_OFFSETS, i, tile_offsets_size);
		memrw_push_back(buffer, ifd->tile_offsets, tile_offsets_size);

		u64 tile_byte_counts_size = ifd->tile_count * sizeof(ifd->tile_byte_counts[0]);
		memrw_push_tiff_block(buffer, SERIAL_BLOCK_TIFF_TILE_BYTE_COUNTS, i, tile_byte_counts_size);
		memrw_push_back(buffer, ifd->tile_byte_counts, tile_byte_counts_size);

		memrw_push_tiff_block(buffer, SERIAL_BLOCK_TIFF_JPEG_TABLES, i, ifd->jpeg_tables_length);
		memrw_push_back(buffer, ifd->jpeg_tables, ifd->jpeg_tables_length);

	}

	memrw_push_tiff_block(buffer, SERIAL_BLOCK_TERMINATOR, 0, 0);

//	console_print("buffer has %llu used bytes, out of %llu capacity\n", buffer->used_size, buffer->capacity);

	// Additional compression step
#if 1
	ASSERT(buffer->used_size == uncompressed_size);
	i32 compression_size_bound = LZ4_COMPRESSBOUND(buffer->used_size);
	u8* compression_buffer = (u8*) malloc(compression_size_bound);
	i32 compressed_size = LZ4_compress_default((char*)buffer->data, (char*)compression_buffer,
	                                           buffer->used_size, compression_size_bound);
	if (compressed_size > 0) {
		// success! We can replace the buffer contents with the compressed data
		memrw_rewind(buffer);
		memrw_push_tiff_block(buffer, SERIAL_BLOCK_LZ4_COMPRESSED_DATA, uncompressed_size, compressed_size);
		memrw_push_back(buffer, compression_buffer, compressed_size);
	} else {
		console_print_error("Warning: tiff_serialize(): payload LZ4 compression failed\n");
	}

	free(compression_buffer);
#endif

	return buffer;

}

u8* pop_from_buffer(u8** pos, i64 size, i64* bytes_left) {
	if (size > *bytes_left) {
		console_print_error("pop_from_buffer(): buffer empty\n");
		return NULL;
	}
	u8* old_pos = *pos;
	*pos += size;
	*bytes_left -= size;
	return old_pos;
}

serial_block_t* pop_block_from_buffer(u8** pos, i64* bytes_left) {
	return (serial_block_t*) pop_from_buffer(pos, sizeof(serial_block_t), bytes_left);
}

i64 find_end_of_http_headers(u8* str, u64 len) {
	static const char crlfcrlf[] = "\r\n\r\n";
	u32 search_key = *(u32*)crlfcrlf;
	i64 result = 0;
	for (i64 offset = 0; offset < len - 4; ++offset) {
		u8* pos = str + offset;
		u32 check = *(u32*)pos;
		if (check == search_key) {
			result = offset + 4;
			break;
		}
	}
	return result;
}



bool32 tiff_deserialize(tiff_t* tiff, u8* buffer, u64 buffer_size) {
	i64 bytes_left = (i64)buffer_size;
	u8* pos = buffer;
	u8* data = NULL;
	serial_block_t* block = NULL;
	bool32 success = false;
	u8* decompressed_buffer = NULL;

	if (0) {
		failed:
		if (decompressed_buffer) free(decompressed_buffer);
		return false;
	}


#define POP_DATA(size) do {if (!(data = pop_from_buffer(&pos, (size), &bytes_left))) goto failed; } while(0)
#define POP_BLOCK() do{if (!(block = pop_block_from_buffer(&pos, &bytes_left))) goto failed; } while(0)


	i64 content_offset = find_end_of_http_headers(buffer, buffer_size);
	i64 content_length = buffer_size - content_offset;
	POP_DATA(content_offset);

	// block: general TIFF header / meta
	POP_BLOCK();

	if (block->block_type == SERIAL_BLOCK_LZ4_COMPRESSED_DATA) {
		// compressed LZ4 stream
		i32 compressed_size = (i32)block->length;
		i32 decompressed_size = (i32)block->index; // used as general purpose field here..
		ASSERT(block->length < INT32_MAX);
		ASSERT(block->index < INT32_MAX);
		POP_DATA(compressed_size);
		decompressed_buffer = (u8*) malloc(decompressed_size);
		i32 bytes_decompressed = LZ4_decompress_safe((char*)data, (char*)decompressed_buffer, compressed_size, decompressed_size);
		if (bytes_decompressed > 0) {
			if (bytes_decompressed != decompressed_size) {
				console_print_error("LZ4_decompress_safe() decompressed %d bytes, however the expected size was %d\n", bytes_decompressed, decompressed_size);
			} else {
				// success, switch over to the uncompressed buffer!
				pos = decompressed_buffer;
				bytes_left = bytes_decompressed;
				POP_BLOCK(); // now pointing to the uncompressed data stream
			}

		} else {
			console_print_error("LZ4_decompress_safe() failed (return value %d)\n", bytes_decompressed);
		}
	}

	if (block->block_type != SERIAL_BLOCK_TIFF_HEADER_AND_META) goto failed;

	POP_DATA(sizeof(tiff_serial_header_t));
	tiff_serial_header_t* serial_header = (tiff_serial_header_t*) data;
	*tiff = (tiff_t) {};
	tiff->is_remote = 0; // set later
	tiff->location = (network_location_t){}; // set later
	tiff->fp = NULL;
#if !IS_SERVER
	tiff->file_handle = 0;
#endif
	tiff->filesize = serial_header->filesize;
	tiff->bytesize_of_offsets = serial_header->bytesize_of_offsets;
	tiff->ifd_count = serial_header->ifd_count;
	tiff->ifds = NULL; // set later
	tiff->main_image_ifd = NULL; // set later
	tiff->main_image_ifd_index = serial_header->main_image_index;
	tiff->macro_image = NULL; // set later
	tiff->macro_image_index = serial_header->macro_image_index;
	tiff->label_image = NULL; // set later
	tiff->label_image_index = serial_header->label_image_index;
	tiff->level_image_ifd_count = serial_header->level_image_ifd_count;
	tiff->level_images_ifd = NULL; // set later
	tiff->level_images_ifd_index = 0; // set later
	tiff->is_bigtiff = serial_header->is_bigtiff;
	tiff->is_big_endian = serial_header->is_big_endian;
	tiff->mpp_x = serial_header->mpp_x;
	tiff->mpp_y = serial_header->mpp_y;

	// block: IFD's
	POP_BLOCK();
	if (block->block_type != SERIAL_BLOCK_TIFF_IFDS) goto failed;
	u64 serial_ifds_block_size = tiff->ifd_count * sizeof(tiff_serial_ifd_t);
	if (block->length != serial_ifds_block_size) goto failed;

	POP_DATA(serial_ifds_block_size);
	tiff_serial_ifd_t* serial_ifds = (tiff_serial_ifd_t*) data;

	// TODO: maybe not use a stretchy_buffer here?
	tiff->ifds = (tiff_ifd_t*) calloc(1, sizeof(tiff_ifd_t) * tiff->ifd_count); // allocate space for the IFD's
	memset(tiff->ifds, 0, tiff->ifd_count * sizeof(tiff_ifd_t));

	for (i32 i = 0; i < tiff->ifd_count; ++i) {
		tiff_ifd_t* ifd = tiff->ifds + i;
		tiff_serial_ifd_t* serial_ifd = serial_ifds + i;
		*ifd = (tiff_ifd_t) {};
		ifd->ifd_index = i;
		ifd->image_width = serial_ifd->image_width;
		ifd->image_height = serial_ifd->image_height;
		ifd->tile_width = serial_ifd->tile_width;
		ifd->tile_height = serial_ifd->tile_height;
		ifd->tile_count = serial_ifd->tile_count;
		ifd->tile_offsets = NULL; // set later
		ifd->tile_byte_counts = NULL; // set later
		ifd->image_description = NULL; // set later
		ifd->image_description_length = serial_ifd->image_description_length;
		ifd->jpeg_tables = NULL; // set later
		ifd->jpeg_tables_length = serial_ifd->jpeg_tables_length;
		ifd->compression = serial_ifd->compression;
		ifd->color_space = serial_ifd->color_space;
		ifd->subimage_type = serial_ifd->subimage_type;
		ifd->level_magnification = serial_ifd->level_magnification; // TODO: delete this?
		ifd->width_in_tiles = serial_ifd->width_in_tiles;
		ifd->height_in_tiles = serial_ifd->height_in_tiles;
		ifd->um_per_pixel_x = serial_ifd->um_per_pixel_x;
		ifd->um_per_pixel_y = serial_ifd->um_per_pixel_y;
		ifd->x_tile_side_in_um = serial_ifd->x_tile_side_in_um;
		ifd->y_tile_side_in_um = serial_ifd->y_tile_side_in_um;
		ifd->chroma_subsampling_horizontal = serial_ifd->chroma_subsampling_horizontal;
		ifd->chroma_subsampling_vertical = serial_ifd->chroma_subsampling_vertical;
		ifd->reference_black_white_rational_count = 0; // unused for now
		ifd->reference_black_white = NULL; // unused for now
	}

	DUMMY_STATEMENT; // for placing a debug breakpoint

	// The number of remaining blocks is unspecified.
	// We are expecting at least byte offsets, tile offsets and JPEG tables for each IFD
	bool32 reached_end = false;
	while (!reached_end) {

		POP_BLOCK();
		if (block->length > 0) {
			POP_DATA(block->length);
		}
		u8* block_content = data;
		// TODO: Need to think about this: are block index's (if present) always referring an IFD index, though? Or are there other use cases?
		if (block->index >= tiff->ifd_count) {
			console_print_error("tiff_deserialize(): found block referencing a non-existent IFD\n");
			goto failed;
		}
		tiff_ifd_t* referenced_ifd = tiff->ifds + block->index;


		switch (block->block_type) {
			case SERIAL_BLOCK_TIFF_IMAGE_DESCRIPTION: {
				if (referenced_ifd->image_description) {
					console_print_error("tiff_deserialize(): IFD %u already has an image description\n", block->index);
					goto failed;
				}
				referenced_ifd->image_description = (char*) malloc(block->length + 1);
				memcpy(referenced_ifd->image_description, block_content, block->length);
				referenced_ifd->image_description[block->length] = '\0';
				referenced_ifd->image_description_length = block->length;
			} break;
			case SERIAL_BLOCK_TIFF_TILE_OFFSETS: {
				if (referenced_ifd->tile_offsets) {
					console_print_error("tiff_deserialize(): IFD %u already has tile offsets\n", block->index);
					goto failed;
				}
				referenced_ifd->tile_offsets = (u64*) malloc(block->length);
				memcpy(referenced_ifd->tile_offsets, block_content, block->length);
			} break;
			case SERIAL_BLOCK_TIFF_TILE_BYTE_COUNTS: {
				if (referenced_ifd->tile_byte_counts) {
					console_print_error("tiff_deserialize(): IFD %u already has tile byte counts\n", block->index);
					goto failed;
				}
				referenced_ifd->tile_byte_counts = (u64*) malloc(block->length);
				memcpy(referenced_ifd->tile_byte_counts, block_content, block->length);
			} break;
			case SERIAL_BLOCK_TIFF_JPEG_TABLES: {
				if (referenced_ifd->jpeg_tables) {
					console_print_error("tiff_deserialize(): IFD %u already has JPEG tables\n", block->index);
					goto failed;
				}
				referenced_ifd->jpeg_tables = (u8*) malloc(block->length);
				memcpy(referenced_ifd->jpeg_tables, block_content, block->length);
				referenced_ifd->jpeg_tables[block->length] = 0;
				referenced_ifd->jpeg_tables_length = block->length;
			} break;
			case SERIAL_BLOCK_TERMINATOR: {
				// Reached the end
#if REMOTE_CLIENT_VERBOSE
				console_print("tiff_deserialize(): found a terminator block\n");
#endif
				reached_end = true;
				break;
			} break;
		}
	}
	if (reached_end) {
		success = true;
#if REMOTE_CLIENT_VERBOSE
		console_print("tiff_deserialize(): bytes_left = %lld, content length = %lld, buffer size = %llu\n", bytes_left, content_length, buffer_size);
#endif
	}

	if (decompressed_buffer != NULL) free(decompressed_buffer);

	tiff_post_init(tiff);

	return success;


#undef POP_DATA
#undef POP_BLOCK

}

void tiff_destroy(tiff_t* tiff) {
	if (tiff->fp) {
		file_stream_close(tiff->fp);
		tiff->fp = NULL;
	}
#if !IS_SERVER
#if WINDOWS
	if (tiff->file_handle) {
		CloseHandle(tiff->file_handle);
	}
#else
	if (tiff->file_handle) {
		close(tiff->file_handle);
	}
#endif
#endif

	for (i32 i = 0; i < tiff->ifd_count; ++i) {
		tiff_ifd_t* ifd = tiff->ifds + i;
		if (ifd->tile_offsets) free(ifd->tile_offsets);
		if (ifd->tile_byte_counts) free(ifd->tile_byte_counts);
		if (ifd->image_description) free(ifd->image_description);
		if (ifd->jpeg_tables) free(ifd->jpeg_tables);
		if (ifd->reference_black_white) free(ifd->reference_black_white);
	}
	// TODO: fix this, choose either stretchy_buffer or regular malloc, not both...
	if (tiff->is_remote) {
		free(tiff->ifds);
	} else {
		arrfree(tiff->ifds);
	}
	memset(tiff, 0, sizeof(*tiff));
}
