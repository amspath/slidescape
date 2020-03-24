#pragma once

#include "common.h"

#if DO_DEBUG
#define STRINGIFY_SHADERS
#endif

#if DO_DEBUG
void write_stringified_shaders();
#endif

void load_shader(u32 shader, const char* source_filename);
u32 load_basic_shader_program(const char* vert_filename, const char* frag_filename);
i32 get_attrib(i32 program, const char *name);
i32 get_uniform(i32 program, const char *name);
