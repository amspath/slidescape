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

#include "common.h"
#include "platform.h"
#include "viewer.h"
#include "gui.h"

void zoom_update_pos(zoom_state_t* zoom, float pos) {
	ASSERT(pos > -50);
	zoom->pos = pos;
	zoom->downsample_factor = exp2f(zoom->pos);
	zoom->pixel_width = zoom->downsample_factor * zoom->base_pixel_width;
	zoom->pixel_height = zoom->downsample_factor * zoom->base_pixel_height;
	zoom->level  = (i32)floorf(pos);
	ASSERT(zoom->notch_size != 0.0f);
	zoom->notches = (i32) floorf((pos / zoom->notch_size));
}

void init_zoom_state(zoom_state_t* zoom, float zoom_position, float notch_size, float base_pixel_width, float base_pixel_height) {
	memset(zoom, 0, sizeof(zoom_state_t));
	zoom->base_pixel_height = base_pixel_height;
	zoom->base_pixel_width = base_pixel_width;
	zoom->notch_size = notch_size;
	zoom_update_pos(zoom, zoom_position);
}

void init_scene(app_state_t *app_state, scene_t *scene) {
	memset(scene, 0, sizeof(scene_t));
	scene->clear_color = app_state->clear_color;
	scene->transparent_color = (v3f){1.0f, 1.0f, 1.0f};
	scene->transparent_tolerance = 0.01f;
	scene->use_transparent_filter = false;
	scene->entity_count = 1; // NOTE: entity 0 = null entity, so start from 1
	scene->camera = (v2f){0.0f, 0.0f}; // center camera at origin
	init_zoom_state(&scene->zoom, 0.0f, 1.0f, 1.0f, 1.0f);
	scene->is_mpp_known = false;
	scene->enable_grid = false;
	scene->enable_annotations = true;
	scene->initialized = true;
}

void update_scale_bar(scene_t* scene, scale_bar_t* scale_bar) {
	if (!scale_bar->initialized) {
		scale_bar->max_width = 200.0f;
		scale_bar->width = scale_bar->max_width;
		scale_bar->height = ImGui::GetFrameHeight();
		scale_bar->pos = (v2f) {50.0f, scene->viewport.h - 50.0f};
		scale_bar->pos_relative_to_corner = scale_bar->pos;
		scale_bar->corner = CORNER_TOP_LEFT;
		scale_bar->enabled = false;
		scale_bar->initialized = true;
	}

	if (scene->viewport_changed) {
		v2f corner_pos = get_corner_pos(scene->viewport, scale_bar->corner);
		scale_bar->pos = v2f_add(corner_pos, scale_bar->pos_relative_to_corner);
	}

	if (scale_bar->enabled) {
		// Update the scale bar width.
		// The scale bar should fill as much as possible of the available max_width, while keeping to factors 1, 2 and 5.
		float um_per_pixel = scene->zoom.pixel_width;
		float width_in_um = scale_bar->max_width * um_per_pixel;
		float scale = log10(width_in_um);
		float factor = powf(10.0f, -floorf(scale));
		float first_digit = floorf(width_in_um * factor);
		if (first_digit > 5.0f) {
			first_digit = 5.0f;
		} else if (first_digit > 2.0f) {
			first_digit = 2.0f;
		} else {
			first_digit = 1.0f;
		}
		float adjusted_width = floorf(first_digit) / factor;

		scale_bar->width = adjusted_width / um_per_pixel;
		scale_bar->pos_max = (v2f){scale_bar->pos.x + scale_bar->width, scale_bar->pos.y + scale_bar->height};
		scale_bar->pos_center = v2f_average(scale_bar->pos, scale_bar->pos_max);

		corner_enum closest_corner = get_closest_corner(rect2f_center_point(scene->viewport), scale_bar->pos);
		scale_bar->corner = closest_corner;
		v2f corner_pos = get_corner_pos(scene->viewport, closest_corner);
		scale_bar->pos_relative_to_corner = v2f_subtract(scale_bar->pos, corner_pos);

		// Update the text in the scale bar.
		const char* unit;
		if (scene->is_mpp_known) {
			if (adjusted_width >= 999.999f) {
				adjusted_width *= 1e-3f;
				unit = "mm";
			} else if (adjusted_width < 1.0f) {
				adjusted_width *= 1e3f;
				unit = "pm";
			} else {
				unit = "μm";
			}
		} else {
			unit = "px";
		}

		snprintf(scale_bar->text, sizeof(scale_bar->text)-1, "%g %s", adjusted_width, unit);

		// Calculate the X offset of the text so that the text appears centered.
		ImVec2 text_dimensions = ImGui::CalcTextSize(scale_bar->text);
		float extra_x = ATLEAST(0, scale_bar->width - text_dimensions.x);
		scale_bar->text_x = extra_x * 0.5f;
	}
}

void draw_scale_bar(scale_bar_t* scale_bar) {
	if (scale_bar->enabled) {
		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

		ImU32 col_a = ImGui::GetColorU32(IM_COL32(0, 0, 0, 255));
		ImU32 col_b = ImGui::GetColorU32(IM_COL32(255, 255, 255, 255));
		draw_list->AddRectFilled(scale_bar->pos, scale_bar->pos_max, col_a);

		ImVec2 text_pos = {scale_bar->pos.x + scale_bar->text_x, scale_bar->pos.y};
		draw_list->AddText(text_pos, col_b, scale_bar->text);
	}
}

void draw_grid(scene_t* scene) {
	if (scene->enable_grid) {
		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

		v2f p0 = {scene->viewport.x, scene->viewport.y};
		v2f p1 = {p0.x + scene->viewport.w, p0.y + scene->viewport.h};

		draw_list->PushClipRect(p0, p1, true);
		{
			const float world_step = 1000.0f;
			const float grid_step = world_step / scene->zoom.pixel_width;
			v2f scrolling;
			scrolling.x = grid_step - fmodf(scene->camera_bounds.min.x, world_step) / scene->zoom.pixel_width;
			scrolling.y = grid_step - fmodf(scene->camera_bounds.min.y, world_step) / scene->zoom.pixel_height;
			u32 line_color = IM_COL32(50, 50, 50, 80);
			for (float x = fmodf(scrolling.x, grid_step); x < scene->viewport.w; x += grid_step) {
				draw_list->AddLine(ImVec2(p0.x + x, p0.y), ImVec2(p0.x + x, p1.y), line_color);
			}
			for (float y = fmodf(scrolling.y, grid_step); y < scene->viewport.h; y += grid_step)
				draw_list->AddLine(ImVec2(p0.x, p0.y + y), ImVec2(p1.x, p0.y + y), line_color);
		}

		draw_list->PopClipRect();

	}
}

