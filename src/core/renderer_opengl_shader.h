/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2023  Pieter Valkema

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

#if DO_DEBUG
#define STRINGIFY_SHADERS
#endif

#if DO_DEBUG
void opengl_write_stringified_shaders();
#endif

u32 opengl_load_basic_shader_program(const char* vert_filename, const char* frag_filename);
i32 opengl_get_attrib(u32 program, const char *name);
i32 opengl_get_uniform(u32 program, const char *name);

#ifdef __cplusplus
};
#endif

