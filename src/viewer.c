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

#include "win32_main.h"
#include "platform.h"
#include "intrinsics.h"
#include "stringutils.h"

#include "openslide_api.h"
#include <glad/glad.h>
#include <linmath.h>
#include <io.h>

#include "arena.h"
#include "arena.c"

#define VIEWER_IMPL
#include "viewer.h"

#define STBI_ASSERT(x) ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "tiff.h"
#include "jpeg_decoder.h"
#include "tlsclient.h"
#include "gui.h"
#include "caselist.h"
#include "annotation.h"
#include "shader.h"

#include "viewer_opengl.c"
#include "viewer_io_file.c"
#include "viewer_io_remote.c"
#include "viewer_zoom.c"

tile_t* get_tile(level_image_t* image_level, i32 tile_x, i32 tile_y) {
	i32 tile_index = tile_y * image_level->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < image_level->tile_count);
	tile_t* result = image_level->tiles + tile_index;
	return result;
}

u32 get_texture_for_tile(image_t* image, i32 level, i32 tile_x, i32 tile_y) {
	level_image_t* level_image = image->level_images + level;

	i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
	ASSERT(tile_index >= 0 && tile_index < level_image->tile_count);
	tile_t* tile = level_image->tiles + tile_index;

	return tile->texture;
}

void unload_image(image_t* image) {
	if (image) {
		if (image->type == IMAGE_TYPE_WSI) {
			unload_wsi(&image->wsi.wsi);
		} else if (image->type == IMAGE_TYPE_SIMPLE) {
			if (image->simple.pixels) {
				stbi_image_free(image->simple.pixels);
				image->simple.pixels = NULL;
			}
			if (image->simple.texture != 0) {
				unload_texture(image->simple.texture);
				image->simple.texture = 0;
			}
		} else if (image->type == IMAGE_TYPE_TIFF) {
			tiff_destroy(&image->tiff.tiff);
		}

		for (i32 i = 0; i < image->level_count; ++i) {
			level_image_t* level_image = image->level_images + i;
			if (level_image->tiles) {
				for (i32 j = 0; j < level_image->tile_count; ++j) {
					tile_t* tile = level_image->tiles + j;
					if (tile->texture != 0) {
						unload_texture(tile->texture);
					}
				}
			}
			free(level_image->tiles);
			level_image->tiles = NULL;
		}


	}
}

void unload_all_images(app_state_t *app_state) {
	i32 current_image_count = sb_count(app_state->loaded_images);
	if (current_image_count > 0) {
		ASSERT(app_state->loaded_images);
		for (i32 i = 0; i < current_image_count; ++i) {
			image_t* old_image = app_state->loaded_images + i;
			unload_image(old_image);
		}
		sb_free(app_state->loaded_images);
		app_state->loaded_images = NULL;
	}
	mouse_show();
	app_state->scene.is_cropped = false;
	app_state->scene.has_selection_box = false;
}

void add_image_from_tiff(app_state_t* app_state, tiff_t tiff) {
	image_t new_image = (image_t){};
	new_image.type = IMAGE_TYPE_TIFF;
	new_image.tiff.tiff = tiff;
	new_image.is_freshly_loaded = true;
	new_image.mpp_x = tiff.mpp_x;
	new_image.mpp_y = tiff.mpp_y;
	ASSERT(tiff.main_image_ifd);
	new_image.tile_width = tiff.main_image_ifd->tile_width;
	new_image.tile_height = tiff.main_image_ifd->tile_height;
	new_image.width_in_pixels = tiff.main_image_ifd->image_width;
	new_image.width_in_um = tiff.main_image_ifd->image_width * tiff.mpp_x;
	new_image.height_in_pixels = tiff.main_image_ifd->image_height;
	new_image.height_in_um = tiff.main_image_ifd->image_height * tiff.mpp_y;
	// TODO: fix code duplication with tiff_deserialize()
	if (tiff.level_image_ifd_count > 0 && tiff.main_image_ifd->tile_width) {

		memset(new_image.level_images, 0, sizeof(new_image.level_images));
		new_image.level_count = tiff.max_downsample_level + 1;

		if (tiff.level_image_ifd_count > new_image.level_count) {
			panic();
		}
		if (new_image.level_count > WSI_MAX_LEVELS) {
			panic();
		}

		i32 ifd_index = 0;
		i32 next_ifd_index_to_check_for_match = 0;
		tiff_ifd_t* ifd = tiff.level_images_ifd + ifd_index;
		for (i32 level_index = 0; level_index < new_image.level_count; ++level_index) {
			level_image_t* level_image = new_image.level_images + level_index;

			i32 wanted_downsample_level = level_index;
			bool found_ifd = false;
			for (ifd_index = next_ifd_index_to_check_for_match; ifd_index < tiff.level_image_ifd_count; ++ifd_index) {
				ifd = tiff.level_images_ifd + ifd_index;
				if (ifd->downsample_level == wanted_downsample_level) {
					// match!
					found_ifd = true;
					next_ifd_index_to_check_for_match = ifd_index + 1; // next iteration, don't reuse the same IFD!
					break;
				}
			}

			if (found_ifd) {
				// The current downsampling level is backed by a corresponding IFD level image in the TIFF.
				level_image->exists = true;
				level_image->pyramid_image_index = ifd_index;
				level_image->downsample_factor = ifd->downsample_factor;
				level_image->tile_count = ifd->tile_count;
				level_image->width_in_tiles = ifd->width_in_tiles;
				level_image->height_in_tiles = ifd->height_in_tiles;
				level_image->tile_width = ifd->tile_width;
				level_image->tile_height = ifd->tile_height;
#if DO_DEBUG
				if (level_image->tile_width != new_image.tile_width) {
					printf("Warning: level image %d (ifd #%d) tile width (%d) does not match base level (%d)\n", level_index, ifd_index, level_image->tile_width, new_image.tile_width);
				}
				if (level_image->tile_height != new_image.tile_height) {
					printf("Warning: level image %d (ifd #%d) tile width (%d) does not match base level (%d)\n", level_index, ifd_index, level_image->tile_width, new_image.tile_width);
				}
#endif
				level_image->um_per_pixel_x = ifd->um_per_pixel_x;
				level_image->um_per_pixel_y = ifd->um_per_pixel_y;
				level_image->x_tile_side_in_um = ifd->x_tile_side_in_um;
				level_image->y_tile_side_in_um = ifd->y_tile_side_in_um;
				level_image->tiles = (tile_t*) calloc(1, ifd->tile_count * sizeof(tile_t));
				ASSERT(ifd->tile_byte_counts != NULL);
				ASSERT(ifd->tile_offsets != NULL);
				// mark the empty tiles, so that we can skip loading them later on
				for (i32 i = 0; i < level_image->tile_count; ++i) {
					tile_t* tile = level_image->tiles + i;
					u64 tile_byte_count = ifd->tile_byte_counts[i];
					if (tile_byte_count == 0) {
						tile->is_empty = true;
					}
				}
			} else {
				// The current downsampling level has no corresponding IFD level image :(
				// So we need only some placeholder information.
				level_image->exists = false;
				level_image->downsample_factor = exp2f((float)wanted_downsample_level);
				// Just in case anyone tries to divide by zero:
				level_image->tile_width = new_image.tile_width;
				level_image->tile_height = new_image.tile_height;
				level_image->um_per_pixel_x = new_image.mpp_x * level_image->downsample_factor;
				level_image->um_per_pixel_y = new_image.mpp_y * level_image->downsample_factor;
				level_image->x_tile_side_in_um = ifd->um_per_pixel_x * (float)tiff.main_image_ifd->tile_width;
				level_image->y_tile_side_in_um = ifd->um_per_pixel_y * (float)tiff.main_image_ifd->tile_height;
			}

			DUMMY_STATEMENT;


		}
	}
	sb_push(app_state->loaded_images, new_image);
}

