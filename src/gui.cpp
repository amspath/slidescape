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

#define GUI_IMPL
#include "gui.h"
#include "annotation.h"
#include "stringutils.h"

void menu_close_file(app_state_t* app_state) {
	unload_all_images(app_state);
	reset_global_caselist(app_state);
	unload_and_reinit_annotations(&app_state->scene.annotation_set);
}

void gui_push_disabled_style(bool condition) {
	if (condition) {
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}
}

void gui_pop_disabled_style(bool condition) {
	if (condition) {
		ImGui::PopItemFlag();
		ImGui::PopStyleVar();
	}
}

void gui_draw_polygon_outline(v2f* points, i32 count, rgba_t rgba, float thickness) {
	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	u32 color = *(u32*)(&rgba);
	draw_list->AddPolyline((ImVec2*)points, count, color, true, thickness);
}

void gui_draw_main_menu_bar(app_state_t* app_state) {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	bool ret = ImGui::BeginMainMenuBar();
	ImGui::PopStyleVar(1);
	if (ret) {
		scene_t* scene = &app_state->scene;
		bounds2f export_bounds;

		bool can_export = true;
		if (scene->has_selection_box) {
			rect2f final_crop_rect = rect2f_recanonicalize(&scene->selection_box);
			export_bounds = rect2f_to_bounds(&final_crop_rect);
		} else if (scene->is_cropped) {
			export_bounds = scene->crop_bounds;
		} else {
			can_export = false;
		}

		static struct {
			bool open_file;
			bool close;
			bool open_remote;
			bool exit_program;
			bool save_annotations;
			bool select_region;
			bool deselect;
			bool crop_region;
			bool export_region_as_bigtiff;
			bool export_region_as_jpeg;
			bool export_region_as_png;
		} menu_items_clicked;
		memset(&menu_items_clicked, 0, sizeof(menu_items_clicked));

		bool prev_is_vsync_enabled = is_vsync_enabled;
		bool prev_fullscreen = is_fullscreen;

		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open...", "Ctrl+O", &menu_items_clicked.open_file)) {}
			if (ImGui::MenuItem("Close", "Ctrl+W", &menu_items_clicked.close)) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Open remote...", NULL, &menu_items_clicked.open_remote)) {}
			ImGui::Separator();
			if (ImGui::BeginMenu("Export", can_export)) {
				bool enabled = app_state->scene.has_selection_box || app_state->scene.is_cropped;
				if (ImGui::MenuItem("Export region as BigTIFF...", NULL, &menu_items_clicked.export_region_as_bigtiff,enabled)) {}
				if (ImGui::MenuItem("Export region as JPEG...", NULL, &menu_items_clicked.export_region_as_jpeg,false)) {}
				if (ImGui::MenuItem("Export region as PNG...", NULL, &menu_items_clicked.export_region_as_png,false)) {}
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
			if (ImGui::MenuItem("Assign group...", NULL, &show_annotation_group_assignment_window)) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			prev_fullscreen = is_fullscreen = check_fullscreen(app_state->main_window); // double-check just in case...
			if (ImGui::MenuItem("Fullscreen", "F11", &is_fullscreen)) {}
			if (ImGui::MenuItem("Image options...", NULL, &show_image_options_window)) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Show case list", NULL, &show_slide_list_window)) {}
			ImGui::Separator();

			if (ImGui::MenuItem("General options...", NULL, &show_general_options_window)) {}
			if (ImGui::BeginMenu("Debug")) {
				if (ImGui::MenuItem("Show console", "F3", &show_console_window)) {}
				if (ImGui::MenuItem("Show demo window", "F1", &show_demo_window)) {}
//				if (ImGui::MenuItem("Save XML annotations", NULL, &menu_items_clicked.save_annotations)) {}
				if (ImGui::MenuItem("Enable Vsync", NULL, &is_vsync_enabled)) {}
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
			open_file_dialog(app_state->main_window);
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
		} else if (menu_items_clicked.export_region_as_bigtiff || menu_items_clicked.export_region_as_jpeg || menu_items_clicked.export_region_as_png) {

			if (can_export) {
				image_t* image = app_state->loaded_images + 0;
				if (menu_items_clicked.export_region_as_bigtiff) {
					char filename[4096];
					if (save_file_dialog(app_state->main_window, filename, sizeof(filename), "BigTIFF\0*.tiff;*.tif;*.ptif\0All\0*.*\0Text\0*.TXT\0")) {
						export_cropped_bigtiff(app_state, image, &image->tiff.tiff, export_bounds, filename, 512, TIFF_PHOTOMETRIC_YCBCR, 80);
					}
				} else if (menu_items_clicked.export_region_as_jpeg) {

				} else if (menu_items_clicked.export_region_as_png) {

				}
			} else {
				ASSERT(!"Trying to export a region without a selected region");
			}


		}
	}

}

void gui_draw(app_state_t* app_state, input_t* input, i32 client_width, i32 client_height) {
	ImGuiIO &io = ImGui::GetIO();

	gui_want_capture_mouse = io.WantCaptureMouse;
	gui_want_capture_keyboard = io.WantCaptureKeyboard;

	if (!is_macos) gui_draw_main_menu_bar(app_state);

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
		ImGui::SetNextWindowSize(ImVec2(386,291), ImGuiCond_FirstUseEver);

		ImGui::Begin("Image options", &show_image_options_window);

        float zoom_objective_factor = 40.0f * exp2f(-app_state->scene.zoom.level);
        ImGui::Text("Current zoom level: %d (%gx)", app_state->scene.zoom.level, zoom_objective_factor);
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
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}

		ImGui::SliderFloat("Black level", &app_state->black_level, 0.0f,
		                   1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		ImGui::SliderFloat("White level", &app_state->white_level, 0.0f,
		                   1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f

		if (disable_gui) {
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
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

		gui_push_disabled_style(!can_move_left);
		if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
			if (caselist->cases && can_move_left) {
				app_state->selected_case_index = (--selected_case_index);
				app_state->selected_case = selected_case = caselist->cases + selected_case_index;
			}
		}
		gui_pop_disabled_style(!can_move_left);
		ImGui::SameLine();
		gui_push_disabled_style(!can_move_right);
		if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
			if (caselist->cases && can_move_right) {
				app_state->selected_case_index = (++selected_case_index);
				app_state->selected_case = selected_case = caselist->cases + selected_case_index;
			}
		}
		gui_pop_disabled_style(!can_move_right);
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
	annotation_modal_dialog(app_state, &app_state->scene.annotation_set);

	if (show_about_window) {
		ImGui::Begin("About Slideviewer", &show_about_window, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

		ImGui::TextUnformatted("Slideviewer - a whole-slide image viewer for digital pathology");
		ImGui::Text("Author: Pieter Valkema\n");
		ImGui::TextUnformatted("Version: " SLIDEVIEWER_VERSION );


		ImGui::Text("\nLicense information:\nThis program is free software: you can redistribute it and/or modify\n"
		            "  it under the terms of the GNU General Public License as published by\n"
		            "  the Free Software Foundation, either version 3 of the License, or\n"
		            "  (at your option) any later version.\n\n");
		if (ImGui::Button("View releases on GitHub")) {
#if WINDOWS
			ShellExecuteW(0, 0, L"https://github.com/Falcury/slideviewer/releases", 0, 0 , SW_SHOW );
#endif
		};


		ImGui::End();
	}

	if (show_console_window) {
		draw_console_window(app_state, "Console", &show_console_window);
	}



//}
//
//void gui_render(app_state_t* app_state, i32 client_width, i32 client_height) {

	// Rendering
	ImGui::Render();
	glViewport(0, 0, client_width, client_height);
//	glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
//	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());



}

struct console_log_item_t {
	char* text;
	bool has_color;
	u32 item_type;
};

console_log_item_t* console_log_items; //sb

void console_clear_log() {
	for (int i = 0; i < sb_count(console_log_items); i++) {
		console_log_item_t* item = console_log_items + i;
		if (item->text) {
			free(item->text);
		}
	}
	sb_free(console_log_items);
	console_log_items = NULL;
}


void draw_console_window(app_state_t* app_state, const char* window_title, bool* p_open) {

	static float desired_fraction_of_height = 0.33f;

	float desired_width = (float) app_state->client_viewport.w;
	float desired_height = roundf((float)app_state->client_viewport.h * desired_fraction_of_height);
	ImGui::SetNextWindowSize(ImVec2(desired_width, desired_height), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(0,app_state->client_viewport.h - desired_height), ImGuiCond_Always);
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
		ImGui::EndPopup();
	}
	i32 item_count = sb_count(console_log_items);
	if (item_count > 0) {
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
		ImGui::PushFont(global_fixed_width_font);
		ImGuiListClipper clipper;
		clipper.Begin(sb_count(console_log_items));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
				console_log_item_t item = console_log_items[i];
//				if (!Filter.PassFilter(item))
//					continue;

				// Normally you would store more information in your item than just a string.
				// (e.g. make Items[] an array of structure, store color/type etc.)
				ImVec4 color;
				if (item.has_color) {
					if (item.item_type == 1)          { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);}
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
	i64 num_lines = 0;
	char** lines = split_into_lines(raw, &num_lines);
	if (lines) {
		for (i32 i = 0; i < num_lines; ++i) {
			char* line = lines[i];
			if (line && strlen(line) > 0) {
				console_log_item_t new_item = {};
				new_item.text = strdup(line);
				new_item.has_color = has_color;
				new_item.item_type = item_type;
				sb_push(console_log_items, new_item);
			}
		}
		free(lines);
	}
}

void console_print(const char* fmt, ...) {

	char buf[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
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
	vsnprintf(buf, sizeof(buf), fmt, args);
	fprintf(stdout, "%s", buf);
	buf[sizeof(buf)-1] = 0;
	va_end(args);

	console_split_lines_and_add_log_item(buf, false, 0);
}


void console_print_error(const char* fmt, ...) {
	char buf[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	fprintf(stderr, "%s", buf);
	buf[sizeof(buf)-1] = 0;
	va_end(args);

	console_split_lines_and_add_log_item(buf, true, 1);
}


