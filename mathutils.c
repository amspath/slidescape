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

#include "common.h"
#define MATHUTILS_IMPL
#include "mathutils.h"
#include <math.h>

rect2i clip_rect(rect2i* first, rect2i* second) {
	i32 x0 = MAX(first->x, second->x);
	i32 y0 = MAX(first->y, second->y);
	i32 x1 = MIN(first->x + first->w, second->x + second->w);
	i32 y1 = MIN(first->y + first->h, second->y + second->h);
	rect2i result = {
			.x = x0,
			.y = y0,
			.w = x1 - x0,
			.h = y1 - y0,
	};
	return result;
}

bool is_point_inside_rect2i(rect2i rect, v2i point) {
	bool result = true;
	if (point.x < rect.x || point.x >= (rect.x + rect.w) || point.y < rect.y || point.y >= (rect.y + rect.h)) {
		result = false;
	}
	return result;
}

v2i rect2i_center_point(rect2i* rect) {
	v2i result = {
			.x = rect->x + rect->w / 2,
			.y = rect->y + rect->h / 2,
	};
	return result;
}

v2f world_pos_to_screen_pos(v2f world_pos, v2f camera_min, float screen_um_per_pixel) {
	v2f transformed_pos = {
			.x = (world_pos.x - camera_min.x) / screen_um_per_pixel,
			.y = (world_pos.y - camera_min.y) / screen_um_per_pixel,
	};
	return transformed_pos;
}

float v2i_distance(v2i v) {
	float result = sqrtf(SQUARE(v.x) + SQUARE(v.y));
	return result;
}

float v2f_distance(v2f v) {
	float result = sqrtf(SQUARE(v.x) + SQUARE(v.y));
	return result;
}


