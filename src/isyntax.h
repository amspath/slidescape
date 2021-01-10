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
#include "platform.h"

#include "yxml.h"

enum isyntax_image_type_enum {
	ISYNTAX_IMAGE_TYPE_NONE = 0,
	ISYNTAX_IMAGE_TYPE_MACROIMAGE = 1,
	ISYNTAX_IMAGE_TYPE_LABELIMAGE = 2,
	ISYNTAX_IMAGE_TYPE_WSI = 3,
};

typedef struct isyntax_image_t {
	u32 image_type;
} isyntax_image_t;

typedef struct isyntax_parser_t {
	yxml_t* x;
	isyntax_image_t* current_image;
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
	bool parsing_dicom_tag;
	bool initialized;
} isyntax_parser_t;

typedef struct isyntax_t {
	i64 filesize;
	isyntax_image_t macro_image;
	isyntax_image_t label_image;
	isyntax_image_t wsi_image;
	isyntax_parser_t parser;
} isyntax_t;

// function prototypes
bool isyntax_open(isyntax_t* isyntax, const char* filename);


#ifdef __cplusplus
}
#endif
