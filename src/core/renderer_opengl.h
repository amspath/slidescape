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
#include "viewer.h"


typedef struct framebuffer_t {
    u32 framebuffer;
    u32 texture;
    u32 depth_stencil_rbo;
    i32 width;
    i32 height;
    bool initialized;
} framebuffer_t;

typedef struct basic_shader_t {
    u32 program;
    i32 u_projection_view_matrix;
    i32 u_model_matrix;
    i32 u_tex;
    i32 u_black_level;
    i32 u_white_level;
    i32 u_background_color;
    i32 u_transparent_color;
    i32 u_transparent_tolerance;
    i32 u_use_transparent_filter;
    i32 u_draw_outlines;
    i32 attrib_location_pos;
    i32 attrib_location_tex_coord;
} basic_shader_t;

typedef struct finalblit_shader_t {
    u32 program;
    i32 u_texture0;
    i32 u_texture1;
    float u_t;
    i32 attrib_location_pos;
    i32 attrib_location_tex_coord;
} finalblit_shader_t;



// globals
#if defined(RENDERER_OPENGL_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif


extern u32 vbo_rect;
extern u32 ebo_rect;
extern u32 vao_rect;
extern bool32 rect_initialized;

extern u32 vbo_screen;
extern u32 vao_screen;


extern u32 default_texture_mag_filter INIT(= GL_NEAREST);
extern u32 default_texture_min_filter INIT(= GL_LINEAR_MIPMAP_LINEAR);

extern framebuffer_t layer_framebuffers[2];
extern bool layer_framebuffers_initialized;

extern basic_shader_t basic_shader;
extern finalblit_shader_t finalblit_shader;

extern u32 dummy_texture;

extern bool finalize_textures_immediately INIT(= true);

#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif
