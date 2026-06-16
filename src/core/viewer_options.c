/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2026  Pieter Valkema

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
#include "viewer.h"
#include "ini.h"
#include "stringutils.h"

static char options_ini_filename[512];
static ini_t* options_ini;

static void viewer_register_options(ini_t* ini, app_state_t* app_state) {
	ini_begin_section(ini, "General");
	ini_register_i32(ini, "window_width", &desired_window_width);
	ini_register_i32(ini, "window_height", &desired_window_height);
	ini_register_bool(ini, "window_start_maximized", &window_start_maximized);
	ini_register_bool(ini, "vsync", &is_vsync_enabled);
	//ini_register_bool(ini, "show_menu_bar", &show_menu_bar); //NOTE: there's a risk of creating a state where the user can't get back to the menu bar

	ini_begin_section(ini, "Controls");
	ini_register_i32(ini, "mouse_sensitivity", &app_state->mouse_sensitivity);
	ini_register_i32(ini, "keyboard_panning_speed", &app_state->keyboard_base_panning_speed);

	ini_begin_section(ini, "Annotations");
	ini_register_bool(ini, "enable_autosave", &app_state->enable_autosave);
	ini_register_bool(ini, "remember_groups_as_template", &app_state->remember_annotation_groups_as_template);

	ini_begin_section(ini, "Backends");
	ini_register_bool(ini, "use_builtin_tiff_backend", &app_state->use_builtin_tiff_backend);
	ini_register_bool(ini, "use_native_mrxs_backend", &global_use_native_mrxs_backend);
}

void viewer_init_options(app_state_t* app_state) {
	if (global_settings_dir) {
		snprintf(options_ini_filename, sizeof(options_ini_filename), "%s" PATH_SEP "%s", global_settings_dir, "slidescape.ini");
	} else {
		copy_cstring(options_ini_filename, "slidescape.ini", sizeof(options_ini_filename));
	}

	options_ini = ini_load_from_file(options_ini_filename);
	viewer_register_options(options_ini, app_state);
	ini_apply(options_ini);
}

void viewer_save_options(app_state_t* app_state) {
	if (!options_ini) {
		options_ini = ini_load_from_file(options_ini_filename);
		viewer_register_options(options_ini, app_state);
	}
	ini_save(options_ini, options_ini_filename);
}
