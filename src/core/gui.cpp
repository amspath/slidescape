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

#include "common.h"
#include "platform.h"

#include OPENGL_H

#include "imgui.h"
#include "imgui_internal.h"

#include "openslide_api.h"
#include "viewer.h"
#include "remote.h"
#include "tiff_write.h"
#include "image.h"
#include "image_registration.h"


#define GUI_IMPL
#include "gui.h"
#include "stringutils.h"

static void*   imgui_malloc_wrapper(size_t size, void* user_data)    { IM_UNUSED(user_data); return ltmalloc(size); }
static void    imgui_free_wrapper(void* ptr, void* user_data)        { IM_UNUSED(user_data); ltfree(ptr); }

static char imgui_ini_filename[512];

void imgui_create_context() {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::SetAllocatorFunctions(imgui_malloc_wrapper, imgui_free_wrapper);
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	if (global_settings_dir) {
		snprintf(imgui_ini_filename, sizeof(imgui_ini_filename), "%s" PATH_SEP "%s", global_settings_dir, "imgui.ini");
		io.IniFilename = imgui_ini_filename;
	}
}

void gui_make_next_window_appear_in_center_of_screen() {
	ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

void menu_close_file(app_state_t* app_state) {
	unload_all_images(app_state);
	reset_global_caselist(app_state);
	unload_and_reinit_annotations(&app_state->scene.annotation_set);
}

void gui_draw_polygon_outline(v2f* points, i32 count, rgba_t rgba, bool closed, float thickness) {
	if (count < 2) return;
	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	u32 color = *(u32*)(&rgba);
	// Workaround for problem with acute angles
	// (lines are being drawn incorrectly with not enough thickness if the angles are too sharp)
	// Solution until the problem is fixed in ImGui: split into segments based on whether the angle is acute or not
	// https://github.com/ocornut/imgui/issues/3366#issuecomment-664779883
	i32 i = 0;
	bool has_at_least_one_split = false;
	while (i + 1 < count) {
		i32 nlin = 2;
		while (i + nlin < count) {
			v2f v0 = points[i+nlin-2];
			v2f v1 = points[i+nlin-1];
			v2f v2 = points[i+nlin];
			v2f s0 = v2f_subtract(v1, v0);
			v2f s1 = v2f_subtract(v2, v1);
			float dotprod = v2f_dot(s0, s1);
			if (dotprod < 0) {
				has_at_least_one_split = true;
				break;
			}
			++nlin;
		}

		// If it's the last segment, we may need to 'close' the polygon (but this only works if there are no splits)
		ImDrawFlags flags = 0;
		if (i + nlin == count && closed && !has_at_least_one_split) {
			flags = ImDrawFlags_Closed;
		}

		draw_list->AddPolyline((ImVec2*)(points + i), nlin, color, flags, thickness);
		i += nlin-1;
	}

	// Close the polygon using a manually added line in case of a split due to acute angles
	if (closed && has_at_least_one_split) {
		draw_list->AddLine(points[0], points[count-1], color, thickness);
	}
}

void gui_draw_polygon_outline_in_scene(v2f* points, i32 count, rgba_t color, bool closed, float thickness, scene_t* scene) {
	for (i32 i = 0; i < count; ++i) {
		points[i] = world_pos_to_screen_pos(points[i], scene->camera_bounds.min, scene->zoom.screen_point_width);
	}
	gui_draw_polygon_outline(points, 4, color, closed, thickness);
}

void gui_draw_bounds_in_scene(bounds2f bounds, rgba_t color, float thickness, scene_t* scene) {
	v2f points[4];
	points[0] = V2F(bounds.left, bounds.top);
	points[1] = V2F(bounds.left, bounds.bottom);
	points[2] = V2F(bounds.right, bounds.bottom);
	points[3] = V2F(bounds.right, bounds.top);
	gui_draw_polygon_outline_in_scene(points, 4, color, true, thickness, scene);
}

bool enable_load_debug_coco_file;
bool enable_load_debug_isyntax_file;
const char* coco_test_filename = "coco_test_in.json";
const char* isyntax_test_filename = "1.isyntax";

static void check_presence_of_debug_test_files() {
	static bool checked;
	if (!checked) {
		enable_load_debug_coco_file = file_exists(coco_test_filename);
		enable_load_debug_isyntax_file = file_exists(isyntax_test_filename);
		checked = true;
	}
}

bool gui_draw_selected_annotation_submenu_section(app_state_t* app_state, scene_t* scene, annotation_set_t* annotation_set) {
	// Change annotation type
	bool proceed = annotation_set->selection_count > 0;
	if (proceed) {
		if (annotation_set->selection_count == 1) {
			annotation_t* selected_annotation = annotation_set->selected_annotations[0];
			if (selected_annotation->type == ANNOTATION_POLYGON && selected_annotation->coordinate_count == 4) {
				if (ImGui::BeginMenu("Set annotation type")) {
					if (ImGui::MenuItem("Freeform", NULL, true)) {}
					if (ImGui::MenuItem("Rectangle", NULL, false)) {
						selected_annotation->type = ANNOTATION_RECTANGLE;
						annotation_set_rectangle_coordinates_to_bounding_box(annotation_set, selected_annotation);
					}
					ImGui::EndMenu();
				}
			} else if (selected_annotation->type == ANNOTATION_RECTANGLE) {
				if (ImGui::BeginMenu("Set annotation type")) {
					if (ImGui::MenuItem("Freeform", NULL, false)) {
						selected_annotation->type = ANNOTATION_POLYGON;
						notify_annotation_set_modified(annotation_set);
					}
					if (ImGui::MenuItem("Rectangle", NULL, true)) {}
					ImGui::EndMenu();
				}
			}
		}
//				if (ImGui::MenuItem("Assign group/feature...", NULL)) {
//					show_annotation_group_assignment_window = true;
//				}

		const char* delete_text = annotation_set->selection_count > 1 ? "Delete annotations" : "Delete annotation";
		if (ImGui::MenuItem(delete_text, "Del")) {
			if (dont_ask_to_delete_annotations) {
				delete_selected_annotations(app_state, annotation_set);
			} else {
				show_delete_annotation_prompt = true;
			}
		};
	}

	// Option for setting the selection box around the selected annotation(s)
	if (annotation_set->selection_count >= 1) {
		if (ImGui::MenuItem("Set export region")) {
			set_region_encompassing_selected_annotations(annotation_set, scene);
		}

	}

	return proceed;
}

void gui_draw_insert_annotation_submenu(app_state_t* app_state) {
	if (ImGui::BeginMenu("New annotation", arrlen(app_state->loaded_images) > 0)) {
		if (ImGui::MenuItem("Point", "Q")) {
			viewer_switch_tool(app_state, TOOL_CREATE_POINT);
		}
		if (ImGui::MenuItem("Line", "M")) {
			viewer_switch_tool(app_state, TOOL_CREATE_LINE);
		}
		if (ImGui::MenuItem("Freeform", "F")) {
			viewer_switch_tool(app_state, TOOL_CREATE_FREEFORM);
		}
//		if (ImGui::MenuItem("Ellipse", "E")) {
//			viewer_switch_tool(app_state, TOOL_CREATE_ELLIPSE);
//		}
		if (ImGui::MenuItem("Rectangle", "R")) {
			viewer_switch_tool(app_state, TOOL_CREATE_RECTANGLE);
		}
//		if (ImGui::MenuItem("Text", "T")) {
//			viewer_switch_tool(app_state, TOOL_CREATE_TEXT);
//		}
		ImGui::EndMenu();
	}
}

static void gui_draw_main_menu_bar(app_state_t* app_state) {
	check_presence_of_debug_test_files();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	bool ret = ImGui::BeginMainMenuBar();
	ImGui::PopStyleVar(1);
	if (ret) {
		scene_t* scene = &app_state->scene;

		static struct {
			bool open_file;
			bool close;
			bool save;
			bool open_remote;
			bool exit_program;
			bool new_dataset_asap_xml;
			bool new_dataset_coco;
			bool new_dataset_geojson;
			bool save_annotations;
			bool select_region_create_box;
			bool select_region_encompass_annotations;
			bool select_region_whole_slide;
			bool deselect;
			bool crop_region;
			bool export_region;
			bool load_coco_test_file;
			bool load_isyntax_test_file;
			bool reset_zoom;
		} menu_items_clicked;
		memset(&menu_items_clicked, 0, sizeof(menu_items_clicked));

		bool prev_is_vsync_enabled = is_vsync_enabled;
		bool prev_fullscreen = is_fullscreen;
		bool has_image_loaded = (arrlen(app_state->loaded_images) > 0);
		bool can_save = scene->annotation_set.modified;

		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open...", "Ctrl+O", &menu_items_clicked.open_file)) {}
			if (ImGui::MenuItem("Close", "Ctrl+W", &menu_items_clicked.close)) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Save", "Ctrl+S", &menu_items_clicked.save, can_save)) {}
			ImGui::Separator();

			if (ImGui::BeginMenu("Export", scene->can_export_region)) {
				if (ImGui::MenuItem("Export region...", NULL, &menu_items_clicked.export_region, scene->can_export_region)) {}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Alt+F4", &menu_items_clicked.exit_program)) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::BeginMenu("Select export region", has_image_loaded)) {
				if (ImGui::MenuItem("Draw selection box...", NULL, &menu_items_clicked.select_region_create_box)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Set region to whole slide", NULL, &menu_items_clicked.select_region_whole_slide, has_image_loaded)) {}
				bool encompass_option_enabled = scene->annotation_set.selection_count > 0;
				if (scene->annotation_set.selection_count == 1) {
					if (ImGui::MenuItem("Set region to selected annotation", NULL, &menu_items_clicked.select_region_encompass_annotations, encompass_option_enabled)) {}
				} else {
					if (ImGui::MenuItem("Set region to selected annotations", NULL, &menu_items_clicked.select_region_encompass_annotations, encompass_option_enabled)) {}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Deselect region", NULL, &menu_items_clicked.deselect, app_state->scene.has_selection_box)) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Restrict view to region", NULL, app_state->scene.is_cropped, app_state->scene.has_selection_box || app_state->scene.is_cropped)) {
				menu_items_clicked.crop_region = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("General options...", NULL, &show_general_options_window)) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Annotation")) {
			gui_draw_insert_annotation_submenu(app_state);
			ImGui::Separator();
			if (ImGui::MenuItem("Annotations...", NULL, &show_annotations_window)) {}
			if (ImGui::MenuItem("Assign group/feature...", NULL, &show_annotation_group_assignment_window)) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Autosave", NULL, &app_state->enable_autosave)) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			prev_fullscreen = is_fullscreen = check_fullscreen(app_state->main_window); // double-check just in case...
			if (ImGui::MenuItem("Reset zoom", NULL, &menu_items_clicked.reset_zoom)) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Fullscreen", "F11", &is_fullscreen)) {}
			if (ImGui::MenuItem("Image options...", NULL, &show_image_options_window)) {}
			if (ImGui::MenuItem("Layers...", "L", &show_layers_window)) {}
			ImGui::Separator();
			bool* show_scale_bar = has_image_loaded ? &scene->scale_bar.enabled : NULL;
			if (ImGui::MenuItem("Show scale bar", "Ctrl+B", show_scale_bar, (show_scale_bar != NULL))) {}
			bool* show_grid = has_image_loaded ? &scene->enable_grid : NULL;
			if (ImGui::MenuItem("Show grid", "Ctrl+G", show_grid, (show_grid != NULL))) {}
			ImGui::Separator();

			if (ImGui::BeginMenu("Debug")) {
				if (ImGui::MenuItem("Show console", "F3 or `", &show_console_window)) {}
				if (ImGui::MenuItem("Show demo window", "F1", &show_demo_window)) {}
				if (ImGui::MenuItem("Show debugging window", "Ctrl+F1", &show_debugging_window)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Open remote...", NULL, &menu_items_clicked.open_remote)) {}
				ImGui::Separator();
//				if (ImGui::MenuItem("Save XML annotations", NULL, &menu_items_clicked.save_annotations)) {}
				if (ImGui::MenuItem("Show menu bar", "Alt+F12", &show_menu_bar)) {}
				if (ImGui::MenuItem("Load next as overlay", "F6", &load_next_image_as_overlay)) {}
				if (enable_load_debug_coco_file) {
					if (ImGui::MenuItem("Load COCO test file", NULL, &menu_items_clicked.load_coco_test_file)) {}
				}
				if (enable_load_debug_isyntax_file) {
					if (ImGui::MenuItem("Load iSyntax test file", NULL, &menu_items_clicked.load_isyntax_test_file, enable_load_debug_isyntax_file)) {}
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Show case list", NULL, &show_slide_list_window)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Show mouse position", NULL, &show_mouse_pos_overlay)) {}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help")) {
			if (ImGui::MenuItem("About...", NULL, &show_about_window)) {}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();


		if (menu_items_clicked.exit_program) {
			is_program_running = false;
		} else if (menu_items_clicked.open_file) {
			u32 filetype_hint = load_next_image_as_overlay ? FILETYPE_HINT_OVERLAY : 0;
			open_file_dialog(app_state, filetype_hint);
		} else if (menu_items_clicked.close) {
			menu_close_file(app_state);
		} else if (menu_items_clicked.save) {
			save_annotations(app_state, &app_state->scene.annotation_set, true);
		}else if (menu_items_clicked.open_remote) {
			show_open_remote_window = true;
		} else if (prev_fullscreen != is_fullscreen) {
			bool currently_fullscreen = check_fullscreen(app_state->main_window);
			if (currently_fullscreen != is_fullscreen) {
				toggle_fullscreen(app_state->main_window);
			}
		} else if(menu_items_clicked.save_annotations) {
			save_asap_xml_annotations(&app_state->scene.annotation_set, "test_out.xml");
		} else if (menu_items_clicked.select_region_create_box) {
			app_state->mouse_mode = MODE_CREATE_SELECTION_BOX;
		} else if (menu_items_clicked.select_region_encompass_annotations) {
			set_region_encompassing_selected_annotations(&scene->annotation_set, scene);
		} else if (menu_items_clicked.select_region_whole_slide) {
			if (arrlen(app_state->loaded_images) > 0) {
				image_t* image = app_state->loaded_images + 0;
				// TODO: what to do if there are multiple layers?
				set_region_for_whole_slide(scene, image);
				scene->need_zoom_reset = true;
			}
		} else if (menu_items_clicked.deselect) {
			scene->has_selection_box = false;
		} else if (menu_items_clicked.crop_region) {
			if (!scene->is_cropped) {
				rect2f final_crop_rect = rect2f_recanonicalize(&scene->selection_box);
				bounds2f bounds = rect2f_to_bounds(final_crop_rect);
				scene->crop_bounds = bounds;
				scene->is_cropped = true;
				scene->has_selection_box = false;
			} else {
				scene->is_cropped = false;
				scene->has_selection_box = false;
			}
		} else if (prev_is_vsync_enabled != is_vsync_enabled) {
			set_swap_interval(is_vsync_enabled ? 1 : 0);
		} else if (menu_items_clicked.export_region) {

			if (scene->can_export_region) {
				show_export_region_dialog = true;
			} else {
				ASSERT(!"Trying to export a region without a selected region");
			}


		} else if (menu_items_clicked.load_coco_test_file) {
			coco_t coco = {};
			if (load_coco_from_file(&coco, coco_test_filename)) {
				save_coco(&coco);
			}
		} else if (menu_items_clicked.load_isyntax_test_file) {
			isyntax_t isyntax = {};
			if (isyntax_open(&isyntax, isyntax_test_filename)) {
			}
		} else if (menu_items_clicked.reset_zoom) {
			scene->need_zoom_reset = true;
		}
	}

}

// TODO
typedef struct image_layer_t {
	i32 index;
	char name[512];
	v2f origin_offset; // TODO: transfer from image->origin_offset
	image_backend_enum backend;
	bool is_loaded;
	bool enabled;
	bool is_mask;
	bool uses_lut;
} image_layer_t;

typedef struct annotation_layer_t {
	i32 index;
	char name[512];
} annotation_layer_t;

void draw_layers_window(app_state_t* app_state) {
	if (!show_layers_window) return;

	ImGui::SetNextWindowPos(ImVec2(20, 50), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(460,541), ImGuiCond_FirstUseEver);

	ImGui::Begin("Layers", &show_layers_window);

	const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;

	i32 image_count = arrlen(app_state->loaded_images);
	if (layers_window_selected_image_index >= image_count) layers_window_selected_image_index = 0;

	static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;

	if (ImGui::BeginTable("layers_table", 4, flags)) {
		// The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 20.0f);
		ImGui::TableSetupColumn("##layers_table_checkbox", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 25.0f);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 18.0f);
		ImGui::TableHeadersRow();

		for (i32 image_index = 0; image_index < image_count; ++image_index) {
			image_t* image = app_state->loaded_images + image_index;
			ImGui::TableNextRow();

			// Display index
			ImGui::TableNextColumn();
			ImGui::Text("%d", image_index);

			// Display 'enabled' checkbox
			ImGui::TableNextColumn();
			char checkbox_label[32];
			snprintf(checkbox_label, sizeof(checkbox_label), "##layers_checkbox_%d", image_index);
			ImGui::Checkbox(checkbox_label, &image->is_enabled);

			// Display layer name
			ImGui::TableNextColumn();
			char name[256];
			snprintf(name, sizeof(name)-1, "%s", image->name);
			bool selected = ImGui::Selectable(name);

			// Displayer layer type
			ImGui::TableNextColumn();
			const char* type = get_image_descriptive_type_name(image);
			if (selected) layers_window_selected_image_index = image_index;
			ImGui::TextUnformatted(type);
		}

		ImGui::EndTable();
	}

	bool disable_gui = image_count == 0;
	if (disable_gui) {
		ImGui::BeginDisabled();
	}
	if (ImGui::ButtonEx("Load paired image...", ImVec2(0,0))) {
		open_file_dialog(app_state, FILETYPE_HINT_OVERLAY);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Load next image as overlay (F6)", &load_next_image_as_overlay);

	if (disable_gui) {
		ImGui::EndDisabled();
	}

	ImGui::NewLine();
	if (layers_window_selected_image_index < image_count) {
		image_t* image = app_state->loaded_images + layers_window_selected_image_index;
		ImGui::Text("Adjust position offset for layer %d:", layers_window_selected_image_index);
		ASSERT(image->mpp_x != 0.0f);
		ASSERT(image->mpp_y != 0.0f);
		i32 px_x = roundf(image->origin_offset.x / image->mpp_x);
		i32 px_y = roundf(image->origin_offset.y / image->mpp_y);
		if (ImGui::DragInt("Offset X", &px_x, 1.0f, 0.0f, 0.0f, "%d px")) {
			image->origin_offset.x = ((float)px_x * image->mpp_x);
		}
		if (ImGui::DragInt("Offset Y", &px_y, 1.0f, 0.0f, 0.0f, "%d px")) {
			image->origin_offset.y = ((float)px_y * image->mpp_y);
		}
		if (ImGui::Button("Reset")) {
			image->origin_offset.x = 0.0f;
			image->origin_offset.y = 0.0f;
		}
        if (layers_window_selected_image_index > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Re-register")) {
                image_transform_t transform = do_image_registration(app_state->loaded_images + 0, image, 3);
                if (transform.is_valid) {
                    // apply translation
                    if (transform.translate.x != 0.0f || transform.translate.y != 0.0f) {
                        image->origin_offset = transform.translate;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Re-register (local)")) {
                image_transform_t transform = do_local_image_registration(app_state->loaded_images + 0, image, app_state->scene.camera, app_state->scene.zoom.level, 1024);
                if (transform.is_valid) {
                    // apply differential translation
                    if (transform.translate.x != 0.0f || transform.translate.y != 0.0f) {
                        image->origin_offset = v2f_add(image->origin_offset, transform.translate);
                    }
                }
            }
        }
//		ImGui::DragFloat("Offset Y", &image->origin_offset.y, image->mpp_y, 0.0f, 0.0f, "%g px");
	}
	ImGui::NewLine();

    bool disable_layer_transition_control = (image_count < 2);
    if (disable_layer_transition_control) {
        ImGui::BeginDisabled();
    }
	ImGui::Text("Currently displayed layer: %d.\nPress Space or F5 to toggle layers.", app_state->scene.active_layer);

	ImGui::SliderFloat("Layer transition", &target_layer_time, 0.0f, 1.0f);
    if (disable_layer_transition_control) {
        ImGui::EndDisabled();
    }

	ImGui::End();
}


void draw_export_region_dialog(app_state_t* app_state) {
	if (show_export_region_dialog) {
		ImGui::OpenPopup("Export region");
		show_export_region_dialog = false;
	}
	gui_make_next_window_appear_in_center_of_screen();
	ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f), ImGuiCond_Appearing);

	if (ImGui::BeginPopupModal("Export region", NULL, 0/*ImGuiWindowFlags_AlwaysAutoResize*/)) {
		scene_t* scene = &app_state->scene;
		image_t* image = app_state->loaded_images + 0;

		bool display_export_annotations_checkbox = (scene->annotation_set.active_annotation_count > 0);
		i32 lines_at_bottom = 2;
//		if (display_export_annotations_checkbox) {
//			++lines_at_bottom;
//		}

		ImGui::BeginGroup();
		ImGui::BeginChild("item view", ImVec2(0, -lines_at_bottom * ImGui::GetFrameHeightWithSpacing())); // Leave room for 2 lines below us

		static bool also_export_annotations = true;
		static bool allow_coordinates_outside_region = false;

		if (scene->can_export_region) {
			if (image->mpp_x > 0.0f && image->mpp_y > 0.0f) {
				bounds2i pixel_bounds = scene->selection_pixel_bounds;

				if (ImGui::TreeNodeEx("Adjust region", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
					rect2i export_rect = {pixel_bounds.left, pixel_bounds.top, pixel_bounds.right - pixel_bounds.left, pixel_bounds.bottom - pixel_bounds.top};
					bool changed = false;
					changed |= ImGui::InputInt("Offset X##export_pixel_bounds", &export_rect.x);
					changed |= ImGui::InputInt("Offset Y##export_pixel_bounds", &export_rect.y);
					changed |= ImGui::InputInt("Width##export_pixel_bounds", &export_rect.w);
					changed |= ImGui::InputInt("Height##export_pixel_bounds", &export_rect.h);

					if (changed) {
						scene->selection_box = pixel_rect_to_world_rect(export_rect, image->mpp_x, image->mpp_y);
						scene->selection_pixel_bounds = BOUNDS2I(export_rect.x, export_rect.y, export_rect.x + export_rect.w, export_rect.y + export_rect.h);
//						pixel_bounds = scene->selection_pixel_bounds;
					}
				}
				ImGui::NewLine();

				if (display_export_annotations_checkbox) {
					ImGui::Checkbox("Also export annotations", &also_export_annotations);
					if (also_export_annotations) {
						if (ImGui::TreeNodeEx("Annotation export options", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {


							bool dummy_checkbox_value = false;
							bool* checkbox_value_ptr = &allow_coordinates_outside_region;
							if (!also_export_annotations) {
								ImGui::BeginDisabled();
								checkbox_value_ptr = &dummy_checkbox_value;
							}
							ImGui::Checkbox("Allow coordinates to extend outside selected region", checkbox_value_ptr);

							if (!also_export_annotations) {
								ImGui::EndDisabled();
							}
						}
					}
					ImGui::NewLine();
				}



//				ImGui::Text("Selected region (pixel coordinates):\nx=%d\ny=%d\nwidth=%d\nheight=%d",
//							 pixel_bounds.left, pixel_bounds.top,
//							 pixel_bounds.right - pixel_bounds.left, pixel_bounds.bottom - pixel_bounds.top);

				const char* export_formats[] = {"Tiled TIFF", };//"JPEG", "PNG"}; TODO: implement JPEG and PNG export
				if (ImGui::BeginCombo("Export format", export_formats[desired_region_export_format])) // The second parameter is the label previewed before opening the combo.
				{
					for (i32 i = 0; i < COUNT(export_formats); ++i) {
						if (ImGui::Selectable(export_formats[i], desired_region_export_format == i)) {
							desired_region_export_format = i;
						}
					}
					ImGui::EndCombo();
				}

				if (desired_region_export_format == 0) {
					if (ImGui::TreeNodeEx("Encoding options", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_NoAutoOpenOnLog)) {
						ImGui::SliderInt("JPEG encoding quality", &tiff_export_jpeg_quality, 0, 100);
						bool prefer_rgb = tiff_export_desired_color_space == TIFF_PHOTOMETRIC_RGB;
						if (ImGui::Checkbox("Use RGB encoding (instead of YCbCr)", &prefer_rgb)) {
							tiff_export_desired_color_space = prefer_rgb ? TIFF_PHOTOMETRIC_RGB : TIFF_PHOTOMETRIC_YCBCR;
						}
					}

				}
			}

		}
		ImGui::EndChild(); // end of top area -- now start drawing bottom area


		const char* name_hint = "output";
		if (arrlen(app_state->loaded_images) > 0) {
			for (i32 i = 0; i < arrlen(app_state->loaded_images); ++i) {
				image_t* image = app_state->loaded_images + i;
				if (image->name[0] != '\0') {
					size_t buffer_size = sizeof(image->name);
					char* new_name_hint = (char*)alloca(buffer_size);
					strncpy(new_name_hint, image->name, buffer_size);
					// Strip filename extension
					size_t len = strlen(new_name_hint);
					for (i32 pos = len-1; pos >= 1; --pos) {
						if (new_name_hint[pos] == '.') {
							new_name_hint[pos] = '\0';
							// add '_region'
							strncpy(new_name_hint + pos, "_region", buffer_size - pos);
							name_hint = new_name_hint;
							break;
						}
					}
				}
			}
		}

		const char* filename_extension_hint = "";
		if (desired_region_export_format == 0) {
#if APPLE
			// macOS does not seem to like tile TIFF files in the Finder (will sometimes stop responding,
			// at least on my system). So choose the .ptif file extension by default as an alternative.
			filename_extension_hint = ".ptif";
#else
			filename_extension_hint = ".tiff";
#endif
		} else if (desired_region_export_format == 1) {
			filename_extension_hint = ".jpeg";
		} else if (desired_region_export_format == 2) {
			filename_extension_hint = ".png";
		}

		char filename_hint[512];
		snprintf(filename_hint, sizeof(filename_hint)-1, "%s%s", name_hint, filename_extension_hint);

		char* filename_buffer = global_export_save_as_filename;
		size_t filename_buffer_size = sizeof(global_export_save_as_filename);

		ImGui::InputTextWithHint("##export_region_output_filename", filename_hint, filename_buffer, filename_buffer_size);
		ImGui::SameLine();
		if (save_file_dialog_open || ImGui::Button("Browse...")) {
			if (save_file_dialog(app_state, filename_buffer, filename_buffer_size, "BigTIFF (*.tiff)\0*.tiff;*.tif;*.ptif\0All\0*.*\0Text\0*.TXT\0", filename_hint)) {
				size_t filename_len = strlen(filename_buffer);
				if (filename_len > 0) {
					const char* extension = get_file_extension(filename_buffer);
					if (!(strcasecmp(extension, "tiff") == 0 || strcasecmp(extension, "tif") == 0 || strcasecmp(extension, "ptif") == 0)) {
						// if extension incorrect, append it at the end
						i64 remaining_len = filename_buffer_size - filename_len;
						strncpy(filename_buffer + filename_len, ".tiff", remaining_len-1);
					}
				} else {
//					console_print_verbose("Export region: save file dialog returned 0\n");
				}
			}
		}

		static bool is_overwrite_confirm_dialog_open;
		if (ImGui::Button("Export", ImVec2(120, 0)) || is_overwrite_confirm_dialog_open) {
			if (filename_buffer[0] == '\0') {
				snprintf(filename_buffer, filename_buffer_size-1, "%s%s", get_active_directory(app_state), filename_hint);
			}
			bool proceed_with_export = true;

			static bool need_overwrite_confirm_dialog;
			if (!is_overwrite_confirm_dialog_open) {
				if (file_exists(filename_buffer)) {
					need_overwrite_confirm_dialog = true;
				}
			}
			if (need_overwrite_confirm_dialog) {
				ImGui::OpenPopup("Overwrite existing file?##export_region");
				is_overwrite_confirm_dialog_open = true;
				need_overwrite_confirm_dialog = false;
			}
			if (is_overwrite_confirm_dialog_open) {
				proceed_with_export = false;
				if (ImGui::BeginPopupModal("Overwrite existing file?##export_region", NULL, ImGuiWindowFlags_AlwaysAutoResize))
				{
					ImGui::Text("Overwrite existing file '%s'?\n\n", filename_buffer);

					if (ImGui::Button("Overwrite", ImVec2(120, 0))) {
						is_overwrite_confirm_dialog_open = false;
						proceed_with_export = true;
						ImGui::CloseCurrentPopup();
					}
					ImGui::SetItemDefaultFocus();
					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(120, 0))) {
						is_overwrite_confirm_dialog_open = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}

			if (proceed_with_export) {
				switch(image->backend) {
					case IMAGE_BACKEND_TIFF: {
						u32 export_flags = 0;
						if (display_export_annotations_checkbox) {
							if (also_export_annotations) {
								export_flags |= EXPORT_FLAGS_ALSO_EXPORT_ANNOTATIONS;
							}
							if (!allow_coordinates_outside_region) {
								export_flags |= EXPORT_FLAGS_PUSH_ANNOTATION_COORDINATES_INWARD;
							}
						}
						begin_export_cropped_bigtiff(app_state, image, &image->tiff, scene->crop_bounds, scene->selection_pixel_bounds,
						                             filename_buffer, 512,
						                             tiff_export_desired_color_space, tiff_export_jpeg_quality, export_flags);
						gui_add_modal_progress_bar_popup("Exporting region...", &global_tiff_export_progress, false);
					} break;
					default: {
						gui_add_modal_message_popup("Error##draw_export_region_dialog",
						                            "This image backend is currently not supported for exporting a region.\n");
						console_print_error("Error: image backend not supported for exporting a region\n");
					}
				}
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		ImGui::SetItemDefaultFocus();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}


		ImGui::EndGroup();

		ImGui::EndPopup();
	}
}

static void draw_mouse_pos_overlay(app_state_t* app_state, bool* p_open) {
	scene_t* scene = &app_state->scene;

	static int corner = 0;
	ImGuiIO& io = ImGui::GetIO();
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if (corner != -1)
	{
		const float PAD = 10.0f;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
		ImVec2 work_size = viewport->WorkSize;
		ImVec2 window_pos, window_pos_pivot;
		window_pos.x = (corner & 1) ? (work_pos.x + work_size.x - PAD) : (work_pos.x + PAD);
		window_pos.y = (corner & 2) ? (work_pos.y + work_size.y - PAD) : (work_pos.y + PAD);
		window_pos_pivot.x = (corner & 1) ? 1.0f : 0.0f;
		window_pos_pivot.y = (corner & 2) ? 1.0f : 0.0f;
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		window_flags |= ImGuiWindowFlags_NoMove;
	}
	ImGui::SetNextWindowBgAlpha(0.65f); // Transparent background
	if (ImGui::Begin("Mouse pos overlay", p_open, window_flags))
	{
		if (ImGui::IsMousePosValid()) {
			ImGui::Text("Mouse Position: (%.1f,%.1f)", io.MousePos.x, io.MousePos.y);
		} else {
			ImGui::Text("Mouse Position: <invalid>");
		}
		// TODO: how to check if a scene is enabled?
		if (arrlen(app_state->loaded_images) > 0) {
			ImGui::Text("Scene Position: (%.1f,%.1f)", scene->mouse.x, scene->mouse.y);
		} else {
			ImGui::Text("Scene Position: <invalid>");
		}

		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::MenuItem("Custom",       NULL, corner == -1)) corner = -1;
			if (ImGui::MenuItem("Top-left",     NULL, corner == 0)) corner = 0;
			if (ImGui::MenuItem("Top-right",    NULL, corner == 1)) corner = 1;
			if (ImGui::MenuItem("Bottom-left",  NULL, corner == 2)) corner = 2;
			if (ImGui::MenuItem("Bottom-right", NULL, corner == 3)) corner = 3;
			if (p_open && ImGui::MenuItem("Close")) *p_open = false;
			ImGui::EndPopup();
		}

		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
		v2f transformed_pos = world_pos_to_screen_pos(scene->mouse, scene->camera_bounds.min, scene->zoom.screen_point_width);
		draw_list->AddCircle(transformed_pos, 20.0f, ImGui::GetColorU32(IM_COL32(70, 70, 70, 255)), 24, 2.0f);

	}
	ImGui::End();
}

void save_changes_modal(app_state_t* app_state, annotation_set_t* annotation_set) {
	if (show_save_quit_prompt) {
		ImGui::OpenPopup("Save changes?");
		show_save_quit_prompt = false;
	}
	gui_make_next_window_appear_in_center_of_screen();
	if (ImGui::BeginPopupModal("Save changes?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("There are unsaved changes to the currently loaded annotations.\nProceed?\n\n");
		ImGui::Separator();

		//static int unused_i = 0;
		//ImGui::Combo("Combo", &unused_i, "Delete\0Delete harder\0");

//		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
//		ImGui::Checkbox("Don't ask me next time", &dont_ask_to_delete_annotations);
//		ImGui::PopStyleVar();

		if (ImGui::Button("Save", ImVec2(120, 0)) || was_key_pressed(app_state->input, KEY_Return)) {
			save_annotations(app_state, annotation_set, true);
			show_save_quit_prompt = false;
			is_program_running = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Don't save", ImVec2(120, 0))) {
			show_save_quit_prompt = false;
			is_program_running = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			show_save_quit_prompt = false;
			need_quit = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void gui_draw(app_state_t* app_state, input_t* input, i32 client_width, i32 client_height) {
	ImGuiIO &io = ImGui::GetIO();

	gui_want_capture_mouse = io.WantCaptureMouse;
	gui_want_capture_keyboard = io.WantCaptureKeyboard;

	// TODO: check if cursor is in client area before taking over control of the cursor from ImGui
	if (gui_want_capture_mouse) {
		// Cursor is handled by ImGui
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
	} else {
		// We are updating the cursor ourselves
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		update_cursor();
	}

	if (show_menu_bar) gui_draw_main_menu_bar(app_state);

	if (show_open_remote_window) {
		ImGui::SetNextWindowPos(ImVec2(120, 100), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(256, 156), ImGuiCond_FirstUseEver);

		ImGui::Begin("Open remote", &show_open_remote_window);

		ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
		bool entered = false;
		entered = entered || ImGui::InputText("Hostname", remote_hostname, sizeof(remote_hostname), input_flags);
		entered = entered || ImGui::InputText("Port", remote_port, sizeof(remote_port), input_flags);
		entered = entered || ImGui::InputText("Filename", remote_filename, sizeof(remote_filename), input_flags);
		if (entered || ImGui::Button("Connect")) {
			const char* ext = get_file_extension(remote_filename);
#if DO_DEBUG
			if (strcmp(remote_filename, "test_google.html") == 0) {
				i32 bytes_read = 0;
				u8* read_buffer = do_http_request(remote_hostname, atoi(remote_port), "/test", &bytes_read, 0);
				if (read_buffer) {
					FILE* test_out = fopen("test_google2.html", "wb");
					fwrite(read_buffer, bytes_read, 1, test_out);
					fclose(test_out);
					free(read_buffer);
				}
			}
			else
#endif
			if (strcasecmp(ext, "json") == 0) {
				// Open as 'caselist'
				unload_all_images(app_state);
				reset_global_caselist(app_state);
				if (load_caselist_from_remote(&app_state->caselist, remote_hostname, atoi(remote_port), remote_filename)) {
					show_slide_list_window = true;
					show_open_remote_window = false; // success!
					caselist_select_first_case(app_state, &app_state->caselist);
				}
			} else {
				// Open as 'slide'
				if (open_remote_slide(app_state, remote_hostname, atoi(remote_port), remote_filename)) {
					show_open_remote_window = false; // success!
				}
			}


		}
		ImGui::End();
	}


	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	if (show_demo_window) {
		ImGui::ShowDemoWindow(&show_demo_window);
	}

	if (show_debugging_window) {

		ImGui::SetNextWindowPos(ImVec2(120,100), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(256,156), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Debugging", &show_debugging_window)) {
//			ImGui::TextUnformatted("Worker threads");
			ImGui::SliderInt("Worker threads", &active_worker_thread_count, 1, worker_thread_count);
			ImGui::SliderInt("Min level display", &app_state->scene.lowest_scale_to_render, 0, 16);
			ImGui::SliderInt("Max level display", &app_state->scene.highest_scale_to_render, 0, 16);

		}
		ImGui::End();
	}

	if (show_image_options_window) {
		static int counter = 0;

		ImGui::SetNextWindowPos(ImVec2(25, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(388,409), ImGuiCond_FirstUseEver);

		ImGui::Begin("Image options", &show_image_options_window);

		if (ImGui::Button("Reset zoom")) {
			app_state->scene.need_zoom_reset = true;
		}
		ImGui::SameLine();
		float zoom_objective_factor = 40.0f * exp2f(-app_state->scene.zoom.pos);
		ImGui::Text("Current zoom level: %.1f (%gx)", app_state->scene.zoom.pos, zoom_objective_factor);

		if (ImGui::SliderFloat("Zoom level", &app_state->scene.zoom.pos, -5.0f, 15.0f, "%.1f")) {
			zoom_update_pos(&app_state->scene.zoom, app_state->scene.zoom.pos);
		}
        if (ImGui::SliderInt("Min zoom (near)", &viewer_min_level, -5, 15)) {
			viewer_max_level = ATLEAST(viewer_min_level, viewer_max_level);
		}
		if (ImGui::SliderInt("Max zoom (far)", &viewer_max_level, -5, 15)) {
			viewer_min_level = ATMOST(viewer_min_level, viewer_max_level);
		}
		ImGui::NewLine();

		ImGui::Checkbox("Use image adjustments", &app_state->use_image_adjustments);

		bool disable_gui = !app_state->use_image_adjustments;
		if (disable_gui) {
			ImGui::BeginDisabled();
		}

		ImGui::SliderFloat("Black level", &app_state->black_level, 0.0f, 1.0f);
		ImGui::SliderFloat("White level", &app_state->white_level, 0.0f, 1.0f);

		if (disable_gui) {
			ImGui::EndDisabled();
		}

		ImGui::NewLine();
		ImGui::Checkbox("Filter transparent color", &app_state->scene.use_transparent_filter);
		disable_gui = !app_state->scene.use_transparent_filter;
		if (disable_gui) {
			ImGui::BeginDisabled();
		}
		ImGui::ColorEdit3("Transparent color", (float*) &app_state->scene.transparent_color); // Edit 3 floats representing a color
		ImGui::SliderFloat("Tolerance", &app_state->scene.transparent_tolerance, 0.0f, 0.2f);
		if (disable_gui) {
			ImGui::EndDisabled();
		}


		ImGui::NewLine();
		ImGui::ColorEdit3("Background color", (float*) &app_state->clear_color); // Edit 3 floats representing a color


		ImGui::End();
	}

	if (show_general_options_window) {

		ImGui::SetNextWindowPos(ImVec2(120, 100), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);

		ImGui::Begin("General options", &show_general_options_window);
		static ImGuiComboFlags combo_flags = 0;
		ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
		if (ImGui::BeginTabBar("General options tab bar", tab_bar_flags)) {
			if (ImGui::BeginTabItem("Appearance")) {
				ImGui::Text("Graphical user interface");
				// General BeginCombo() API, you have full control over your selection data and display type.
				// (your selection data could be an index, a pointer to the object, an id for the object, a flag stored in the object itself, etc.)
				const char* items[] = {"Dark (default)", "Light", "Classic"};
				static i32 style_color = 0;
				int old_style_color = style_color;

//		        ImGui::Text("User interface colors");               // Display some text (you can use a format strings too)
				if (ImGui::BeginCombo("Colors##user interface", items[style_color],
				                      combo_flags)) // The second parameter is the label previewed before opening the combo.
				{
					for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
						bool is_selected = (style_color == n);
						if (ImGui::Selectable(items[n], is_selected))
							style_color = n;
						if (style_color)
							ImGui::SetItemDefaultFocus();   // Set the initial focus when opening the combo (scrolling + for keyboard navigation support in the upcoming navigation branch)
					}
					ImGui::EndCombo();

					if (style_color != old_style_color) {
						if (style_color == 0) {
							ImGui::StyleColorsDark();
						} else if (style_color == 1) {
							ImGui::StyleColorsLight();
						} else if (style_color == 2) {
							ImGui::StyleColorsClassic();
						}
					}
				}
				ImGui::SliderFloat("Opacity##user interface", &ImGui::GetStyle().Alpha, 0.20f, 1.0f, "%.2f"); // Not exposing zero here so user doesn't "lose" the UI (zero alpha clips all widgets). But application code could have a toggle to switch between zero and non-zero.

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Controls")) {
				ImGui::TextUnformatted("Panning speed");
				ImGui::SliderInt("Mouse sensitivity", &app_state->mouse_sensitivity, 1, 50);
				ImGui::SliderInt("Keyboard sensitivity", &app_state->keyboard_base_panning_speed, 1, 50);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Advanced")) {
				ImGui::Text("\nTIFF backend");
//		        ImGui::Checkbox("Prefer built-in TIFF backend over OpenSlide", &use_builtin_tiff_backend);
				const char* tiff_backends[] = {"Built-in", "OpenSlide"};
				if (ImGui::BeginCombo("##tiff_backend", tiff_backends[1 - app_state->use_builtin_tiff_backend],
				                      combo_flags)) // The second parameter is the label previewed before opening the combo.
				{
					if (ImGui::Selectable(tiff_backends[0], app_state->use_builtin_tiff_backend)) {
						app_state->use_builtin_tiff_backend = true;
					}
					if (app_state->use_builtin_tiff_backend) ImGui::SetItemDefaultFocus();
					if (is_openslide_available) {
						if (ImGui::Selectable(tiff_backends[1], !app_state->use_builtin_tiff_backend)) {
							app_state->use_builtin_tiff_backend = false;
						}
						if (!app_state->use_builtin_tiff_backend) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::NewLine();

				bool prev_is_vsync_enabled = is_vsync_enabled;
				ImGui::Checkbox("Enable Vsync", &is_vsync_enabled);
				if (prev_is_vsync_enabled != is_vsync_enabled) {
					set_swap_interval(is_vsync_enabled ? 1 : 0);
				}
				ImGui::EndTabItem();
			}


			ImGui::EndTabBar();
		}







		ImGui::End();
	}



	if (show_slide_list_window) {

		ImGui::SetNextWindowPos(ImVec2(20, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(460,541), ImGuiCond_FirstUseEver);

		ImGui::Begin("Case info", &show_slide_list_window);

		caselist_t* caselist = &app_state->caselist;

		const char* case_preview = "";
		case_t* selected_case = app_state->selected_case;
		i32 selected_case_index = app_state->selected_case_index;

		case_t* previous_selected_case = selected_case;

		if (selected_case) {
			case_preview = selected_case->name;
		}

		bool can_move_left = (selected_case_index > 0);
		bool can_move_right = (selected_case_index < (i32)caselist->case_count-1);

		if (!can_move_left) ImGui::BeginDisabled();
		if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
			if (caselist->cases && can_move_left) {
				app_state->selected_case_index = (--selected_case_index);
				app_state->selected_case = selected_case = caselist->cases + selected_case_index;
			}
		}
		if (!can_move_left) ImGui::EndDisabled();
		ImGui::SameLine();
		if (!can_move_right) ImGui::BeginDisabled();
		if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
			if (caselist->cases && can_move_right) {
				app_state->selected_case_index = (++selected_case_index);
				app_state->selected_case = selected_case = caselist->cases + selected_case_index;
			}
		}
		if (!can_move_right) ImGui::EndDisabled();
		ImGui::SameLine();

		if (ImGui::BeginCombo("##Select_case", case_preview, ImGuiComboFlags_HeightLarge)) {
			if (caselist->cases) {
				for (u32 i = 0; i < caselist->case_count; ++i) {
					case_t* the_case = caselist->cases + i;
					if (ImGui::Selectable(the_case->name, selected_case_index == (i32)i)) {
						app_state->selected_case = selected_case = the_case;
						app_state->selected_case_index = selected_case_index = (i32)i;
					}
				}
			}
			ImGui::EndCombo();
		}

		if (selected_case != previous_selected_case) {
			if (selected_case && selected_case->slides) {
				caselist_open_slide(app_state, caselist, selected_case->slides);
			}
		}


		ImGui::NewLine();
		ImGui::Separator();
		ImGui::NewLine();

		if (selected_case != NULL) {
//			ImGui::TextWrapped("%s\n", selected_case->name);

			slide_info_t* slides = selected_case->slides;
			u32 slide_count = selected_case->slide_count;
			for (u32 slide_index = 0; slide_index < slide_count; ++slide_index) {
				slide_info_t* slide = slides + slide_index;
				if (ImGui::Button(slide->stain) && slide_count > 1) {
					caselist_open_slide(app_state, &app_state->caselist, slide);
				}
				// TODO: correctly wrap buttons to the next line? For now, 5 per line.
				if (slide_index < 4 || ((slide_index + 1) % 5) != 0) {
					ImGui::SameLine();
				}
			}
			ImGui::NewLine();

			ImGui::TextWrapped("%s\n", selected_case->clinical_context);
			ImGui::NewLine();

			if (ImGui::TreeNode("Diagnosis and comment")) {
				ImGui::TextWrapped("%s\n", selected_case->diagnosis);
				ImGui::TextWrapped("%s\n", selected_case->notes);
				ImGui::TreePop();
			}


		}

		if (caselist->case_count == 0) {
			ImGui::TextWrapped("No case list has currently been loaded.\n\n"
					  "To load a case list, you can do one of the following:\n"
	                  "- Open a local case list file (with a '.json' file extension)\n"
			          "- Connect to a remote case list (using File > Open remote)\n");
		}



		ImGui::End();


	}



	if (show_annotations_window || show_annotation_group_assignment_window) {
		draw_annotations_window(app_state, input);
	}
//	draw_annotation_palette_window();



	if (show_layers_window) {
		draw_layers_window(app_state);
	}

	if (show_about_window) {
		ImGui::Begin("About " APP_TITLE, &show_about_window, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

		ImGui::TextUnformatted(APP_TITLE " - a whole-slide image viewer for digital pathology");
		ImGui::Text("Author: Pieter Valkema\n");
		ImGui::TextUnformatted("Version: " APP_VERSION );


		ImGui::Text("\nLicense information:\nThis program is free software: you can redistribute it and/or modify\n"
		            "  it under the terms of the GNU General Public License as published by\n"
		            "  the Free Software Foundation, either version 3 of the License, or\n"
		            "  (at your option) any later version.\n\n");
		if (ImGui::Button("View releases on GitHub")) {
#if WINDOWS
			ShellExecuteW(0, 0, L"https://github.com/amspath/slidescape/releases", 0, 0 , SW_SHOW );
#elif APPLE
			system("open https://github.com/amspath/slidescape/releases");
#elif LINUX
			system("gio open https://github.com/amspath/slidescape/releases");
#endif
		};

		ImGui::End();
	}

	if (show_mouse_pos_overlay) {
		draw_mouse_pos_overlay(app_state, &show_mouse_pos_overlay);
	}

	if (show_console_window) {
		draw_console_window(app_state, "Console", &show_console_window);
	}

	// Draw modal popups last
	draw_export_region_dialog(app_state);
	annotation_modal_dialog(app_state, &app_state->scene.annotation_set);
	save_changes_modal(app_state, &app_state->scene.annotation_set);
	gui_do_modal_popups();

#if (LINUX || APPLE)
	gui_draw_open_file_dialog(app_state);
#endif
}

static i32 ticks_to_delay_before_first_dialog = 1;

void gui_do_modal_popups() {

	if (ticks_to_delay_before_first_dialog > 0) {
		// For whatever reason, modal dialogs might not open properly on the first frame of the program.
		// So, display the first dialog (e.g. "Could not load file") only after a short delay.
		--ticks_to_delay_before_first_dialog;
	} else {
		if (arrlen(gui_modal_stack) > 0) {
			gui_modal_popup_t* popup = gui_modal_stack;
			if (popup->need_open) {
				ImGui::OpenPopup(popup->title);

				// Check that the popup is actually open!
				ImGuiContext& g = *GImGui;
				ImGuiWindow* window = g.CurrentWindow;
				const ImGuiID id = window->GetID(popup->title);
				if (ImGui::IsPopupOpen(id, ImGuiPopupFlags_None)) {
					popup->need_open = false;
				}
			}
			gui_make_next_window_appear_in_center_of_screen();
			if (popup->type == GUI_MODAL_MESSAGE) {
				if (ImGui::BeginPopupModal(popup->title, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
					ImGui::TextUnformatted(popup->message);
					if (ImGui::Button("OK", ImVec2(120, 0))) {
						popup->need_open = false;
						ImGui::CloseCurrentPopup();
						arrdel(gui_modal_stack, 0);
					}
					ImGui::EndPopup();
				}
			} else if (popup->type == GUI_MODAL_PROGRESS_BAR) {
				if (ImGui::BeginPopupModal(popup->title, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
					float progress = (popup->progress) ? *popup->progress : 0.0f;
					float difference = progress - popup->visual_progress;
					if (difference > 0.0f) {
						popup->visual_progress += ATLEAST(MIN(difference, 0.002f), difference * 0.1f);
						if (popup->visual_progress > progress) popup->visual_progress = progress;
					} else if (difference < 0.0f) {
						difference = -difference;
						popup->visual_progress -= ATLEAST(MIN(difference, 0.002f), difference * 0.1f);
						if (popup->visual_progress < progress) popup->visual_progress = progress;
					}
					ImGui::ProgressBar(popup->visual_progress, ImVec2(0.0f, 0.0f), "");
					if (progress >= 1.0f || (popup->allow_cancel && ImGui::Button("Cancel", ImVec2(120, 0)))) {
						ImGui::CloseCurrentPopup();
						arrdel(gui_modal_stack, 0);
					}
					ImGui::EndPopup();
				}
			}
		}
	}
}

void gui_add_modal_message_popup(const char* title, const char* message, ...) {
	gui_modal_popup_t popup = {};
	popup.type = GUI_MODAL_MESSAGE;
	popup.title = title;

	va_list args;
	va_start(args, message);
	vsnprintf(popup.message, sizeof(popup.message)-1, message, args);
	popup.message[sizeof(popup.message)-1] = 0;
	va_end(args);

	popup.need_open = true;
	arrpush(gui_modal_stack, popup);
}

void gui_add_modal_progress_bar_popup(const char* title, float* progress, bool allow_cancel) {
	gui_modal_popup_t popup = {};
	popup.type = GUI_MODAL_PROGRESS_BAR;
	popup.title = title;
	popup.progress = progress;
	popup.allow_cancel = allow_cancel;
	popup.need_open = true;
	arrpush(gui_modal_stack, popup);
}

static void a_very_long_task(i32 logical_thread_index, void* userdata) {
	for (i32 i = 0; i < 10; ++i) {
		platform_sleep(1000);
		global_progress_bar_test_progress = (i + 1) * 0.1f;
	}
}

// for testing the progress bar popup
void begin_a_very_long_task() {
	add_work_queue_entry(&global_work_queue, a_very_long_task, NULL, 0);
}




