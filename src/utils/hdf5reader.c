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

#include "common.h"
#include "hdf5reader.h"

#include "hdf5.h"

ndarray_t hdf5_read_ndarray(hdf5_handle_t h5, const char* name) {
    ndarray_t result = {};

    hid_t dataset = H5Dopen2(h5, name, H5P_DEFAULT);
    if (dataset == H5I_INVALID_HID) {
        console_print_error("hdf5_read_ndarray(): error reading dataset %s: H5Dopen2() failed\n", name);
        return result;
    }

    hid_t datatype = H5Dget_type(dataset); /* datatype handle */
    H5T_class_t datatype_class = H5Tget_class(datatype);
    size_t datatype_size = H5Tget_size(datatype);

    switch(datatype_class) {
        default: break;
        case H5T_INTEGER: {
            if (datatype_size == 4) {
                result.dtype = NDARRAY_TYPE_INT32;
            } else if (datatype_size == 8) {
                result.dtype = NDARRAY_TYPE_INT64;
            }
        } break;
        case H5T_FLOAT: {
            if (datatype_size == 4) {
                result.dtype = NDARRAY_TYPE_FLOAT32;
            } else if (datatype_size == 8) {
                result.dtype = NDARRAY_TYPE_FLOAT64;
            }
        } break;
    }

    hid_t dataspace = H5Dget_space(dataset); /* dataspace handle */
    i32 rank = H5Sget_simple_extent_ndims(dataspace);
    if (rank > NDARRAY_MAX_RANK) {
        console_print_error("hdf5_read_ndarray(): error reading dataset %s: rank=%d, exceeds maximum of %d\n", name, rank, NDARRAY_MAX_RANK);
        return result;
    }
    result.rank = rank;
    hsize_t dims_out[NDARRAY_MAX_RANK] = {};
    i32 status_n  = H5Sget_simple_extent_dims(dataspace, dims_out, NULL);

    for (i32 i = 0; i < rank; ++i) {
        result.shape[i] = (i64)dims_out[i];
    }

    size_t element_count = dims_out[0];
    for (i32 i = 1; i < rank; ++i) {
        element_count *= dims_out[i];
    }
    result.data = calloc(1, element_count * datatype_size);

    /*
 * Define hyperslab in the dataset.
 */
    hsize_t offset[NDARRAY_MAX_RANK] = {};     /* hyperslab offset in the file */
    hsize_t* count = dims_out;      /* size of the hyperslab in the file */
    herr_t status    = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);

    // Define the memory dataspace.
    hsize_t* dimsm = dims_out;    /* memory space dimensions */
    hid_t memspace = H5Screate_simple(rank, dimsm, NULL);

    hsize_t offset_out[NDARRAY_MAX_RANK] = {}; // hyperslab offset in memory

    hsize_t* count_out = dims_out;  /* size of the hyperslab in memory */
    status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset_out, NULL, count_out, NULL);

    status = H5Dread(dataset, datatype, memspace, dataspace, H5P_DEFAULT, result.data);
    result.is_valid = true; // TODO: more thorough checking?

    // Close/release resources.
    H5Tclose(datatype);
    H5Dclose(dataset);
    H5Sclose(dataspace);
    H5Sclose(memspace);

    return result;
}

ndarray_int32_t hdf5_read_ndarray_int32(hdf5_handle_t h5, const char* name) {
    ndarray_t result = hdf5_read_ndarray(h5, name);
    if (result.dtype != NDARRAY_TYPE_INT32) {
        result.is_valid = false;
    }
    return *(ndarray_int32_t*)&result;
}

ndarray_float32_t hdf5_read_ndarray_float32(hdf5_handle_t h5, const char* name) {
    ndarray_t result = hdf5_read_ndarray(h5, name);
    if (result.dtype != NDARRAY_TYPE_FLOAT32) {
        result.is_valid = false;
    }
    return *(ndarray_float32_t*)&result;
}

void ndarray_destroy(ndarray_t* ndarray) {
    if (ndarray->data) {
        free(ndarray->data);
    }
}




hdf5_handle_t hdf5_open(const char* filename) {
    hid_t file = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    return file;
}

void hdf5_close(hdf5_handle_t h5_file) {
    H5Fclose(h5_file);
}

void hdf5_test(const char* filename) {

    hid_t       datatype, dataspace;
    hid_t       memspace;
    H5T_class_t t_class; /* data type class */
    H5T_order_t order;   /* data order */
    size_t      size;    /*
                          * size of the data element
                          * stored in file
                          */

    hsize_t dims_out[2]; /* dataset dimensions */
    herr_t  status;





    int     i, j, k, status_n, rank;


    /*
     * Open the file and the dataset.
     */
    hid_t file    = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    hid_t coords_dataset = H5Dopen2(file, "coords", H5P_DEFAULT);
    hid_t attn_dataset = H5Dopen2(file, "attention_z_scores", H5P_DEFAULT);

    ndarray_int32_t coords = hdf5_read_ndarray_int32(file, "coords");
    ndarray_float32_t attention_z_scores = hdf5_read_ndarray_float32(file, "attention_z_scores");
    ndarray_float32_t attention_raw_scores = hdf5_read_ndarray_float32(file, "attention_raw_scores");

    /*
     * Get datatype and dataspace handles and then query
     * dataset class, order, size, rank and dimensions.
     */
    datatype = H5Dget_type(coords_dataset); /* datatype handle */
    t_class  = H5Tget_class(datatype);
    if (t_class == H5T_INTEGER)
        printf("Data set has INTEGER type \n");
    order = H5Tget_order(datatype);
    if (order == H5T_ORDER_LE)
        printf("Little endian order \n");

    size = H5Tget_size(datatype);
    printf(" Data size is %d \n", (int)size);

    dataspace = H5Dget_space(coords_dataset); /* dataspace handle */
    rank      = H5Sget_simple_extent_ndims(dataspace);
    status_n  = H5Sget_simple_extent_dims(dataspace, dims_out, NULL);
    printf("rank %d, dimensions %lu x %lu \n", rank, (unsigned long)(dims_out[0]),
           (unsigned long)(dims_out[1]));

    size_t element_count = dims_out[0] * dims_out[1];
    i32* data_out = (i32*) calloc(1, element_count * sizeof(i32));

    /*
 * Define hyperslab in the dataset.
 */
    hsize_t offset[2] = {0, 0};     /* hyperslab offset in the file */
    hsize_t count[2];      /* size of the hyperslab in the file */
    count[0]  = dims_out[0];
    count[1]  = dims_out[1];
    status    = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);

    /*
     * Define the memory dataspace.
     */
    hsize_t dimsm[2];    /* memory space dimensions */
    dimsm[0] = dims_out[0];
    dimsm[1] = dims_out[1];
    memspace = H5Screate_simple(2, dimsm, NULL);

    /*
     * Define memory hyperslab.
     */
    hsize_t offset_out[2]; /* hyperslab offset in memory */
    offset_out[0] = 0;
    offset_out[1] = 0;

    hsize_t count_out[2];  /* size of the hyperslab in memory */
    count_out[0]  = dims_out[0];
    count_out[1]  = dims_out[1];
    status        = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset_out, NULL, count_out, NULL);


    status = H5Dread(coords_dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data_out);

    /*
     * Close/release resources.
     */
    H5Tclose(datatype);
    H5Dclose(coords_dataset);
    H5Sclose(dataspace);
    H5Sclose(memspace);
    H5Fclose(file);

    DUMMY_STATEMENT;

}