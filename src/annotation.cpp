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

#include "common.h"
#include "viewer.h"
#include "annotation.h"
#include "platform.h"
#include "gui.h"
#include "yxml.h"


// XML parsing using the yxml library.
// Note: what is the optimal stack buffer size for yxml?
// https://dev.yorhel.nl/yxml/man
#define YXML_STACK_BUFFER_SIZE KILOBYTES(32)

typedef struct asap_xml_parse_state_t {
	annotation_group_t current_group;
	asap_xml_element_enum element_type;
} asap_xml_parse_state_t;

asap_xml_parse_state_t global_parse_state;

void draw_annotations(app_state_t* app_state, scene_t* scene, annotation_set_t* annotation_set, v2f camera_min) {
	if (!annotation_set->enabled) return;

	refresh_annotation_pointers(app_state, annotation_set); // TODO: move this?
	recount_selected_annotations(app_state, annotation_set);

	bool did_popup = false;

	for (i32 annotation_index = 0; annotation_index < annotation_set->active_annotation_count; ++annotation_index) {
		annotation_t* annotation = annotation_set->active_annotations[annotation_index];
		annotation_group_t* group = annotation_set->groups + annotation->group_id;
//		rgba_t rgba = {50, 50, 0, 255 };
		rgba_t base_color = group->color;
		u8 alpha = (u8)(annotation_opacity * 255.0f);
		base_color.a = alpha;
		float thickness = annotation_normal_line_thickness;
		if (annotation->selected) {
			base_color.r = LERP(0.2f, base_color.r, 255);
			base_color.g = LERP(0.2f, base_color.g, 255);
			base_color.b = LERP(0.2f, base_color.b, 255);
			thickness = annotation_selected_line_thickness;
		}
		u32 annotation_color = *(u32*)(&base_color);
		if (annotation->has_coordinates) {
			ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
			// TODO: don't abuse the stack here
			v2f* points = (v2f*) alloca(sizeof(v2f) * annotation->coordinate_count);
			for (i32 i = 0; i < annotation->coordinate_count; ++i) {
				i32 coordinate_index = annotation->first_coordinate + i;
				coordinate_t* coordinate = annotation_set->coordinates + coordinate_index;
				v2f world_pos = {(float)coordinate->x, (float)coordinate->y};
				v2f transformed_pos = world_pos_to_screen_pos(world_pos, camera_min, scene->zoom.pixel_width);
				points[i] = transformed_pos;
			}
			// Draw the annotation in the background list (behind UI elements), as a thick colored line
			draw_list->AddPolyline((ImVec2*)points, annotation->coordinate_count, annotation_color, true, thickness);

			// draw coordinate nodes
			if (annotation->selected && (annotation_show_polygon_nodes_outside_edit_mode || annotation_set->is_edit_mode)) {
				bool need_hover = false;
				v2f hovered_node_point = {};
				for (i32 i = 0; i < annotation->coordinate_count; ++i) {
					i32 coordinate_index = annotation->first_coordinate + i;
					v2f point = points[i];
					if (annotation_set->is_edit_mode && !annotation_set->is_insert_coordinate_mode &&
					         coordinate_index == annotation_set->hovered_coordinate &&
					         annotation_set->hovered_coordinate_pixel_distance < annotation_hover_distance)
					{
						hovered_node_point = point;
						need_hover = true;
					} else {
						float node_size;
						rgba_t node_color;
						if (coordinate_index == annotation_set->selected_coordinate_index) {
							node_size = annotation_node_size * 1.2f;
//							node_color = {(u8)(group->color.r / 2) , (u8)(group->color.g / 2), (u8)(group->color.b / 2), 255};
							node_color = group->color;
							node_color.r = LERP(0.9f, 0, node_color.r);
							node_color.g = LERP(0.9f, 0, node_color.g);
							node_color.b = LERP(0.9f, 0, node_color.b);
							node_color.a = alpha;
						} else {
							node_size = annotation_node_size;
							node_color = base_color;
						}
						draw_list->AddCircleFilled(point, node_size, *(u32*)(&node_color), 12);
					}
				}
				if (need_hover) {
//											rgba_t node_rgba = {0, 0, 0, 255};
//					rgba_t node_rgba = {(u8)(group->color.r / 2) , (u8)(group->color.g / 2), (u8)(group->color.b / 2), 255};
					rgba_t hover_color = group->color;
					hover_color.a = alpha;
					draw_list->AddCircleFilled(hovered_node_point, annotation_node_size * 1.4f, *(u32*)(&hover_color), 12);
					if (ImGui::BeginPopupContextVoid()) {
						did_popup = true;
						if (ImGui::MenuItem("Delete coordinate", "C")) {
							delete_coordinate(annotation_set, annotation, annotation_set->hovered_coordinate);
						};
						if (ImGui::MenuItem("Insert coordinate", "Shift", &annotation_set->force_insert_mode)) {
							annotation_set->is_insert_coordinate_mode = true;
						};
						if (ImGui::MenuItem("Split annotation here", NULL, false, false)) {}
						ImGui::EndPopup();
					}
				}

				if (annotation_set->is_edit_mode && (annotation_set->is_insert_coordinate_mode)) {
					v2f projected_point = {};
					float distance = 1e9f;
					i32 insert_before_index = find_insertion_point_for_annotation(annotation_set, annotation, app_state->scene.mouse, &projected_point, &distance);
					if (insert_before_index >= 0) {
						float transformed_distance = distance / scene->zoom.pixel_width;
						if (transformed_distance < annotation_hover_distance) {
							v2f transformed_pos = world_pos_to_screen_pos(projected_point, camera_min, scene->zoom.pixel_width);
							rgba_t hover_color = group->color;
							hover_color.a = alpha / 2;
							draw_list->AddCircleFilled(transformed_pos, annotation_node_size * 1.4f, *(u32*)(&hover_color), 12);
						}
					}
				}
			}

		}
	}

	if (!did_popup) {
		if (ImGui::BeginPopupContextVoid()) {
			did_popup = true;
			if (ImGui::MenuItem("Enable editing", "E", &annotation_set->is_edit_mode)) {}
			if (annotation_set->selection_count > 0) {
				if (ImGui::MenuItem("Delete selected annotations", "Del")) {
					delete_selected_annotations(app_state, annotation_set);
				};
			}
			ImGui::EndPopup();
		}
	}
}

