#include "common.h"

#include "stdio.h"

#include <windows.h>
#include "platform.h"
#include "win32_main.h"

#include <glad/glad.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include "openslide_api.h"
#include "viewer.h"
#include "tlsclient.h"

#define GUI_IMPL
#include "gui.h"


void do_gui(i32 client_width, i32 client_height) {
	ImGuiIO& io = ImGui::GetIO();

	gui_want_capture_mouse = io.WantCaptureMouse;
	gui_want_capture_keyboard = io.WantCaptureKeyboard;


	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	if (ImGui::BeginMainMenuBar())
	{
		static struct {
			bool open_file;
			bool open_remote;
			bool exit_program;
		} menu_items_clicked;
		memset(&menu_items_clicked, 0, sizeof(menu_items_clicked));

		bool prev_fullscreen = is_fullscreen;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open...", "Ctrl+O", &menu_items_clicked.open_file)) {}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Alt+F4", &menu_items_clicked.exit_program)) {}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			prev_fullscreen = is_fullscreen = win32_is_fullscreen(main_window); // double-check just in case...
			if (ImGui::MenuItem("Fullscreen", "F11", &is_fullscreen)) {}
			if (ImGui::MenuItem("Image adjustments...", NULL, &show_image_adjustments_window)) {}
			ImGui::Separator();

			if (ImGui::MenuItem("Options...", NULL, &show_display_options_window)) {}
			if (ImGui::BeginMenu("Debug"))
			{
				if (ImGui::MenuItem("Demo window", "F1", &show_demo_window)) {}
				if (ImGui::MenuItem("Open remote", NULL, &menu_items_clicked.open_remote)) {}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();

		if (menu_items_clicked.exit_program) {
			is_program_running = false;
		} else if (menu_items_clicked.open_file) {
			win32_open_file_dialog(main_window);
		} else if (menu_items_clicked.open_remote) {
//			open_remote_slide("google.com", 443, "/");
//			open_remote_slide("ectopic.tech", 2000, "");
			open_remote_slide("ectopic.tech", 2000, "sample.tiff");
//			open_remote_slide("localhost", 2000, "sample.tiff");
		}
		else if (prev_fullscreen != is_fullscreen) {
			bool currently_fullscreen = win32_is_fullscreen(main_window);
			if (currently_fullscreen != is_fullscreen) {
				win32_toggle_fullscreen(main_window);
			}
		}
	}


	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	if (show_demo_window)
		ImGui::ShowDemoWindow(&show_demo_window);

	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
	if (show_image_adjustments_window) {
		static int counter = 0;

		ImGui::SetNextWindowPos(ImVec2(25, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(360, 200), ImGuiCond_FirstUseEver);

		ImGui::Begin("Image adjustments", &show_image_adjustments_window);                          // Create a window called "Hello, world!" and append into it.

//		ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
		ImGui::Checkbox("Use image adjustments", &use_image_adjustments);

		ImGui::SliderFloat("black level", &black_level, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		ImGui::SliderFloat("white level", &white_level, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f

		ImGui::Text("\nBackground color");               // Display some text (you can use a format strings too)
		ImGui::ColorEdit3("color", (float*)&clear_color); // Edit 3 floats representing a color

//		if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
//			counter++;
//		ImGui::SameLine();
//		ImGui::Text("counter = %d", counter);
//
//		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
	}

	if (show_display_options_window) {

		ImGui::SetNextWindowPos(ImVec2(120, 100), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_FirstUseEver);

		ImGui::Begin("Display options", &show_display_options_window);



		ImGui::Text("User interface colors");               // Display some text (you can use a format strings too)
		static int style_color = 1;
		int old_style_color = style_color;
		ImGui::RadioButton("Light", &style_color, 0); ImGui::SameLine();
		ImGui::RadioButton("Dark", &style_color, 1);

		if (style_color != old_style_color) {
			if (style_color == 0) {
				ImGui::StyleColorsLight();
			} else if (style_color == 1) {
				ImGui::StyleColorsDark();
			}
		}

		ImGui::End();
	}


	// Rendering
	ImGui::Render();
	glViewport(0, 0, client_width, client_height);
//	glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
//	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());



}

void win32_init_gui(HWND hwnd) {

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
//	ImGui::StyleColorsLight();
//	ImGui::StyleColorsClassic();

	ImGuiStyle& style = ImGui::GetStyle();
	style.Alpha = 0.95f;

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplOpenGL3_Init(NULL);

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
	ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 17.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	if (!font) {
		// could not load font
	}
	io.Fonts->AddFontDefault();
//	IM_ASSERT(font != NULL);

	is_fullscreen = win32_is_fullscreen(main_window);

}