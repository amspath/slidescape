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
#include "phasecorrelate.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum image_register_preprocess_method_enum {
    REGISTER_PREPROCESS_UNDEFINED = 0,
    REGISTER_PREPROCESS_NONE = 1,
    REGISTER_PREPROCESS_ISOLATE_HEMATOXYLIN = 2,
} image_register_preprocess_method_enum;

typedef struct image_transform_t {
    bool is_valid;
    v2f translate;
    float response;
} image_transform_t;

image_transform_t do_image_registration(image_t* image1, image_t* image2, i32 levels_from_top);
image_transform_t do_local_image_registration(image_t* image1, image_t* image2, v2f center_point, i32 level, i32 patch_width, image_register_preprocess_method_enum preprocess_method);

#ifdef __cplusplus
}
#endif