i32 find_nearest_annotation(annotation_set_t* annotation_set, float x, float y, float* distance_ptr,
                            i32* coordinate_index) {
	i32 result = -1;
	float shortest_sq_distance = 1e50;
	// TODO: use bounding boxes and subdivide line segments with far apart coordinates
	for (i32 annotation_index = 0; annotation_index < annotation_set->active_annotation_count; ++annotation_index) {
		annotation_t* annotation = annotation_set->active_annotations[annotation_index];
		if (annotation->has_coordinates) {
			for (i32 i = 0; i < annotation->coordinate_count; ++i) {
				coordinate_t* coordinate = annotation_set->coordinates + annotation->first_coordinate + i;
				float delta_x = x - (float)coordinate->x;
				float delta_y = y - (float)coordinate->y;
				float sq_distance = SQUARE(delta_x) + SQUARE(delta_y);
				if (sq_distance < shortest_sq_distance) {
					shortest_sq_distance = sq_distance;
					if (distance_ptr) {
						*distance_ptr = sqrtf(shortest_sq_distance);
					}
					result = annotation_index;
					if (coordinate_index) {
						*coordinate_index = annotation->first_coordinate + i;
					}
				}
			}
		}

	}
	return result;
}

i32 find_insertion_point_for_annotation(annotation_set_t* annotation_set, annotation_t* annotation, v2f point, v2f* projected_point_ptr, float* distance_ptr) {
	i32 insert_before_index = -1;
	ASSERT(annotation->coordinate_count > 0);
	if (annotation->coordinate_count == 1) {
		// trivial case
		coordinate_t* coordinate = annotation_set->coordinates + annotation->first_coordinate;
		v2f line_point = {(float)coordinate->x, (float)coordinate->y};
		if (projected_point_ptr) {
			*projected_point_ptr = line_point;
		}
		if (distance_ptr) {
			float distance = v2f_length(v2f_subtract(point, line_point));
			*distance_ptr = distance;
		}
		insert_before_index = 1;
	} else if (annotation->coordinate_count > 1) {
		// find the line segment (between coordinates) closest to the point we are checking against
		float closest_distance_sq = 1e9f;
		v2f closest_projected_point = {};
		bool found_closest = false;
		for (i32 i = 0; i < annotation->coordinate_count; ++i) {
			coordinate_t* coordinate_current = annotation_set->coordinates + annotation->first_coordinate + i;
			i32 coordinate_index_after = (i + 1) % annotation->coordinate_count;
			coordinate_t* coordinate_after = annotation_set->coordinates + annotation->first_coordinate + coordinate_index_after;
			v2f line_start = {(float)coordinate_current->x, (float)coordinate_current->y};
			v2f line_end = {(float)coordinate_after->x, (float)coordinate_after->y};
			v2f projected_point = project_point_on_line_segment(point, line_start, line_end);
			float distance_sq = v2f_length_squared(v2f_subtract(point, projected_point));
			if (distance_sq < closest_distance_sq) {
				found_closest = true;
				closest_distance_sq = distance_sq;
				closest_projected_point = projected_point;
				insert_before_index = coordinate_index_after;
			}
		}
		ASSERT(found_closest);
		if (found_closest) {
			if (projected_point_ptr) {
				*projected_point_ptr = closest_projected_point;
			}
			if (distance_ptr) {
				float closest_distance = sqrtf(closest_distance_sq);
				*distance_ptr = closest_distance;
			}
		}
	}
	return insert_before_index;
}

void annotations_modified(annotation_set_t* annotation_set) {
	annotation_set->modified = true; // need to (auto-)save the changes
	annotation_set->last_modification_time = get_clock();
}

void insert_coordinate(annotation_set_t* annotation_set, annotation_t* annotation, i32 insert_at_index, coordinate_t new_coordinate) {
	if (insert_at_index >= 0 && insert_at_index <= annotation->coordinate_count) {
		if (annotation->coordinate_count == annotation->coordinate_capacity) {
			// Make room by expanding annotation_set->coordinates and copying the coordinates to the end
			i32 new_capacity = annotation->coordinate_capacity * 2;
			i32 old_first_coordinate = annotation->first_coordinate;
			annotation->first_coordinate = annotation_set->coordinate_count;
			coordinate_t* new_coordinates = sb_add(annotation_set->coordinates, new_capacity);
			annotation_set->coordinate_count += new_capacity;
			annotation->coordinate_capacity = new_capacity;
			coordinate_t* old_coordinates = annotation_set->coordinates + old_first_coordinate;
			memcpy(new_coordinates, old_coordinates, sizeof(coordinate_t) * annotation->coordinate_count);
		}
		coordinate_t* coordinates_at_insertion_point = annotation_set->coordinates + annotation->first_coordinate + insert_at_index;
		i32 num_coordinates_to_move = annotation->coordinate_count - insert_at_index;
		memmove(coordinates_at_insertion_point + 1, coordinates_at_insertion_point, num_coordinates_to_move * sizeof(coordinate_t));
		++annotation->coordinate_count;
		*coordinates_at_insertion_point = new_coordinate;
//		console_print("inserted a coordinate at index %d\n", insert_at_index);
	} else {
#if DO_DEBUG
		console_print_error("Error: tried to insert a coordinate at an out of bounds index (%d)\n", insert_at_index);
#endif
	}

}

