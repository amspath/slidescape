#pragma once

#include <stdint.h>

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

typedef struct isyntax_t isyntax_t;
typedef struct isyntax_image_t isyntax_image_t;
typedef struct isyntax_level_t isyntax_level_t;
typedef struct isyntax_cache_t isyntax_cache_t;

//== Common API ==
// TODO(avirodov): are repeated calls of libisyntax_init() allowed? Currently I believe not.
isyntax_error_t libisyntax_init();
isyntax_error_t libisyntax_open(const char* filename, int32_t is_init_allocators, isyntax_t** out_isyntax);
void            libisyntax_close(isyntax_t* isyntax);

//== Getters API ==
int32_t                libisyntax_get_tile_width(const isyntax_t* isyntax);
int32_t                libisyntax_get_tile_height(const isyntax_t* isyntax);
int32_t                libisyntax_get_wsi_image_index(const isyntax_t* isyntax);
const isyntax_image_t* libisyntax_get_image(const isyntax_t* isyntax, int32_t wsi_image_index);
int32_t                libisyntax_image_get_level_count(const isyntax_image_t* image);
const isyntax_level_t* libisyntax_image_get_level(const isyntax_image_t* image, int32_t index);

int32_t                libisyntax_level_get_scale(const isyntax_level_t* level);
int32_t                libisyntax_level_get_width_in_tiles(const isyntax_level_t* level);
int32_t                libisyntax_level_get_height_in_tiles(const isyntax_level_t* level);
int32_t                libisyntax_level_get_width(const isyntax_level_t* level);
int32_t                libisyntax_level_get_height(const isyntax_level_t* level);
float                  libisyntax_level_get_mpp_x(const isyntax_level_t* level);
float                  libisyntax_level_get_mpp_y(const isyntax_level_t* level);

//== Cache API ==
isyntax_error_t libisyntax_cache_create(const char* debug_name_or_null, int32_t cache_size,
                                        isyntax_cache_t** out_isyntax_cache);
// Note: returns LIBISYNTAX_INVALID_ARGUMENT  if isyntax_to_inject was not initialized with is_init_allocators = 0.
// TODO(avirodov): this function will fail if the isyntax object has different block size than the first isyntax injected.
//  Block size variation was not observed in practice, and a proper fix may include supporting multiple block sizes
//  within isyntax_cache_t implementation.
isyntax_error_t libisyntax_cache_inject(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax);
void            libisyntax_cache_destroy(isyntax_cache_t* isyntax_cache);


//== Tile API ==
// Reads a tile into a user-supplied buffer. Buffer size should be [tile_width * tile_height * 4], as returned by
// `libisyntax_get_tile_width()`/`libisyntax_get_tile_height()`. The caller is responsible for managing the buffer
// allocation/deallocation.
// pixel_format is one of isyntax_pixel_format_t.
isyntax_error_t libisyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache,
                                     int32_t level, int64_t tile_x, int64_t tile_y,
                                     uint32_t* pixels_buffer, int32_t pixel_format);


