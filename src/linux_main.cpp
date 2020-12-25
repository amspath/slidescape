// Dear ImGui: standalone example application for SDL2 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// (GL3W is a helper library to access OpenGL functions since there is no standard header to access modern OpenGL functions easily. Alternatives are GLEW, Glad, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "common.h"
#include "platform.h"
#include "viewer.h"
#include "gui.h" // TODO: move

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL2/SDL.h>

#include "imgui_freetype.h"

// About Desktop OpenGL function loaders:
//  Modern desktop OpenGL doesn't have a standard portable header file to load OpenGL function pointers.
//  Helper libraries are often used for this purpose! Here we are supporting a few common ones (gl3w, glew, glad).
//  You may use another loader/header of your choice (glext, glLoadGen, etc.), or chose to manually implement your own.
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#include <GL/gl3w.h>            // Initialize with gl3wInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#include <GL/glew.h>            // Initialize with glewInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#include <glad/glad.h>          // Initialize with gladLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
#include <glad/gl.h>            // Initialize with gladLoadGL(...) or gladLoaderLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
#define GLFW_INCLUDE_NONE       // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#include <glbinding/Binding.h>  // Initialize with glbinding::Binding::initialize()
#include <glbinding/gl/gl.h>
using namespace gl;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
#define GLFW_INCLUDE_NONE       // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#include <glbinding/glbinding.h>// Initialize with glbinding::initialize()
#include <glbinding/gl/gl.h>
using namespace gl;
#else
#include IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

#include <pthread.h>

void* worker_thread(void* parameter) {
    platform_thread_info_t* thread_info = (platform_thread_info_t*) parameter;

//	fprintf(stderr, "Hello from thread %d\n", thread_info->logical_thread_index);

    // Allocate a private memory buffer
    u64 thread_memory_size = MEGABYTES(16);
    thread_local_storage[thread_info->logical_thread_index] = platform_alloc(thread_memory_size); // how much actually needed?
    thread_memory_t* thread_memory = (thread_memory_t*) thread_local_storage[thread_info->logical_thread_index];
    memset(thread_memory, 0, sizeof(thread_memory_t));
#if 0
    // TODO: implement this
	thread_memory->async_io_event = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (!thread_memory->async_io_event) {
		win32_diagnostic("CreateEvent");
	}
#endif
    thread_memory->thread_memory_raw_size = thread_memory_size;

    thread_memory->aligned_rest_of_thread_memory = (void*)
            ((((u64)thread_memory + sizeof(thread_memory_t) + os_page_size - 1) / os_page_size) * os_page_size); // round up to next page boundary
    thread_memory->thread_memory_usable_size = thread_memory_size - ((u64)thread_memory->aligned_rest_of_thread_memory - (u64)thread_memory);

    for (;;) {
        if (!is_queue_work_in_progress(thread_info->queue)) {
            platform_sleep(1);
            sem_wait(thread_info->queue->semaphore);
        }
        do_worker_work(thread_info->queue, thread_info->logical_thread_index);
    }

    return 0;
}

platform_thread_info_t thread_infos[MAX_THREAD_COUNT];

void linux_init_multithreading() {
    i32 semaphore_initial_count = 0;
    worker_thread_count = total_thread_count - 1;
    global_work_queue.semaphore = sem_open("/worksem", O_CREAT, 0644, semaphore_initial_count);
    global_completion_queue.semaphore = sem_open("/completionsem", O_CREAT, 0644, semaphore_initial_count);

    pthread_t threads[MAX_THREAD_COUNT] = {};

    // NOTE: the main thread is considered thread 0.
    for (i32 i = 1; i < total_thread_count; ++i) {
        thread_infos[i] = (platform_thread_info_t){ .logical_thread_index = i, .queue = &global_work_queue};

        if (pthread_create(threads + i, NULL, &worker_thread, (void*)(&thread_infos[i])) != 0) {
            fprintf(stderr, "Error creating thread\n");
        }

    }

    test_multithreading_work_queue();


}

