/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2022  Pieter Valkema

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

#ifndef DONT_INCLUDE_DICOM_DICT_H
#include "dicom_dict.h"
#else
typedef u32 dicom_uid_enum;
typedef u32 dicom_tag_enum;
#endif

// Convert two-byte strings into their little-endian number equivalent
#define LE_2CHARS(a,b) ( ((b)<<8) | (a) )
#define LE_4CHARS(a,b,c,d) ( ((d)<<24) | ((c)<<16) | ((b)<<8) | (a) )

enum dicom_value_representation_enum {
	DICOM_VR_AE = LE_2CHARS('A','E'), // Application Entity      // 4 bytes fixed
	DICOM_VR_AS = LE_2CHARS('A','S'), // Age String              // 4 bytes fixed
	DICOM_VR_AT = LE_2CHARS('A','T'), // Attribute Tag           // 4 bytes fixed
	DICOM_VR_CS = LE_2CHARS('C','S'), // Code String             // 16 bytes maximum
	DICOM_VR_DA = LE_2CHARS('D','A'), // Date                    // 8 bytes fixed
	DICOM_VR_DS = LE_2CHARS('D','S'), // Decimal String          // 16 bytes maximum
	DICOM_VR_DT = LE_2CHARS('D','T'), // Date Time               // 26 bytes maximum (or 54 for a range)
	DICOM_VR_FD = LE_2CHARS('F','D'), // Floating Point Double   // 8 bytes fixed
	DICOM_VR_FL = LE_2CHARS('F','L'), // Floating Point Single   // 4 bytes fixed
	DICOM_VR_IS = LE_2CHARS('I','S'), // Integer String          // 12 bytes maximum
	DICOM_VR_LO = LE_2CHARS('L','O'), // Long String             // 64 chars maximum
	DICOM_VR_LT = LE_2CHARS('L','T'), // Long Text               // 10240 chars maximum
	DICOM_VR_OB = LE_2CHARS('O','B'), // Other Byte (String)
	DICOM_VR_OD = LE_2CHARS('O','D'), // Other Double (String)
	DICOM_VR_OF = LE_2CHARS('O','F'), // Other Float (String)
	DICOM_VR_OL = LE_2CHARS('O','L'), // Other Long (String)
	DICOM_VR_OV = LE_2CHARS('O','V'), // Other 64-Bit Very Long (String)
	DICOM_VR_OW = LE_2CHARS('O','W'), // Other Word (String)
	DICOM_VR_PN = LE_2CHARS('P','N'), // Person Name
	DICOM_VR_SH = LE_2CHARS('S','H'), // Short String            // 16 chars maximum
	DICOM_VR_SL = LE_2CHARS('S','L'), // Signed Long             // 4 bytes fixed
	DICOM_VR_SQ = LE_2CHARS('S','Q'), // Sequence of Items
	DICOM_VR_SS = LE_2CHARS('S','S'), // Signed Short            // 2 bytes fixed
	DICOM_VR_ST = LE_2CHARS('S','T'), // Short Text              // 1024 chars maximum
	DICOM_VR_SV = LE_2CHARS('S','V'), // Signed 64-Bit Very Long // 8 bytes fixed
	DICOM_VR_TM = LE_2CHARS('T','M'), // Time                    // 14 bytes maximum
	DICOM_VR_UC = LE_2CHARS('U','C'), // Unlimited Characters
	DICOM_VR_UI = LE_2CHARS('U','I'), // Unique Identifier       // 64 bytes maximum
	DICOM_VR_UL = LE_2CHARS('U','L'), // Unsigned Long           // 4 bytes fixed
	DICOM_VR_UN = LE_2CHARS('U','N'), // Unknown
	DICOM_VR_UR = LE_2CHARS('U','R'), // URI/URL
	DICOM_VR_US = LE_2CHARS('U','S'), // Unsigned Short          // 2 bytes fixed
	DICOM_VR_UT = LE_2CHARS('U','T'), // Unlimited Text
	DICOM_VR_UV = LE_2CHARS('U','V'), // Unsigned 64-Bit Very Long // 8 bytes fixed
};


typedef enum dicom_transfer_syntax_enum {
	DICOM_TRANSFER_SYNTAX_IMPLICIT_VR_LITTLE_ENDIAN,
	DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_LITTLE_ENDIAN,
	DICOM_TRANSFER_SYNTAX_DEFLATED_EXPLICIT_VR_LITTLE_ENDIAN,
	DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_BIG_ENDIAN_RETIRED,
} dicom_transfer_syntax_enum;

typedef enum dicom_photometric_interpretation_enum {
	DICOM_PHOTOMETRIC_INTERPRETATION_UNKNOWN = 0,
	DICOM_PHOTOMETRIC_INTERPRETATION_MONOCHROME1 = 1,
	DICOM_PHOTOMETRIC_INTERPRETATION_MONOCHROME2 = 2,
	DICOM_PHOTOMETRIC_INTERPRETATION_PALETTE_COLOR = 3,
	DICOM_PHOTOMETRIC_INTERPRETATION_RGB = 4,
	DICOM_PHOTOMETRIC_INTERPRETATION_HSV = 5, // retired
	DICOM_PHOTOMETRIC_INTERPRETATION_ARGB = 6, // retired
	DICOM_PHOTOMETRIC_INTERPRETATION_CMYK = 7, // retired
	DICOM_PHOTOMETRIC_INTERPRETATION_YBR_FULL = 8,
	DICOM_PHOTOMETRIC_INTERPRETATION_YBR_FULL_422 = 9,
	DICOM_PHOTOMETRIC_INTERPRETATION_YBR_PARTIAL_422 = 10, // retired
	DICOM_PHOTOMETRIC_INTERPRETATION_YBR_PARTIAL_420 = 11,
	DICOM_PHOTOMETRIC_INTERPRETATION_YBR_ICT = 12,
	DICOM_PHOTOMETRIC_INTERPRETATION_YBR_RCT = 13,
} dicom_photometric_interpretation_enum;