void delete_coordinate(annotation_set_t* annotation_set, annotation_t* annotation, i32 coordinate_index) {
	if (coordinate_index >= annotation->first_coordinate && coordinate_index < annotation->first_coordinate + annotation->coordinate_count) {
		i32 one_past_last_coordinate = annotation->first_coordinate + annotation->coordinate_count;
		i32 trailing_coordinate_count = one_past_last_coordinate - (coordinate_index + 1);
		if (trailing_coordinate_count > 0) {
			// move the trailing coordinates one place forward
			size_t temp_size = trailing_coordinate_count * sizeof(coordinate_t);
			void* temp_copy = alloca(temp_size);
			memcpy(temp_copy, annotation_set->coordinates + (coordinate_index + 1), temp_size);
			memcpy(annotation_set->coordinates + coordinate_index, temp_copy, temp_size);

		}

		annotation->coordinate_count--;
#if 0
		// fixing the order field seems unneeded, the order field is ignored in the XML file anyway.
	for (i32 i = coordinate_index - annotation->first_coordinate; i < annotation->coordinate_count ; ++i) {
		coordinate_t* coordinate = annotation_set->coordinates + annotation->first_coordinate + i;
		coordinate->order = i;
	}
#endif

		annotations_modified(annotation_set);
		annotation_set->selected_coordinate_index = -1;
	} else {
#if DO_DEBUG
		console_print_error("Error: tried to delete an out of bounds index (coordinate %d, valid range for annotation %d-%d)\n",
					  coordinate_index, annotation->first_coordinate, annotation->first_coordinate + annotation->coordinate_count);
#endif
	}
}

void delete_selected_annotations(app_state_t* app_state, annotation_set_t* annotation_set) {
	if (!annotation_set->stored_annotations) return;
	bool has_selected = false;
	for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
		annotation_t* annotation = annotation_set->active_annotations[i];
		if (annotation->selected) {
			has_selected = true;
			break;
		}
	}
	if (has_selected) {
		// rebuild the annotations, leaving out the deleted ones
		size_t copy_size = annotation_set->active_annotation_count * sizeof(i32);
		i32* temp_copy = (i32*) alloca(copy_size);
		memcpy(temp_copy, annotation_set->active_annotation_indices, copy_size);

		sb_raw_count(annotation_set->active_annotation_indices) = 0;
		for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
			annotation_t* annotation = annotation_set->active_annotations[i];
			if (annotation->selected) {
				// TODO: allow undo?
				continue; // skip (delete)
			} else {
				sb_push(annotation_set->active_annotation_indices, i);
			}
		}
		annotation_set->active_annotation_count = sb_count(annotation_set->active_annotation_indices);
		refresh_annotation_pointers(app_state, annotation_set);
		annotations_modified(annotation_set);
	}


}

i32 interact_with_annotations(app_state_t* app_state, scene_t* scene, input_t* input) {
	annotation_set_t* annotation_set = &scene->annotation_set;
	float coordinate_distance = 0.0f;
	i32 nearest_coordinate_index = -1;
	i32 nearest_annotation_index = find_nearest_annotation(annotation_set, scene->mouse.x, scene->mouse.y, &coordinate_distance,
	                                                       &nearest_coordinate_index);

	if (was_key_pressed(input, 'E')) {
		annotation_set->is_edit_mode = !annotation_set->is_edit_mode;
	}

	if (nearest_annotation_index >= 0 && nearest_coordinate_index >= 0) {
		ASSERT(scene->zoom.pixel_width > 0.0f);
		float coordinate_pixel_distance = coordinate_distance / scene->zoom.pixel_width;

		annotation_t* nearest_annotation = annotation_set->active_annotations[nearest_annotation_index];
		coordinate_t* nearest_coordinate = annotation_set->coordinates + nearest_coordinate_index;
		annotation_set->hovered_coordinate = nearest_coordinate_index;
		annotation_set->hovered_coordinate_pixel_distance = coordinate_pixel_distance;
//		console_print("The nearest coordinate is %d\n", nearest_coordinate_index);


		if (annotation_set->is_edit_mode && app_state->mouse_mode == MODE_VIEW && is_key_down(input, KEYCODE_SHIFT)) {
			annotation_set->is_insert_coordinate_mode = true;
		} else if (!annotation_set->force_insert_mode) {
			annotation_set->is_insert_coordinate_mode = false;
		}

		if (scene->drag_started && annotation_set->is_edit_mode && nearest_annotation->selected) {
			if (annotation_set->is_insert_coordinate_mode) {
				// check if we have clicked close enough to a line segment to insert a coordinate there
				v2f projected_point = {};
				float distance_to_edge = 1e9f;
				i32 insert_at_index = find_insertion_point_for_annotation(annotation_set, nearest_annotation, app_state->scene.mouse, &projected_point, &distance_to_edge);
				if (insert_at_index >= 0) {
					float pixel_distance_to_edge = distance_to_edge / scene->zoom.pixel_width;
					if (pixel_distance_to_edge < annotation_hover_distance) {
						// try to insert a new coordinate, and start dragging that
						coordinate_t new_coordinate = { .x = (double)projected_point.x, .y = (double)projected_point.y };
						insert_coordinate(annotation_set, nearest_annotation, insert_at_index, new_coordinate);
						// update state so we can immediately interact with the newly created coordinate
						annotation_set->is_insert_coordinate_mode = false;
						annotation_set->force_insert_mode = false;
						coordinate_distance = coordinate_pixel_distance = 0.0f;
						nearest_coordinate_index = nearest_annotation->first_coordinate + insert_at_index;
						nearest_coordinate = annotation_set->coordinates + nearest_coordinate_index;
						annotation_set->hovered_coordinate = nearest_coordinate_index;
						annotation_set->hovered_coordinate_pixel_distance = coordinate_pixel_distance;
					}
				}
			}

			if (coordinate_pixel_distance < annotation_hover_distance) {
				// Start dragging a coordinate
//				console_print("Selecting coordinate %d\n", nearest_coordinate_index);
				app_state->mouse_mode = MODE_DRAG_ANNOTATION_NODE;
				annotation_set->selected_coordinate_index = nearest_coordinate_index;
				annotation_set->coordinate_drag_start_offset.x = scene->mouse.x - nearest_coordinate->x;
				annotation_set->coordinate_drag_start_offset.y = scene->mouse.y - nearest_coordinate->y;
			}
		}

		if (scene->clicked) {
			// if annotation was already selected, we can try to select a coordinate as well
			annotation_set->selected_coordinate_index = -1;
			if (nearest_annotation->selected && coordinate_pixel_distance < annotation_hover_distance) {
				annotation_set->selected_coordinate_index = nearest_coordinate_index;
			}
			else {
				// have to click somewhat close to a coordinate, otherwise treat as unselect
				if (coordinate_pixel_distance < 500.0f) {
					nearest_annotation->selected = !nearest_annotation->selected;
				}
				annotation_set->selected_coordinate_index = -1;
				annotation_set->force_insert_mode = false;
			}

			if (nearest_annotation->selected && auto_assign_last_group && annotation_set->last_assigned_group_is_valid) {
				nearest_annotation->group_id = annotation_set->last_assigned_annotation_group;
			}
		}

	}

	// unselect all annotations (except if Ctrl held down)
	if (scene->clicked && !is_key_down(input, KEYCODE_CONTROL)) {
		for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
			if (i == nearest_annotation_index) continue; // skip the one we just selected!
			annotation_t* annotation = annotation_set->active_annotations[i];
			annotation->selected = false;
		}
	}

	recount_selected_annotations(app_state, annotation_set);

	if (annotation_set->selection_count > 0) {
		if (was_key_pressed(input, KEYCODE_DELETE)) {
			if (nearest_annotation_index > 0) {
				show_delete_annotation_prompt = true;
				ImGui::OpenPopup("delete_annotation_prompt");
				// TODO: fix release keyboard events bug for good
				input->keyboard.keys[KEYCODE_DELETE].down = false;
			}
		}

		if (was_key_pressed(input, 'C')) {
			if (nearest_annotation_index > 0 && annotation_set->selected_coordinate_index >= 0) {
				delete_coordinate(annotation_set, annotation_set->active_annotations[nearest_annotation_index], annotation_set->selected_coordinate_index);
			}
		}
	} else {
		// nothing selected
		annotation_set->force_insert_mode = false;
	}



	return nearest_annotation_index;
}

