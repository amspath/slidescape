#include "common.h"

#include <glad/glad.h>

#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>

#include "tiff.h"

u32 get_tiff_field_size(u16 data_type) {
	u32 size = 0;
	switch(data_type) {
		default:
			printf("Warning: encountered a TIFF field with an unrecognized data type (%d)\n", data_type);
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
			void* pos = field;
			for (i32 i = 0; i < sub_count; ++i, pos += field_size) {
				switch(field_size) {
					case 2: *(u16*)pos = _byteswap_ushort(*(u16*)pos); break;
					case 4: *(u32*)pos = _byteswap_ulong(*(u32*)pos); break;
					case 8: *(u64*)pos = _byteswap_uint64(*(u64*)pos); break;
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
		case TIFF_TAG_PLANAR_CONFIGURATION: result = "PlanarConfiguration"; break;
		case TIFF_TAG_SOFTWARE: result = "Software"; break;
		case TIFF_TAG_TILE_WIDTH: result = "TileWidth"; break;
		case TIFF_TAG_TILE_LENGTH: result = "TileLength"; break;
		case TIFF_TAG_TILE_OFFSETS: result = "TileOffsets"; break;
		case TIFF_TAG_TILE_BYTE_COUNTS: result = "TileByteCounts"; break;
		case TIFF_TAG_JPEG_TABLES: result = "JPEGTables"; break;
		default: break;
	}
	return result;
}

u64 file_read_at_offset(void* dest, FILE* fp, u64 offset, u64 num_bytes) {
	fpos_t prev_read_pos = 0;
	int ret = fgetpos64(fp, &prev_read_pos); // for restoring the file position later
	ASSERT(ret == 0); (void)ret;

	fseeko64(fp, offset, SEEK_SET);
	u64 result = fread(dest, num_bytes, 1, fp);

	ret = fsetpos64(fp, &prev_read_pos); // restore previous file position
	ASSERT(ret == 0); (void)ret;

	return result;
}

char* tiff_read_field_ascii(tiff_t* tiff, tiff_tag_t* tag) {
	size_t description_length = tag->data_count;
	char* result = calloc(MAX(8, description_length + 1), 1);
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
		if (file_read_at_offset(temp_integers, tiff->fp, tag->offset, tag->data_count * bytesize) != 1) {
			free(temp_integers);
			return NULL; // failed
		}

		if (bytesize == 8) {
			// the numbers are already 64-bit, no need to widen
			integers = (u64*) temp_integers;
			if (tiff->is_big_endian) {
				for (i32 i = 0; i < tag->data_count; ++i) {
					integers[i] = _byteswap_uint64(integers[i]);
				}
			}
		} else {
			// offsets are 32-bit or less -> widen to 64-bit offsets
			integers = malloc(tag->data_count * sizeof(u64));
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
		integers = malloc(sizeof(u64));
		integers[0] = tag->data_u64;
	}

	return integers;
}

bool32 tiff_read_ifd(tiff_t* tiff, tiff_ifd_t* ifd, u64* next_ifd_offset) {
	bool32 is_bigtiff = tiff->is_bigtiff;
	bool32 is_big_endian = tiff->is_big_endian;

	// Set the file position to the start of the IFD
	if (!(next_ifd_offset != NULL && fseeko64(tiff->fp, *next_ifd_offset, SEEK_SET) == 0)) {
		return false; // failed
	}

	u64 tag_count = 0;
	u64 tag_count_num_bytes = is_bigtiff ? 8 : 2;
	if (fread(&tag_count, tag_count_num_bytes, 1, tiff->fp) != 1) return false;
	if (is_big_endian) {
		tag_count = is_bigtiff ? _byteswap_uint64(tag_count) : _byteswap_ushort(tag_count);
	}

	// Read the tags
	u64 tag_size = is_bigtiff ? 20 : 12;
	u64 bytes_to_read = tag_count * tag_size;
	u8* raw_tags = malloc(bytes_to_read);
	if (fread(raw_tags, bytes_to_read, 1, tiff->fp) != 1) {
		free(raw_tags);
		return false; // failed
	}

	// Restructure the fields so we don't have to worry about the memory layout, endianness, etc
	tiff_tag_t* tags = calloc(sizeof(tiff_tag_t) * tag_count, 1);
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
		printf("tag %2d: %30s - code=%d, data_type=%2d, count=%5llu, offset=%llu\n",
		       tag_index, get_tiff_tag_name(tag->code), tag->code, tag->data_type, tag->data_count, tag->offset);
		switch(tag->code) {
			// Note: the data type of many tags (E.g. ImageWidth) can actually be either SHORT or LONG,
			// but because we already converted the byte order to native (=little-endian) with enough
			// padding in the tag struct, we can get away with treating them as if they are always LONG.
			case TIFF_TAG_IMAGE_WIDTH: {
				ifd->image_width = tag->data_u32;
			} break;
			case TIFF_TAG_IMAGE_LENGTH: {
				ifd->image_height = tag->data_u32;
			} break;
			case TIFF_TAG_COMPRESSION: {
				ifd->compression = tag->data_u16;
			} break;
			case TIFF_TAG_IMAGE_DESCRIPTION: {
				ifd->image_description = tiff_read_field_ascii(tiff, tag);
				ifd->image_description_length = tag->data_count;
				printf("%.500s\n", ifd->image_description);
				if (strncmp(ifd->image_description, "Macro", 5) == 0) {
					tiff->macro_image = ifd;
				} else if (strncmp(ifd->image_description, "Label", 5) == 0) {
					tiff->label_image = ifd;
				} else if (strncmp(ifd->image_description, "level", 5) == 0) {
					ifd->is_level_image = true;
				}
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
					printf("Error: mismatch in the TIFF tile count reported by TileByteCounts and TileOffsets tags\n");
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
				ifd->jpeg_tables = tiff_read_field_undefined(tiff, tag);
				ifd->jpeg_tables_length = tag->data_count;
			} break;
			default: {
			} break;
		}


	}

	free(tags);

	// Read the next IFD
	if (fread(next_ifd_offset, tiff->bytesize_of_offsets, 1, tiff->fp) != 1) return false;

	printf("next ifd offset = %lld\n", *next_ifd_offset);

	return true; // success
};

bool32 open_tiff_file(tiff_t* tiff, const char* filename) {
	int ret = 0; (void)ret; // for checking return codes from fgetpos, fsetpos, etc
	FILE* fp = fopen64(filename, "rb");
	bool32 success = false;
	if (fp) {
		tiff->fp = fp;
		struct stat st;
		if (fstat(fileno(fp), &st) == 0) {
			i64 filesize = st.st_size;
			if (filesize > 8) {
				// read the 8-byte TIFF header / 16-byte BigTIFF header
				tiff_header_t tiff_header = {};
				if (fread(&tiff_header, sizeof(tiff_header_t) /*16*/, 1, fp) != 1) goto fail;
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
					printf("Reading IFD #%llu\n", tiff->ifd_count);
					tiff_ifd_t ifd = {};
					if (!tiff_read_ifd(tiff, &ifd, &next_ifd_offset)) goto fail;
					sb_push(tiff->ifds, ifd);
					tiff->ifd_count += 1;
				}

				// TODO: make more robust
				// Assume the first IFD is the main image, and also level 0
				tiff->main_image = tiff->ifds;
				tiff->level_images = tiff->main_image;

				// TODO: make more robust
				u64 level_counter = 1; // begin at 1 because we are also counting level 0 (= the first IFD)
				for (i32 i = 1; i < tiff->ifd_count; ++i) {
					tiff_ifd_t* ifd = tiff->ifds + i;
					if (ifd->is_level_image) ++level_counter;
				}
				tiff->level_count = level_counter;

				// TODO: make more robust
				tiff->mpp_x = tiff->mpp_y = 0.25f;
				float um_per_pixel = 0.25f;
				for (i32 i = 0; i < tiff->level_count; ++i) {
					tiff_ifd_t* level = tiff->level_images + i;
					// TODO: allow other tile sizes?
					ASSERT(level->tile_width == 512);
					ASSERT(level->tile_height == 512);
					ASSERT(level->image_width % level->tile_width == 0);
					ASSERT(level->image_height % level->tile_height == 0);
					level->width_in_tiles = level->image_width / level->tile_width;
					level->height_in_tiles = level->image_height / level->tile_height;
					level->um_per_pixel_x = um_per_pixel;
					level->um_per_pixel_y = um_per_pixel;
					level->x_tile_side_in_um = level->um_per_pixel_x * (float)level->tile_width;
					level->y_tile_side_in_um = level->um_per_pixel_y * (float)level->tile_height;
					level->tiles = calloc(1, level->tile_count * sizeof(tiff_tile_t));
					um_per_pixel *= 2.0f; // downsample, so at higher levels there are more pixels per micrometer
				}


				success = true;

			}

		}
		// TODO: better error handling than this crap
		if (0) {
		}
		fail:;
		// Note: we need async i/o in the worker threads...
		// so for now we close and reopen the file using platform-native APIs to make that possible.
		fclose(fp);

		// TODO: make async I/O platform agnostic
		// TODO: set FILE_FLAG_NO_BUFFERING for maximum performance (but: need to align read requests to page size...)
		// http://vec3.ca/using-win32-asynchronous-io/
		tiff->win32_file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		                                                 FILE_ATTRIBUTE_NORMAL | /*FILE_FLAG_SEQUENTIAL_SCAN |*/
		                                                 /*FILE_FLAG_NO_BUFFERING |*/ FILE_FLAG_OVERLAPPED,
		                                                 NULL);


	}
	if (!success) {
		// could not open file
	}
	return success;
}

void tiff_destroy(tiff_t* tiff) {
	if (tiff->fp) {
		fclose(tiff->fp);
		tiff->fp = NULL;
	}
	if (tiff->win32_file_handle) {
		CloseHandle(tiff->win32_file_handle);
	}
	for (i32 i = 0; i < tiff->level_count; ++i) {
		tiff_ifd_t* level_image = tiff->level_images + i;
		if (level_image->tiles) {
			for (i32 j = 0; j < level_image->tile_count; ++j) {
				tiff_tile_t* tile = level_image->tiles + j;
				if (tile->texture != 0) {
					glDeleteTextures(1, &tile->texture);
				}
			}
			free(level_image->tiles);
		}
	}
	for (i32 i = 0; i < tiff->ifd_count; ++i) {
		tiff_ifd_t* ifd = tiff->ifds + i;
		if (ifd->tile_offsets) free(ifd->tile_offsets);
		if (ifd->tile_byte_counts) free(ifd->tile_byte_counts);
		if (ifd->image_description) free(ifd->image_description);
		if (ifd->jpeg_tables) free(ifd->jpeg_tables);


	}
	sb_free(tiff->ifds);
	memset(tiff, 0, sizeof(*tiff));
}
