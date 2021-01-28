/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2021  Pieter Valkema

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
#ifndef OPENSLIDE_H
#define OPENSLIDE_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _openslide openslide_t;

#define OPENSLIDE_PROPERTY_NAME_COMMENT "openslide.comment"
#define OPENSLIDE_PROPERTY_NAME_VENDOR "openslide.vendor"
#define OPENSLIDE_PROPERTY_NAME_QUICKHASH1 "openslide.quickhash-1"
#define OPENSLIDE_PROPERTY_NAME_BACKGROUND_COLOR "openslide.background-color"
#define OPENSLIDE_PROPERTY_NAME_OBJECTIVE_POWER "openslide.objective-power"
#define OPENSLIDE_PROPERTY_NAME_MPP_X "openslide.mpp-x"
#define OPENSLIDE_PROPERTY_NAME_MPP_Y "openslide.mpp-y"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_X "openslide.bounds-x"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_Y "openslide.bounds-y"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_WIDTH "openslide.bounds-width"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_HEIGHT "openslide.bounds-height"

#if !defined(_WIN32) && !defined(WINAPI)
#define WINAPI
#endif

typedef struct openslide_api {
	const char*  (WINAPI *openslide_detect_vendor)(const char *filename);
	openslide_t* (WINAPI *openslide_open)(const char *filename);
    i32          (WINAPI *openslide_get_level_count)(openslide_t *osr);
	void         (WINAPI *openslide_get_level0_dimensions)(openslide_t *osr, i64 *w, i64 *h);
	void         (WINAPI *openslide_get_level_dimensions)(openslide_t *osr, i32 level, i64 *w, i64 *h);
	double       (WINAPI *openslide_get_level_downsample)(openslide_t *osr, i32 level);
    i32          (WINAPI *openslide_get_best_level_for_downsample)(openslide_t *osr, double downsample);
	void         (WINAPI *openslide_read_region)(openslide_t *osr, uint32_t *dest, i64 x, i64 y, i32 level, i64 w, i64 h);
	void         (WINAPI *openslide_close)(openslide_t *osr);
	const char * (WINAPI *openslide_get_error)(openslide_t *osr);
	const char * const *(WINAPI *openslide_get_property_names)(openslide_t *osr);
	const char * (WINAPI *openslide_get_property_value)(openslide_t *osr, const char *name);
	const char * const *(WINAPI *openslide_get_associated_image_names)(openslide_t *osr);
	void         (WINAPI *openslide_get_associated_image_dimensions)(openslide_t *osr, const char *name, i64 *w, i64 *h);
	void         (WINAPI *openslide_read_associated_image)(openslide_t *osr, const char *name, u32 *dest);
	const char * (WINAPI *openslide_get_version)(void);
} openslide_api;



// prototypes
bool init_openslide();

// globals
#if defined(OPENSLIDE_API_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern openslide_api openslide;
extern bool is_openslide_available;
extern bool is_openslide_loading_done;

#undef INIT
#undef extern


#ifdef __cplusplus
}
#endif

#endif //OPENSLIDE_H

