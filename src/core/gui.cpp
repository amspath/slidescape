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

#include "common.h"
#include "platform.h"

#include OPENGL_H

#include "imgui.h"
#include "imgui_freetype.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"

#include "openslide_api.h"
#include "viewer.h"
#include "remote.h"
#include "caselist.h"
#include "tiff_write.h"
#include "coco.h"
#include "isyntax.h"

#define GUI_IMPL
#include "gui.h"
#include "annotation.h"
#include "stringutils.h"

static void*   imgui_malloc_wrapper(size_t size, void* user_data)    { IM_UNUSED(user_data); return ltmalloc(size); }
static void    imgui_free_wrapper(void* ptr, void* user_data)        { IM_UNUSED(user_data); ltfree(ptr); }

void imgui_create_context() {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::SetAllocatorFunctions(imgui_malloc_wrapper, imgui_free_wrapper);
	ImGui::CreateContext();
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
		points[i] = world_pos_to_screen_pos(points[i], scene->camera_bounds.min, scene->zoom.pixel_width);
	}
	gui_draw_polygon_outline(points, 4, color, thickness);
}

void gui_draw_bounds_in_scene(bounds2f bounds, rgba_t color, float thickness, scene_t* scene) {
	v2f points[4];
	points[0] = (v2f) { bounds.left, bounds.top };
	points[1] = (v2f) { bounds.left, bounds.bottom };
	points[2] = (v2f) { bounds.right, bounds.bottom };
	points[3] = (v2f) { bounds.right, bounds.top };
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
			if (ImGui::MenuItem("Crop view to region", NULL, app_state->scene.is_cropped, app_state->scene.has_selection_box || app_state->scene.is_cropped)) {
				menu_items_clicked.crop_region = true;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Annotation")) {
			if (ImGui::MenuItem("Load...", NULL, &menu_items_clicked.open_file)) {} // TODO: only accept annotation files here?
			ImGui::Separator();
			if (ImGui::MenuItem("Annotations...", NULL, &show_annotations_window)) {}
			if (ImGui::MenuItem("Assign group/feature...", NULL, &show_annotation_group_assignment_window)) {}
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
			bool* show_scale_bar = (arrlen(app_state->loaded_images) > 0) ? &scene->scale_bar.enabled : NULL;
			if (ImGui::MenuItem("Show scale bar", NULL, show_scale_bar, (show_scale_bar != NULL))) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Show case list", NULL, &show_slide_list_window)) {}
			ImGui::Separator();

			if (ImGui::MenuItem("General options...", NULL, &show_general_options_window)) {}
			if (ImGui::BeginMenu("Debug")) {
				if (ImGui::MenuItem("Show console", "F3", &show_console_window)) {}
				if (ImGui::MenuItem("Show demo window", "F1", &show_demo_window)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Open remote...", NULL, &menu_items_clicked.open_remote)) {}
				ImGui::Separator();
//				if (ImGui::MenuItem("Save XML annotations", NULL, &menu_items_clicked.save_annotations)) {}
				if (ImGui::MenuItem("Show menu bar", "Alt+F11", &show_menu_bar)) {}
				if (ImGui::MenuItem("Load next as overlay", "F6", &load_next_image_as_overlay)) {}
				if (enable_load_debug_coco_file) {
					if (ImGui::MenuItem("Load COCO test file", NULL, &menu_items_clicked.load_coco_test_file)) {}
				}
				if (enable_load_debug_isyntax_file) {
					if (ImGui::MenuItem("Load iSyntax test file", NULL, &menu_items_clicked.load_isyntax_test_file, enable_load_debug_isyntax_file)) {}
				}
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
			open_file_dialog(app_state, 0);
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
	}

}

static const char* get_image_type_name(image_t* image) {
	const char* result = "--";
	if (image->type == IMAGE_TYPE_SIMPLE) {
		result = "Simple";
	} else if (image->type == IMAGE_TYPE_WSI) {
		if (image->backend == IMAGE_BACKEND_TIFF) {
			result = "TIFF";
		} else if (image->backend == IMAGE_BACKEND_OPENSLIDE) {
			result = "OpenSlide";
		} else if (image->backend == IMAGE_BACKEND_ISYNTAX) {
			result = "iSyntax";
		} else {
			result = "WSI (?)";
		}
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
	ImGui::Checkbox("Load next dragged image as overlay (F6)", &load_next_image_as_overlay);

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
	// Always center this window when appearing
	ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
	ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

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
					changed = changed || ImGui::InputInt("Offset X##export_pixel_bounds", &export_rect.x);
					changed = changed || ImGui::InputInt("Offset Y##export_pixel_bounds", &export_rect.y);
					changed = changed || ImGui::InputInt("Width##export_pixel_bounds", &export_rect.w);
					changed = changed || ImGui::InputInt("Height##export_pixel_bounds", &export_rect.h);

					if (changed) {
						scene->selection_box = pixel_rect_to_world_rect(export_rect, image->mpp_x, image->mpp_y);
						scene->selection_pixel_bounds = (bounds2i){export_rect.x, export_rect.y, export_rect.x + export_rect.w, export_rect.y + export_rect.h};
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
			filename_hint = "output.tiff";
		} else if (desired_region_export_format == 1) {
			filename_hint = "output.jpeg";
		} else if (desired_region_export_format == 2) {
			filename_hint = "output.png";
		}

		static char filename[4096];
		ImGui::InputTextWithHint("##export_region_output_filename", filename_hint, filename, sizeof(filename));
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) {
			if (save_file_dialog(app_state, filename, sizeof(filename), "BigTIFF (*.tiff)\0*.tiff;*.tif;*.ptif\0All\0*.*\0Text\0*.TXT\0")) {
				size_t filename_len = strlen(filename);
				if (filename_len > 0) {
					const char* extension = get_file_extension(filename);
					if (!(strcasecmp(extension, "tiff") == 0 || strcasecmp(extension, "tif") == 0 || strcasecmp(extension, "ptif") == 0)) {
						// if extension incorrect, append it at the end
						i64 remaining_len = sizeof(filename) - filename_len;
						strncpy(filename + filename_len, ".tiff", remaining_len-1);
					}
				} else {
//					console_print_verbose("Export region: save file dialog returned 0\n");
				}
			}
		}


		if (ImGui::Button("Export", ImVec2(120, 0))) {
			switch(image->backend) {
				case IMAGE_BACKEND_TIFF: {
					export_cropped_bigtiff(app_state, image, &image->tiff, scene->selection_pixel_bounds, filename, 512, tiff_export_desired_color_space, tiff_export_jpeg_quality);
				} break;
				default: {
					gui_add_modal_popup("Error##draw_export_region_dialog", "This image backend is currently not supported for exporting a region.\n");
					console_print_error("Error: image backend not supported for exporting a region\n");
				}
			}
			show_export_region_dialog = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		ImGui::SetItemDefaultFocus();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			show_export_region_dialog = false;
			ImGui::CloseCurrentPopup();
		}


		ImGui::EndGroup();

		ImGui::EndPopup();
	}
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
		ImGui::Checkbox("Enable Vsync", &is_vsync_enabled);




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
	annotation_modal_dialog(app_state, &app_state->scene.annotation_set);
	draw_export_region_dialog(app_state);

	gui_do_modal_popups();

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
			ShellExecuteW(0, 0, L"https://github.com/Falcury/slideviewer/releases", 0, 0 , SW_SHOW );
#elif APPLE
			system("open https://github.com/Falcury/slideviewer/releases");
#elif LINUX
			system("gio open https://github.com/Falcury/slideviewer/releases");
#endif
		};

		ImGui::End();
	}

	if (show_console_window) {
		draw_console_window(app_state, "Console", &show_console_window);
	}

