#define _WIN32_WINNT 0x0600
#define WINVER 0x0600

#include "common.h"
#include "platform.h"
#include "intrinsics.h"
#include "openslide_api.h"
#include "wsi.h"

#include <stdio.h>
#include <sys/stat.h>

//#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <glad/glad.h>

bool32 is_program_running;
bool32 show_cursor;

void toggle_fullscreen(SDL_Window* window) {
	uint32_t flags = SDL_GetWindowFlags(window);
	if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
		SDL_SetWindowFullscreen(window, 0);
//		SDL_ShowCursor(SDL_ENABLE);
	}
	else {
		SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
//		SDL_ShowCursor(SDL_DISABLE);
	}
}


int main(int argc, char** argv) {
	g_argc = argc;
	g_argv = argv;

	// Setup SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_NOPARACHUTE) != 0)
	{
		printf("Error: %s\n", SDL_GetError());
		return -1;
	}

	init_timer();

	// TODO: set up multithreading

	i32 context_flags = 0;
#if DO_DEBUG
	context_flags |= SDL_GL_CONTEXT_DEBUG_FLAG;
#endif
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, context_flags);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	// Create window with graphics context
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	SDL_Window* window = SDL_CreateWindow("Slideviewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	bool32 err = gladLoadGL() == 0;
	if (err) {
		fprintf(stderr, "Failed to initialize OpenGL loader!\n");
		return 1;
	}

	is_openslide_available = init_openslide();
	write_barrier;
	is_openslide_loading_done = true;

	if (g_argc > 1) {
		char* filename = g_argv[1];
		wsi_t wsi = {};
		load_wsi(&wsi, filename);
		if (wsi.osr) {
			printf("WSI succesfully loaded\n");
		}
	}

	is_program_running = true;
	while(is_program_running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT) {
				is_program_running = false;
			}

			if (event.type == SDL_KEYDOWN) {
				int modifier = event.key.keysym.mod;
				int scancode = event.key.keysym.scancode;
				switch (event.key.keysym.sym) {
					case SDLK_ESCAPE:
						is_program_running = false;
						break;
					case SDLK_r:
						glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
						break;
					case SDLK_g:
						glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
						break;
					case SDLK_b:
						glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
						break;
					default:
						break;
				}

				if (scancode == SDL_SCANCODE_RETURN && (modifier & KMOD_ALT)) {
					toggle_fullscreen(window);
				}
			}

		}

		glClear(GL_COLOR_BUFFER_BIT);
		SDL_GL_SwapWindow(window);
	}



	return 0;
}
