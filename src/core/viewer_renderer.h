/*
Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2026  Pieter Valkema

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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include <linmath.h>

typedef struct app_state_t app_state_t;
typedef struct scene_t scene_t;

typedef enum renderer_pixel_format_t {
	RENDERER_PIXEL_FORMAT_BGRA,
	RENDERER_PIXEL_FORMAT_RGBA,
} renderer_pixel_format_t;

void renderer_begin_image_render(app_state_t* app_state, scene_t* scene, mat4x4 projection_view_matrix);
void renderer_set_image_model_matrix(mat4x4 model_matrix);
void renderer_draw_textured_rect(u32 texture);
void renderer_disable_stencil_test();
void renderer_begin_stencil_write();
void renderer_end_stencil_write();
void renderer_set_tile_blend_enabled(bool enabled);
void renderer_finish_image_render();
void renderer_clear_and_set_up_framebuffer(v4f clear_color, i32 client_width, i32 client_height);
void renderer_ensure_layer_framebuffers(app_state_t* app_state);
void renderer_prepare_layer_framebuffer(i32 framebuffer_index, i32 width, i32 height, v4f clear_color);
void renderer_bind_screen_framebuffer();
void renderer_final_blit_layers(float layer_time);
u32 renderer_get_dummy_texture();
void renderer_finish();

extern bool finalize_textures_immediately;

#ifdef __cplusplus
}
#endif
