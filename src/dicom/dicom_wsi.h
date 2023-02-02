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
#include "dicom.h"

void dicom_wsi_interpret_top_level_data_element(dicom_instance_t *instance, dicom_data_element_t element);
void dicom_wsi_interpret_nested_data_element(dicom_instance_t* instance, dicom_data_element_t element);
void dicom_wsi_finalize_sequence_item(dicom_instance_t* instance);
u8* dicom_wsi_decode_tile_to_bgra(dicom_series_t* dicom_series, i32 scale, i32 tile_index);

#ifdef __cplusplus
}
#endif
