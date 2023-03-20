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

// directories to know:
// exe directory: the directory the executable is located in
// run/working directory: the directory the program was launched from
// target/active directory: the directory of the input image
// output directory: the directory where an export operation saves files to

#include "tiff_write.h"

app_command_t app_parse_commandline(int argc, const char** argv) {
	app_command_t app_command = {};

	// Skip argument 0 (= the executable path)
	argc = ATLEAST(0, argc - 1);
	const char** args = argv + 1;
	i32 arg_index = 0;
	for (i32 arg_index = 0; arg_index < argc; ++arg_index) {
		const char* arg = args[arg_index];
		if (strcmp(arg, "--version") == 0) {
			app_command.headless = true;
			app_command.command = COMMAND_PRINT_VERSION;
			app_command.exit_immediately = true;
		} else if (strcmp(arg, "--export") == 0) {
			app_command.headless = true;
			app_command.command = COMMAND_EXPORT;
			app_command.export_command.with_annotations = true;
			app_command.export_command.error = COMMAND_EXPORT_ERROR_NO_ROI;
			// slidescape 1.tiff --export --roi "Annotation 0"
			++arg_index;
			for (; arg_index < argc; ++arg_index) {
				arg = args[arg_index];
				if (strcmp(arg, "--roi") == 0) {
					if (arg_index < argc) {
						++arg_index;
						arg = args[arg_index];
						app_command.export_command.roi = arg;
						app_command.export_command.error = COMMAND_EXPORT_ERROR_NONE;
					}
				} else if (strcmp(arg, "--no-annotations") == 0) {
					app_command.export_command.with_annotations = false;
				}
			}
		} else {
			// Unknown command, assume that it's an input file
			arrput(app_command.inputs, arg);

		}
	}



	return app_command;
}

void app_command_execute_immediately(app_command_t* app_command) {
	if (app_command->command == COMMAND_PRINT_VERSION) {
		console_print(APP_TITLE " " APP_VERSION "\n");
	}
}

void export_region_get_name_hint(app_state_t* app_state, char* output_buffer, size_t output_size) {
	const char* name_hint = "output";
	if (arrlen(app_state->loaded_images) > 0) {
		for (i32 i = 0; i < arrlen(app_state->loaded_images); ++i) {
			image_t* image = app_state->loaded_images[i];
			if (image->name[0] != '\0') {
				size_t buffer_size = sizeof(image->name);
				char* new_name_hint = (char*)alloca(buffer_size);
				strncpy(new_name_hint, image->name, buffer_size);
				// Strip filename extension
				size_t len = strlen(new_name_hint);
				for (i32 pos = len-1; pos >= 1; --pos) {
					if (new_name_hint[pos] == '.') {
						new_name_hint[pos] = '\0';
						// add '_region'
						strncpy(new_name_hint + pos, "_region", buffer_size - pos);
						name_hint = new_name_hint;
						break;
					}
				}
			}
		}
	}

	const char* filename_extension_hint = "";
	if (desired_region_export_format == 0) {
#if APPLE
		// macOS does not seem to like tile TIFF files in the Finder (will sometimes stop responding,
		// at least on my system). So choose the .ptif file extension by default as an alternative.
		filename_extension_hint = ".ptif";
#else
		filename_extension_hint = ".tiff";
#endif
	} else if (desired_region_export_format == 1) {
		filename_extension_hint = ".jpeg";
	} else if (desired_region_export_format == 2) {
		filename_extension_hint = ".png";
	}

	snprintf(output_buffer, output_size-1, "%s%s", name_hint, filename_extension_hint);
};

int app_command_execute(app_state_t* app_state) {
	app_command_t* command = &app_state->command;
	if (command->command == COMMAND_EXPORT) {
		for (i32 i = 0; i < arrlen(command->inputs); ++i) {
			console_print("input: %s\n", command->inputs[i]);
		}

		for (i32 input_index = 0; input_index < arrlen(command->inputs); ++input_index) {
			const char* filename = command->inputs[input_index];
			if (load_generic_file(app_state, filename, 0)) {
				if (arrlen(app_state->loaded_images) > 0) {
					image_t* image = app_state->loaded_images[0];
					if (image->backend == IMAGE_BACKEND_TIFF) {
						u32 export_flags = 0;
						// TODO: allow configuration
						if (command->export_command.with_annotations) {
							export_flags |= EXPORT_FLAGS_ALSO_EXPORT_ANNOTATIONS;
						}
						export_flags |= EXPORT_FLAGS_PUSH_ANNOTATION_COORDINATES_INWARD;

						annotation_set_t* annotation_set = &app_state->scene.annotation_set;
						if (annotation_set->active_annotation_count > 0) {

							// Search for the ROI

							bool found_roi = false;
							annotation_t* roi_annotation;
							for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
								annotation_t* annotation = get_active_annotation(&app_state->scene.annotation_set, i);
								if (strncmp(annotation->name, command->export_command.roi, COUNT(annotation->name)-1) == 0) {
									found_roi = true;
									roi_annotation = annotation;
									// Makes no sense to export the annotations if the only one existing is the one specifying what to export
									if (annotation_set->active_annotation_count == 1 && annotation_set->active_group_count <= 1 && annotation_set->active_feature_count <= 1) {
										export_flags &= ~EXPORT_FLAGS_ALSO_EXPORT_ANNOTATIONS;
									}
									break;
								}
							}

							if (found_roi) {
								bounds2f world_bounds = bounds_for_annotation(roi_annotation);
								bounds2i pixel_bounds = world_bounds_to_pixel_bounds(&world_bounds, image->mpp_x, image->mpp_y);

								char filename_hint[512];
								export_region_get_name_hint(app_state, filename_hint, sizeof(filename_hint));

								export_cropped_bigtiff(app_state, image, &image->tiff, world_bounds, pixel_bounds,
								                       filename_hint, 512,
								                       tiff_export_desired_color_space, tiff_export_jpeg_quality, export_flags);
							}


						}


					}
				}
			}
		}
	}
	return 0;

}