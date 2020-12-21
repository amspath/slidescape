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
