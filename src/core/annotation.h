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
#ifndef ANNOTATION_H
#define ANNOTATION_H

#include "common.h"
#include "mathutils.h"
#include "viewer.h"
#include "coco.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct scene_t scene_t;

typedef enum annotation_type_enum {
	ANNOTATION_UNKNOWN_TYPE = 0,
	ANNOTATION_RECTANGLE = 1,
	ANNOTATION_POLYGON = 2,
	ANNOTATION_POINT = 3,
	ANNOTATION_LINE = 4,
	ANNOTATION_SPLINE = 5,
	ANNOTATION_ELLIPSE = 6,
	ANNOTATION_TEXT = 7,
} annotation_type_enum;



#define MAX_ANNOTATION_FEATURES 64

enum annotation_valid_flags_enum {
	ANNOTATION_VALID_BOUNDS = 1,
	ANNOTATION_VALID_TESSELATION = 2,
	ANNOTATION_VALID_AREA = 4,
	ANNOTATION_VALID_LENGTH = 8,
};

typedef struct annotation_t {
	annotation_type_enum type;
	char name[256];
	float features[MAX_ANNOTATION_FEATURES]; // TODO: make expandable
	rgba_t color;
	i32 group_id;
	v2f* coordinates;
	i32 coordinate_count;

	// 'Derived' calculations
	bounds2f bounds;
	v2f* tesselated_trianges;
	bool is_complex_polygon; // meaning tesselation failed, most likely due to the polygon intersection itself
	float area;
	float length;
	u32 valid_flags; // keeps track of whether derived calculations are valid
	u32 fallback_valid_flags; // if invalidated, keep track of whether an old/outdated derived calculation exists

	// TODO: use flags here?
	bool8 selected;
	bool8 has_properties;
	bool8 is_open; // for 'unfinished' polygons

	v2f p0, p1;
} annotation_t;

/*typedef struct coordinate_t {
	float x;
	float y;
//	i32 order;
//	bool selected;
} coordinate_t;*/

typedef struct annotation_group_t {
	char name[256];
	rgba_t color;
	i32 id;
	bool is_explicitly_defined; // true if there is an associated <Group> in the XML file
	bool selected;
	bool deleted; // TODO: is this right?
} annotation_group_t;

typedef struct annotation_feature_t {
	char name[256];
	rgba_t color;
	i32 id;
	bool restrict_to_group;
	bool deleted;
	i32 group_id;
} annotation_feature_t;

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
	annotation_t* stored_annotations; // array
	i32 stored_annotation_count;
	i32* active_annotation_indices; // array
	i32 active_annotation_count;

	annotation_group_t* stored_groups; // array
	i32 stored_group_count;
	i32* active_group_indices; // array
	i32 active_group_count;

	annotation_feature_t* stored_features; // array
	i32 stored_feature_count;
	i32* active_feature_indices; // array
	i32 active_feature_count;

	char asap_xml_filename[512];
	char* coco_filename;
	char base_filename[512];
	bool modified;
	i64 last_modification_time;
	i32 hovered_annotation;
	i32 hovered_coordinate;
	float hovered_coordinate_pixel_distance;
	bool is_edit_mode;
	bool is_insert_coordinate_mode;
	bool force_insert_mode;
	bool is_split_mode;
	i32 selection_count;
	annotation_t** selected_annotations; // recreated every frame
	i32 selected_coordinate_annotation_index;
	i32 selected_coordinate_index;
	//annotation_t* annotation_belonging_to_selected_coordinate; // TODO: implement; recalculate together with active_annotations?
	annotation_hit_result_t hit_result;
	v2f coordinate_drag_start_offset;
	i32 last_assigned_annotation_group;
	bool last_assigned_group_is_valid;
	i32 editing_annotation_index; // The active index of the annotation that is currently being edited
	v2f mpp; // microns per pixel
	coco_t coco;
	bool export_as_asap_xml;
	bool annotations_were_loaded_from_file;
} annotation_set_t;


static inline bool coordinate_index_valid_for_annotation(i32 coordinate_index, annotation_t* annotation) {
	bool result = (coordinate_index >= 0 && coordinate_index < annotation->coordinate_count);
	return result;
}

static inline annotation_t* get_active_annotation(annotation_set_t* annotation_set, i32 active_index) {
	ASSERT(active_index >= 0 && active_index < annotation_set->active_annotation_count);
	return annotation_set->stored_annotations + annotation_set->active_annotation_indices[active_index];
}

static inline annotation_group_t* get_active_annotation_group(annotation_set_t* annotation_set, i32 active_index) {
	ASSERT(active_index >= 0 && active_index < annotation_set->active_group_count);
	return annotation_set->stored_groups + annotation_set->active_group_indices[active_index];
}

