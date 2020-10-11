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
#include "platform.h"

#include OPENGL_H

#include "stringutils.h"
#include "stringified_shaders.c"

#include "shader.h"

#if DO_DEBUG
#define STRINGIFY_SHADERS
#endif

i32 shader_count = 0;
bool32 are_any_shader_sources_missing;
#define MAX_SHADER_FILENAME 64


#ifdef STRINGIFY_SHADERS

// Shader sources can be either loaded from file (preferred, if available), or loaded directly from memory.
// In debug builds, the file stringified_shaders.c is automatically generated, containing the shader sources.
// In this way, the shader sources are embedded in the executable; they do not need to be included in a release package.

#define MAX_SHADERS 32
char shader_filenames[MAX_SHADER_FILENAME][MAX_SHADERS];
char* shader_sources[MAX_SHADERS];

void write_stringified_shaders() {
	if (are_any_shader_sources_missing) {
		return;
	}

	FILE* f_output = fopen("src/stringified_shaders.c", "w");

	fprintf(f_output, "// This file is automatically generated at runtime.\n#pragma once\n\n");

	for (int shader_index = 0; shader_index < shader_count; ++shader_index) {
		char* name = shader_filenames[shader_index];
		dots_to_underscores(name, MAX_SHADER_FILENAME);
		char* source = shader_sources[shader_index];
		i32 source_len = strlen(source);

		// Adapted from bin2c:
		// https://github.com/gwilymk/bin2c

		bool32 need_comma = false;
		fprintf(f_output, "const char stringified_shader_source__%s[%i] = {", name, source_len + 1);
		for (i32 i = 0; i < source_len + 1; ++i) {
			if (need_comma)
				fprintf(f_output, ", ");
			else
				need_comma = 1;
			if ((i % 11) == 0)
				fprintf(f_output, "\n\t");
			if (i == source_len) {
				fprintf(f_output, "0");
			} else {
				fprintf(f_output, "0x%.2x", source[i] & 0xff);
			}
		}
		fprintf(f_output, "\n};\n\n");

	}

	fprintf(f_output, "const char* stringified_shader_sources[%i] = {", shader_count);
	for (int shader_index = 0; shader_index < shader_count; ++shader_index) {
		fprintf(f_output, "\n\tstringified_shader_source__%s,", shader_filenames[shader_index]);
	}
	fprintf(f_output, "\n};\n\n");

	fprintf(f_output, "const char* stringified_shader_source_names[%i] = {", shader_count);
	for (int shader_index = 0; shader_index < shader_count; ++shader_index) {
		fprintf(f_output, "\n\t\"%s\",", shader_filenames[shader_index]);
	}
	fprintf(f_output, "\n};\n\n");

	fclose(f_output);

}



#endif

void load_shader(u32 shader, const char* source_filename) {
	mem_t* shader_source_file = platform_read_entire_file(source_filename);
	const char* shader_source = NULL;
	bool32 source_from_file = false;

	if (shader_source_file) {
		source_from_file = true;
		shader_source = (char*) shader_source_file->data;

#ifdef STRINGIFY_SHADERS
		const char* stripped_filename = one_past_last_slash(source_filename, MAX_SHADER_FILENAME);

		ASSERT(shader_count < COUNT(shader_filenames));
		shader_sources[shader_count] = strdup(shader_source);
		strncpy(shader_filenames[shader_count], stripped_filename, MAX_SHADER_FILENAME);
#endif
		++shader_count;
	} else {
		are_any_shader_sources_missing = true;
		const char* stripped_filename = one_past_last_slash(source_filename, MAX_SHADER_FILENAME);
		char source_name_temp[MAX_SHADER_FILENAME] = {};
		strncpy(source_name_temp, stripped_filename, MAX_SHADER_FILENAME);
		dots_to_underscores(source_name_temp, MAX_SHADER_FILENAME);
		for (i32 i = 0; i < COUNT(stringified_shader_source_names); ++i) {
			const char* name = stringified_shader_source_names[i];
			if (strncmp(source_name_temp, name, MAX_SHADER_FILENAME) == 0) {
				shader_source = stringified_shader_sources[i];
			}
		}
	}

	if (!shader_source) {
		console_print_error("Could not locate the shader source for %s.\n", source_filename);
	}

	const char* sources[] = { shader_source, };
	glShaderSource(shader, COUNT(sources), sources, NULL);
	free(shader_source_file);
	glCompileShader(shader);

	i32 success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (success) {
//		console_print("Loaded %sshader: %s\n", source_from_file ? "" : "cached ", source_filename);
	} else {
		char info_log[2048];
		glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
		console_print_error("Error: compilation of shader '%s' failed:\n%s", source_filename, info_log);
		console_print_error("Shader source: %s\n", shader_source);
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
			console_print_error("Error: shader linking failed: %s", info_log);
			panic();
		}
	}

	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	return shader_program;
}

i32 get_attrib(i32 program, const char *name) {
	i32 attribute = glGetAttribLocation(program, name);
	if(attribute == -1)
		console_print_error("Could not get attribute location %s\n", name);
	return attribute;
}

i32 get_uniform(i32 program, const char *name) {
	i32 uniform = glGetUniformLocation(program, name);
	if(uniform == -1)
		console_print_error("Could not get uniform location %s\n", name);
	return uniform;
}