bool32 was_button_pressed(button_state_t* button) {
	bool32 result = button->down && button->transition_count > 0;
	return result;
}

bool32 was_button_released(button_state_t* button) {
	bool32 result = (!button->down) && button->transition_count > 0;
	return result;
}

bool32 was_key_pressed(input_t* input, i32 keycode) {
	u8 key = keycode & 0xFF;
	button_state_t* button = &input->keyboard.keys[key];
	bool32 result = was_button_pressed(button);
	return result;
}

bool32 is_key_down(input_t* input, i32 keycode) {
	u8 key = keycode & 0xFF;
	bool32 result = input->keyboard.keys[key].down;
	return result;
}

int priority_cmp_func (const void* a, const void* b) {
	return ( (*(load_tile_task_t*)b).priority - (*(load_tile_task_t*)a).priority );
}

void init_scene(app_state_t *app_state, scene_t *scene) {
	memset(scene, 0, sizeof(scene_t));
	scene->clear_color = app_state->clear_color;
	scene->entity_count = 1; // NOTE: entity 0 = null entity, so start from 1
	scene->camera = (v2f){0.0f, 0.0f}; // center camera at origin
	init_zoom_state(&scene->zoom, 0.0f, 1.0f, 1.0f, 1.0f);
	scene->initialized = true;
}

void init_app_state(app_state_t* app_state) {
	ASSERT(!app_state->initialized); // check sanity
	ASSERT(app_state->temp_storage_memory == NULL);
	memset(app_state, 0, sizeof(app_state_t));

	size_t temp_storage_size = MEGABYTES(16); // Note: what is a good size to use here?
	app_state->temp_storage_memory = platform_alloc(temp_storage_size);
	init_arena(&app_state->temp_arena, temp_storage_size, app_state->temp_storage_memory);

	app_state->clear_color = (v4f){0.95f, 0.95f, 0.95f, 1.00f};
	app_state->black_level = 0.10f;
	app_state->white_level = 0.95f;
	app_state->use_builtin_tiff_backend = true; // If disabled, revert to OpenSlide when loading TIFF files.

	for (i32 i = 0; i < COUNT(app_state->pixel_transfer_states); ++i) {
		pixel_transfer_state_t* transfer_state = app_state->pixel_transfer_states + i;
		u32 pbo = 0;
		glGenBuffers(1, &pbo);
		transfer_state->pbo = pbo;
		transfer_state->initialized = true;
	}

	app_state->initialized = true;
}

void autosave(app_state_t* app_state, bool force_ignore_delay) {
	annotation_set_t* annotation_set = &app_state->scene.annotation_set;
	autosave_annotations(app_state, annotation_set, force_ignore_delay);
}


