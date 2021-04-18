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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "platform.h"

#include "yxml.h"

enum isyntax_image_type_enum {
	ISYNTAX_IMAGE_TYPE_NONE = 0,
	ISYNTAX_IMAGE_TYPE_MACROIMAGE = 1,
	ISYNTAX_IMAGE_TYPE_LABELIMAGE = 2,
	ISYNTAX_IMAGE_TYPE_WSI = 3,
};

enum isyntax_node_type_enum {
	ISYNTAX_NODE_NONE = 0,
	ISYNTAX_NODE_LEAF = 1, // ex. <Attribute Name="DICOM_MANUFACTURER" Group="0x0008" Element="0x0070"PMSVR="IString">PHILIPS</ Attribute>
	ISYNTAX_NODE_BRANCH = 2, // ex. <DataObject ObjectType="DPScannedImage"> (leaf nodes) </DataObject>
	ISYNTAX_NODE_ARRAY = 3, // <Array> (contains one or more similar type of leaf/branch nodes)
};

enum isyntax_group_0x301D_dicom_element_enum {
	PIM_DP_SCANNED_IMAGES                   = 0x1003, // DPScannedImage
	UFS_IMAGE_GENERAL_HEADERS               = 0x2000, // UFSImageGeneralHeader
	UFS_IMAGE_DIMENSIONS                    = 0x2003, // UFSImageDimension
	UFS_IMAGE_BLOCK_HEADER_TEMPLATES        = 0x2009, // UFSImageBlockHeaderTemplate
	UFS_IMAGE_DIMENSION_RANGES              = 0x200a, // UFSImageDimensionRange
	DP_COLOR_MANAGEMENT                     = 0x200b, // DPColorManagement
	DP_IMAGE_POST_PROCESSING                = 0x1014, // DPImagePostProcessing
	DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR = 0x1019, // DPWaveletQuantizerSeetingsPerColor
	DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL = 0x101a, // DPWaveletQuantizerSeetingsPerLevel
};

enum isyntax_data_object_flag_enum {
	ISYNTAX_OBJECT_DPUfsImport = 1,
	ISYNTAX_OBJECT_DPScannedImage = 2,
	ISYNTAX_OBJECT_UFSImageGeneralHeader = 4,
	ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate = 8,
	ISYNTAX_OBJECT_UFSImageDimension = 0x10,
	ISYNTAX_OBJECT_UFSImageDimensionRange = 0x20,
	ISYNTAX_OBJECT_DPColorManagement = 0x40,
	ISYNTAX_OBJECT_DPImagePostProcessing = 0x80,
	ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor = 0x100,
	ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel = 0x200,
};

#pragma pack(push, 1)
typedef struct dicom_tag_header_t {
	u16 group;
	u16 element;
	u32 size;
} dicom_tag_header_t;

typedef struct isyntax_partial_block_header_t {
	dicom_tag_header_t sequence_element_header;
	dicom_tag_header_t block_coordinates_header;
	u32 x_coordinate;
	u32 y_coordinate;
	u32 color_component;
	u32 scale;
	u32 coefficient;
	/* [MISSING] dicom_tag_header_t block_data_offset_header; */
	/* [MISSING] [MISSING] u64 block_data_offset; */
	/* [MISSING] [MISSING] dicom_tag_header_t block_size_header; */
	/* [MISSING] [MISSING] u64 block_size; */
	dicom_tag_header_t block_header_template_id_header;
	u32 block_header_template_id;
} isyntax_partial_block_header_t;

typedef struct isyntax_full_block_header_t {
	dicom_tag_header_t sequence_element_header;
	dicom_tag_header_t block_coordinates_header;
	u32 x_coordinate;
	u32 y_coordinate;
	u32 color_component;
	u32 scale;
	u32 coefficient;
	dicom_tag_header_t block_data_offset_header;
	u64 block_data_offset;
	dicom_tag_header_t block_size_header;
	u64 block_size;
	dicom_tag_header_t block_header_template_id_header;
	u32 block_header_template_id;
} isyntax_full_block_header_t;

