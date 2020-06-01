/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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
#define TO_BGRA(r,g,b,a) ((a) << 24 | (r) << 16 | (g) << 8 | (b) << 0)
#define TO_RGBA(r,g,b,a) ((a) << 24 | (r) << 0 | (g) << 8 | (b) << 16)

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

typedef struct v2f {
	float x, y;
} v2f;

typedef struct v3f {
	union {
		struct {float r, g, b; };
		struct {float x, y, z; };
	};
} v3f;

typedef struct v4f {
	union {
		struct {float r, g, b, a; };
		struct {float x, y, z, w; };
	};
} v4f;
#pragma pack(pop)

// prototypes
rect2i clip_rect(rect2i* first, rect2i* second);
bool is_point_inside_rect2i(rect2i rect, v2i point);
v2i rect2i_center_point(rect2i* rect);
v2f world_pos_to_screen_pos(v2f world_pos, v2f camera_min, float screen_um_per_pixel);
float v2i_distance(v2i v);
float v2f_distance(v2f v);

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

