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

typedef int64_t hdf5_handle_t; // avoid including the HDF5 header for now (prevent unneeded namespace pollution)

#define NDARRAY_MAX_RANK 4

typedef enum ndarray_dtype_enum {
    NDARRAY_TYPE_UNKNOWN,
    NDARRAY_TYPE_INT32,
    NDARRAY_TYPE_INT64,
    NDARRAY_TYPE_FLOAT32,
    NDARRAY_TYPE_FLOAT64,
} ndarray_dtype_enum;

typedef struct ndarray_t {
    bool is_valid;
    u8 rank;
    ndarray_dtype_enum dtype;
    i64 shape[NDARRAY_MAX_RANK];
    void* data;
} ndarray_t;

typedef struct ndarray_int32_t {
    bool is_valid;
    u8 rank;
    ndarray_dtype_enum dtype;
    i64 shape[NDARRAY_MAX_RANK];
    i32* data;
} ndarray_int32_t;

typedef struct ndarray_float32_t {
    bool is_valid;
    u8 rank;
    ndarray_dtype_enum dtype;
    i64 shape[NDARRAY_MAX_RANK];
    float* data;
} ndarray_float32_t;

ndarray_t hdf5_read_ndarray(hdf5_handle_t h5, const char* name);
ndarray_int32_t hdf5_read_ndarray_int32(hdf5_handle_t h5, const char* name);
ndarray_float32_t hdf5_read_ndarray_float32(hdf5_handle_t h5, const char* name);
hdf5_handle_t hdf5_open(const char* filename);
void hdf5_close(hdf5_handle_t h5_file);

void ndarray_destroy(ndarray_t* ndarray);
static inline void ndarray_int32_destroy(ndarray_int32_t* ndarray) {
    ndarray_destroy((ndarray_t*)ndarray);
}
static inline void ndarray_float32_destroy(ndarray_float32_t* ndarray) {
    ndarray_destroy((ndarray_t*)ndarray);
}


#ifdef __cplusplus
}
#endif

