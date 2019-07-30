#pragma once
#include "common.h"
#include "viewer.h"

void load_shader(u32 shader, const char* source_filename) {
	file_mem_t* shader_source_file = platform_read_entire_file(source_filename);
	if (shader_source_file) {
		const char* shader_source = (char*) shader_source_file->data;
		const char* sources[] = { shader_source, };
		glShaderSource(shader, COUNT(sources), sources, NULL);
		glCompileShader(shader);

		i32 success = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
		if (!success) {
			char info_log[2048];
			glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
			printf("Error: compilation of shader '%s' failed:\n%s", source_filename, info_log);
			printf("Shader source: %s\n", shader_source);
		}
		free(shader_source_file);
	}

}

u32 load_basic_shader_program(const char* vert_filename, const char* frag_filename) {
	u32 vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	u32 fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

	load_shader(vertex_shader, vert_filename);
	load_shader(fragment_shader, frag_filename);

	u32 shader_program = glCreateProgram();

	glAttachShader(shader_program, vertex_shader);
	glAttachShader(shader_program, fragment_shader);
	glLinkProgram(shader_program);

	{
		i32 success;
		glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
		if (!success) {
			char info_log[2048];
			glGetProgramInfoLog(shader_program, sizeof(info_log), NULL, info_log);
			printf("Error: shader linking failed: %s", info_log);
			panic();
		}
	}

	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	return shader_program;
}

static u32 vbo_rect;
static u32 vao_rect;
static bool32 rect_initialized;
static u32 basic_shader;
static bool32 opengl_stuff_initialized;

void init_draw_rect() {
	ASSERT(!rect_initialized);
	rect_initialized = true;

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
	glUseProgram(basic_shader);
	glBindVertexArray(vao_rect);
	glUniform1i(glGetUniformLocation(basic_shader, "the_texture"), 0);
	glBindTexture(GL_TEXTURE_2D, texture);
	gl_diagnostic("glBindTexture");
	glDrawArrays(GL_TRIANGLES, 0, 6);
	gl_diagnostic("glDrawArrays");
}



void init_opengl_stuff() {
	ASSERT(!opengl_stuff_initialized);
	opengl_stuff_initialized = true;

	// TODO: don't depend on seperate shader text files in a release build
	// TODO: look in the executable directory
	basic_shader = load_basic_shader_program("shaders/basic.vert", "shaders/basic.frag");
	glEnable(GL_TEXTURE_2D);

	init_draw_rect();
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