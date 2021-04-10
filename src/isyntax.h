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
	UFS_IMAGE_GENERAL_HEADERS   = 0x2000,
	UFS_IMAGE_BLOCK_HEADER_TEMPLATES = 0x2009,
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

} isyntax_image_block_header_template_t;

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
} isyntax_codeblock_t;

typedef struct isyntax_image_t {
	u32 image_type;
	u8* pixels;
	i32 width;
	i32 height;
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
	u16 image_header_parsing_mode;
	bool initialized;
} isyntax_parser_t;

typedef struct isyntax_t {
	i64 filesize;
	isyntax_image_t images[16];
	i32 image_count;
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