void request_tiles(app_state_t* app_state, image_t* image, load_tile_task_t* wishlist, i32 tiles_to_load) {
	if (tiles_to_load > 0){
		app_state->allow_idling_next_frame = false;



		if (image->type == IMAGE_TYPE_TIFF && image->tiff.tiff.is_remote) {
			// For remote slides, only send out a batch request every so often, instead of single tile requests every frame.
			// (to reduce load on the server)
			static u32 intermittent = 0;
			++intermittent;
			u32 intermittent_interval = 1;
			intermittent_interval = 5; // reduce load on remote server; can be tweaked
			if (intermittent % intermittent_interval == 0) {
				load_tile_task_batch_t* batch = (load_tile_task_batch_t*) calloc(1, sizeof(load_tile_task_batch_t));
				batch->task_count = ATMOST(COUNT(batch->tile_tasks), tiles_to_load);
				memcpy(batch->tile_tasks, wishlist, batch->task_count * sizeof(load_tile_task_t));
				if (add_work_queue_entry(&work_queue, tiff_load_tile_batch_func, batch)) {
					// success
					for (i32 i = 0; i < batch->task_count; ++i) {
						load_tile_task_t* task = batch->tile_tasks + i;
						tile_t* tile = task->tile;
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task->need_gpu_residency;
						tile->need_keep_in_cache = task->need_keep_in_cache;
					}
				}
			}
		} else {
			// regular file loading
			for (i32 i = 0; i < tiles_to_load; ++i) {
				load_tile_task_t* task = (load_tile_task_t*) malloc(sizeof(load_tile_task_t)); // should be freed after uploading the tile to the gpu
				*task = wishlist[i];

				tile_t* tile = task->tile;
				if (tile->is_cached && tile->texture == 0 && task->need_gpu_residency) {
					// only GPU upload needed
					if (add_work_queue_entry(&thread_message_queue, viewer_upload_already_cached_tile_to_gpu, task)) {
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task->need_gpu_residency;
						tile->need_keep_in_cache = task->need_keep_in_cache;
					}
				} else {
					if (add_work_queue_entry(&work_queue, load_tile_func, task)) {
						// TODO: should we even allow this to fail?
						// success
						tile->is_submitted_for_loading = true;
						tile->need_gpu_residency = task->need_gpu_residency;
						tile->need_keep_in_cache = task->need_keep_in_cache;
					}
				}


			}
		}


	}
}


