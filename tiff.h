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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include "common.h"
#include "stdio.h"

#ifndef IS_SERVER
#define IS_SERVER 0
#endif
#if !IS_SERVER
#include "win32_main.h"
#endif

#define TIFF_LITTLE_ENDIAN 0x4949
#define TIFF_BIG_ENDIAN 0x4D4D

// Documentation for TIFF tags: https://www.awaresystems.be/imaging/tiff/tifftags/search.html

enum tiff_tag_code_enum {
	TIFF_TAG_NEW_SUBFILE_TYPE = 254,
	TIFF_TAG_IMAGE_WIDTH = 256,
	TIFF_TAG_IMAGE_LENGTH = 257,
	TIFF_TAG_BITS_PER_SAMPLE = 258,
	TIFF_TAG_COMPRESSION = 259,
	TIFF_TAG_PHOTOMETRIC_INTERPRETATION = 262,
	TIFF_TAG_IMAGE_DESCRIPTION = 270,
	TIFF_TAG_STRIP_OFFSETS = 273,
	TIFF_TAG_ORIENTATION = 274,
	TIFF_TAG_SAMPLES_PER_PIXEL = 277,
	TIFF_TAG_ROWS_PER_STRIP = 278,
	TIFF_TAG_STRIP_BYTE_COUNTS = 279,
	TIFF_TAG_PLANAR_CONFIGURATION = 284,
	TIFF_TAG_SOFTWARE = 305,
	TIFF_TAG_TILE_WIDTH = 322,
	TIFF_TAG_TILE_LENGTH = 323,
	TIFF_TAG_TILE_OFFSETS = 324,
	TIFF_TAG_TILE_BYTE_COUNTS = 325,
	TIFF_TAG_JPEG_TABLES = 347,
	TIFF_TAG_YCBCRSUBSAMPLING = 530,
	TIFF_TAG_REFERENCEBLACKWHITE = 532,
};

enum tiff_data_type_enum {
	TIFF_UINT8 = 1,
	TIFF_ASCII = 2,
	TIFF_UINT16 = 3, // SHORT
	TIFF_UINT32 = 4, // LONG
	TIFF_RATIONAL = 5,
	TIFF_INT8 = 6, // SBYTE
	TIFF_UNDEFINED = 7,
	TIFF_INT16 = 8, // SSHORT
	TIFF_INT32 = 9, // SLONG
	TIFF_SRATIONAL = 10,
	TIFF_FLOAT = 11,
	TIFF_DOUBLE = 12,
	TIFF_IFD = 13, // equal to LONG
	TIFF_UINT64 = 16, // LONG8
	TIFF_INT64 = 17, // SLONG8
	TIFF_IFD8 = 18,
};

#pragma pack(push, 1)

typedef struct {
	i32 a, b;
} tiff_rational_t;

typedef struct {
	u16 byte_order_indication; // either 0x4949 for little-endian or 0x4D4D for big-endian
	u16 filetype; // must be 0x002A for standard TIFF or 0x002B for BigTIFF
	union {
		struct {
			u32 first_ifd_offset;
		} tiff;
		struct {
			u16 offset_size; // expected to be 0x0008 for 64-bit
			u16 always_zero;
			u64 first_ifd_offset;
		} bigtiff;
	};
} tiff_header_t;

// Packed standard TIFF and BigTIFF tag structs, with layout matching the file structure
typedef struct {
	u16 code;
	u16 data_type;
	u32 data_count;
	union {
		u8 data[4];
		u32 offset;
	};
} raw_tiff_tag_t;

typedef struct {
	u16 code;
	u16 data_type;
	u64 data_count;
	union {
		u8 data[8];
		u64 offset;
	};
} raw_bigtiff_tag_t;

#pragma pack(pop)

// Converted TIFF tag, with native byte order, for internal use
typedef struct {
	u16 code;
	u16 data_type;
	u64 data_count;
	union {
		u8 data[8];
		u64 offset;
		u8 data_u8;
		u16 data_u16;
		u32 data_u32;
		u64 data_u64;
	};
	bool8 data_is_offset;
} tiff_tag_t;

