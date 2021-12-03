/*
  Slidescape, a whole-slide image viewer for digital pathology.
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

#include "common.h"
#define MATHUTILS_IMPL
#include "mathutils.h"

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

bounds2i clip_bounds2i(bounds2i a, bounds2i b) {
	bounds2i result = {0};
	result.left = MIN(b.right, MAX(a.left, b.left));
	result.top = MIN(b.bottom, MAX(a.top, b.top));
	result.right = MAX(b.left, MIN(a.right, b.right));
	result.bottom = MAX(b.top, MIN(a.bottom, b.bottom));
	return result;
}

bounds2f clip_bounds2f(bounds2f a, bounds2f b) {
	bounds2f result = {0};
	result.left = MIN(b.right, MAX(a.left, b.left));
	result.top = MIN(b.bottom, MAX(a.top, b.top));
	result.right = MAX(b.left, MIN(a.right, b.right));
	result.bottom = MAX(b.top, MIN(a.bottom, b.bottom));
	return result;
}

bool is_point_inside_rect2i(rect2i rect, v2i point) {
	bool result = true;
	if (point.x < rect.x || point.x >= (rect.x + rect.w) || point.y < rect.y || point.y >= (rect.y + rect.h)) {
		result = false;
	}
	return result;
}

v2i rect2i_center_point(rect2i rect) {
	v2i result = {
			.x = rect.x + rect.w / 2,
			.y = rect.y + rect.h / 2,
	};
	return result;
}

v2f rect2f_center_point(rect2f rect) {
	v2f result = {
			.x = rect.x + rect.w * 0.5f,
			.y = rect.y + rect.h * 0.5f,
	};
	return result;
}

// reorient a rect with possible negative width and/or height
rect2f rect2f_recanonicalize(rect2f* rect) {
	rect2f result = {0};
	if (rect->w >= 0.0f) {
		result.x = rect->x;
		result.w = rect->w;
	} else {
		result.x = rect->x + rect->w; // negative, so move coordinate left
		result.w = -rect->w;
	}
	if (rect->h >= 0.0f) {
		result.y = rect->y;
		result.h = rect->h;
	} else {
		result.y = rect->y + rect->h; // negative, so move coordinate to top
		result.h = -rect->h;
	}
	return result;
}

bounds2f rect2f_to_bounds(rect2f* rect) {
	bounds2f result = {0};
	result.left = rect->x;
	result.top = rect->y;
	result.right = rect->x + rect->w;
	result.bottom = rect->y + rect->h;
	return result;
}

v2f world_pos_to_screen_pos(v2f world_pos, v2f camera_min, float screen_um_per_pixel) {
	v2f transformed_pos = {
			.x = (world_pos.x - camera_min.x) / screen_um_per_pixel,
			.y = (world_pos.y - camera_min.y) / screen_um_per_pixel,
	};
	return transformed_pos;
}


i32 tile_pos_from_world_pos(float world_pos, float tile_side) {
	ASSERT(tile_side > 0);
	float tile_float = (world_pos / tile_side);
	float tile = (i32)floorf(tile_float);
	return tile;
}

bounds2i world_bounds_to_tile_bounds(bounds2f* world_bounds, float tile_width, float tile_height, v2f image_pos) {
	bounds2i result = {0};
	result.left = tile_pos_from_world_pos(world_bounds->left - image_pos.x, tile_width);
	result.top = tile_pos_from_world_pos(world_bounds->top - image_pos.y, tile_height);
	result.right = tile_pos_from_world_pos(world_bounds->right - image_pos.x, tile_width) + 1;
	result.bottom = tile_pos_from_world_pos(world_bounds->bottom - image_pos.y, tile_height) + 1;
	return result;
}

bounds2f tile_bounds_to_world_bounds(bounds2i tile_bounds, float tile_width, float tile_height, v2f image_pos) {
	bounds2f result = {0};
	result.left = tile_bounds.left * tile_width + image_pos.x;
	result.right = (tile_bounds.right) * tile_width + image_pos.x;
	result.top = tile_bounds.top * tile_height + image_pos.y;
	result.bottom = (tile_bounds.bottom) * tile_height + image_pos.y;
	return result;
}

bounds2f bounds_from_center_point(v2f center, float r_minus_l, float t_minus_b) {
	bounds2f bounds = {
			.left = center.x - r_minus_l * 0.5f,
			.top = center.y - t_minus_b * 0.5f,
			.right = center.x + r_minus_l * 0.5f,
			.bottom = center.y + t_minus_b * 0.5f,
	};
	return bounds;
}

bounds2f bounds_from_pivot_point(v2f pivot, v2f pivot_relative_pos, float r_minus_l, float t_minus_b) {
	bounds2f bounds = {
			.left = pivot.x - r_minus_l * pivot_relative_pos.x,
			.top = pivot.y - t_minus_b * pivot_relative_pos.y,
			.right = pivot.x + r_minus_l * (1.0f - pivot_relative_pos.x),
			.bottom = pivot.y + t_minus_b * (1.0f - pivot_relative_pos.y),
	};
	return bounds;
}

bounds2i world_bounds_to_pixel_bounds(bounds2f* world_bounds, float mpp_x, float mpp_y) {
	bounds2i pixel_bounds = {0};
	pixel_bounds.left = (i32) floorf(world_bounds->left / mpp_x);
	pixel_bounds.right = (i32) ceilf(world_bounds->right / mpp_x);
	pixel_bounds.top = (i32) floorf(world_bounds->top / mpp_y);
	pixel_bounds.bottom = (i32) ceilf(world_bounds->bottom / mpp_y);
	return pixel_bounds;
}

rect2f pixel_rect_to_world_rect(rect2i pixel_rect, float mpp_x, float mpp_y) {
	rect2f world_rect = {0};
	world_rect.x = pixel_rect.x * mpp_x;
	world_rect.y = pixel_rect.y * mpp_y;
	world_rect.w = pixel_rect.w * mpp_x;
	world_rect.h = pixel_rect.h * mpp_y;
	return world_rect;
}

// https://math.stackexchange.com/questions/330269/the-distance-from-a-point-to-a-line-segment
// https://stackoverflow.com/questions/849211/shortest-distance-between-a-point-and-a-line-segment
v2f project_point_on_line_segment(v2f point, v2f line_start, v2f line_end, float* t_ptr) {
	v2f line_end_minus_start = v2f_subtract(line_end, line_start);
	float segment_length_sq = v2f_length_squared(line_end_minus_start);
	if (segment_length_sq == 0.0f) {
		return line_start; // line_start == line_end case
	}
	// Consider the line extending the segment, parameterized as v + t (w - v).
	// We find projection of point p onto the line.
	// It falls where t = [(p-v) . (w-v)] / |w-v|^2
	// We clamp t from [0,1] to handle points outside the segment vw.
	float t = v2f_dot(v2f_subtract(point, line_start), line_end_minus_start) / segment_length_sq;
	float t_clamped = MAX(0, MIN(1, t));
	if (t_ptr != NULL) {
		*t_ptr = t_clamped; // return value to caller
	}
	v2f projection = v2f_lerp(line_start, line_end_minus_start, t_clamped); // lerp
	return projection;
}

bool v2f_within_bounds(bounds2f bounds, v2f point) {
	bool result = point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top && point.y < bounds.bottom;
	return result;
}

bool v2f_between_points(v2f v, v2f p0, v2f p1) {
	bool result = v.x >= p0.x && v.x < p1.x && v.y >= p0.y && v.y < p1.y;
	return result;
}

v2f v2f_average(v2f a, v2f b) {
	v2f result = {a.x + b.x, a.y + b.y};
	result.x *= 0.5f;
	result.y *= 0.5f;
	return result;
}

corner_enum get_closest_corner(v2f center_point, v2f p) {
	if (p.x <= center_point.x) {
		if (p.y <= center_point.y) {
			return CORNER_TOP_LEFT;
		} else {
			return CORNER_BOTTOM_LEFT;
		}
	} else {
		if (p.y <= center_point.y) {
			return CORNER_TOP_RIGHT;
		} else {
			return CORNER_BOTTOM_RIGHT;
		}
	}
}

v2f get_corner_pos(rect2f rect, corner_enum corner) {
	switch(corner) {
		default:
		case CORNER_TOP_LEFT: {
			return (v2f){rect.x, rect.y};
		} break;
		case CORNER_TOP_RIGHT: {
			return (v2f){rect.x + rect.w, rect.y};
		} break;
		case CORNER_BOTTOM_LEFT: {
			return (v2f){rect.x, rect.y + rect.h};
		} break;
		case CORNER_BOTTOM_RIGHT: {
			return (v2f){rect.x + rect.w, rect.y + rect.h};
		} break;
	}
}




