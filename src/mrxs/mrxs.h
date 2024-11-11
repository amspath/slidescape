/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2024  Pieter Valkema

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
#include "viewer.h" // for file_info_t and directory_info_t

enum mrxs_section_enum {
    MRXS_SECTION_UNKNOWN = 0,
    MRXS_SECTION_GENERAL,
    MRXS_SECTION_HIERARCHICAL,
    MRXS_SECTION_DATAFILE,
    MRXS_SECTION_LAYER_N_LEVEL_N_SECTION,
    MRXS_SECTION_LAYER_N_SECTION,
    MRXS_SECTION_NONHIERLAYER_N_LEVEL_N_SECTION,
    MRXS_SECTION_NONHIERLAYER_N_SECTION,
};

enum mrxs_hier_enum {
    MRXS_HIER_UNKNOWN = 0,
    MRXS_HIER_SLIDE_ZOOM_LEVEL = 0,
    MRXS_HIER_SLIDE_FILTER_LEVEL,
    MRXS_HIER_MICROSCOPE_FOCUS_LEVEL,
    MRXS_HIER_SCAN_INFO_LAYER,
};

enum mrxs_nonhier_enum {
    MRXS_NONHIER_UNKNOWN = 0,
    MRXS_NONHIER_SCAN_DATA_LAYER = 0,
    MRXS_NONHIER_STITCHING_LAYER,
    MRXS_NONHIER_STITCHING_INTENSITY_LAYER,
    MRXS_NONHIER_VIMSLIDE_HISTOGRAM_DATA,
};

enum mrxs_hier_val_enum {
    MRXS_HIER_VAL_UNKNOWN = 0,
    MRXS_HIER_VAL_ZOOMLEVEL,
};

typedef struct mrxs_level_t {
//    i32 level;
    const char* section_name;
    i32 hier_val_index;
} mrxs_level_t;

typedef struct mrxs_hier_val_t {
    enum mrxs_hier_val_enum type;
    const char* name;
    const char* section;
    i32 index;
} mrxs_hier_val_t;

typedef struct mrxs_hier_t {
    enum mrxs_hier_enum name;
    i32 count;
    const char* section;
    mrxs_hier_val_t* val;
} mrxs_hier_t;

typedef struct mrxs_nonhier_t {
    enum mrxs_nonhier_enum name;
    i32 count;
    const char* section;
} mrxs_nonhier_t;

typedef struct mrxs_t {
    memrw_t string_pool; // NOTE: need destroy
    const char* index_dat_filename;
    const char** dat_filenames; // NOTE: need free
    i32 dat_count;
    i32 hier_count;
    i32 nonhier_count;
    mrxs_hier_t* hier; // NOTE: need free
    mrxs_nonhier_t* nonhier; // NOTE: need free
    i32 slide_zoom_level_hier_index;
    i32 base_width_in_tiles;
    i32 base_height_in_tiles;
    mrxs_level_t levels[16];
    bool is_valid;
} mrxs_t;

bool mrxs_open_from_directory(mrxs_t* mrxs, file_info_t* file, directory_info_t* directory);
void mrxs_destroy(mrxs_t* mrxs);

#ifdef __cplusplus
}
#endif

