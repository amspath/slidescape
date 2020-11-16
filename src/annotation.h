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
#ifndef ANNOTATION_H
#define ANNOTATION_H

#include "common.h"
#include "mathutils.h"
#include "viewer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct scene_t scene_t;

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
	bounds2f bounds;
	rgba_t color;
	i32 group_id;
	i32 first_coordinate;
	i32 coordinate_count;
	i32 coordinate_capacity;
	bool8 has_coordinates;
	bool8 selected;
	bool8 has_valid_bounds;
} annotation_t;

typedef struct coordinate_t {
	double x;
	double y;
//	i32 order;
	bool8 selected;
} coordinate_t;

typedef struct annotation_group_t {
	char name[64];
	rgba_t color;
	bool8 is_explicitly_defined; // true if there is an associated <Group> in the XML file
	bool8 selected;
} annotation_group_t;

typedef struct annotation_hit_result_t {
	i32 annotation_index;
	i32 line_segment_coordinate_index;
	float line_segment_distance;
	v2f line_segment_projected_point;
	float line_segment_t_clamped;
	i32 coordinate_index;
	float coordinate_distance;
	bool is_valid;
} annotation_hit_result_t;

typedef struct annotation_set_t {
	annotation_t* stored_annotations; // sb
	i32 stored_annotation_count;
	annotation_t** active_annotations; // recreated every frame
	coordinate_t* coordinates; // sb
	i32 coordinate_count;
	i32* active_annotation_indices; // sb
	i32 active_annotation_count;
	annotation_group_t* groups; // sb
	i32 group_count;
	bool enabled;
	char* filename;
	bool modified;
	i64 last_modification_time;
	i32 hovered_coordinate;
	float hovered_coordinate_pixel_distance;
	bool is_edit_mode;
	bool is_insert_coordinate_mode;
	bool force_insert_mode;
	bool is_split_mode;
	i32 selection_count;
	annotation_t** selected_annotations; // recreated every frame
	i32 selected_coordinate_index;
	//annotation_t* annotation_belonging_to_selected_coordinate; // TODO: implement; recalculate together with active_annotations?
	annotation_hit_result_t hit_result;
	v2f coordinate_drag_start_offset;
	i32 last_assigned_annotation_group;
	bool last_assigned_group_is_valid;
} annotation_set_t;


static inline bool coordinate_index_valid_for_annotation(i32 coordinate_index, annotation_t* annotation) {
	bool result = (coordinate_index >= annotation->first_coordinate && coordinate_index < annotation->first_coordinate + annotation->coordinate_count);
	return result;
}


void draw_annotations(app_state_t* app_state, scene_t* scene, annotation_set_t* annotation_set, v2f camera_min);
i32 find_nearest_annotation(annotation_set_t* annotation_set, float x, float y, float* distance_ptr, i32* coordinate_index);
i32 find_insertion_point_for_annotation(annotation_set_t* annotation_set, annotation_t* annotation, v2f point, float* t_ptr, v2f* projected_point_ptr, float* distance_ptr);
void annotations_modified(annotation_set_t* annotation_set);
void delete_selected_annotations(app_state_t* app_state, annotation_set_t* annotation_set);
void interact_with_annotations(app_state_t* app_state, scene_t* scene, input_t* input);
void delete_coordinate(annotation_set_t* annotation_set, annotation_t* annotation, i32 coordinate_index);
void draw_annotations_window(app_state_t* app_state, input_t* input);
void annotation_modal_dialog(app_state_t* app_state, annotation_set_t* annotation_set);
void unload_and_reinit_annotations(annotation_set_t* annotation_set);
bool32 load_asap_xml_annotations(app_state_t* app_state, const char* filename);
void save_asap_xml_annotations(annotation_set_t* annotation_set, const char* filename_out);
void autosave_annotations(app_state_t* app_state, annotation_set_t* annotation_set, bool force_ignore_delay);
void refresh_annotation_pointers(app_state_t* app_state, annotation_set_t* annotation_set);
void recount_selected_annotations(app_state_t* app_state, annotation_set_t* annotation_set);

#ifdef __cplusplus
}
#endif

#endif //ANNOTATION_H
