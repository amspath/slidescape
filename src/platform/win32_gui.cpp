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

#include <windows.h>
#include "platform.h"
#include "win32_platform.h"

#include <glad/glad.h>

#include "imgui.h"
#include "misc/freetype/imgui_freetype.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include "win32_gui.h"
#include "gui.h"
#include "font_definitions.h"

void win32_gui_new_frame() {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}


void win32_init_gui(app_state_t* app_state) {

	imgui_create_context();
	ImGuiIO& io = ImGui::GetIO();
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
//	ImGui::StyleColorsLight();
//	ImGui::StyleColorsClassic();

	ImGuiStyle& style = ImGui::GetStyle();
	style.Alpha = 0.95f;
	style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);
	style.TouchExtraPadding = ImVec2(0.0f, 1.0f);

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(app_state->main_window);
	ImGui_ImplOpenGL3_Init(NULL, global_is_using_software_renderer ? "opengl32software.dll" : "opengl32.dll");


	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'docs/FONTS.txt' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	ImFontConfig font_config = ImFontConfig();
//	font_config.OversampleH = 3;
//	font_config.OversampleV = 2;
//	font_config.RasterizerMultiply = 1.2f;
	float system_font_size = 17.0f;
	static const ImWchar ranges[] =
	{
			0x0020, 0x00FF, // Basic Latin + Latin Supplement
			0x0370, 0x03FF, // Greek
			0,
	};
	const char* main_ui_font_filename = "c:\\Windows\\Fonts\\segoeui.ttf";
	if (file_exists(main_ui_font_filename)) {
		global_main_font = io.Fonts->AddFontFromFileTTF(main_ui_font_filename, system_font_size, &font_config, ranges);
	}
	if (!global_main_font) {
		console_print_error("Main UI font '%s' could not be loaded", main_ui_font_filename);
	}

	const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	const char* icon_font_filename = "resources/FontAwesome4/font.ttf";
	if (file_exists(icon_font_filename)) {
		global_icon_font = io.Fonts->AddFontFromFileTTF(icon_font_filename, 40.0f, &font_config, icon_ranges);
	}
	if (!global_icon_font) {
//		console_print_error("Icon font could not be loaded");
	}

	const char* fixed_width_font_filename = "c:\\Windows\\Fonts\\consola.ttf";
	if (file_exists(fixed_width_font_filename)) {
		global_fixed_width_font = io.Fonts->AddFontFromFileTTF(fixed_width_font_filename, 14.0f, &font_config, ranges);
	}
	if (!global_fixed_width_font) {
		console_print_error("Fixed width font '%s' could not be loaded", fixed_width_font_filename);
	}


	io.Fonts->AddFontDefault();
//	IM_ASSERT(font != NULL);

	io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_MonoHinting;
	io.Fonts->Build();


	// TODO: Windows high DPI code

	is_fullscreen = check_fullscreen(app_state->main_window);

}