// https://www.awaresystems.be/imaging/tiff/tifftags/compression.html
enum tiff_compression_enum {
	TIFF_COMPRESSION_NONE = 1,
	TIFF_COMPRESSION_CCCITTRLE = 2,
	TIFF_COMPRESSION_CCITTFAX3 = 3,
	TIFF_COMPRESSION_CCITTFAX4 = 4,
	TIFF_COMPRESSION_LZW = 5,
	TIFF_COMPRESSION_OJPEG = 6, // old-style JPEG -> ignore
	TIFF_COMPRESSION_JPEG = 7,
	TIFF_COMPRESSION_ADOBE_DEFLATE = 8,
	TIFF_COMPRESSION_JP2000 = 34712,
};

// https://www.awaresystems.be/imaging/tiff/tifftags/photometricinterpretation.html
enum tiff_photometric_interpretation_enum {
	TIFF_PHOTOMETRIC_MINISWHITE = 0,
	TIFF_PHOTOMETRIC_MINISBLACK = 1,
	TIFF_PHOTOMETRIC_RGB = 2,
	TIFF_PHOTOMETRIC_PALETTE = 3,
	TIFF_PHOTOMETRIC_MASK = 4,
	TIFF_PHOTOMETRIC_SEPARATED = 5,
	TIFF_PHOTOMETRIC_YCBCR = 6,
	TIFF_PHOTOMETRIC_CIELAB = 8,
	TIFF_PHOTOMETRIC_ICCLAB = 9,
	TIFF_PHOTOMETRIC_ITULAB = 10,
	TIFF_PHOTOMETRIC_LOGL = 32844,
	TIFF_PHOTOMETRIC_LOGLUV = 32845,
};

typedef struct tiff_t tiff_t;




typedef struct tiff_ifd_t {
	u64 ifd_index;
	u32 image_width;
	u32 image_height;
	u32 tile_width;
	u32 tile_height;
	u64 tile_count;
	u64* tile_offsets;
	u64* tile_byte_counts;
	char* image_description;
	u64 image_description_length;
	u8* jpeg_tables;
	u64 jpeg_tables_length;
	u16 compression; // 7 = JPEG
	u16 color_space;
	bool8 is_level_image;
	float level_magnification;
	u32 width_in_tiles;
	u32 height_in_tiles;
	float um_per_pixel_x;
	float um_per_pixel_y;
	float x_tile_side_in_um;
	float y_tile_side_in_um;
	u16 chroma_subsampling_horizontal;
	u16 chroma_subsampling_vertical;
	u64 reference_black_white_rational_count;
	tiff_rational_t* reference_black_white;
} tiff_ifd_t;


typedef struct network_location_t {
	i32 portno;
	const char* hostname;
	const char* filename;
} network_location_t;

struct tiff_t {
	bool32 is_remote;
	network_location_t location;
	FILE* fp;
#if !IS_SERVER
	HANDLE win32_file_handle;
#endif
	i64 filesize;
	u32 bytesize_of_offsets;
	u64 ifd_count;
	tiff_ifd_t* ifds; // sb
	tiff_ifd_t* main_image; // level 0 of the WSI; in Philips TIFF it's typically the first IFD
	u64 main_image_index;
	tiff_ifd_t* macro_image; // in Philips TIFF: typically the second-to-last IFD
	u64 macro_image_index;
	tiff_ifd_t* label_image; // in Philips TIFF: typically the last IFD
	u64 label_image_index;
	u64 level_count;
	tiff_ifd_t* level_images;
	u64 level_image_index;
	bool8 is_bigtiff;
	bool8 is_big_endian;
	float mpp_x;
	float mpp_y;
};

#pragma pack(push, 1)
typedef struct {
	i64 filesize;
	u64 ifd_count;
	u64 main_image_index; // level 0 of the WSI; in Philips TIFF it's typically the first IFD
	u64 macro_image_index; // in Philips TIFF: typically the second-to-last IFD
	u64 label_image_index; // in Philips TIFF: typically the last IFD
	u64 level_count;
	u64 level_image_index;
	u32 bytesize_of_offsets;
	bool8 is_bigtiff;
	bool8 is_big_endian;
	float mpp_x;
	float mpp_y;
} tiff_serial_header_t;