void set_group_for_selected_annotations(annotation_set_t* annotation_set, i32 new_group) {
	annotation_set->last_assigned_annotation_group = new_group;
	annotation_set->last_assigned_group_is_valid = true;
	for (i32 i = 0; i < annotation_set->selection_count; ++i) {
		annotation_t* annotation = annotation_set->selected_annotations[i];
		ASSERT(annotation->selected);
		annotation->group_id = new_group;
		annotations_modified(annotation_set);
	}
}

void draw_annotations_window(app_state_t* app_state, input_t* input) {

	annotation_set_t* annotation_set = &app_state->scene.annotation_set;

	const char** item_previews = (const char**) alloca(annotation_set->group_count * sizeof(char*));
	for (i32 i = 0; i < annotation_set->group_count; ++i) {
		annotation_group_t* group = annotation_set->groups + i;
		item_previews[i] = group->name;
	}

//	static i32 selected_group_index = -1;

	// find group corresponding to the currently selected annotations
	i32 annotation_group_index = -1;
	for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
		annotation_t* annotation = annotation_set->active_annotations[i];
		if (annotation->selected) {
			if (annotation_group_index == -1) {
				annotation_group_index = annotation->group_id;
			} else if (annotation_group_index != annotation->group_id) {
				annotation_group_index = -2; // multiple groups selected
			}

		}
	}
	bool nothing_selected = (annotation_group_index == -1);
	bool multiple_selected = (annotation_group_index == -2);


	// Detect hotkey presses for group assignment
	bool* hotkey_pressed = (bool*) alloca(annotation_set->group_count * sizeof(bool));
	memset(hotkey_pressed, 0, annotation_set->group_count * sizeof(bool));
	for (i32 i = 0; i < ATMOST(9, annotation_set->group_count); ++i) {
		if (was_key_pressed(input, '1'+i)) {
			hotkey_pressed[i] = true;
		}
	}
	if (annotation_set->group_count >= 10 && was_key_pressed(input, '0')) {
		hotkey_pressed[9] = true;
	}

	const char* preview = "";
	if (annotation_group_index >= 0 && annotation_group_index < annotation_set->group_count) {
		preview = item_previews[annotation_group_index];
	} else if (multiple_selected) {
		preview = "(multiple)"; // if multiple annotations with different groups are selected
	} else if (nothing_selected) {
		preview = "(nothing selected)";
	}

	if (show_annotations_window) {

		ImGui::SetNextWindowPos((ImVec2){830,43}, ImGuiCond_FirstUseEver, (ImVec2){0, 0});
		ImGui::SetNextWindowSize((ImVec2){525,673}, ImGuiCond_FirstUseEver);

		ImGui::Begin("Annotations", &show_annotations_window, 0);

		ImGui::Text("Annotation filename: %s\n", annotation_set->filename);
		ImGui::Text("Number of annotations active: %d\n", annotation_set->active_annotation_count);
		ImGui::Spacing();


		if (ImGui::CollapsingHeader("Options"))
		{
			ImGui::Checkbox("Show annotations", &annotation_set->enabled);
			ImGui::SliderFloat("Annotation opacity", &annotation_opacity, 0.0f, 1.0f);

			ImGui::SliderFloat("Line thickness (normal)", &annotation_normal_line_thickness, 0.0f, 10.0f);
			ImGui::SliderFloat("Line thickness (selected)", &annotation_selected_line_thickness, 0.0f, 10.0f);
			ImGui::NewLine();

//			ImGui::Checkbox("Show polygon nodes", &annotation_show_polygon_nodes_outside_edit_mode);
			ImGui::Checkbox("Allow editing annotation coordinates (press E to toggle)", &annotation_set->is_edit_mode);
			ImGui::SliderFloat("Coordinate node size", &annotation_node_size, 0.0f, 20.0f);

			ImGui::NewLine();
		}

		// Interface for viewing/editing annotation groups
		if (ImGui::CollapsingHeader("Groups"))
		{
			static i32 edit_group_index = -1;
			const char* edit_group_preview = "";

			if (edit_group_index >= 0 && edit_group_index < annotation_set->group_count) {
				edit_group_preview = item_previews[edit_group_index];
			} else {
				edit_group_index = -1;
			}

			bool disable_interface = annotation_set->group_count <= 0;
			u32 selectable_flags = 0;

			if (disable_interface) {
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
				selectable_flags |= ImGuiSelectableFlags_Disabled;
			}

			ImGui::Text("Number of groups: %d\n", annotation_set->group_count);
			if (ImGui::BeginCombo("Select group", edit_group_preview, ImGuiComboFlags_HeightLargest)) {
				for (i32 group_index = 0; group_index < annotation_set->group_count; ++group_index) {
					annotation_group_t* group = annotation_set->groups + group_index;

					if (ImGui::Selectable(item_previews[group_index], (edit_group_index == group_index), selectable_flags)) {
						edit_group_index = group_index;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Spacing();

			annotation_group_t* selected_group = NULL;
			if (edit_group_index >= 0) {
				selected_group = annotation_set->groups + edit_group_index;
			}
			const char* group_name = selected_group ? selected_group->name : "";
			ImGui::Text("Group name: %s\n", group_name);

			ImGuiColorEditFlags flags = 0;
			float color[3] = {};
			if (edit_group_index >= 0) {
				annotation_group_t* group = annotation_set->groups + edit_group_index;
				rgba_t rgba = group->color;
				color[0] = BYTE_TO_FLOAT(rgba.r);
				color[1] = BYTE_TO_FLOAT(rgba.g);
				color[2] = BYTE_TO_FLOAT(rgba.b);
				if (ImGui::ColorEdit3("Group color", (float*) color, flags)) {
					rgba.r = FLOAT_TO_BYTE(color[0]);
					rgba.g = FLOAT_TO_BYTE(color[1]);
					rgba.b = FLOAT_TO_BYTE(color[2]);
					group->color = rgba;
					annotations_modified(annotation_set);
				}
			} else {
				flags = ImGuiColorEditFlags_NoPicker;
				ImGui::ColorEdit3("Group color", (float*) color, flags);
			}

			if (disable_interface) {
				ImGui::PopItemFlag();
				ImGui::PopStyleVar();
			}

			ImGui::NewLine();


		}

		if (ImGui::CollapsingHeader("Annotation"))
		{
			u32 selectable_flags = 0;
			if (nothing_selected) {
				selectable_flags |= ImGuiSelectableFlags_Disabled;
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}

			if (ImGui::BeginCombo("Assign group", preview, ImGuiComboFlags_HeightLargest)) {
				for (i32 group_index = 0; group_index < annotation_set->group_count; ++group_index) {
					annotation_group_t* group = annotation_set->groups + group_index;

					if (ImGui::Selectable(item_previews[group_index], (annotation_group_index == group_index), selectable_flags, (ImVec2){})
					    || ((!nothing_selected) && hotkey_pressed[group_index])) {
						set_group_for_selected_annotations(annotation_set, group_index);
					}
				}
				ImGui::EndCombo();
			}

			if (nothing_selected) {
				ImGui::PopItemFlag();
				ImGui::PopStyleVar();
			}

			if (ImGui::Button("Open group assignment window")) {
				show_annotation_group_assignment_window = true;
			}

			ImGui::NewLine();

		}






		ImGui::End();
	}


	if (show_annotation_group_assignment_window) {

		ImGui::SetNextWindowPos((ImVec2){1359,43}, ImGuiCond_FirstUseEver, (ImVec2){0, 0});
		ImGui::SetNextWindowSize((ImVec2){285,572}, ImGuiCond_FirstUseEver);

		ImGui::Begin("Assign group", &show_annotation_group_assignment_window);

		ImGui::TextUnformatted(preview);


		u32 selectable_flags = 0;
		if (nothing_selected) {
			selectable_flags |= ImGuiSelectableFlags_Disabled;
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}

		for (i32 group_index = 0; group_index < annotation_set->group_count; ++group_index) {
			annotation_group_t* group = annotation_set->groups + group_index;

			u32 rgba_u32 = *(u32*) &group->color;
			ImVec4 color = ImColor(rgba_u32);

			static int e = 0;
			ImGui::PushID(group_index);
			color.w = 1.0f;
			ImGui::PushStyleColor(ImGuiCol_CheckMark, color);
//		    color.w = 0.6f;
//		    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color);
//		    color.w = 0.7f;
//		    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color);
			if (ImGui::Selectable("", (annotation_group_index == group_index), selectable_flags, ImVec2(0,ImGui::GetFrameHeight()))
			    || ((!nothing_selected) && hotkey_pressed[group_index])) {
				set_group_for_selected_annotations(annotation_set, group_index);
			}

			ImGui::SameLine(0); ImGui::RadioButton(item_previews[group_index], &annotation_group_index, group_index);
			if (group_index <= 9) {
				ImGui::SameLine(ImGui::GetWindowWidth()-40.0f);
				if (group_index <= 8) {
					ImGui::Text("[%d]", group_index+1);
				} else if (group_index == 9) {
					ImGui::TextUnformatted("[0]");
				}

			}
			ImGui::PopStyleColor(1);
			ImGui::PopID();

		}

		ImGui::Separator();
		ImGui::Checkbox("Auto-assign last group", &auto_assign_last_group);

		if (nothing_selected) {
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
		}

		ImGui::End();
	}

	if (show_delete_annotation_prompt) {
		// Always center this window when appearing
		ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ImGui::BeginPopupModal("delete_annotation_prompt", &show_delete_annotation_prompt, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("All those beautiful files will be deleted.\nThis operation cannot be undone!\n\n");
			ImGui::Separator();

			//static int unused_i = 0;
			//ImGui::Combo("Combo", &unused_i, "Delete\0Delete harder\0");

			static bool dont_ask_me_next_time = false;
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
			ImGui::Checkbox("Don't ask me next time", &dont_ask_me_next_time);
			ImGui::PopStyleVar();

			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				delete_selected_annotations(app_state, annotation_set);
				show_delete_annotation_prompt = false;
			}
			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				show_delete_annotation_prompt = false;
			}
			ImGui::EndPopup();
		}

	}



}

u32 add_annotation_group(annotation_set_t* annotation_set, const char* name) {
	annotation_group_t new_group = {};
	strncpy(new_group.name, name, sizeof(new_group.name));
	sb_push(annotation_set->groups, new_group);
	u32 new_group_index = annotation_set->group_count;
	++annotation_set->group_count;
	return new_group_index;
}

i32 find_annotation_group(annotation_set_t* annotation_set, const char* group_name) {
	for (i32 i = 0; i < annotation_set->group_count; ++i) {
		if (strcmp(annotation_set->groups[i].name, group_name) == 0) {
			return i;
		}
	}
	return -1; // not found
}

rgba_t asap_xml_parse_color(const char* value) {
	rgba_t rgba = {0, 0, 0, 255};
	if (strlen(value) != 7 || value[0] != '#') {
		console_print("annotation_set_attribute(): Color attribute \"%s\" not in form #rrggbb\n", value);
	} else {
		char temp[3] = {};
		temp[0] = value[1];
		temp[1] = value[2];
		rgba.r = (u8)strtoul(temp, NULL, 16);
		temp[0] = value[3];
		temp[1] = value[4];
		rgba.g = (u8)strtoul(temp, NULL, 16);
		temp[0] = value[5];
		temp[1] = value[6];
		rgba.b = (u8)strtoul(temp, NULL, 16);
	}
	return rgba;
}

