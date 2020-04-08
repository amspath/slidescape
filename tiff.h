#pragma once

#include "common.h"
#include "stdio.h"

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
	TIFF_TAG_JPEG_TABLES = 347
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

typedef struct {
	u32 image_width;
	u32 image_height;
	u32 tile_width;
	u32 tile_height;
	u64* tile_offsets;
	u64 tile_count;
	u64 tile_byte_counts;
	char* image_description;
	u64 image_description_length;
	u16 compression; // 7 = JPEG
} tiff_ifd_t;

typedef struct {
	FILE* fp;
	i64 filesize;
	u32 bytesize_of_offsets;
	u64 ifd_count;
	tiff_ifd_t* ifds; // sb
	tiff_ifd_t* main_image; // level 0 of the WSI; in Philips TIFF it's typically the first IFD
	tiff_ifd_t* macro_image; // in Philips TIFF: typically the second-to-last IFD
	tiff_ifd_t* label_image; // in Philips TIFF: typically the last IFD
	u64 level_count;
	tiff_ifd_t* level_images; // sb
	bool8 is_bigtiff;
	bool8 is_big_endian;
} tiff_t;

static inline u16 maybe_swap_16(u16 x, bool32 is_big_endian) {
	return is_big_endian ? _byteswap_ushort(x) : x;
}

static inline u32 maybe_swap_32(u32 x, bool32 is_big_endian) {
	return is_big_endian ? _byteswap_ulong(x) : x;
}

static inline u64 maybe_swap_64(u64 x, bool32 is_big_endian) {
	return is_big_endian ? _byteswap_uint64(x) : x;
}

bool32 open_tiff_file(tiff_t* tiff, const char* filename);
