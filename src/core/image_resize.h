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

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct image_buffer_t {
    u8* pixels;
    i32 channels;
    i32 width;
    i32 height;
    pixel_format_enum pixel_format;
    bool is_valid;
} image_buffer_t;

typedef struct filter_t {
    float (*filter)(float);
    float support;
} filter_t;

void debug_test_resample();

#ifdef __cplusplus
}
#endif
