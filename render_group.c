#pragma once
#include "common.h"
#include "viewer.h"


static u32 vbo_rect;
static u32 vao_rect;
//static u32 basic_shader;
//static u32 text_shader;
volatile bool32 is_opengl_initialized;

typedef struct basic_shader_t {
	u32 program;
	i32 uniform_model;
	i32 uniform_view;
	i32 uniform_projection;
	i32 uniform_tex;
	i32 uniform_black_level;
	i32 uniform_white_level;
} basic_shader_t;

basic_shader_t basic_shader;



void init_draw_rect() {
	static bool32 initialized;
	ASSERT(!initialized);
	initialized = true;

	glGenVertexArrays(1, &vao_rect);
	glBindVertexArray(vao_rect);

	glGenBuffers(1, &vbo_rect);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_rect);

	static float vertices[] = {
			0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // x, y, z,  u, v
			1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

			0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
			1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	u32 vertex_stride = 5 * sizeof(float);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride, (void*)0); // position coordinates
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertex_stride, (void*)(3*sizeof(float))); // texture coordinates
	glEnableVertexAttribArray(1);


}

void draw_rect(u32 texture) {
	glDisable(GL_BLEND);
	glUseProgram(basic_shader.program);
	glBindVertexArray(vao_rect);
	glUniform1i(basic_shader.uniform_tex, 0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}



void init_opengl_stuff() {
	ASSERT(!is_opengl_initialized);

	basic_shader.program = load_basic_shader_program("shaders/basic.vert", "shaders/basic.frag");
	if (!basic_shader.program) {
		printf("Error: could not load basic shader\n");
		panic();
	}

	basic_shader.uniform_model = get_uniform(basic_shader.program, "model");
	basic_shader.uniform_view = get_uniform(basic_shader.program, "view");
	basic_shader.uniform_projection = get_uniform(basic_shader.program, "projection");
	basic_shader.uniform_tex = get_uniform(basic_shader.program, "tex");
	basic_shader.uniform_black_level = get_uniform(basic_shader.program, "black_level");
	basic_shader.uniform_white_level = get_uniform(basic_shader.program, "white_level");

#if DO_DEBUG
	init_font_test_text_shader();
#endif

#ifdef STRINGIFY_SHADERS
	write_stringified_shaders();
#endif

	init_draw_rect();

	write_barrier;
	is_opengl_initialized = true;
}


typedef struct render_group_t {
	u32 max_pushbuffer_size;
	u32 pushbuffer_size;
	u8* pushbuffer_base;

} render_group_t;

typedef struct render_basis_t {

} render_basis_t;


render_group_t* allocate_render_group(arena_t* arena, u32 max_pushbuffer_size) {
	render_group_t* result = push_struct(arena, render_group_t);
	*result = (render_group_t) {
		.pushbuffer_base = (u8*) push_size(arena, max_pushbuffer_size),
	};
}

void* push_render_entry(render_group_t* group, u32 size) {
	void* result = NULL;
	if (group->pushbuffer_size + size < group->max_pushbuffer_size) {
		result = group->pushbuffer_base + group->pushbuffer_size;
	} else {
		panic();
	}
	return result;
}