void annotation_set_attribute(annotation_set_t* annotation_set, annotation_t* annotation, const char* attr,
                              const char* value) {
	if (strcmp(attr, "Color") == 0) {
		annotation->color = asap_xml_parse_color(value);
	} else if (strcmp(attr, "Name") == 0) {
		strncpy(annotation->name, value, sizeof(annotation->name));
	} else if (strcmp(attr, "PartOfGroup") == 0) {
		i32 group_index = find_annotation_group(annotation_set, value);
		if (group_index < 0) {
			group_index = add_annotation_group(annotation_set, value); // Group not found --> create it
		}
		annotation->group_id = group_index;
	} else if (strcmp(attr, "Type") == 0) {
		if (strcmp(value, "Rectangle") == 0) {
			annotation->type = ANNOTATION_RECTANGLE;
		} else if (strcmp(value, "Polygon") == 0) {
			annotation->type = ANNOTATION_POLYGON;
		} else {
			console_print("Warning: annotation '%s' with unrecognized type '%s', defaulting to 'Polygon'.\n", annotation->name, value);
			annotation->type = ANNOTATION_POLYGON;
		}
	}
}

void coordinate_set_attribute(coordinate_t* coordinate, const char* attr, const char* value) {
	if (strcmp(attr, "Order") == 0) {
		// ignored
//		coordinate->order = atoi(value);
	} else if (strcmp(attr, "X") == 0) {
		coordinate->x = atof(value) * 0.25; // TODO: address assumption
	} else if (strcmp(attr, "Y") == 0) {
		coordinate->y = atof(value) * 0.25; // TODO: address assumption
	}
}

void group_set_attribute(annotation_group_t* group, const char* attr, const char* value) {
	if (strcmp(attr, "Color") == 0) {
		group->color = asap_xml_parse_color(value);
	} else if (strcmp(attr, "Name") == 0) {
		strncpy(group->name, value, sizeof(group->name));
	} else if (strcmp(attr, "PartOfGroup") == 0) {
		// TODO: allow nested groups?
	}
}

void unload_and_reinit_annotations(annotation_set_t* annotation_set) {
	if (annotation_set->stored_annotations) sb_free(annotation_set->stored_annotations);
	if (annotation_set->coordinates) sb_free(annotation_set->coordinates);
	if (annotation_set->active_annotation_indices) sb_free(annotation_set->active_annotation_indices);
	if (annotation_set->groups) sb_free(annotation_set->groups);
	if (annotation_set->filename) free(annotation_set->filename);
	memset(annotation_set, 0, sizeof(*annotation_set));

	// reserve annotation group 0 for the "None" category
	add_annotation_group(annotation_set, "None");
}

enum {
	ASAP_XML_PARSE_GROUPS = 0,
	ASAP_XML_PARSE_ANNOTATIONS = 1,
};

bool32 load_asap_xml_annotations(app_state_t* app_state, const char* filename) {
	annotation_set_t* annotation_set = &app_state->scene.annotation_set;
	unload_and_reinit_annotations(annotation_set);

	asap_xml_parse_state_t* parse_state = &global_parse_state;
	memset(parse_state, 0, sizeof(*parse_state));

	mem_t* file = platform_read_entire_file(filename);
	yxml_t* x = NULL;
	bool32 success = false;
	i64 start = get_clock();

	if (0) { failed: cleanup:
		if (x) free(x);
		if (file) free(file);
		return success;
	}

	if (file) {
		// hack: merge memory for yxml_t struct and stack buffer
		x = (yxml_t*) malloc(sizeof(yxml_t) + YXML_STACK_BUFFER_SIZE);

		// ASAP puts all of the group definitions at the end of the file, instead of the beginning.
		// To preserve the order of the groups, we need to load the XML in two passes:
		// pass 0: read annotation groups only
		// pass 1: read annotations and coordinates
		for (i32 pass = 0; pass < 2; ++pass) {
			yxml_init(x, x + 1, YXML_STACK_BUFFER_SIZE);

			// parse XML byte for byte
			char attrbuf[128];
			char* attrbuf_end = attrbuf + sizeof(attrbuf);
			char* attrcur = NULL;
			char contentbuf[128];
			char* contentbuf_end = contentbuf + sizeof(contentbuf);
			char* contentcur = NULL;

			char* doc = (char*) file->data;
			for (; *doc; doc++) {
				yxml_ret_t r = yxml_parse(x, *doc);
				if (r == YXML_OK) {
					continue; // nothing worthy of note has happened -> continue
				} else if (r < 0) {
					goto failed;
				} else if (r > 0) {
					// token
					switch(r) {
						case YXML_ELEMSTART: {
							// start of an element: '<Tag ..'
//						    console_print("element start: %s\n", x->elem);
							contentcur = contentbuf;
							*contentcur = '\0';

							parse_state->element_type = ASAP_XML_ELEMENT_NONE;
							if (pass == ASAP_XML_PARSE_ANNOTATIONS && strcmp(x->elem, "Annotation") == 0) {
								annotation_t new_annotation = (annotation_t){};
								sb_push(annotation_set->stored_annotations, new_annotation);
								++annotation_set->stored_annotation_count;
								parse_state->element_type = ASAP_XML_ELEMENT_ANNOTATION;
							} else if (pass == ASAP_XML_PARSE_ANNOTATIONS && strcmp(x->elem, "Coordinate") == 0) {
								coordinate_t new_coordinate = (coordinate_t){};
								sb_push(annotation_set->coordinates, new_coordinate);
								parse_state->element_type = ASAP_XML_ELEMENT_COORDINATE;

								annotation_t* current_annotation = &sb_last(annotation_set->stored_annotations);
								if (!current_annotation->has_coordinates) {
									current_annotation->first_coordinate = annotation_set->coordinate_count;
									current_annotation->has_coordinates = true;
								}
								current_annotation->coordinate_count++;
								current_annotation->coordinate_capacity++; // used for delete/insert operations
								++annotation_set->coordinate_count;
							} else if (pass == ASAP_XML_PARSE_GROUPS && strcmp(x->elem, "Group") == 0) {
								parse_state->element_type = ASAP_XML_ELEMENT_GROUP;
								// reset the state (start parsing a new group)
								parse_state->current_group = (annotation_group_t){};
								parse_state->current_group.is_explicitly_defined = true; // (because this group has an XML tag)
							}
						} break;
						case YXML_CONTENT: {
							// element content
//						    console_print("   element content: %s\n", x->elem);
							if (!contentcur) break;
							char* tmp = x->data;
							while (*tmp && contentbuf < contentbuf_end) {
								*(contentcur++) = *(tmp++);
							}
							if (contentcur == contentbuf_end) {
								// too long content
								console_print("load_asap_xml_annotations(): encountered a too long XML element content\n");
								goto failed;
							}
							*contentcur = '\0';
						} break;
						case YXML_ELEMEND: {
							// end of an element: '.. />' or '</Tag>'
//						    console_print("element end: %s\n", x->elem);
							if (contentcur) {
								// NOTE: usually only whitespace (newlines and such)
//							    console_print("elem content: %s\n", contentbuf);
							}
							if (pass == ASAP_XML_PARSE_GROUPS && strcmp(x->elem, "Group") == 0) {
								annotation_group_t* parsed_group = &parse_state->current_group;
								// Check if a group already exists with this name, if not create it
								i32 group_index = find_annotation_group(annotation_set, parsed_group->name);
								if (group_index < 0) {
									group_index = add_annotation_group(annotation_set, parsed_group->name);
								}
								annotation_group_t* destination_group = annotation_set->groups + group_index;
								// 'Commit' the group with all its attributes
								memcpy(destination_group, parsed_group, sizeof(*destination_group));
							}
						} break;
						case YXML_ATTRSTART: {
							// attribute: 'Name=..'
//						    console_print("attr start: %s\n", x->attr);
							attrcur = attrbuf;
							*attrcur = '\0';
						} break;
						case YXML_ATTRVAL: {
							// attribute value
//						    console_print("   attr val: %s\n", x->attr);
							if (!attrcur) break;
							char* tmp = x->data;
							while (*tmp && attrbuf < attrbuf_end) {
								*(attrcur++) = *(tmp++);
							}
							if (attrcur == attrbuf_end) {
								// too long attribute
								console_print("load_asap_xml_annotations(): encountered a too long XML attribute\n");
								goto failed;
							}
							*attrcur = '\0';
						} break;
						case YXML_ATTREND: {
							// end of attribute '.."'
							if (attrcur) {
//							    console_print("attr %s = %s\n", x->attr, attrbuf);
								if (pass == ASAP_XML_PARSE_ANNOTATIONS && parse_state->element_type == ASAP_XML_ELEMENT_ANNOTATION) {
									annotation_set_attribute(annotation_set, &sb_last(annotation_set->stored_annotations), x->attr, attrbuf);
								} else if (pass == ASAP_XML_PARSE_ANNOTATIONS && parse_state->element_type == ASAP_XML_ELEMENT_COORDINATE) {
									coordinate_set_attribute(&sb_last(annotation_set->coordinates), x->attr, attrbuf);
								} else if (pass == ASAP_XML_PARSE_GROUPS && parse_state->element_type == ASAP_XML_ELEMENT_GROUP) {
									group_set_attribute(&parse_state->current_group, x->attr, attrbuf);
								}
							}
						} break;
						case YXML_PISTART:
						case YXML_PICONTENT:
						case YXML_PIEND:
							break; // processing instructions (uninteresting, skip)
						default: {
							console_print("yxml_parse(): unrecognized token (%d)\n", r);
							goto failed;
						}
					}
				}
			}
		}
	}

	// At this point, the indices for the 'active' annotations are all nicely in order (as they are loaded).
	// So we simply set the indices in ascending order, as a reference to look up the actual annotation_t struct.
	// (later on, the indices might get reordered by the user, annotations might get deleted, inserted, etc.)
	ASSERT(annotation_set->active_annotation_indices == NULL);
	annotation_set->active_annotation_indices = sb_add(annotation_set->active_annotation_indices, annotation_set->stored_annotation_count);
	annotation_set->active_annotation_count = annotation_set->stored_annotation_count;
	for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
		annotation_set->active_annotation_indices[i] = i;
	}

	annotation_set->filename = strdup(filename);
	success = true;
	annotation_set->enabled = true;
	float seconds_elapsed = get_seconds_elapsed(start, get_clock());
	console_print("Loaded annotations in %g seconds.\n", seconds_elapsed);

	goto cleanup;
	// return success;
}