typedef struct isyntax_seektable_codeblock_header_t {
	dicom_tag_header_t start_header;
	dicom_tag_header_t block_data_offset_header;
	u64 block_data_offset;
	dicom_tag_header_t block_size_header;
	u64 block_size;
} isyntax_seektable_codeblock_header_t;
#pragma pack(pop)

typedef struct isyntax_image_dimension_range_t {
	i32 start;
	i32 step;
	i32 end;
	i32 range;
} isyntax_image_dimension_range_t;

typedef struct isyntax_image_block_header_template_t {
	u32 tile_width;      // 256, 512, 1024, ...
	u32 tile_height;     // 256, 512, 1024, ...
	u8 color_component;  // 0=Y 1=Co 2=Cg
	u8 scale;            // range 0-8
	u8 waveletcoeff;     // either 1 for LL, or 3 for LH+HL+HH
} isyntax_header_template_t;

typedef struct isyntax_codeblock_t {
	u32 x_coordinate;
	u32 y_coordinate;
	u32 color_component;
	u32 scale;
	u32 coefficient;
	u64 block_data_offset;
	u8* data;
	u64 block_size;
	u32 block_header_template_id;
	u64 decompressed_size;
} isyntax_codeblock_t;

typedef struct isyntax_level_t {
	i32 scale;
	i32 tile_width;
	i32 tile_height;
	i32 width_in_tiles;
	i32 height_in_tiles;
	u64 codeblock_count;
	isyntax_codeblock_t* codeblocks;
} isyntax_level_t;

typedef struct isyntax_image_t {
	u32 image_type;
	u8* pixels;
	i32 width;
	i32 height;
	i32 offset_x;
	i32 offset_y;
	i32 num_levels;
	isyntax_level_t levels[16];
	bool compression_is_lossy;
	i32 lossy_image_compression_ratio;
	u8* encoded_image_data;
	size_t encoded_image_size;
	u8* block_header_table;
	size_t block_header_size;
	i32 codeblock_count;
	isyntax_codeblock_t* codeblocks;
	bool header_codeblocks_are_partial;
} isyntax_image_t;

typedef struct isyntax_parser_array_node_t {
	u32 array_len;
} isyntax_parser_array_node_t;

typedef struct isyntax_parser_node_t {
	u32 node_type; // leaf, branch, or array
	bool has_children;
	bool has_base64_content;
	u16 group;
	u16 element;
} isyntax_parser_node_t;

#define ISYNTAX_MAX_NODE_DEPTH 16

typedef struct isyntax_parser_t {
	yxml_t* x;
	isyntax_image_t* current_image;
	i32 running_image_index;
	u32 current_image_type;
	char* current_element_name;
	char* attrbuf;
	char* attrbuf_end;
	char* attrcur;
	size_t attrlen;
	size_t attrbuf_capacity;
	char* contentbuf;
	char* contentcur;
	size_t contentlen;
	size_t contentbuf_capacity;
	char current_dicom_attribute_name[256];
	u32 current_dicom_group_tag;
	u32 current_dicom_element_tag;
	i32 attribute_index;
	u32 current_node_type;
	bool current_node_has_children;
	// We need to use negative indexing into the node_stack (max 4) to check
	isyntax_parser_node_t safety_bytes_for_node_stack[4];
	isyntax_parser_node_t node_stack[ISYNTAX_MAX_NODE_DEPTH];
	i32 node_stack_index;
	u16 data_object_stack[ISYNTAX_MAX_NODE_DEPTH];
	i32 data_object_stack_index;
	u32 data_object_flags;
	i32 header_template_index;
	i32 dimension_index;
	bool initialized;
} isyntax_parser_t;

typedef struct isyntax_t {
	i64 filesize;
	isyntax_image_t images[16];
	i32 image_count;
	isyntax_header_template_t header_templates[64];
	isyntax_image_t* macro_image;
	isyntax_image_t* label_image;
	isyntax_image_t* wsi_image;
	isyntax_parser_t parser;
} isyntax_t;

// function prototypes
bool isyntax_open(isyntax_t* isyntax, const char* filename);


#ifdef __cplusplus
}
#endif
