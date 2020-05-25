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

#include "common.h"
#include "mathutils.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum annotation_type_enum {
	ANNOTATION_UNKNOWN_TYPE = 0,
	ANNOTATION_RECTANGLE = 1,
	ANNOTATION_POLYGON = 2,
} annotation_type_enum;

typedef enum asap_xml_element_enum {
	ASAP_XML_ELEMENT_NONE = 0, // for unhandled elements
	ASAP_XML_ELEMENT_ANNOTATION = 1,
	ASAP_XML_ELEMENT_COORDINATE = 2,
	ASAP_XML_ELEMENT_GROUP = 3,
} asap_xml_element_enum;

typedef enum asap_xml_attribute_enum {
	ASAP_XML_ATTRIBUTE_NONE = 0, // for unhandled attributes
	ASAP_XML_ATTRIBUTE_COLOR = 1,
	ASAP_XML_ATTRIBUTE_NAME = 2,
	ASAP_XML_ATTRIBUTE_PARTOFGROUP = 3,
	ASAP_XML_ATTRIBUTE_TYPE = 4,
	ASAP_XML_ATTRIBUTE_X = 5,
	ASAP_XML_ATTRIBUTE_Y = 6,
} asap_xml_attribute_enum;

typedef struct annotation_t {
	annotation_type_enum type;
	char name[64];
	rgba_t color;
	u32 group_id;
	u32 first_coordinate;
	u32 coordinate_count;
	bool8 has_coordinates;
} annotation_t;

typedef struct coordinate_t {
	i32 order;
	double x;
	double y;
} coordinate_t;

typedef struct annotation_group_t {
	char name[64];
	rgba_t color;
	bool8 is_explicitly_defined; // true if there is an associated <Group> in the XML file
} annotation_group_t;

typedef struct annotation_set_t_ {
	annotation_t* annotations; // sb
	u32 annotation_count;
	coordinate_t* coordinates; // sb
	u32 coordinate_count;
	annotation_group_t* groups; // sb
	u32 group_count;
} annotation_set_t;

void draw_annotations(annotation_set_t* annotation_set, v2f camera_min, float screen_um_per_pixel);
bool32 load_asap_xml_annotations(app_state_t* app_state, const char* filename);
void save_asap_xml_annotations(annotation_set_t* annotation_set, const char* filename_out);

#ifdef __cplusplus
}
#endif