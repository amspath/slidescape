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

// directories to know:
// exe directory: the directory the executable is located in
// run/working directory: the directory the program was launched from
// target/active directory: the directory of the input image
// output directory: the directory where an export operation saves files to



app_command_t app_parse_commandline(app_state_t* app_state, int argc, const char** argv) {
	app_command_t app_command = {};

	// Skip argument 0 (= the executable path)
	i32 args_left = ATLEAST(0, argc - 1);
	const char** args = argv + 1;
	i32 arg_index = 0;
	for (i32 arg_index = 0; args_left > 0; --args_left, ++arg_index) {
		const char* arg = args[arg_index];
		if (strcmp(arg, "--version") == 0) {
			app_command.print_version = true;
			app_command.headless = true;
		}
	}

	return app_command;
}

void app_command_execute(app_state_t* app_state, app_command_t* app_command) {
	if (app_command->print_version) {
		console_print(APP_TITLE " " APP_VERSION "\n");
	}
}