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

#pragma once
#include "common.h"
#include "viewer.h"

#include "shader.h"
#include "stretchy_buffer.h"


static u32 vbo_rect;
static u32 ebo_rect;
static u32 vao_rect;
static bool32 rect_initialized;

u32 basic_shader;
i32 basic_shader_u_projection_view_matrix;
i32 basic_shader_u_model_matrix;
i32 basic_shader_u_tex;
i32 basic_shader_u_black_level;
i32 basic_shader_u_white_level;
i32 basic_shader_u_background_color;
i32 basic_shader_attrib_location_pos;
i32 basic_shader_attrib_location_tex_coord;
u32 dummy_texture;


void init_draw_rect() {
	ASSERT(!rect_initialized);
	rect_initialized = true;

	// suppress NVidia OpenGL driver warnings about 'no defined base level' while no textures are loaded
	glDisable(GL_TEXTURE_2D);

	glGenVertexArrays(1, &vao_rect);
	glBindVertexArray(vao_rect);

	glGenBuffers(1, &vbo_rect);
	glGenBuffers(1, &ebo_rect);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_rect);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_rect);

	static float vertices[] = {
			0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // x, y, z,  u, v
			1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

//			0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
//			1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	static u16 indices[] = {
			0,1,2,1,2,3,
	};
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	u32 vertex_stride = 5 * sizeof(float);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride, (void*)0); // position coordinates
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertex_stride, (void*)(3*sizeof(float))); // texture coordinates
	glEnableVertexAttribArray(1);

}

void draw_rect(u32 texture) {
	glBindVertexArray(vao_rect);
//	glUniform1i(basic_shader_u_tex, 0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
}

u32 load_texture(void* pixels, i32 width, i32 height) {
	u32 texture = 0; //gl_gen_texture();
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &texture);
//	printf("Generated texture %d\n", texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
//	glGenerateMipmap(GL_TEXTURE_2D);
//	gl_diagnostic("glTexImage2D");
	return texture;
}

void init_opengl_stuff() {

	basic_shader = load_basic_shader_program("shaders/basic.vert", "shaders/basic.frag");
	basic_shader_u_projection_view_matrix = get_uniform(basic_shader, "projection_view_matrix");
	basic_shader_u_model_matrix = get_uniform(basic_shader, "model_matrix");
	basic_shader_u_tex = get_uniform(basic_shader, "the_texture");
	basic_shader_u_black_level = get_uniform(basic_shader, "black_level");
	basic_shader_u_white_level = get_uniform(basic_shader, "white_level");
	basic_shader_u_background_color = get_uniform(basic_shader, "bg_color");
	basic_shader_attrib_location_pos = get_attrib(basic_shader, "pos");
	basic_shader_attrib_location_tex_coord = get_attrib(basic_shader, "tex_coord");

#ifdef STRINGIFY_SHADERS
	write_stringified_shaders();
#endif
	glEnable(GL_TEXTURE_2D);

	init_draw_rect();
	u32 dummy_texture_color = MAKE_BGRA(255, 255, 0, 255);
	dummy_texture = load_texture(&dummy_texture_color, 1, 1);

}


typedef struct render_group_t {
	u32 max_pushbuffer_size;
	u32 pushbuffer_size;
	u8* pushbuffer_base;

} render_group_t;

typedef struct render_basis_t {

} render_basis_t;


/*
render_group_t* allocate_render_group(arena_t* arena, u32 max_pushbuffer_size) {
	render_group_t* result = push_struct(arena, render_group_t);
	*result = (render_group_t) {
		.pushbuffer_base = (u8*) push_size(arena, max_pushbuffer_size),
	};
}
*/

void* push_render_entry(render_group_t* group, u32 size) {
	void* result = NULL;
	if (group->pushbuffer_size + size < group->max_pushbuffer_size) {
		result = group->pushbuffer_base + group->pushbuffer_size;
	} else {
		panic();
	}
	return result;
}