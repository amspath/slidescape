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

#include <math.h>

//#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
//#define CIMGUI_NO_EXPORT
#include "imgui.h"
#include "imgui_internal.h"
//#include "cimgui.h"

// XML parsing using the yxml library.
// Note: what is the optimal stack buffer size for yxml?
// https://dev.yorhel.nl/yxml/man
#define YXML_STACK_BUFFER_SIZE KILOBYTES(32)

typedef struct asap_xml_parse_state_t {
	annotation_group_t current_group;
	asap_xml_element_enum element_type;
} asap_xml_parse_state_t;

asap_xml_parse_state_t global_parse_state;

void draw_annotations(annotation_set_t* annotation_set, v2f camera_min, float screen_um_per_pixel) {
	if (!annotation_set->enabled) return;
	for (i32 annotation_index = 0; annotation_index < annotation_set->annotation_count; ++annotation_index) {
		annotation_t* annotation = annotation_set->annotations + annotation_index;
		annotation_group_t* group = annotation_set->groups + annotation->group_id;
//		rgba_t rgba = {50, 50, 0, 255 };
		rgba_t rgba = group->color;
		rgba.a = 255;
		float thickness = 2.0f;
		if (annotation->selected) {
			rgba.r = LERP(0.2f, rgba.r, 255);
			rgba.g = LERP(0.2f, rgba.g, 255);
			rgba.b = LERP(0.2f, rgba.b, 255);
			thickness *= 2.0f;
		}
		u32 color = *(u32*)(&rgba);
		if (annotation->has_coordinates) {
			v2f* points = (v2f*) alloca(sizeof(v2f) * annotation->coordinate_count);
			for (i32 i = 0; i < annotation->coordinate_count; ++i) {
				coordinate_t* coordinate = annotation_set->coordinates + annotation->first_coordinate + i;
				v2f world_pos = {(float)coordinate->x, (float)coordinate->y};
				v2f transformed_pos = world_pos_to_screen_pos(world_pos, camera_min, screen_um_per_pixel);
				points[i] = transformed_pos;
			}
			// Draw the annotation in the background list (behind UI elements), as a thick colored line
			ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
			draw_list->AddPolyline((ImVec2*)points, annotation->coordinate_count, color, true, thickness);
		}
	}
}

i32 find_nearest_annotation(annotation_set_t* annotation_set, float x, float y, float* distance_ptr) {
	i32 result = -1;
	float shortest_sq_distance = 1e50;
	for (i32 annotation_index = 0; annotation_index < annotation_set->annotation_count; ++annotation_index) {
		annotation_t* annotation = annotation_set->annotations + annotation_index;
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
				}
			}
		}

	}
	return result;
}

void annotations_modified(annotation_set_t* annotation_set) {
	annotation_set->modified = true; // need to (auto-)save the changes
	annotation_set->last_modification_time = get_clock();
}

void delete_selected_annotations(annotation_set_t* annotation_set) {
	if (!annotation_set->annotations) return;
	bool has_selected = false;
	for (i32 i = 0; i < annotation_set->annotation_count; ++i) {
		annotation_t* annotation = annotation_set->annotations + i;
		if (annotation->selected) {
			has_selected = true;
			break;
		}
	}
	if (has_selected) {
		// rebuild the annotations, leaving out the deleted ones
		size_t copy_size = annotation_set->annotation_count * sizeof(annotation_t);
		annotation_t* temp_copy = (annotation_t*) malloc(copy_size);
		memcpy(temp_copy, annotation_set->annotations, copy_size);

		sb_raw_count(annotation_set->annotations) = 0;
		for (i32 i = 0; i < annotation_set->annotation_count; ++i) {
			annotation_t* annotation = temp_copy + i;
			if (annotation->selected) continue; // skip (delete)
					sb_push(annotation_set->annotations, *annotation);
		}
		annotation_set->annotation_count = sb_count(annotation_set->annotations);
		annotations_modified(annotation_set);
	}


}

i32 select_annotation(scene_t* scene, bool32 additive) {
	annotation_set_t* annotation_set = &scene->annotation_set;
	float distance = 0.0f;

	i32 nearest_annotation_index = find_nearest_annotation(annotation_set, scene->mouse.x, scene->mouse.y, &distance);
	if (nearest_annotation_index >= 0) {
		ASSERT(scene->pixel_width > 0.0f);

		// have to click somewhat close to a coordinate, otherwise treat as unselect
		float pixel_distance = distance / scene->pixel_width;
		if (pixel_distance < 500.0f) {
			annotation_t* annotation = annotation_set->annotations + nearest_annotation_index;
			annotation->selected = !annotation->selected;
		}
	}

	// unselect all annotations (except if Ctrl held down)
	if (!additive) {
		for (i32 i = 0; i < annotation_set->annotation_count; ++i) {
			if (i == nearest_annotation_index) continue; // skip the one we just selected!
			annotation_t* annotation = annotation_set->annotations + i;
			annotation->selected = false;
		}
	}

	return nearest_annotation_index;
}