void linux_init_input() {
    old_input = &inputs[0];
    curr_input = &inputs[1];
}

void linux_process_button_event(button_state_t* new_state, bool32 down) {
    down = (down != 0);
    if (new_state->down != down) {
        new_state->down = (bool8)down;
        ++new_state->transition_count;
    }
}

bool linux_process_input() {
    // Swap
    input_t* temp = old_input;
    old_input = curr_input;
    curr_input = temp;


    // reset the transition counts.
    // TODO: can't we just do that, instead of reinitializing the reset?


    curr_input->drag_start_xy = old_input->drag_start_xy;
    curr_input->drag_vector = old_input->drag_vector;

    ImGuiIO& io = ImGui::GetIO();
    curr_input->mouse_xy = io.MousePos;

    u32 button_count = MIN(COUNT(curr_input->mouse_buttons), COUNT(io.MouseDown));
    memset_zero(&curr_input->mouse_buttons);
    for (u32 i = 0; i < button_count; ++i) {
        curr_input->mouse_buttons[i].down = old_input->mouse_buttons[i].down;
        linux_process_button_event(&curr_input->mouse_buttons[i], io.MouseDown[i]);
    }

    memset_zero(&curr_input->keyboard);
    for (u32 i = 0; i < COUNT(curr_input->keyboard.buttons); ++i) {
        curr_input->keyboard.buttons[i].down = old_input->keyboard.buttons[i].down;

    }
    u32 key_count = MIN(COUNT(curr_input->keyboard.keys), COUNT(io.KeysDown));
    for (u32 i = 0; i < key_count; ++i) {
        curr_input->keyboard.keys[i].down = old_input->keyboard.keys[i].down;
        linux_process_button_event(&curr_input->keyboard.keys[i], io.KeysDown[i]);
    }

    curr_input->keyboard.key_shift.down = old_input->keyboard.key_shift.down;
    curr_input->keyboard.key_ctrl.down = old_input->keyboard.key_ctrl.down;
    curr_input->keyboard.key_alt.down = old_input->keyboard.key_alt.down;
    curr_input->keyboard.key_super.down = old_input->keyboard.key_super.down;
    linux_process_button_event(&curr_input->keyboard.key_shift, io.KeyShift);
    linux_process_button_event(&curr_input->keyboard.key_ctrl, io.KeyCtrl);
    linux_process_button_event(&curr_input->keyboard.key_alt, io.KeyAlt);
    linux_process_button_event(&curr_input->keyboard.key_super, io.KeySuper);

	curr_input->mouse_z = io.MouseWheel;

    v2f mouse_delta = io.MouseDelta;
//    mouse_delta.x *= window_scale_factor;
//    mouse_delta.y *= window_scale_factor;
    curr_input->drag_vector = mouse_delta;

    curr_input->are_any_buttons_down = false;
    for (u32 i = 0; i < COUNT(curr_input->keyboard.buttons); ++i) {
        curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->keyboard.buttons[i].down;
    }
    for (u32 i = 0; i < COUNT(curr_input->keyboard.keys); ++i) {
        curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->keyboard.keys[i].down;
    }
    for (u32 i = 0; i < COUNT(curr_input->mouse_buttons); ++i) {
        curr_input->are_any_buttons_down = (curr_input->are_any_buttons_down) || curr_input->mouse_buttons[i].down;
    }


    bool did_idle = false;
    return did_idle;
}

extern SDL_Window* g_window;

