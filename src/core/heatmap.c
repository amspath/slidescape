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

#include "viewer.h"

#include "hdf5reader.h"
#include "hdf5.h"


heatmap_t load_heatmap(file_info_t* file) {
    heatmap_t heatmap = {};

    hid_t h5_file = hdf5_open(file->filename);
    if (h5_file != H5I_INVALID_HID) {

        ndarray_int32_t coords = hdf5_read_ndarray_int32(h5_file, "coords");
        ndarray_float32_t attention_z_scores = hdf5_read_ndarray_float32(h5_file, "attention_z_scores");

        if (coords.is_valid && attention_z_scores.is_valid) {
            // coords: expect shape to be (patch_count x 2) ==> (patch_count x (x,y))
            if (coords.rank == 2 && coords.shape[1] == 2) {
                i64 patch_count = coords.shape[0];
                // attention_z_scores: expect shape to be (patch_count x class_count)
                if (attention_z_scores.rank == 2 && coords.shape[0] == patch_count) {
                    i64 class_count = attention_z_scores.shape[1];

                    i32 x_min = INT32_MAX;
                    i32 y_min = INT32_MAX;
                    i32 x_max = INT32_MIN;
                    i32 y_max = INT32_MIN;

                    for (i32 i = 0; i < patch_count; ++i) {
                        i32 x = coords.data[i * 2];
                        i32 y = coords.data[i * 2 + 1];
                        if (x < x_min) x_min = x;
                        if (y < y_min) y_min = y;
                        if (x > x_max) x_max = x;
                        if (y > y_max) y_max = y;
                    }

                    i32 width = x_max - x_min;
                    i32 height = y_max - y_min;

                    // TODO: fix hard-coded values
                    i32 level = 1;
                    heatmap.tile_width = 256;
                    heatmap.tile_height = 256;


                    ASSERT(width % heatmap.tile_width == 0);
                    ASSERT(height % heatmap.tile_height == 0);

                    heatmap.width_in_tiles = width / heatmap.tile_width;
                    heatmap.height_in_tiles = height / heatmap.tile_height;
                    size_t tile_count = heatmap.width_in_tiles * heatmap.height_in_tiles;

                    heatmap.classes = calloc(1, class_count * sizeof(heatmap_class_t));
                    heatmap.class_count = class_count;
                    heatmap.tile_storage = calloc(1, class_count * tile_count * sizeof(heatmap_tile_t));
                    for (i32 class_index = 0; class_index < class_count; ++class_index) {
                        heatmap_class_t* heatmap_class = heatmap.classes + class_index;
                        heatmap_class->tiles = heatmap.tile_storage + class_index * tile_count;

                        for (i32 i = 0; i < patch_count; ++i) {
                            i32 x = coords.data[i * 2];
                            i32 y = coords.data[i * 2 + 1];
                            i32 tile_x = (x - x_min) / heatmap.tile_width;
                            i32 tile_y = (y - y_min) / heatmap.tile_height;
                            heatmap_tile_t* tile = heatmap_class->tiles + tile_y * heatmap.width_in_tiles + tile_x;
                            ASSERT(!tile->exists);
                            tile->exists = true;
                            tile->value = attention_z_scores.data[i * class_count + class_index];
                        }

                    }

                    heatmap.pixel_pos = V2I(x_min << level, y_min << level);
                    heatmap.tile_width <<= level;
                    heatmap.tile_height <<= level;
                    heatmap.max_opacity = 0.5f;
                    heatmap.current_class = 0;
                    heatmap.is_valid = true;

                }

            }
        }

        hdf5_close(h5_file);
    }


    return heatmap;
}

void heatmap_destroy(heatmap_t* heatmap) {
    if (heatmap->classes) {
        free(heatmap->classes);
        heatmap->classes = NULL;
    }
    if (heatmap->tile_storage) {
        free(heatmap->tile_storage);
        heatmap->tile_storage = NULL;
    }
    memset(heatmap, 0, sizeof(*heatmap));
}
