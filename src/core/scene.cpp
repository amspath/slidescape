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

void update_scale_bar(scene_t* scene, scale_bar_t* scale_bar) {
	if (!scale_bar->initialized) {
		scale_bar->max_width = 200.0f;
		scale_bar->width = scale_bar->max_width;
		scale_bar->height = ImGui::GetFrameHeight();
		scale_bar->pos = (v2f) {50.0f, scene->viewport.h - 50.0f};
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

void scale_bar_set_pos(scene_t* scene, scale_bar_t* scale_bar, v2f pos) {
	corner_enum closest_corner = get_closest_corner(rect2f_center_point(scene->viewport), scale_bar->pos);
	scale_bar->corner = closest_corner;
	v2f corner_pos = get_corner_pos(scene->viewport, closest_corner);
	scale_bar->pos_relative_to_corner = v2f_subtract(scale_bar->pos, corner_pos);
}

void draw_scale_bar(scene_t* scene, scale_bar_t* scale_bar) {
	if (scale_bar->enabled) {
		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

		ImU32 col_a = ImGui::GetColorU32(IM_COL32(0, 0, 0, 255));
		ImU32 col_b = ImGui::GetColorU32(IM_COL32(255, 255, 255, 255));
		draw_list->AddRectFilled(scale_bar->pos, scale_bar->pos_max, col_a);

		ImVec2 text_pos = {scale_bar->pos.x + scale_bar->text_x, scale_bar->pos.y};
		draw_list->AddText(text_pos, col_b, scale_bar->text);
	}
}