typedef enum dicom_lossy_image_compression_method_enum {
	DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_UNKNOWN = 0,
	DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_ISO_10918_1, // JPEG Lossy Compression
	DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_ISO_14495_1, // JPEG-LS Near-lossless Compression
	DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_ISO_15444_1, // JPEG 2000 Irreversible Compression
	DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_ISO_13818_2, // MPEG2 Compression
	DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_ISO_14496_10, // MPEG-4 AVC/H.264 Compression
	DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_ISO_23008_2, // HEVC/H.265 Lossy Compression

} dicom_lossy_image_compression_method_enum;

typedef enum dicom_image_flavor_enum {
	DICOM_IMAGE_FLAVOR_UNKNOWN = 0,
	DICOM_IMAGE_FLAVOR_VOLUME,
	DICOM_IMAGE_FLAVOR_LABEL,
	DICOM_IMAGE_FLAVOR_OVERVIEW,
	DICOM_IMAGE_FLAVOR_THUMBNAIL,
} dicom_image_flavor_enum;

typedef struct dicom_header_t {
	u8 preamble[128]; // the preamble will be all zeroes, if not used by a specific application/implementation
	union {
		char prefix[4]; // optional; should contain "DICM"
		u32 prefix_as_u32;
	};
} dicom_header_t;

#define DICOM_UNDEFINED_LENGTH 0xFFFFFFFF

#pragma pack(push,1)

typedef struct dicom_tag_t {
	union {
		struct {
			u16 group;
			u16 element;
		};
		u32 as_u32;
		dicom_tag_enum as_enum;
	};
} dicom_tag_t;

typedef struct dicom_explicit_data_element_header_t {
	dicom_tag_t tag;
	union {
		u16 vr;
		char vr_string[2];
	};
	u8 variable_part[0];
} dicom_explicit_data_element_header_t;

typedef struct dicom_implicit_data_element_header_t {
	dicom_tag_t tag;
	u32 value_length;
	u8 data[0];
} dicom_implicit_data_element_header_t;

typedef struct dicom_sequence_item_t {
	dicom_tag_t item_tag;
	u32 length;
	u8 data[0];
} dicom_sequence_item_t;
#pragma pack(pop)

typedef struct dicom_data_element_t {
	dicom_tag_t tag;
	u32 length;
	u16 vr;
	bool is_valid;
//	u8* data;
	i64 data_offset;
} dicom_data_element_t;


typedef struct dicom_cs_t {
	char value[17]; // 16 bytes maximum, plus an extra byte to zero-terminate the string
} dicom_cs_t;


typedef struct dicom_series_t dicom_series_t; // fwd declaration
typedef struct dicom_instance_t dicom_instance_t; // fwd declaration
typedef void dicom_parser_callback_func_t(dicom_series_t* series, dicom_instance_t* instance, dicom_data_element_t element);

typedef struct dicom_parser_pos_t {
	dicom_data_element_t element;
	i64 offset;
	i64 element_index;
	i64 item_number;
	i64 bytes_left_in_sequence_or_item;
} dicom_parser_pos_t;

typedef struct dicom_series_t {
	dicom_parser_callback_func_t* tag_handler_func;
	i64 bytes_read;
	dicom_transfer_syntax_enum encoding;
	FILE* debug_output_file;
} dicom_series_t;

typedef struct dicom_instance_t {
	dicom_series_t* series;
	dicom_parser_callback_func_t* tag_handler_func;
	i32 nesting_level;
//	u32 current_item_number;
	dicom_transfer_syntax_enum encoding;
	u8* data;
	i64 bytes_read_from_file;
	i64 total_bytes_in_stream;
	dicom_parser_pos_t pos_stack[16]; // one per nesting level
	bool is_image_invalid;
	bool found_pixel_data;
	bool is_pixel_data_encapsulated;
	bool has_basic_offset_table;
	dicom_data_element_t pixel_data;
	u32* offsets; // array
	dicom_uid_enum media_storage_sop_class_uid;
	dicom_uid_enum transfer_syntax_uid;
	bool is_image_original;
	dicom_image_flavor_enum image_flavor;
	bool is_image_resampled;
	u16 samples_per_pixel; // 1 for monochrome and palette color images, 3 for RGB images
	dicom_photometric_interpretation_enum photometric_interpretation;
	u16 planar_configuration;
	i64 number_of_frames;
	u16 rows;
	u16 columns;
	u16 bits_allocated;
	u16 bits_stored;
	u16 high_bit;
	u16 pixel_representation;
	dicom_lossy_image_compression_method_enum lossy_image_compression_method;
	float imaged_volume_width;
	float imaged_volume_height;
	float imaged_volume_depth;
	u32 total_pixel_matrix_columns;
	u32 total_pixel_matrix_rows;
} dicom_instance_t;

typedef struct dicom_context_t {

} dicom_context_t;

typedef struct directory_info_t directory_info_t; // from viewer.h
typedef struct file_info_t file_info_t;

bool dicom_init();
bool is_file_a_dicom_file(u8* file_header_data, size_t file_header_data_len);
bool dicom_open_from_directory(dicom_series_t* dicom, directory_info_t* directory);
bool dicom_open_from_file(dicom_series_t* dicom, file_info_t* file);
dicom_cs_t dicom_parse_code_string(const char* s, size_t len, const char** next);

#ifdef __cplusplus
}
#endif
