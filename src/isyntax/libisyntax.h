#pragma once

#include <stdint.h>
#include <stdbool.h>

// API conventions:
// - return type is one of:
// -- appropriate data type for simple getters. For complex getters doing work, isyntax_error_t + out variable.
// -- void if the function doesn't fail at runtime (e.g. destructors).
// -- isyntax_error_t otherwise.
// - Output arguments are last, name prefixed with 'out_'.
// - 'Object operated on' argument is first.
// - Function names are prefixed with 'libisyntax_', as C doesn't have namespaces. Naming convention
//   is: libisyntax_<object>_<action>(..), where <object> is omitted for the object representing the isyntax file.
// - Functions don't check for null/out of bounds, for efficiency. (they may assert, but that is implementation detail).
// - Booleans are represented as int32_t and prefxied with 'is_' or 'has_'.
// - Enums are represented as int32_t and not the enum type. (enum type size forcing is C23 or C++11).
// - Const applied to pointers is used as a signal that the object will not be modified.
// - Prefer int even for unsigned types, see java rationale.
// - Prefer double over float - we are dealing with small/large scales in WSI, and float may not be enough.


typedef int32_t isyntax_error_t;

#define LIBISYNTAX_OK 0
// Generic error that the user should not expect to recover from.
#define LIBISYNTAX_FATAL 1
// One of the arguments passed to a function is invalid.
#define LIBISYNTAX_INVALID_ARGUMENT 2

enum isyntax_pixel_format_t {
  _LIBISYNTAX_PIXEL_FORMAT_START = 0x100,
  LIBISYNTAX_PIXEL_FORMAT_RGBA,
  LIBISYNTAX_PIXEL_FORMAT_BGRA,
  _LIBISYNTAX_PIXEL_FORMAT_END,
};

enum libisyntax_open_flags_t {
	// Set this flag to also initialize the allocators needed for tile loading.
	LIBISYNTAX_OPEN_FLAG_INIT_ALLOCATORS = 1,

	// Set this flag to only read the barcode, then abort (if you only need the barcode, this will be faster).
	LIBISYNTAX_OPEN_FLAG_READ_BARCODE_ONLY = 2,
};

typedef struct isyntax_t isyntax_t;
typedef struct isyntax_image_t isyntax_image_t;
typedef struct isyntax_level_t isyntax_level_t;
typedef struct isyntax_cache_t isyntax_cache_t;

//== Common API ==
// TODO(avirodov): are repeated calls of libisyntax_init() allowed? Currently I believe not.
isyntax_error_t libisyntax_init(void);
isyntax_error_t libisyntax_open(const char* filename, enum libisyntax_open_flags_t flags, isyntax_t** out_isyntax);
void            libisyntax_close(isyntax_t* isyntax);

//== Getters API ==
int32_t                libisyntax_get_tile_width(const isyntax_t* isyntax);
int32_t                libisyntax_get_tile_height(const isyntax_t* isyntax);
const isyntax_image_t* libisyntax_get_wsi_image(const isyntax_t* isyntax);
const isyntax_image_t* libisyntax_get_label_image(const isyntax_t* isyntax);
const isyntax_image_t* libisyntax_get_macro_image(const isyntax_t* isyntax);
const char*            libisyntax_get_barcode(const isyntax_t* isyntax);
int32_t                libisyntax_get_is_mpp_known(const isyntax_t* isyntax);
double                 libisyntax_get_mpp_x(const isyntax_t* isyntax);
double                 libisyntax_get_mpp_y(const isyntax_t* isyntax);
const char*            libisyntax_get_acquisition_datetime(const isyntax_t* isyntax);
const char*            libisyntax_get_manufacturer(const isyntax_t* isyntax);
const char*            libisyntax_get_manufacturers_model_name(const isyntax_t* isyntax);
const char*            libisyntax_get_derivation_description(const isyntax_t* isyntax);
const char*            libisyntax_get_device_serial_number(const isyntax_t* isyntax);
int32_t                libisyntax_get_software_versions_count(const isyntax_t* isyntax);
const char*            libisyntax_get_software_versions(const isyntax_t* isyntax, int32_t index);
int32_t                libisyntax_get_date_of_last_calibration_count(const isyntax_t* isyntax);
const char*            libisyntax_get_date_of_last_calibration(const isyntax_t* isyntax, int32_t index);
int32_t                libisyntax_get_time_of_last_calibration_count(const isyntax_t* isyntax);
const char*            libisyntax_get_time_of_last_calibration(const isyntax_t* isyntax, int32_t index);
bool                   libisyntax_is_lossy_image_compression(const isyntax_t* isyntax);
double                 libisyntax_get_lossy_image_compression_ratio(const isyntax_t* isyntax);
const char*            libisyntax_get_lossy_image_compression_method(const isyntax_t* isyntax);
const char*            libisyntax_scale_unit(const isyntax_t* isyntax);