// Main code
int main(int argc, const char** argv)
{

    g_argc = argc;
    g_argv = argv;
    console_print("Starting up...\n");
    get_system_info();
    linux_init_multithreading();
    linux_init_input();

    // Setup SDL
    // (Some versions of SDL before <2.0.10 appears to have performance/stalling issues on a minority of Windows systems,
    // depending on whether SDL_INIT_GAMECONTROLLER is enabled or disabled.. updating to latest version of SDL is recommended!)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        console_print_error("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Decide GL+GLSL versions
#ifdef __APPLE__
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);
    SDL_Window* window = SDL_CreateWindow("Slideviewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    g_window = window;
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);

    char* version_string = (char*)glGetString(GL_VERSION);
    console_print("OpenGL supported version: %s\n", version_string);

    is_vsync_enabled = 0;
    SDL_GL_SetSwapInterval(is_vsync_enabled); // Enable vsync

    // Initialize OpenGL loader
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
    bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
    bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
    bool err = gladLoadGL() == 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
    bool err = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress) == 0; // glad2 recommend using the windowing library loader instead of the (optionally) bundled one.
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
    bool err = false;
    glbinding::Binding::initialize();
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
    bool err = false;
    glbinding::initialize([](const char* name) { return (glbinding::ProcAddress)SDL_GL_GetProcAddress(name); });
#else
    bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	global_main_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/noto/NotoSans-Regular.ttf", 17.0f);
	if (!global_main_font) {
		global_main_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16.0f);
	}
	global_fixed_width_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/noto/NotoMono-Regular.ttf/NotoMono-Regular.ttf", 15.0f);
	if (!global_fixed_width_font) {
		global_fixed_width_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/noto/NotoMono-Regular.ttf", 15.0f);
		if (!global_fixed_width_font) {
			global_fixed_width_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 15.0f);
		}
	}
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    io.Fonts->AddFontDefault();

	unsigned int flags = ImGuiFreeType::MonoHinting;
	ImGuiFreeType::BuildFontAtlas(io.Fonts, flags);

    // Our state
//    bool show_demo_window = true;
//    bool show_another_window = false;
//    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);



    app_state_t* app_state = &global_app_state;
    init_app_state(app_state, window);
    init_opengl_stuff(app_state);

    // Load a slide from the command line or through the OS (double-click / drag on executable, etc.)
    if (g_argc > 1) {
        const char* filename = g_argv[1];
        load_generic_file(app_state, filename);
    }

    // Main loop
    is_program_running = true;
    i64 last_clock = get_clock();
    while (is_program_running)
    {
        i64 current_clock = get_clock();
        app_state->last_frame_start = current_clock;
        float delta_t = (float)(current_clock - last_clock) / (float)1e9;
//        printf("delta_t = %g, clock = %lld\n", delta_t, current_clock);
        last_clock = current_clock;
        delta_t = CLAMP(delta_t, 0.00001f, 2.0f / 60.0f); // prevent physics overshoot at lag spikes

        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                is_program_running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                is_program_running = false;
        }

#if 1
        linux_process_input();

	    if (was_key_pressed(curr_input, KEY_F4) && curr_input->keyboard.key_alt.down) {
		    is_program_running = false;
	    }
	    if (was_key_pressed(curr_input, KEY_O) && curr_input->keyboard.key_ctrl.down) {
		    open_file_dialog(app_state);
	    }
	    if (was_key_pressed(curr_input, KEY_F11)) {
		    toggle_fullscreen(app_state->main_window);
	    }

        int w, h;
        int display_w, display_h;
        SDL_GetWindowSize(window, &w, &h);
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
            w = h = 0;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(g_window);
        ImGui::NewFrame();

        // Update and render our application
        viewer_update_and_render(app_state, curr_input, display_w, display_h, delta_t);

        // Finish up by rendering the UI
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);

        float frame_time = get_seconds_elapsed(last_clock, get_clock());

        float target_frame_time = 0.002f;
        float time_to_sleep = target_frame_time - frame_time;
        if (time_to_sleep > 0) {
	        platform_sleep_ns((i64)(time_to_sleep * 1e9));
        }

//	    printf("Frame time: %g\n", get_seconds_elapsed(last_clock, get_clock()));

#else
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
#endif
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