static inline annotation_feature_t* get_active_annotation_feature(annotation_set_t* annotation_set, i32 active_index) {
	ASSERT(active_index >= 0 && active_index < annotation_set->active_feature_count);
	return annotation_set->stored_features + annotation_set->active_feature_indices[active_index];
}

u32 add_annotation_group(annotation_set_t* annotation_set, const char* name);
u32 add_annotation_feature(annotation_set_t* annotation_set, const char* name);
i32 find_annotation_group(annotation_set_t* annotation_set, const char* group_name);
void select_annotation(annotation_set_t* annotation_set, annotation_t* annotation);
void deselect_all_annotations(annotation_set_t* annotation_set);
void do_drag_annotation_node(scene_t* scene);
void create_ellipse_annotation(annotation_set_t* annotation_set, v2f pos);
void do_mouse_tool_create_ellipse(app_state_t* app_state, input_t* input, scene_t* scene, annotation_set_t* annotation_set);
void create_rectangle_annotation(annotation_set_t* annotation_set, v2f pos);
void do_mouse_tool_create_rectangle(app_state_t* app_state, input_t* input, scene_t* scene, annotation_set_t* annotation_set);
void create_freeform_annotation(annotation_set_t* annotation_set, v2f pos);
void do_mouse_tool_create_freeform(app_state_t* app_state, input_t* input, scene_t* scene, annotation_set_t* annotation_set);
void create_line_annotation(annotation_set_t* annotation_set, v2f pos);
void do_mouse_tool_create_line(app_state_t* app_state, input_t* input, scene_t* scene, annotation_set_t* annotation_set);
void create_point_annotation(annotation_set_t* annotation_set, v2f pos);
void interact_with_annotations(app_state_t* app_state, scene_t* scene, input_t* input);
bounds2f bounds_for_annotation(annotation_set_t* annotation_set, annotation_t* annotation);
void annotation_recalculate_bounds_if_necessary(annotation_set_t* annotation_set, annotation_t* annotation);
bool is_point_within_annotation_bounds(annotation_set_t* annotation_set, annotation_t* annotation, v2f point, float tolerance_margin);
annotation_hit_result_t get_annotation_hit_result(annotation_set_t* annotation_set, v2f point, float bounds_check_tolerance, float bias_for_selected);
i32 project_point_onto_annotation(annotation_set_t* annotation_set, annotation_t* annotation, v2f point, float* t_ptr, v2f* projected_point_ptr, float* distance_ptr);
void deselect_annotation_coordinates(annotation_set_t* annotation_set);
void notify_annotation_set_modified(annotation_set_t* annotation_set);
void annotation_invalidate_derived_calculations(annotation_t* annotation);
void insert_coordinate(app_state_t* app_state, annotation_set_t* annotation_set, annotation_t* annotation, i32 insert_at_index, v2f new_coordinate);
void delete_coordinate(annotation_set_t* annotation_set, i32 annotation_index, i32 coordinate_index);
void delete_selected_annotations(app_state_t* app_state, annotation_set_t* annotation_set);
void delete_annotation(annotation_set_t* annotation_set, i32 active_annotation_index);
void split_annotation(app_state_t* app_state, annotation_set_t* annotation_set, annotation_t* annotation, i32 first_coordinate_index, i32 second_coordinate_index);
void set_group_for_selected_annotations(annotation_set_t* annotation_set, i32 new_group);
i32 annotation_cycle_selection_within_group(annotation_set_t* annotation_set, i32 delta);
void set_region_for_whole_slide(scene_t* scene, image_t* image);
void set_region_encompassing_selected_annotations(annotation_set_t* annotation_set, scene_t* scene);
void center_scene_on_annotation(scene_t* scene, annotation_set_t* annotation_set, annotation_t* annotation);
void draw_annotations(app_state_t* app_state, scene_t* scene, annotation_set_t* annotation_set, v2f camera_min);
void draw_annotations_window(app_state_t* app_state, input_t* input);
void annotation_modal_dialog(app_state_t* app_state, annotation_set_t* annotation_set);
void draw_annotation_palette_window();
annotation_t duplicate_annotation(annotation_t* annotation);
void destroy_annotation(annotation_t* annotation);
void destroy_annotation_set(annotation_set_t* annotation_set);
void unload_and_reinit_annotations(annotation_set_t* annotation_set);
bool32 load_asap_xml_annotations(app_state_t* app_state, const char* filename);
void save_asap_xml_annotations(annotation_set_t* annotation_set, const char* filename_out);
void autosave_annotations(app_state_t* app_state, annotation_set_t* annotation_set, bool force_ignore_delay);
void recount_selected_annotations(app_state_t* app_state, annotation_set_t* annotation_set);
annotation_set_t create_offsetted_annotation_set_for_area(annotation_set_t* annotation_set, bounds2f area, bool push_coordinates_inward);

#ifdef __cplusplus
}
#endif

#endif //ANNOTATION_H