int32_t                libisyntax_image_get_level_count(const isyntax_image_t* image);
int32_t                libisyntax_image_get_offset_x(const isyntax_image_t* image);
int32_t                libisyntax_image_get_offset_y(const isyntax_image_t* image);
const isyntax_level_t* libisyntax_image_get_level(const isyntax_image_t* image, int32_t index);

int32_t                libisyntax_level_get_scale(const isyntax_level_t* level);
int32_t                libisyntax_level_get_width_in_tiles(const isyntax_level_t* level);
int32_t                libisyntax_level_get_height_in_tiles(const isyntax_level_t* level);
int32_t                libisyntax_level_get_width(const isyntax_level_t* level);
int32_t                libisyntax_level_get_height(const isyntax_level_t* level);
float                  libisyntax_level_get_mpp_x(const isyntax_level_t* level);
float                  libisyntax_level_get_mpp_y(const isyntax_level_t* level);
double                 libisyntax_level_get_downsample_factor(const isyntax_level_t* level);
double                 libisyntax_level_get_origin_offset_in_pixels(const isyntax_level_t* level);

//== Cache API ==
isyntax_error_t libisyntax_cache_create(const char* debug_name_or_null, int32_t cache_size,
                                        isyntax_cache_t** out_isyntax_cache);
// Note: returns LIBISYNTAX_INVALID_ARGUMENT  if isyntax_to_inject was not initialized with is_init_allocators = 0.
// TODO(avirodov): this function will fail if the isyntax object has different block size than the first isyntax injected.
//  Block size variation was not observed in practice, and a proper fix may include supporting multiple block sizes
//  within isyntax_cache_t implementation.
isyntax_error_t libisyntax_cache_inject(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax);
// Flushes the cache. If 'isyntax_or_null' is not NULL, will attempt to flush only the tiles of that isyntax.
// TODO(avirodov): currently flushes all cache, isyntax_or_null is unused.
void            libisyntax_cache_flush(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax_or_null);
void            libisyntax_cache_destroy(isyntax_cache_t* isyntax_cache);


//== Tile API ==
// Reads a tile into a user-supplied buffer. Buffer size should be [tile_width * tile_height * 4], as returned by
// `libisyntax_get_tile_width()`/`libisyntax_get_tile_height()`. The caller is responsible for managing the buffer
// allocation/deallocation.
// pixel_format is one of isyntax_pixel_format_t.
isyntax_error_t libisyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache,
                                     int32_t level, int64_t tile_x, int64_t tile_y,
                                     uint32_t* pixels_buffer, int32_t pixel_format);
isyntax_error_t libisyntax_read_region(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache, int32_t level,
                                       int64_t x, int64_t y, int64_t width, int64_t height, uint32_t* pixels_buffer,
                                       int32_t pixel_format);


isyntax_error_t libisyntax_read_label_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                                   uint32_t** pixels_buffer, int32_t pixel_format);
isyntax_error_t libisyntax_read_macro_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                                   uint32_t** pixels_buffer, int32_t pixel_format);
isyntax_error_t libisyntax_read_label_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size);
isyntax_error_t libisyntax_read_macro_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size);
isyntax_error_t libisyntax_read_icc_profile(isyntax_t* isyntax, isyntax_image_t* image, uint8_t** icc_profile_buffer, uint32_t* icc_profile_size);