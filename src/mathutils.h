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

#ifdef __cplusplus
extern "C" {
#endif

#define FLOAT_TO_BYTE(x) ((u8)(255.0f * CLAMP((x), 0.0f, 1.0f)))
#define BYTE_TO_FLOAT(x) CLAMP(((float)((x & 0x0000ff))) /255.0f, 0.0f, 1.0f)
#define MAKE_BGRA(r,g,b,a) (((u32)(a)) << 24u | ((u32)(r)) << 16u | ((u32)(g)) << 8u | ((u32)(b)) << 0u)
#define MAKE_RGBA(r,g,b,a) (((u32)(a)) << 24u | ((u32)(r)) << 0u | ((u32)(g)) << 8u | ((u32)(b)) << 16u)
#define BGRA_SET_ALPHA(p, a) (((p) & 0x00FFFFFF) | (((u32)(a)) << 24u))

#pragma pack(push, 1)
typedef struct rect2i {
	i32 x, y, w, h;
} rect2i;

typedef struct rect2f {
	float x, y, w, h;
} rect2f;

typedef struct v2i {
	i32 x, y;
} v2i;

typedef struct rgba_t {
	u8 r, g, b, a;
} rgba_t;

#ifndef V2F_DEFINED
#define V2F_DEFINED
typedef struct v2f {
	float x, y;
} v2f;
#endif

typedef struct v3f {
	union {
		struct {float r, g, b; };
		struct {float x, y, z; };
	};
} v3f;

#ifndef V4F_DEFINED
#define V4F_DEFINED
typedef struct v4f {
	union {
		struct {float r, g, b, a; };
		struct {float x, y, z, w; };
	};
} v4f;
#endif

typedef struct bounds2i {
	union {
		struct { i32 left, top, right, bottom; };
		struct { v2i min, max; };
	};
} bounds2i;

typedef struct bounds2f {
	union {
		struct { float left, top, right, bottom; };
		struct { v2f min, max; };
	};
} bounds2f;


#pragma pack(pop)

static inline float v2i_length(v2i v)                       { return sqrtf(SQUARE(v.x) + SQUARE(v.y)); }
static inline float v2f_length(v2f v)                       { return sqrtf(SQUARE(v.x) + SQUARE(v.y)); }
static inline float v2f_length_squared(v2f v)               { return SQUARE(v.x) + SQUARE(v.y); }

static inline v2f v2f_add(v2f a, v2f b)                     { return (v2f){ a.x + b.x, a.y + b.y }; }
static inline v2f v2f_subtract(v2f a, v2f b)                { return (v2f){ a.x - b.x, a.y - b.y }; }
static inline float v2f_dot(v2f a, v2f b)                   { return a.x * b.x + a.y * b.y; }
static inline v2f v2f_scale(float scalar, v2f v)            { return (v2f){ v.x * scalar, v.y * scalar }; }
static inline v2f v2f_lerp(v2f a, v2f b_minus_a, float t)   { return v2f_add(a, v2f_scale(t, b_minus_a)); }

// prototypes
rect2i clip_rect(rect2i* first, rect2i* second);
bounds2i clip_bounds2i(bounds2i a, bounds2i b);
bounds2f clip_bounds2f(bounds2f a, bounds2f b);
bool is_point_inside_rect2i(rect2i rect, v2i point);
v2i rect2i_center_point(rect2i* rect);
rect2f rect2f_recanonicalize(rect2f* rect);
bounds2f rect2f_to_bounds(rect2f* rect);
v2f world_pos_to_screen_pos(v2f world_pos, v2f camera_min, float screen_um_per_pixel);
i32 tile_pos_from_world_pos(float world_pos, float tile_side);
bounds2i world_bounds_to_tile_bounds(bounds2f* world_bounds, float tile_width, float tile_height, v2f image_pos);
bounds2f bounds_from_center_point(v2f center, float r_minus_l, float t_minus_b);
bounds2f bounds_from_pivot_point(v2f pivot, v2f pivot_relative_pos, float r_minus_l, float t_minus_b);
bounds2i world_bounds_to_pixel_bounds(bounds2f* world_bounds, float mpp_x, float mpp_y);
rect2f pixel_rect_to_world_rect(rect2i pixel_rect, float mpp_x, float mpp_y);
v2f project_point_on_line_segment(v2f point, v2f line_start, v2f line_end, float* t_ptr);
bool v2f_within_bounds(bounds2f bounds, v2f point);

// globals
#if defined(MATHUTILS_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

//...

#undef INIT
#undef extern


#ifdef __cplusplus
}
#endif