typedef struct {
	u32 image_width;
	u32 image_height;
	u32 tile_width;
	u32 tile_height;
//	u64* tile_offsets;
	u64 tile_count;
//	u64* tile_byte_counts;
//	char* image_description;
	u64 image_description_length;
//	u8* jpeg_tables;
	u64 jpeg_tables_length;
	u16 compression; // 7 = JPEG
	u16 color_space;
	float level_magnification;
	u32 width_in_tiles;
	u32 height_in_tiles;
	float um_per_pixel_x;
	float um_per_pixel_y;
	float x_tile_side_in_um;
	float y_tile_side_in_um;
	u16 chroma_subsampling_horizontal;
	u16 chroma_subsampling_vertical;
	bool8 is_level_image;
//	tiff_tile_t* tiles;
} tiff_serial_ifd_t;

enum serial_block_type_enum {
	SERIAL_BLOCK_LZ4_COMPRESSED_DATA = 4444,
	SERIAL_BLOCK_TIFF_HEADER_AND_META = 9001, // using ridiculous numbers to make invalid file structure easier to detect
	SERIAL_BLOCK_TIFF_IFDS = 9002,
	SERIAL_BLOCK_TIFF_IMAGE_DESCRIPTION = 9003,
	SERIAL_BLOCK_TIFF_TILE_OFFSETS = 9004,
	SERIAL_BLOCK_TIFF_TILE_BYTE_COUNTS = 9005,
	SERIAL_BLOCK_TIFF_JPEG_TABLES = 9006,
	SERIAL_BLOCK_TERMINATOR = 800,
};

typedef struct {
	u32 block_type;
	u32 index; // e.g. which IFD this data block belongs to
	u64 length;
} serial_block_t;

#pragma pack(pop)

typedef struct {
	u8* raw_memory;
	u8* data;
	u64 used_size;
	u64 capacity;
} push_buffer_t;


// see:
// https://stackoverflow.com/questions/41770887/cross-platform-definition-of-byteswap-uint64-and-byteswap-ulong
// byte swap operations adapted from this code:
// https://github.com/google/cityhash/blob/8af9b8c2b889d80c22d6bc26ba0df1afb79a30db/src/city.cc#L50
// License information copied in below:

// Copyright (c) 2011 Google, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// CityHash, by Geoff Pike and Jyrki Alakuijala

#ifdef __GNUC__

#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)

#elif _MSC_VER

#include <stdlib.h>
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)

#elif defined(__APPLE__)

// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#elif defined(__sun) || defined(sun)

#include <sys/byteorder.h>
#define bswap_16(x) BSWAP_16(x)
#define bswap_32(x) BSWAP_32(x)
#define bswap_64(x) BSWAP_64(x)

#elif defined(__FreeBSD__)

#include <sys/endian.h>
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)

#elif defined(__OpenBSD__)

#include <sys/types.h>
#define bswap_32(x) swap16(x)
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)

#elif defined(__NetBSD__)

#include <sys/types.h>
#include <machine/bswap.h>
#if defined(__BSWAP_RENAME) && !defined(__bswap_32)
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#endif

#else

#include <byteswap.h>

#endif

static inline u16 maybe_swap_16(u16 x, bool32 is_big_endian) {
	return is_big_endian ? bswap_16(x) : x;
}

static inline u32 maybe_swap_32(u32 x, bool32 is_big_endian) {
	return is_big_endian ? bswap_32(x) : x;
}

static inline u64 maybe_swap_64(u64 x, bool32 is_big_endian) {
	return is_big_endian ? bswap_64(x) : x;
}


u64 file_read_at_offset(void* dest, FILE* fp, u64 offset, u64 num_bytes);
bool32 open_tiff_file(tiff_t* tiff, const char* filename);
push_buffer_t* tiff_serialize(tiff_t* tiff, push_buffer_t* buffer);
i64 find_end_of_http_headers(u8* str, u64 len);
bool32 tiff_deserialize(tiff_t* tiff, u8* buffer, u64 buffer_size);
void tiff_destroy(tiff_t* tiff);

#ifdef __cplusplus
};
#endif

