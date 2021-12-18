/*
  Slidescape, a whole-slide image viewer for digital pathology.
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

#include "common.h"
#include "platform.h"

#include OPENGL_H

#include "imgui.h"
#include "imgui_internal.h"

#include "openslide_api.h"
#include "viewer.h"
#include "remote.h"
#include "tiff_write.h"
#include "isyntax.h"

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

void gui_draw_polygon_outline(v2f* points, i32 count, rgba_t rgba, float thickness) {
	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	u32 color = *(u32*)(&rgba);
	draw_list->AddPolyline((ImVec2*)points, count, color, true, thickness);
}

void gui_draw_polygon_outline_in_scene(v2f* points, i32 count, rgba_t color, float thickness, scene_t* scene) {
	for (i32 i = 0; i < count; ++i) {
		points[i] = world_pos_to_screen_pos(points[i], scene->camera_bounds.min, scene->zoom.screen_point_width);
	}
	gui_draw_polygon_outline(points, 4, color, thickness);
}

void gui_draw_bounds_in_scene(bounds2f bounds, rgba_t color, float thickness, scene_t* scene) {
	v2f points[4];
	points[0] = V2F(bounds.left, bounds.top);
	points[1] = V2F(bounds.left, bounds.bottom);
	points[2] = V2F(bounds.right, bounds.bottom);
	points[3] = V2F(bounds.right, bounds.top);
	gui_draw_polygon_outline_in_scene(points, 4, color, thickness, scene);
}

bool enable_load_debug_coco_file;
bool enable_load_debug_isyntax_file;
const char* coco_test_filename = "coco_test_in.json";
const char* isyntax_test_filename = "1.isyntax";

void check_presence_of_debug_test_files() {
	static bool checked;
	if (!checked) {
		enable_load_debug_coco_file = file_exists(coco_test_filename);
		enable_load_debug_isyntax_file = file_exists(isyntax_test_filename);
		checked = true;
	}
}

void gui_draw_main_menu_bar(app_state_t* app_state) {
	check_presence_of_debug_test_files();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	bool ret = ImGui::BeginMainMenuBar();
	ImGui::PopStyleVar(1);
	if (ret) {
		scene_t* scene = &app_state->scene;

		static struct {
			bool open_file;
			bool close;
			bool open_remote;
			bool exit_program;
			bool new_dataset_asap_xml;
			bool new_dataset_coco;
			bool new_dataset_geojson;
			bool save_annotations;
			bool select_region;
			bool deselect;
			bool crop_region;
			bool export_region;
			bool load_coco_test_file;
			bool load_isyntax_test_file;
			bool reset_zoom;
			bool insert_point;
			bool insert_line;
			bool insert_freeform;
			bool insert_ellipse;
			bool insert_rectangle;
			bool insert_text;
		} menu_items_clicked;
		memset(&menu_items_clicked, 0, sizeof(menu_items_clicked));

		bool prev_is_vsync_enabled = is_vsync_enabled;
		bool prev_fullscreen = is_fullscreen;

		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open...", "Ctrl+O", &menu_items_clicked.open_file)) {}
			if (ImGui::MenuItem("Close", "Ctrl+W", &menu_items_clicked.close)) {}
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
			if (ImGui::MenuItem("Select region", NULL, &menu_items_clicked.select_region)) {}
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
#if ENABLE_INSERT_TOOLS
			if (ImGui::BeginMenu("Insert")) {
				if (ImGui::MenuItem("Point", "Q", &menu_items_clicked.insert_point)) {}
				if (ImGui::MenuItem("Line", "M", &menu_items_clicked.insert_line)) {}
				if (ImGui::MenuItem("Freeform", "F", &menu_items_clicked.insert_freeform)) {}
				if (ImGui::MenuItem("Ellipse", "E", &menu_items_clicked.insert_ellipse)) {}
				if (ImGui::MenuItem("Rectangle", "R", &menu_items_clicked.insert_rectangle)) {}
				if (ImGui::MenuItem("Text", "T", &menu_items_clicked.insert_text)) {}
				ImGui::EndMenu();
			}
			ImGui::Separator();
#endif
			if (ImGui::MenuItem("Annotations...", NULL, &show_annotations_window)) {}
			if (ImGui::MenuItem("Assign group/feature...", NULL, &show_annotation_group_assignment_window)) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			bool has_image_loaded = (arrlen(app_state->loaded_images) > 0);
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
				if (ImGui::MenuItem("Show console", "F3", &show_console_window)) {}
				if (ImGui::MenuItem("Show demo window", "F1", &show_demo_window)) {}
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
		} else if (menu_items_clicked.open_remote) {
			show_open_remote_window = true;
		} else if (prev_fullscreen != is_fullscreen) {
			bool currently_fullscreen = check_fullscreen(app_state->main_window);
			if (currently_fullscreen != is_fullscreen) {
				toggle_fullscreen(app_state->main_window);
			}
		} else if(menu_items_clicked.save_annotations) {
			save_asap_xml_annotations(&app_state->scene.annotation_set, "test_out.xml");
		} else if (menu_items_clicked.select_region) {
			app_state->mouse_mode = MODE_CREATE_SELECTION_BOX;
		} else if (menu_items_clicked.deselect) {
			scene->has_selection_box = false;
		} else if (menu_items_clicked.crop_region) {
			if (!scene->is_cropped) {
				rect2f final_crop_rect = rect2f_recanonicalize(&scene->selection_box);
				bounds2f bounds = rect2f_to_bounds(&final_crop_rect);
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
#if ENABLE_INSERT_TOOLS
		else if (menu_items_clicked.insert_point) {
			viewer_switch_tool(app_state, TOOL_CREATE_POINT);
		} else if (menu_items_clicked.insert_line) {
			viewer_switch_tool(app_state, TOOL_CREATE_LINE);
		} else if (menu_items_clicked.insert_freeform) {
			viewer_switch_tool(app_state, TOOL_CREATE_FREEFORM);
		} else if (menu_items_clicked.insert_ellipse) {
			viewer_switch_tool(app_state, TOOL_CREATE_ELLIPSE);
		} else if (menu_items_clicked.insert_rectangle) {
			viewer_switch_tool(app_state, TOOL_CREATE_RECTANGLE);
		} else if (menu_items_clicked.insert_text) {
			viewer_switch_tool(app_state, TOOL_CREATE_TEXT);
		}
#endif
	}

}

static const char* get_image_type_name(image_t* image) {
	const char* result = "--";
	if (image->type == IMAGE_TYPE_WSI) {
		if (image->backend == IMAGE_BACKEND_TIFF) {
			result = "WSI (TIFF)";
		} else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
			result = "WSI (OpenSlide)";
		} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
			result = "WSI (iSyntax)";
		} else if (image->backend == IMAGE_BACKEND_STBI) {
			result = "Simple image";
		}
	} else {
		result = "Unknown";
	}
	return result;
}

void draw_layers_window(app_state_t* app_state) {
	if (!show_layers_window) return;

	ImGui::SetNextWindowPos(ImVec2(20, 50), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(460,541), ImGuiCond_FirstUseEver);

	ImGui::Begin("Layers", &show_layers_window);

	const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;

	static i32 selected_image_index = 0;
	i32 image_count = arrlen(app_state->loaded_images);
	if (selected_image_index >= image_count) selected_image_index = 0;

	static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;

	if (ImGui::BeginTable("layers_table", 2, flags)) {
		// The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
//		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 12.0f);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 18.0f);
		ImGui::TableHeadersRow();

		for (i32 image_index = 0; image_index < image_count; ++image_index) {
			image_t* image = app_state->loaded_images + image_index;
			char name[256];
			snprintf(name, sizeof(name)-1, "Layer %d", image_index);
			const char* type = get_image_type_name(image);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			bool selected = ImGui::Selectable(name);
			if (selected) selected_image_index = image_index;
			ImGui::TableNextColumn();
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
	if (selected_image_index < image_count) {
		image_t* image = app_state->loaded_images + selected_image_index;
		ImGui::Text("Adjust position offset for layer %d:", selected_image_index);
		ImGui::DragFloat("Offset X", &image->origin_offset.x, image->mpp_x, 0.0f, 0.0f, "%g");
		ImGui::DragFloat("Offset Y", &image->origin_offset.y, image->mpp_y, 0.0f, 0.0f, "%g");
	}
	ImGui::NewLine();
	ImGui::Text("Currently displayed layer: %d.\nPress Tab to toggle layers.", app_state->scene.active_layer);

	ImGui::SliderFloat("Layer transition", &target_layer_t, 0.0f, 1.0f);

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

		ImGui::BeginGroup();
		ImGui::BeginChild("item view", ImVec2(0, -2.0f * ImGui::GetFrameHeightWithSpacing())); // Leave room for 2 lines below us


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

		const char* filename_hint = "";
		if (desired_region_export_format == 0) {
#if APPLE
			// macOS does not seem to like tile TIFF files in the Finder (will sometimes stop responding,
			// at least on my system). So choose the .ptif file extension by default as an alternative.
			filename_hint = "output.ptif";
#else
			filename_hint = "output.tiff";
#endif
		} else if (desired_region_export_format == 1) {
			filename_hint = "output.jpeg";
		} else if (desired_region_export_format == 2) {
			filename_hint = "output.png";
		}

		char* filename_buffer = global_export_save_as_filename;
		size_t filename_buffer_size = sizeof(global_export_save_as_filename);

		ImGui::InputTextWithHint("##export_region_output_filename", filename_hint, filename_buffer, filename_buffer_size);
		ImGui::SameLine();
		if (save_file_dialog_open || ImGui::Button("Browse...")) {
			if (save_file_dialog(app_state, filename_buffer, filename_buffer_size, "BigTIFF (*.tiff)\0*.tiff;*.tif;*.ptif\0All\0*.*\0Text\0*.TXT\0")) {
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
						begin_export_cropped_bigtiff(app_state, image, &image->tiff, scene->selection_pixel_bounds,
						                             filename_buffer, 512,
						                             tiff_export_desired_color_space, tiff_export_jpeg_quality);
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

void gui_draw(app_state_t* app_state, input_t* input, i32 client_width, i32 client_height) {
	ImGuiIO &io = ImGui::GetIO();

	gui_want_capture_mouse = io.WantCaptureMouse;
	gui_want_capture_keyboard = io.WantCaptureKeyboard;

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

	if (show_image_options_window) {
		static int counter = 0;

		ImGui::SetNextWindowPos(ImVec2(25, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(388,409), ImGuiCond_FirstUseEver);

		ImGui::Begin("Image options", &show_image_options_window);

		if (ImGui::Button("Reset zoom")) {
			app_state->scene.need_zoom_reset = true;
		}
		ImGui::SameLine();
		float zoom_objective_factor = 40.0f * exp2f(-app_state->scene.zoom.level);
		ImGui::Text("Current zoom level: %d (%gx)", app_state->scene.zoom.level, zoom_objective_factor);

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

		ImGui::Text("User interface options");
		ImGui::SliderFloat("Opacity##user interface", &ImGui::GetStyle().Alpha, 0.20f, 1.0f, "%.2f"); // Not exposing zero here so user doesn't "lose" the UI (zero alpha clips all widgets). But application code could have a toggle to switch between zero and non-zero.
		// General BeginCombo() API, you have full control over your selection data and display type.
		// (your selection data could be an index, a pointer to the object, an id for the object, a flag stored in the object itself, etc.)
		const char* items[] = {"Dark", "Light", "Classic"};
		static i32 style_color = 0;
		int old_style_color = style_color;
		static ImGuiComboFlags flags = 0;
//		ImGui::Text("User interface colors");               // Display some text (you can use a format strings too)
		if (ImGui::BeginCombo("Colors##user interface", items[style_color],
		                      flags)) // The second parameter is the label previewed before opening the combo.
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


		ImGui::Text("\nTIFF backend");
//		ImGui::Checkbox("Prefer built-in TIFF backend over OpenSlide", &use_builtin_tiff_backend);
		const char* tiff_backends[] = {"Built-in", "OpenSlide"};
		if (ImGui::BeginCombo("##tiff_backend", tiff_backends[1 - app_state->use_builtin_tiff_backend],
		                      flags)) // The second parameter is the label previewed before opening the combo.
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

void a_very_long_task(i32 logical_thread_index, void* userdata) {
	for (i32 i = 0; i < 10; ++i) {
		platform_sleep(1000);
		global_progress_bar_test_progress = (i + 1) * 0.1f;
	}
}

void begin_a_very_long_task() {
	add_work_queue_entry(&global_work_queue, a_very_long_task, NULL, 0);
}




