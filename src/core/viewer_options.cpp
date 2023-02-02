/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2023  Pieter Valkema

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

static char options_ini_filename[512];

void viewer_init_options(app_state_t* app_state) {
	if (global_settings_dir) {
		snprintf(options_ini_filename, sizeof(options_ini_filename), "%s" PATH_SEP "%s", global_settings_dir, "slidescape.ini");
	} else {
		strncpy(options_ini_filename, "slidescape.ini", sizeof(options_ini_filename));
	}

	ini_t* ini = ini_load_from_file(options_ini_filename);

	ini_begin_section(ini, "General");
	ini_register_i32(ini, "window_width", &desired_window_width);
	ini_register_i32(ini, "window_height", &desired_window_height);
	ini_register_bool(ini, "window_start_maximized", &window_start_maximized);
	ini_register_bool(ini, "vsync", &is_vsync_enabled);

	ini_apply(ini);
}