#if (LINUX || APPLE)
	gui_draw_open_file_dialog(app_state);
#endif


}

struct console_log_item_t {
	char* text;
	bool has_color;
	u32 item_type;
};

console_log_item_t* console_log_items; //sb

void console_clear_log() {
	benaphore_lock(&console_printer_benaphore);
	for (int i = 0; i < arrlen(console_log_items); i++) {
		console_log_item_t* item = console_log_items + i;
		if (item->text) {
			free(item->text);
		}
	}
	arrfree(console_log_items);
	console_log_items = NULL;
	benaphore_unlock(&console_printer_benaphore);
}

bool console_fill_screen = false;

void draw_console_window(app_state_t* app_state, const char* window_title, bool* p_open) {

	float desired_fraction_of_height = console_fill_screen ? 1.0f : 0.33f;

	rect2i viewport = app_state->client_viewport;
	viewport.x *= app_state->display_points_per_pixel;
	viewport.y *= app_state->display_points_per_pixel;
	viewport.w *= app_state->display_points_per_pixel;
	viewport.h *= app_state->display_points_per_pixel;

	float desired_width = (float) viewport.w;
	float desired_height = roundf((float)viewport.h * desired_fraction_of_height);
	if (show_menu_bar) {
		float vertical_space_left = viewport.h - desired_height;
		float need_space = 23.0f;
		if (vertical_space_left < need_space) {
			desired_height = (float)viewport.h - need_space;
		}
	}
	ImGui::SetNextWindowSize(ImVec2(desired_width, desired_height), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(0,viewport.h - desired_height), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.8f);
	if (!ImGui::Begin(window_title, p_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}
	ImGui::PopStyleVar(2);

#if 0
	if (ImGui::SmallButton("Add Debug Text"))  { console_print("%d some text\n", sb_count(console_log_items)); console_print("some more text\n"); console_print("display very important message here!\n"); } ImGui::SameLine();
	if (ImGui::SmallButton("Add Debug Error")) { console_print_error("[error] something went wrong\n"); } ImGui::SameLine();
	if (ImGui::SmallButton("Clear"))           { console_clear_log(); } ImGui::SameLine();
	bool copy_to_clipboard = ImGui::SmallButton("Copy");
	ImGui::Separator();
#endif



	// Reserve enough left-over height for 1 separator + 1 input text
	const float footer_height_to_reserve = 0.0f;//ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);
	if (ImGui::BeginPopupContextWindow())
	{
		if (ImGui::Selectable("Clear")) console_clear_log();
		if (ImGui::MenuItem("Verbose mode", NULL, &is_verbose_mode)) {}
		if (ImGui::MenuItem("Fill screen", NULL, &console_fill_screen)) {}
		ImGui::EndPopup();
	}
	benaphore_lock(&console_printer_benaphore);
	i32 item_count = arrlen(console_log_items);
	if (item_count > 0) {
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
		ImGui::PushFont(global_fixed_width_font);
		ImGuiListClipper clipper;
		clipper.Begin(arrlen(console_log_items));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
				console_log_item_t item = console_log_items[i];
//				if (!Filter.PassFilter(item))
//					continue;

				// Normally you would store more information in your item than just a string.
				// (e.g. make Items[] an array of structure, store color/type etc.)
				ImVec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
				if (item.has_color) {
					if (item.item_type == 1)      { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);}
					else if (item.item_type == 2) { color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);}
					else if (strncmp(item.text, "# ", 2) == 0) { color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);  }
					ImGui::PushStyleColor(ImGuiCol_Text, color);
				}
				ImGui::TextUnformatted(item.text);
				if (item.has_color) {
					ImGui::PopStyleColor();
				}
			}
		}
		ImGui::PopFont();
		ImGui::PopStyleVar();
	}
	benaphore_unlock(&console_printer_benaphore);
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(0.0f);

	ImGui::EndChild();