// TODO: refactor delta_t
// TODO: think about having access to both current and old input. (for comparing); is transition count necessary?
void viewer_update_and_render(app_state_t *app_state, input_t *input, i32 client_width, i32 client_height, float delta_t) {

	i64 last_section = get_clock(); // start profiler section

	if (!app_state->initialized) init_app_state(app_state);
	// Note: the window might get resized, so need to update this every frame
	app_state->client_viewport = (rect2i){0, 0, client_width, client_height};

	scene_t* scene = &app_state->scene;
	if (!scene->initialized) init_scene(app_state, scene);

	// Note: could be changed to allow e.g. multiple scenes side by side
	scene->viewport = app_state->client_viewport;

	// TODO: this is part of rendering and doesn't belong here
	gui_new_frame();

	// Set up rendering state for the next frame
	glDrawBuffer(GL_BACK);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glViewport(0, 0, client_width, client_height);
	glClearColor(app_state->clear_color.r, app_state->clear_color.g, app_state->clear_color.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	last_section = profiler_end_section(last_section, "viewer_update_and_render: new frame", 20.0f);

	app_state->allow_idling_next_frame = true; // but we might set it to false later

	i32 image_count = sb_count(app_state->loaded_images);
	ASSERT(image_count >= 0);

	if (image_count == 0) {
		goto after_scene_render;
	}

	image_t* image = app_state->loaded_images + app_state->displayed_image;



	// TODO: mutate state here

	// todo: process even more of the mouse/keyboard input here?
	v2i current_drag_vector = {};
	scene->clicked = false;
	scene->drag_started = false;
	scene->drag_ended = false;
	if (input) {
		if (input->are_any_buttons_down) app_state->allow_idling_next_frame = false;

		if (was_key_pressed(input, 'W') && is_key_down(input, KEYCODE_CONTROL)) {
			menu_close_file(app_state);
			return;
		}

		if (was_button_released(&input->mouse_buttons[0])) {
			float drag_distance = v2i_distance(scene->cumulative_drag_vector);
			// TODO: tweak this
			if (drag_distance < 2.0f) {
				scene->clicked = true;
			}
		}

		if (input->mouse_buttons[0].down) {
			// Mouse drag.
			if (input->mouse_buttons[0].transition_count != 0) {
				// Don't start dragging if clicked outside the window
				rect2i valid_drag_start_rect = {0, 0, client_width, client_height};
				if (is_point_inside_rect2i(valid_drag_start_rect, input->mouse_xy)) {
					scene->is_dragging = true; // drag start
					scene->drag_started = true;
					scene->cumulative_drag_vector = (v2i){};
//					printf("Drag started: x=%d y=%d\n", input->mouse_xy.x, input->mouse_xy.y);
				}
			} else if (scene->is_dragging) {
				// already started dragging on a previous frame
				current_drag_vector = input->drag_vector;
				scene->cumulative_drag_vector.x += current_drag_vector.x;
				scene->cumulative_drag_vector.y += current_drag_vector.y;
			}
			input->drag_vector = (v2i){};
			mouse_hide();
		} else {
			if (input->mouse_buttons[0].transition_count != 0) {
				mouse_show();
				scene->is_dragging = false;
				scene->drag_ended = true;
//			        printf("Drag ended: dx=%d dy=%d\n", input->drag_vector.x, input->drag_vector.y);
			}
		}
	}

	last_section = profiler_end_section(last_section, "viewer_update_and_render: process input (1)", 5.0f);


	if (image->type == IMAGE_TYPE_SIMPLE) {
		// Display a basic image

		float display_pos_x = 0.0f;
		float display_pos_y = 0.0f;

		float L = display_pos_x;
		float R = display_pos_x + client_width;
		float T = display_pos_y;
		float B = display_pos_y + client_height;
		mat4x4 ortho_projection =
				{
						{ 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
						{ 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
						{ 0.0f,         0.0f,        -1.0f,   0.0f },
						{ (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
				};

		// Set up model matrix: scale and translate to the correct world position
		static v2f obj_pos;
		if (image->is_freshly_loaded) {
			obj_pos = (v2f) {50, 100};
			image->is_freshly_loaded = false;
		}
		float pan_multiplier = 2.0f;
		if (scene->is_dragging) {
			obj_pos.x += current_drag_vector.x * pan_multiplier;
			obj_pos.y += current_drag_vector.y * pan_multiplier;
		}

		mat4x4 model_matrix;
		mat4x4_identity(model_matrix);
		mat4x4_translate_in_place(model_matrix, obj_pos.x, obj_pos.y, 0.0f);
		mat4x4_scale_aniso(model_matrix, model_matrix, image->simple.width * 2, image->simple.height * 2, 1.0f);


		glUseProgram(basic_shader);
		glUniform1i(basic_shader_u_tex, 0);

		if (app_state->use_image_adjustments) {
			glUniform1f(basic_shader_u_black_level, app_state->black_level);
			glUniform1f(basic_shader_u_white_level, app_state->white_level);
		} else {
			glUniform1f(basic_shader_u_black_level, 0.0f);
			glUniform1f(basic_shader_u_white_level, 1.0f);
		}

		glUniformMatrix4fv(basic_shader_u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);

		v2f* view_pos = &simple_view_pos;

		mat4x4 view_matrix;
		mat4x4_identity(view_matrix);
		mat4x4_translate_in_place(view_matrix, -view_pos->x, -view_pos->y, 0.0f);
		mat4x4_scale_aniso(view_matrix, view_matrix, 0.5f, 0.5f, 1.0f);

		mat4x4 projection_view_matrix;
		mat4x4_mul(projection_view_matrix, ortho_projection, view_matrix);

		glUniformMatrix4fv(basic_shader_u_projection_view_matrix, 1, GL_FALSE, &projection_view_matrix[0][0]);

		// todo: bunch up vertex and index uploads

		draw_rect(image->simple.texture);
	}
	else if (image->type == IMAGE_TYPE_TIFF || image->type == IMAGE_TYPE_WSI) {

		if (image->is_freshly_loaded) {
			float times_larger_x = (float)image->width_in_pixels / (float)client_width;
			float times_larger_y = (float)image->height_in_pixels / (float)client_height;
			float times_larger = MAX(times_larger_x, times_larger_y);
			float desired_zoom_pos = ceilf(log2f(times_larger * 1.5f));

			init_zoom_state(&scene->zoom, desired_zoom_pos, 1.0f, image->mpp_x, image->mpp_y);
			scene->camera.x = image->width_in_um / 2.0f;
			scene->camera.y = image->height_in_um / 2.0f;

			image->is_freshly_loaded = false;
		}

		zoom_state_t old_zoom = scene->zoom;

		i32 max_level = 10;//image->level_count - 1;

		float r_minus_l = scene->zoom.pixel_width * (float) client_width;
		float t_minus_b = scene->zoom.pixel_height * (float) client_height;

		bounds2f camera_bounds = bounds_from_center_point(scene->camera, r_minus_l, t_minus_b);

		scene->mouse = scene->camera;

		if (input) {

			scene->mouse.x = camera_bounds.min.x + (float)input->mouse_xy.x * scene->zoom.pixel_width;
			scene->mouse.y = camera_bounds.min.y + (float)input->mouse_xy.y * scene->zoom.pixel_height;

			i32 dlevel = 0;
			bool32 used_mouse_to_zoom = false;

			// Zoom in or out using the mouse wheel.
			if (input->mouse_z != 0) {
				dlevel = (input->mouse_z > 0 ? -1 : 1);
				used_mouse_to_zoom = true;
			}

			float key_repeat_interval = 0.15f; // in seconds

			// Zoom out using Z or /
			if (is_key_down(input, 'Z') || is_key_down(input, KEYCODE_OEM_2 /* '/' */)) {

				if (was_key_pressed(input, 'Z') || was_key_pressed(input, KEYCODE_OEM_2 /* '/' */)) {
					dlevel += 1;
					zoom_in_key_hold_down_start_time = get_clock();
					zoom_in_key_times_zoomed_while_holding = 0;
				} else {
					float time_elapsed = get_seconds_elapsed(zoom_in_key_hold_down_start_time, get_clock());
					int zooms = (int) (time_elapsed / key_repeat_interval);
					if ((zooms - zoom_in_key_times_zoomed_while_holding) == 1) {
						zoom_in_key_times_zoomed_while_holding = zooms;
						dlevel += 1;
					}
				}
			}

			// Zoom in using X or .
			if (is_key_down(input, 'X') || is_key_down(input, KEYCODE_OEM_PERIOD)) {


				if (was_key_pressed(input, 'X') || was_key_pressed(input, KEYCODE_OEM_PERIOD)) {
					dlevel -= 1;
					zoom_out_key_hold_down_start_time = get_clock();
					zoom_out_key_times_zoomed_while_holding = 0;
				} else {
					float time_elapsed = get_seconds_elapsed(zoom_out_key_hold_down_start_time, get_clock());
					int zooms = (int) (time_elapsed / key_repeat_interval);
					if ((zooms - zoom_out_key_times_zoomed_while_holding) == 1) {
						zoom_out_key_times_zoomed_while_holding = zooms;
						dlevel -= 1;
					}
				}
			}


			if (dlevel != 0) {
//		        printf("mouse_z = %d\n", input->mouse_z);

				i32 new_level = scene->zoom.level + dlevel;
				if (scene->need_zoom_animation) {
					i32 residual_dlevel = scene->zoom_target_state.level - scene->zoom.level;
					new_level += residual_dlevel;
				}
				new_level = CLAMP(new_level, 0, max_level);
				zoom_state_t new_zoom = scene->zoom;
				zoom_update_pos(&new_zoom, (float) new_level);

				if (new_zoom.level != old_zoom.level) {
					if (used_mouse_to_zoom) {
						scene->zoom_pivot = scene->mouse;
					} else {
						scene->zoom_pivot = scene->camera;
					}
					scene->zoom_target_state = new_zoom;
					scene->need_zoom_animation = true;
				}

			}

			if (scene->need_zoom_animation) {
				float d_zoom = scene->zoom_target_state.pos - scene->zoom.pos;

				float abs_d_zoom = fabsf(d_zoom);
				if (abs_d_zoom < 1e-5f) {
					scene->need_zoom_animation = false;
				}
				float sign_d_zoom = signbit(d_zoom) ? -1.0f : 1.0f;
				float linear_catch_up_speed = 12.0f * delta_t;
				float exponential_catch_up_speed = 15.0f * delta_t;
				if (abs_d_zoom > linear_catch_up_speed) {
					d_zoom = (linear_catch_up_speed + (abs_d_zoom - linear_catch_up_speed) * exponential_catch_up_speed) *
					         sign_d_zoom;
				}

				zoom_update_pos(&scene->zoom, scene->zoom.pos + d_zoom);

				// get the relative position of the pivot point on the screen (with x and y between 0 and 1)
				v2f pivot_relative_to_screen = scene->zoom_pivot;
				pivot_relative_to_screen.x -= camera_bounds.min.x;
				pivot_relative_to_screen.y -= camera_bounds.min.y;
				pivot_relative_to_screen.x /= (float)r_minus_l;
				pivot_relative_to_screen.y /= (float)t_minus_b;

				// recalculate the camera position
				r_minus_l = scene->zoom.pixel_width * (float) client_width;
				t_minus_b = scene->zoom.pixel_height * (float) client_height;
				camera_bounds = bounds_from_pivot_point(scene->zoom_pivot, pivot_relative_to_screen, r_minus_l, t_minus_b);
				scene->camera.x = (camera_bounds.right + camera_bounds.left) / 2.0f;
				scene->camera.y = (camera_bounds.top + camera_bounds.bottom) / 2.0f;

				// camera updated, need to updated mouse position
				scene->mouse.x = camera_bounds.min.x + (float)input->mouse_xy.x * scene->zoom.pixel_width;
				scene->mouse.y = camera_bounds.min.y + (float)input->mouse_xy.y * scene->zoom.pixel_height;

			}

			if (scene->need_zoom_animation) {
				app_state->allow_idling_next_frame = false;
			}

			// Panning should be faster when zoomed in very far.
			float panning_multiplier = 1.0f + 3.0f * ((float) max_level - scene->zoom.pos) / (float) max_level;
			if (is_key_down(input, KEYCODE_SHIFT)) {
				panning_multiplier *= 0.25f;
			}

			// Panning using the arrow or WASD keys.
			float panning_speed = 900.0f * delta_t * panning_multiplier;
			if (input->keyboard.action_down.down || is_key_down(input, 'S')) {
				scene->camera.y += scene->zoom.pixel_height * panning_speed;
				mouse_hide();
			}
			if (input->keyboard.action_up.down || is_key_down(input, 'W')) {
				scene->camera.y -= scene->zoom.pixel_height * panning_speed;
				mouse_hide();
			}
			if (input->keyboard.action_right.down || is_key_down(input, 'D')) {
				scene->camera.x += scene->zoom.pixel_height * panning_speed;
				mouse_hide();
			}
			if (input->keyboard.action_left.down || is_key_down(input, 'A')) {
				scene->camera.x -= scene->zoom.pixel_width * panning_speed;
				mouse_hide();
			}

			// camera has been updated (now we need to recalculate some things)
			r_minus_l = scene->zoom.pixel_width * (float) client_width;
			t_minus_b = scene->zoom.pixel_height * (float) client_height;
			camera_bounds = bounds_from_center_point(scene->camera, r_minus_l, t_minus_b);
			scene->mouse.x = camera_bounds.min.x + (float)input->mouse_xy.x * scene->zoom.pixel_width;
			scene->mouse.y = camera_bounds.min.y + (float)input->mouse_xy.y * scene->zoom.pixel_height;


			/*if (was_key_pressed(input, 'O')) {
				app_state->mouse_mode = MODE_CREATE_SELECTION_BOX;
//				printf("switching to creation mode\n");
			}*/

			if (was_key_pressed(input, 'P')) {
				app_state->use_image_adjustments = !app_state->use_image_adjustments;
			}


			if (app_state->mouse_mode == MODE_VIEW) {
				if (scene->is_dragging) {
					scene->camera.x -= current_drag_vector.x * scene->zoom.pixel_width * panning_multiplier;
					scene->camera.y -= current_drag_vector.y * scene->zoom.pixel_height * panning_multiplier;

					// camera has been updated (now we need to recalculate some things)
					camera_bounds = bounds_from_center_point(scene->camera, r_minus_l, t_minus_b);
					scene->mouse.x = camera_bounds.min.x + (float)input->mouse_xy.x * scene->zoom.pixel_width;
					scene->mouse.y = camera_bounds.min.y + (float)input->mouse_xy.y * scene->zoom.pixel_height;
				}

				if (!gui_want_capture_mouse) {
					if (scene->clicked || was_key_pressed(input, 'Q')) {
						// try to select an annotation
						if (scene->annotation_set.annotation_count > 0) {
							i64 select_begin = get_clock();
							select_annotation(scene, is_key_down(input, KEYCODE_CONTROL));
//				    	    float selection_ms = get_seconds_elapsed(select_begin, get_clock()) * 1000.0f;
//			    	    	printf("Selecting took %g ms.\n", selection_ms);
						}
					}

				}

			} else if (app_state->mouse_mode == MODE_CREATE_SELECTION_BOX) {
				if (!gui_want_capture_mouse) {
					if (scene->drag_started) {
						scene->selection_box = (rect2f){ scene->mouse.x, scene->mouse.y, 0.0f, 0.0f };
						scene->has_selection_box = true;
					} else if (scene->is_dragging) {
						scene->selection_box.w = scene->mouse.x - scene->selection_box.x;
						scene->selection_box.h = scene->mouse.y - scene->selection_box.y;
					} else if (scene->drag_ended) {
						app_state->mouse_mode = MODE_VIEW;

					}

				}
			}

			/*if (scene->clicked && !gui_want_capture_mouse) {
				scene->has_selection_box = false; // deselect selection box
			}*/

			// draw selection box
			if (scene->has_selection_box){
				rect2f final_selection_rect = rect2f_recanonicalize(&scene->selection_box);
				bounds2f bounds = rect2f_to_bounds(&final_selection_rect);
				v2f points[4];
				points[0] = (v2f) { bounds.left, bounds.top };
				points[1] = (v2f) { bounds.left, bounds.bottom };
				points[2] = (v2f) { bounds.right, bounds.bottom };
				points[3] = (v2f) { bounds.right, bounds.top };
				for (i32 i = 0; i < 4; ++i) {
					points[i] = world_pos_to_screen_pos(points[i], camera_bounds.min, scene->zoom.pixel_width);
				}
				rgba_t rgba = {0, 0, 0, 128};
				gui_draw_polygon_outline(points, 4, rgba, 3.0f);
			}



			if (!gui_want_capture_keyboard && was_key_pressed(input, KEYCODE_DELETE)) {
				delete_selected_annotations(&scene->annotation_set);
			}

		}

		draw_annotations(&scene->annotation_set, camera_bounds.min, scene->zoom.pixel_width);

		last_section = profiler_end_section(last_section, "viewer_update_and_render: process input (2)", 5.0f);

		// IO

#if 0
		// Finalize textures that were uploaded via PBO the previous frame
		for (i32 transfer_index = 0; transfer_index < COUNT(app_state->pixel_transfer_states); ++transfer_index) {
			pixel_transfer_state_t* transfer_state = app_state->pixel_transfer_states + transfer_index;
			if (transfer_state->need_finalization) {
				finalize_texture_upload_using_pbo(transfer_state);
				transfer_state->need_finalization = false;
				tile_t* tile = (tile_t*) transfer_state->userdata;  // TODO: think of something more elegant?
				tile->texture = transfer_state->texture;
			}
			float time_elapsed = get_seconds_elapsed(app_state->last_frame_start, get_clock());
//			if (time_elapsed > 0.005f) {
//				printf("Warning: texture finalization is taking too much time\n");
//				break;
//			}
		}

		float time_elapsed = get_seconds_elapsed(last_section, get_clock());
		if (time_elapsed > 0.005f) {
			printf("Warning: texture finalization took %g ms\n", time_elapsed * 1000.0f);
		}

		last_section = profiler_end_section(last_section, "viewer_update_and_render: texture finalization", 7.0f);
#endif

		// Retrieve completed tasks from the worker threads
		i32 pixel_transfer_index_start = app_state->next_pixel_transfer_to_submit;
		while (is_queue_work_in_progress(&thread_message_queue)) {
			work_queue_entry_t entry = get_next_work_queue_entry(&thread_message_queue);
			if (entry.is_valid) {
				if (!entry.callback) panic();
				mark_queue_entry_completed(&thread_message_queue);

				if (entry.callback == viewer_notify_load_tile_completed) {
					viewer_notify_tile_completed_task_t* task = (viewer_notify_tile_completed_task_t*) entry.data;
					if (task->pixel_memory) {
						bool need_free_pixel_memory = true;
						if (task->tile) {
							tile_t* tile = task->tile;
							if (tile->need_gpu_residency) {
								pixel_transfer_state_t* transfer_state = submit_texture_upload_via_pbo(app_state, task->tile_width, task->tile_width, 4, task->pixel_memory);
								finalize_texture_upload_using_pbo(transfer_state);
								tile->texture = transfer_state->texture;
							}
							if (tile->need_keep_in_cache) {
								need_free_pixel_memory = false;
								tile->pixels = task->pixel_memory;
								tile->is_cached = true;
							}
						}
						if (need_free_pixel_memory) {
							free(task->pixel_memory);
						}
					}

					free(entry.data);
				} else if (entry.callback == viewer_upload_already_cached_tile_to_gpu) {
					load_tile_task_t* task = (load_tile_task_t*) entry.data;
					tile_t* tile = task->tile;
					if (tile->is_cached && tile->pixels) {
						if (tile->need_gpu_residency) {
							pixel_transfer_state_t* transfer_state = submit_texture_upload_via_pbo(app_state, TILE_DIM, TILE_DIM, 4, tile->pixels);
							finalize_texture_upload_using_pbo(transfer_state);
							tile->texture = transfer_state->texture;
						} else {
							ASSERT(!"viewer_only_upload_cached_tile() called but !tile->need_gpu_residency\n");
						}

						if (!task->need_keep_in_cache) {
							free(tile->pixels);
							tile->pixels = NULL;
							tile->is_cached = false;
						}
					} else {
						printf("Warning: viewer_only_upload_cached_tile() called on a non-cached tile\n");
					}
				}
			}

			float time_elapsed = get_seconds_elapsed(app_state->last_frame_start, get_clock());
			if (time_elapsed > 0.007f) {
//				printf("Warning: texture submission is taking too much time\n");
				break;
			}

			if (pixel_transfer_index_start == app_state->next_pixel_transfer_to_submit) {
//				printf("Warning: not enough PBO's to do all the pixel transfers\n");
				break;
			}
		}

		/*float time_elapsed = get_seconds_elapsed(last_section, get_clock());
		if (time_elapsed > 0.005f) {
			printf("Warning: texture submission took %g ms\n", time_elapsed * 1000.0f);
		}*/


		// Determine the highest and lowest levels with image data that need to be loaded and rendered.
		// The lowest needed level might be lower than the actual current downsampling level,
		// because some levels may not have image data available (-> need to fall back to lower level).
		i32 highest_visible_level = image->level_count - 1;
		i32 lowest_visible_level = scene->zoom.level;
		lowest_visible_level = ATMOST(highest_visible_level, lowest_visible_level);
		for (; lowest_visible_level > 0; --lowest_visible_level) {
			if (image->level_images[lowest_visible_level].exists) {
				break; // done, no need to go lower
			}
		}

		// Create a 'wishlist' of tiles to request
		load_tile_task_t tile_wishlist[32];
		i32 num_tasks_on_wishlist = 0;
		float screen_radius = ATLEAST(1.0f, sqrtf(SQUARE(client_width/2) + SQUARE(client_height/2)));

		for (i32 level = highest_visible_level; level >= lowest_visible_level; --level) {
			level_image_t *drawn_level = image->level_images + level;
			if (!drawn_level->exists) {
				continue; // no image data
			}

			bounds2i level_tiles_bounds = {
					.left = 0,
					.top = 0,
					.right = drawn_level->width_in_tiles,
					.bottom = drawn_level->height_in_tiles,
			};

			bounds2i visible_tiles = world_bounds_to_tile_bounds(&camera_bounds, drawn_level->x_tile_side_in_um, drawn_level->y_tile_side_in_um);
			visible_tiles = clip_bounds2i(&visible_tiles, &level_tiles_bounds);

			if (scene->is_cropped) {
				bounds2i crop_tile_bounds = world_bounds_to_tile_bounds(&scene->crop_bounds, drawn_level->x_tile_side_in_um, drawn_level->y_tile_side_in_um);
				visible_tiles = clip_bounds2i(&visible_tiles, &crop_tile_bounds);
			}

			i32 base_priority = (image->level_count - level) * 100; // highest priority for the most zoomed in levels


			for (i32 tile_y = visible_tiles.min.y; tile_y < visible_tiles.max.y; ++tile_y) {
				for (i32 tile_x = visible_tiles.min.x; tile_x < visible_tiles.max.x; ++tile_x) {



					tile_t* tile = get_tile(drawn_level, tile_x, tile_y);
					if (tile->texture != 0 || tile->is_empty || tile->is_submitted_for_loading) {
						continue; // nothing needs to be done with this tile
					}

					float tile_distance_from_center_of_screen_x =
							(scene->camera.x - ((tile_x + 0.5f) * drawn_level->x_tile_side_in_um)) / drawn_level->um_per_pixel_x;
					float tile_distance_from_center_of_screen_y =
							(scene->camera.y - ((tile_y + 0.5f) * drawn_level->y_tile_side_in_um)) / drawn_level->um_per_pixel_y;
					float tile_distance_from_center_of_screen =
							sqrtf(SQUARE(tile_distance_from_center_of_screen_x) + SQUARE(tile_distance_from_center_of_screen_y));
					tile_distance_from_center_of_screen /= screen_radius;
					// prioritize tiles close to the center of the screen
					float priority_bonus = (1.0f - tile_distance_from_center_of_screen) * 300.0f; // can be tweaked.
					i32 tile_priority = base_priority + (i32)priority_bonus;



					if (num_tasks_on_wishlist >= COUNT(tile_wishlist)) {
						break;
					}
					tile_wishlist[num_tasks_on_wishlist++] = (load_tile_task_t){
							.image = image, .tile = tile, .level = level, .tile_x = tile_x, .tile_y = tile_y,
							.priority = tile_priority,
							.need_gpu_residency = true,
							.need_keep_in_cache = tile->need_keep_in_cache,
							.completion_callback = viewer_notify_load_tile_completed,
					};

				}
			}

		}
//		printf("Num tiles on wishlist = %d\n", num_tasks_on_wishlist);

		qsort(tile_wishlist, num_tasks_on_wishlist, sizeof(load_tile_task_t), priority_cmp_func);

		last_section = profiler_end_section(last_section, "viewer_update_and_render: create tiles wishlist", 5.0f);

		i32 max_tiles_to_load = (image->type == IMAGE_TYPE_TIFF && image->tiff.tiff.is_remote) ? 3 : 10;
		i32 tiles_to_load = ATMOST(num_tasks_on_wishlist, max_tiles_to_load);

		request_tiles(app_state, image, tile_wishlist, tiles_to_load);

		last_section = profiler_end_section(last_section, "viewer_update_and_render: load tiles", 5.0f);


		// RENDERING


		mat4x4 projection = {};
		{
			float l = -0.5f * r_minus_l;
			float r = +0.5f * r_minus_l;
			float b = +0.5f * t_minus_b;
			float t = -0.5f * t_minus_b;
			float n = 100.0f;
			float f = -100.0f;
			mat4x4_ortho(projection, l, r, b, t, n, f);
		}

		mat4x4 I;
		mat4x4_identity(I);

		// define view matrix
		mat4x4 view_matrix;
		mat4x4_translate(view_matrix, -scene->camera.x, -scene->camera.y, 0.0f);

		mat4x4 projection_view_matrix;
		mat4x4_mul(projection_view_matrix, projection, view_matrix);

		glUseProgram(basic_shader);
		glActiveTexture(GL_TEXTURE0);
		glUniform1i(basic_shader_u_tex, 0);

		glUniformMatrix4fv(basic_shader_u_projection_view_matrix, 1, GL_FALSE, &projection_view_matrix[0][0]);

		glUniform3fv(basic_shader_u_background_color, 1, (GLfloat *) &app_state->clear_color);
		if (app_state->use_image_adjustments) {
			glUniform1f(basic_shader_u_black_level, app_state->black_level);
			glUniform1f(basic_shader_u_white_level, app_state->white_level);
		} else {
			glUniform1f(basic_shader_u_black_level, 0.0f);
			glUniform1f(basic_shader_u_white_level, 1.0f);
		}

		last_section = profiler_end_section(last_section, "viewer_update_and_render: render (1)", 5.0f);

		if (scene->is_cropped) {
			// Set up the stencil buffer to prevent rendering outside the image area
			///*
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_ALWAYS, 1, 0xFF);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0xFF);
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // don't actually draw the stencil rectangle
			glDepthMask(GL_FALSE); // don't write to depth buffer
//*/
			{
				mat4x4 model_matrix;
				mat4x4_translate(model_matrix, scene->crop_bounds.left, scene->crop_bounds.top, 0.0f);
				mat4x4_scale_aniso(model_matrix, model_matrix,
				                   scene->crop_bounds.right - scene->crop_bounds.left,
				                   scene->crop_bounds.bottom - scene->crop_bounds.top,
				                   1.0f);
				glUniformMatrix4fv(basic_shader_u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);
				draw_rect(dummy_texture);
			}

			/*
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_ALWAYS, 1, 0xFF);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0xFF);
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // don't actually draw the stencil rectangle
			glDepthMask(GL_FALSE); // don't write to depth buffer
	//*/

			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDepthMask(GL_TRUE);
			glStencilMask(0x00);
			glStencilFunc(GL_EQUAL, 1, 0xFF);
		} else {
			// TODO: do not draw beyond the borders of the image (instead of cropping the tiles themselves)
			glDisable(GL_STENCIL_TEST);
		}


		// Draw all levels within the viewport, up to the current zoom factor
		for (i32 level = lowest_visible_level; level <= highest_visible_level; ++level) {
			level_image_t *drawn_level = image->level_images + level;
			if (!drawn_level->exists) {
				continue;
			}

			bounds2i level_tiles_bounds = {
					.left = 0,
					.top = 0,
					.right = drawn_level->width_in_tiles,
					.bottom = drawn_level->height_in_tiles,
			};

			bounds2i visible_tiles = world_bounds_to_tile_bounds(&camera_bounds, drawn_level->x_tile_side_in_um, drawn_level->y_tile_side_in_um);
			visible_tiles = clip_bounds2i(&visible_tiles, &level_tiles_bounds);

			if (scene->is_cropped) {
				bounds2i crop_tile_bounds = world_bounds_to_tile_bounds(&scene->crop_bounds, drawn_level->x_tile_side_in_um, drawn_level->y_tile_side_in_um);
				visible_tiles = clip_bounds2i(&visible_tiles, &crop_tile_bounds);
			}

			i32 missing_tiles_on_this_level = 0;
			for (i32 tile_y = visible_tiles.min.y; tile_y < visible_tiles.max.y; ++tile_y) {
				for (i32 tile_x = visible_tiles.min.x; tile_x < visible_tiles.max.x; ++tile_x) {

					tile_t *tile = get_tile(drawn_level, tile_x, tile_y);
					if (tile->texture) {
						tile->time_last_drawn = app_state->frame_counter;
						u32 texture = get_texture_for_tile(image, level, tile_x, tile_y);

						float tile_pos_x = drawn_level->x_tile_side_in_um * tile_x;
						float tile_pos_y = drawn_level->y_tile_side_in_um * tile_y;

						// define model matrix
						mat4x4 model_matrix;
						mat4x4_translate(model_matrix, tile_pos_x, tile_pos_y, 0.0f);
						mat4x4_scale_aniso(model_matrix, model_matrix, drawn_level->x_tile_side_in_um,
						                   drawn_level->y_tile_side_in_um, 1.0f);
						glUniformMatrix4fv(basic_shader_u_model_matrix, 1, GL_FALSE, &model_matrix[0][0]);

						draw_rect(texture);
					} else {
						++missing_tiles_on_this_level;
					}
				}
			}

			if (missing_tiles_on_this_level == 0) {
				break; // don't need to bother drawing the next level, there are no gaps left to fill in!
			}

		}

		// restore OpenGL state
		glDisable(GL_STENCIL_TEST);

		last_section = profiler_end_section(last_section, "viewer_update_and_render: render (2)", 5.0f);

	}

	after_scene_render:

	gui_draw(app_state, curr_input, client_width, client_height);
	last_section = profiler_end_section(last_section, "gui draw", 10.0f);

	autosave(app_state, false);
	last_section = profiler_end_section(last_section, "autosave", 10.0f);

	glFinish();

	float update_and_render_time = get_seconds_elapsed(app_state->last_frame_start, get_clock());
//	printf("Frame time: %g ms\n", update_and_render_time * 1000.0f);

	++app_state->frame_counter;
}