void draw_annotations_window(app_state_t* app_state) {

	annotation_set_t* annotation_set = &app_state->scene.annotation_set;

	const char** item_previews = (const char**) alloca(annotation_set->group_count * sizeof(char*));
	for (i32 i = 0; i < annotation_set->group_count; ++i) {
		annotation_group_t* group = annotation_set->groups + i;
		item_previews[i] = group->name;
	}

//	static i32 selected_group_index = -1;

	// find group corresponding to the currently selected annotations
	i32 annotation_group_index = -1;
	for (i32 i = 0; i < annotation_set->annotation_count; ++i) {
		annotation_t* annotation = annotation_set->annotations + i;
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
	u32 selectable_flags = 0;
	if (nothing_selected) {
		selectable_flags |= ImGuiSelectableFlags_Disabled;
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

		ImGui::SetNextWindowPos((ImVec2){20, 600}, ImGuiCond_FirstUseEver, (ImVec2){0, 0});
		ImGui::SetNextWindowSize((ImVec2){400, 250}, ImGuiCond_FirstUseEver);


		ImGui::Begin("Annotations", &show_annotations_window, 0);

		ImGui::Text("(work in progress, this window should be redesigned/removed/repurposed...)");

		ImGui::Text("Number of annotations loaded: %d\n", annotation_set->annotation_count);
		ImGui::Checkbox("Show annotations", &annotation_set->enabled);


		if (nothing_selected) {
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}

		if (ImGui::BeginCombo("Assign group", preview, ImGuiComboFlags_HeightLargest)) {
			for (i32 group_index = 0; group_index < annotation_set->group_count; ++group_index) {
				annotation_group_t* group = annotation_set->groups + group_index;

				if (ImGui::Selectable(item_previews[group_index], (annotation_group_index == group_index), selectable_flags, (ImVec2){})) {
					// set group
					for (i32 i = 0; i < annotation_set->annotation_count; ++i) {
						annotation_t* annotation = annotation_set->annotations + i;
						if (annotation->selected) {
							annotation->group_id = group_index;
							annotations_modified(annotation_set);
						}
					}
				}
			}
			ImGui::EndCombo();
		}



		if (ImGui::Button("Assign annotation to group...")) {
			show_annotation_group_assignment_window = true;
		}

		ImGuiColorEditFlags flags = 0;
		float color[3] = {};
		if (annotation_group_index >= 0) {
			annotation_group_t* group = annotation_set->groups + annotation_group_index;
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

		if (nothing_selected) {
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
		}

		ImGui::End();
	}


	if (show_annotation_group_assignment_window) {

		ImGui::SetNextWindowPos((ImVec2){1359,43}, ImGuiCond_FirstUseEver, (ImVec2){0, 0});
		ImGui::SetNextWindowSize((ImVec2){214,343}, ImGuiCond_FirstUseEver);

		ImGui::Begin("Assign to group", &show_annotation_group_assignment_window);

		ImGui::TextUnformatted(preview);


		if (nothing_selected) {
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
//		color.w = 0.6f;
//		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color);
//		color.w = 0.7f;
//		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color);
			if (ImGui::Selectable("", (annotation_group_index == group_index), selectable_flags, ImVec2(0,ImGui::GetFrameHeight()))) {
				// set group
				for (i32 i = 0; i < annotation_set->annotation_count; ++i) {
					annotation_t* annotation = annotation_set->annotations + i;
					if (annotation->selected) {
						annotation->group_id = group_index;
						annotations_modified(annotation_set);
					}
				}
			}
			ImGui::SameLine(0); ImGui::RadioButton(item_previews[group_index], &annotation_group_index, group_index);
			//ImGui::SameLine(300); ImGui::Text(" 2,345 bytes");
			ImGui::PopStyleColor(1);
			ImGui::PopID();

		}

		if (nothing_selected) {
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
		}

		ImGui::End();
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
		printf("annotation_set_attribute(): Color attribute \"%s\" not in form #rrggbb\n", value);
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
		annotation->type = ANNOTATION_UNKNOWN_TYPE;
		if (strcmp(value, "Rectangle") == 0) {
			annotation->type = ANNOTATION_RECTANGLE;
		} else if (strcmp(value, "Polygon") == 0) {
			annotation->type = ANNOTATION_POLYGON;
		}
	}
}

void coordinate_set_attribute(coordinate_t* coordinate, const char* attr, const char* value) {
	if (strcmp(attr, "Order") == 0) {
		coordinate->order = atoi(value);
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
	if (annotation_set->annotations) {
		sb_free(annotation_set->annotations);
	}
	if (annotation_set->coordinates) {
		sb_free(annotation_set->coordinates);
	}
	if (annotation_set->groups) {
		sb_free(annotation_set->groups);
	}
	if (annotation_set->filename) {
		free(annotation_set->filename);
	}
	memset(annotation_set, 0, sizeof(*annotation_set));

	// reserve annotation group 0 for the "None" category
	add_annotation_group(annotation_set, "None");
}


bool32 load_asap_xml_annotations(app_state_t* app_state, const char* filename) {
	annotation_set_t* annotation_set = &app_state->scene.annotation_set;
	unload_and_reinit_annotations(annotation_set);

	asap_xml_parse_state_t* parse_state = &global_parse_state;
	memset(parse_state, 0, sizeof(*parse_state));

	file_mem_t* file = platform_read_entire_file(filename);
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
//						printf("element start: %s\n", x->elem);
						contentcur = contentbuf;

						parse_state->element_type = ASAP_XML_ELEMENT_NONE;
						if (strcmp(x->elem, "Annotation") == 0) {
							annotation_t new_annotation = (annotation_t){};
							sb_push(annotation_set->annotations, new_annotation);
							++annotation_set->annotation_count;
							parse_state->element_type = ASAP_XML_ELEMENT_ANNOTATION;
						} else if (strcmp(x->elem, "Coordinate") == 0) {
							coordinate_t new_coordinate = (coordinate_t){};
							sb_push(annotation_set->coordinates, new_coordinate);
							parse_state->element_type = ASAP_XML_ELEMENT_COORDINATE;

							annotation_t* current_annotation = &sb_last(annotation_set->annotations);
							if (!current_annotation->has_coordinates) {
								current_annotation->first_coordinate = annotation_set->coordinate_count;
								current_annotation->has_coordinates = true;
							}
							current_annotation->coordinate_count++;
							++annotation_set->coordinate_count;
						} else if (strcmp(x->elem, "Group") == 0) {
							parse_state->element_type = ASAP_XML_ELEMENT_GROUP;
							// reset the state (start parsing a new group)
							parse_state->current_group = (annotation_group_t){};
							parse_state->current_group.is_explicitly_defined = true; // (because this group has an XML tag)
						}
					} break;
					case YXML_CONTENT: {
						// element content
//						printf("   element content: %s\n", x->elem);
						if (!contentcur) break;
						char* tmp = x->data;
						while (*tmp && contentbuf < contentbuf_end) {
							*(contentcur++) = *(tmp++);
						}
						if (contentcur == contentbuf_end) {
							// too long content
							printf("load_asap_xml_annotations(): encountered a too long XML element content\n");
							goto failed;
						}
						*contentcur = '\0';
					} break;
					case YXML_ELEMEND: {
						// end of an element: '.. />' or '</Tag>'
//						printf("element end: %s\n", x->elem);
						if (contentcur) {
							// NOTE: usually only whitespace (newlines and such)
//							printf("elem content: %s\n", contentbuf);
						}
						if (strcmp(x->elem, "Group") == 0) {
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
//						printf("attr start: %s\n", x->attr);
						attrcur = attrbuf;
					} break;
					case YXML_ATTRVAL: {
						// attribute value
//						printf("   attr val: %s\n", x->attr);
						if (!attrcur) break;
						char* tmp = x->data;
						while (*tmp && attrbuf < attrbuf_end) {
							*(attrcur++) = *(tmp++);
						}
						if (attrcur == attrbuf_end) {
							// too long attribute
							printf("load_asap_xml_annotations(): encountered a too long XML attribute\n");
							goto failed;
						}
						*attrcur = '\0';
					} break;
					case YXML_ATTREND: {
						// end of attribute '.."'
						if (attrcur) {
//							printf("attr %s = %s\n", x->attr, attrbuf);
							if (parse_state->element_type == ASAP_XML_ELEMENT_ANNOTATION) {
								annotation_set_attribute(annotation_set, &sb_last(annotation_set->annotations), x->attr, attrbuf);
							} else if (parse_state->element_type == ASAP_XML_ELEMENT_COORDINATE) {
								coordinate_set_attribute(&sb_last(annotation_set->coordinates), x->attr, attrbuf);
							} else if (parse_state->element_type == ASAP_XML_ELEMENT_GROUP) {
								group_set_attribute(&parse_state->current_group, x->attr, attrbuf);
							}
						}
					} break;
					case YXML_PISTART:
					case YXML_PICONTENT:
					case YXML_PIEND:
						break; // processing instructions (uninteresting, skip)
					default: {
						printf("yxml_parse(): unrecognized token (%d)\n", r);
						goto failed;
					}
				}
			}
		}
	}

	annotation_set->filename = strdup(filename);
	success = true;
	annotation_set->enabled = true;
	float seconds_elapsed = get_seconds_elapsed(start, get_clock());
	printf("Loaded annotations in %g seconds.\n", seconds_elapsed);

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

		for (i32 annotation_index = 0; annotation_index < annotation_set->annotation_count; ++annotation_index) {
			annotation_t* annotation = annotation_set->annotations + annotation_index;
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