//	ImGui::Separator();

	// Command-line
	/*bool reclaim_focus = false;
	ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
	if (ImGui::InputText("Input", InputBuf, IM_ARRAYSIZE(InputBuf), input_text_flags, &TextEditCallbackStub, (void*)this))
	{
		char* s = InputBuf;
		Strtrim(s);
		if (s[0])
			ExecCommand(s);
		strcpy(s, "");
		reclaim_focus = true;
	}

	// Auto-focus on window apparition
	ImGui::SetItemDefaultFocus();
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget*/

	ImGui::End();
}



void console_split_lines_and_add_log_item(char* raw, bool has_color, u32 item_type) {
	size_t num_lines = 0;
	char** lines = split_into_lines(raw, &num_lines);
	if (lines) {
		for (i32 i = 0; i < num_lines; ++i) {
			char* line = lines[i];
			size_t line_len = strlen(line);
			if (line && line_len > 0) {
				console_log_item_t new_item = {};
				// TODO: fix strdup() conflicting with ltmalloc()
				new_item.text = (char*)malloc(line_len+1);
				memcpy(new_item.text, line, line_len);
				new_item.text[line_len] = '\0';
				new_item.has_color = has_color;
				new_item.item_type = item_type;
				benaphore_lock(&console_printer_benaphore);
				arrput(console_log_items, new_item);
				benaphore_unlock(&console_printer_benaphore);
			}
		}
		free(lines);
	}
}

