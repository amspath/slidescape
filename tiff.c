#include "common.h"

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
		printf("tag %d:\n", tag_index);
		printf(" code=%d, count=%llu, offset=%llu\n   %s\n",
		       tag->code, tag->data_count, tag->offset, get_tiff_tag_name(tag->code));
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
			case TIFF_TAG_TILE_WIDTH: {
				ifd->tile_width = tag->data_u32;
			} break;
			case TIFF_TAG_TILE_LENGTH: {
				ifd->tile_height = tag->data_u32;
			} break;
			case TIFF_TAG_TILE_OFFSETS: {
				// TODO: need check PlanarConfiguration to check how to interpret the data count?
				ifd->tile_count = tag->data_count;
				if (ifd->tile_count == 0) {
					printf("Error: TIFF file has a TileOffsets tag, but it has zero tiles referenced\n");
					return false; // failed
				} else if (ifd->tile_count == 1) {
					// tag data is small enough to be inlined
					ASSERT(!tag->data_is_offset);
					ifd->tile_offsets = malloc(1 * sizeof(u64));
					ifd->tile_offsets[0] = tag->data_u64;
				} else {
					// tag data is not inlined (referenced elsewhere in the file)
					ASSERT(tag->data_is_offset);

					fpos_t prev_read_pos = 0;
					int ret = fgetpos64(tiff->fp, &prev_read_pos); // for restoring the file position later
					ASSERT(ret == 0); (void)ret;

					void* temp_offsets = calloc(tiff->bytesize_of_offsets, ifd->tile_count);
					if (fread(temp_offsets, ifd->tile_count * tiff->bytesize_of_offsets, 1, tiff->fp) != 1) {
						free(temp_offsets);
						return false; // failed
					}

					if (is_bigtiff) {
						// offsets are already 64-bit, no need to widen
						ifd->tile_offsets = (u64*) temp_offsets;
						if (is_big_endian) {
							for (i32 i = 0; i < tag->data_count; ++i) {
								ifd->tile_offsets[i] = _byteswap_uint64(ifd->tile_offsets[i]);
							}
						}
					} else {
						// offsets are 32-bit -> widen to 64-bit offsets
						ifd->tile_offsets = malloc(tag->data_count * sizeof(u64));
						for (i32 i = 0; i < tag->data_count; ++i) {
							ifd->tile_offsets[i] = maybe_swap_32(((u32*) temp_offsets)[i], is_big_endian);
						}
						free(temp_offsets);
					}

					ret = fsetpos64(tiff->fp, &prev_read_pos); // restore previous file position
					ASSERT(ret == 0); (void)ret;
				}

			} break;
			case TIFF_TAG_TILE_BYTE_COUNTS: {
			} break;
			case TIFF_TAG_JPEG_TABLES: {
			} break;
			default: {
			} break;
		}




//					putc('\n', stdout);
	}

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

				while (next_ifd_offset != 0) {
					printf("Reading IFD #%llu\n", tiff->ifd_count);
					tiff_ifd_t ifd = {};
					if (!tiff_read_ifd(tiff, &ifd, &next_ifd_offset)) goto fail;
					sb_push(tiff->ifds, ifd);
					tiff->ifd_count += 1;
				}

			}

		}
		fail:
		fclose(fp);


	}
	if (!success) {
		// could not open file
	}
	return success;
}