const char* get_annotation_type_name(annotation_type_enum type) {
	const char* result = "";
	switch(type) {
		case ANNOTATION_UNKNOWN_TYPE: default: break;
		case ANNOTATION_RECTANGLE: result = "Rectangle"; break;
		case ANNOTATION_POLYGON: result = "Polygon"; break;
	}
	return result;

}

void asap_xml_print_color(char* buf, size_t bufsize, rgba_t rgba) {
	snprintf(buf, bufsize, "#%02x%02x%02x", rgba.r, rgba.g, rgba.b);
}

void save_asap_xml_annotations(annotation_set_t* annotation_set, const char* filename_out) {
	ASSERT(annotation_set);
	FILE* fp = fopen(filename_out, "wb");
	if (fp) {
//		const char* base_tag = "<ASAP_Annotations><Annotations>";

		fprintf(fp, "<ASAP_Annotations>");

		fprintf(fp, "<AnnotationGroups>");

		for (i32 group_index = 1 /* Skip group 0 ('None') */; group_index < annotation_set->group_count; ++group_index) {
			annotation_group_t* group = annotation_set->groups + group_index;

			char color_buf[32];
			asap_xml_print_color(color_buf, sizeof(color_buf), group->color);

			const char* part_of_group = "None";

			fprintf(fp, "<Group Color=\"%s\" Name=\"%s\" PartOfGroup=\"%s\"><Attributes /></Group>",
					color_buf, group->name, part_of_group);

		}

		fprintf(fp, "</AnnotationGroups>");

		fprintf(fp, "<Annotations>");

		for (i32 annotation_index = 0; annotation_index < annotation_set->active_annotation_count; ++annotation_index) {
			annotation_t* annotation = annotation_set->active_annotations[annotation_index];
			char color_buf[32];
			asap_xml_print_color(color_buf, sizeof(color_buf), annotation->color);

			const char* part_of_group = annotation_set->groups[annotation->group_id].name;
			const char* type_name = get_annotation_type_name(annotation->type);

			fprintf(fp, "<Annotation Color=\"%s\" Name=\"%s\" PartOfGroup=\"%s\" Type=\"%s\">",
			        color_buf, annotation->name, part_of_group, type_name);

			if (annotation->has_coordinates) {
				fprintf(fp, "<Coordinates>");
				for (i32 coordinate_index = 0; coordinate_index < annotation->coordinate_count; ++coordinate_index) {
					coordinate_t* coordinate = annotation_set->coordinates + annotation->first_coordinate + coordinate_index;
					fprintf(fp, "<Coordinate Order=\"%d\" X=\"%g\" Y=\"%g\" />", coordinate_index, coordinate->x / 0.25, coordinate->y / 0.25);
				}
				fprintf(fp, "</Coordinates>");
			}


			fprintf(fp, "</Annotation>");
		}

		fprintf(fp, "</Annotations></ASAP_Annotations>\n");

		fclose(fp);


	}
}

void autosave_annotations(app_state_t* app_state, annotation_set_t* annotation_set, bool force_ignore_delay) {
	if (!annotation_set->modified) return; // no changes, nothing to do
	if (!annotation_set->filename) return; // don't know where to save to / file doesn't already exist (?)

	bool proceed = force_ignore_delay;
	if (!force_ignore_delay) {
		float seconds_since_last_modified = get_seconds_elapsed(annotation_set->last_modification_time, get_clock());
		// only autosave if there haven't been any additional changes for some time (don't do it too often)
		if (seconds_since_last_modified > 2.0f) {
			proceed = true;
		}
	}
	if (proceed) {
		char backup_filename[4096];
		snprintf(backup_filename, sizeof(backup_filename), "%s.orig", annotation_set->filename);
		if (!file_exists(backup_filename)) {
			rename(annotation_set->filename, backup_filename);
		}
		save_asap_xml_annotations(annotation_set, annotation_set->filename);
		annotation_set->modified = false;
	}
}

// Should be called every time the number of active annotations changes.
void refresh_annotation_pointers(app_state_t* app_state, annotation_set_t* annotation_set) {
	annotation_set->active_annotations = NULL; // invalidated (temporary storage)
	size_t alive_annotations_array_size = sizeof(annotation_t*) * annotation_set->active_annotation_count;
	annotation_set->active_annotations = arena_push_array(&app_state->temp_arena, annotation_set->active_annotation_count, annotation_t*);
	for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
		annotation_set->active_annotations[i] = annotation_set->stored_annotations + annotation_set->active_annotation_indices[i];
	}
//	scene->has_annotations = (annotation_set->stored_annotation_count > 0); // TODO: move this somewhere?

}

void recount_selected_annotations(app_state_t* app_state, annotation_set_t* annotation_set) {
	i32 selection_count = 0;
	annotation_set->selected_annotations = (annotation_t**) arena_current_pos(&app_state->temp_arena);
	for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
		annotation_t* annotation = annotation_set->active_annotations[i];
		if (annotation->selected) {
			arena_push_array(&app_state->temp_arena, 1, annotation_t*); // reserve
			annotation_set->selected_annotations[selection_count] = annotation;
			++selection_count;
		}
	}
	annotation_set->selection_count = selection_count;
}