typedef struct gui_modal_popup_t gui_modal_popup_t;
struct gui_modal_popup_t {
	char message[4096];
	const char* title;
	bool need_open;
};

static gui_modal_popup_t* gui_popup_stack;
static i32 ticks_to_delay_before_first_dialog = 1;

void gui_do_modal_popups() {

	if (ticks_to_delay_before_first_dialog > 0) {
		// For whatever reason, modal dialogs might not open properly on the first frame of the program.
		// So, display the first dialog (e.g. "Could not load file") only after a short delay.
		--ticks_to_delay_before_first_dialog;
	} else {
		if (arrlen(gui_popup_stack) > 0) {
			gui_modal_popup_t* popup = gui_popup_stack;
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
			// Always center this window when appearing
			ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			bool open = true;
			if (ImGui::BeginPopupModal(popup->title, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextUnformatted(popup->message);
				if (ImGui::Button("OK", ImVec2(120, 0))) {
					popup->need_open = false;
					ImGui::CloseCurrentPopup();
					// destroy
					arrdel(gui_popup_stack, 0);
				}
				ImGui::EndPopup();
			}

		}
	}


}

void gui_add_modal_popup(const char* title, const char* message, ...) {
	gui_modal_popup_t popup = {};
	popup.title = title;

	va_list args;
	va_start(args, message);
	vsnprintf(popup.message, sizeof(popup.message)-1, message, args);
	popup.message[sizeof(popup.message)-1] = 0;
	va_end(args);

	popup.need_open = true;
	arrpush(gui_popup_stack, popup);
}

void console_print(const char* fmt, ...) {

	char buf[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf)-1, fmt, args);
	fprintf(stdout, "%s", buf);
	buf[sizeof(buf)-1] = 0;
	va_end(args);

	console_split_lines_and_add_log_item(buf, false, 0);
}

void console_print_verbose(const char* fmt, ...) {
	if (!is_verbose_mode) return;
	char buf[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf)-1, fmt, args);
	fprintf(stdout, "%s", buf);
	buf[sizeof(buf)-1] = 0;
	va_end(args);

	console_split_lines_and_add_log_item(buf, true, 2);
}


void console_print_error(const char* fmt, ...) {
	char buf[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf)-1, fmt, args);
	fprintf(stderr, "%s", buf);
	buf[sizeof(buf)-1] = 0;
	va_end(args);

	console_split_lines_and_add_log_item(buf, true, 1);
}


