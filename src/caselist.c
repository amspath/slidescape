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

#include <stdio.h>

#include "platform.h"
#include "parson.h"
#include "stringutils.h"

#define CASELIST_IMPL
#include "viewer.h"
#include "caselist.h"
#include "gui.h"


void reset_global_caselist(app_state_t* app_state) {
	app_state->selected_case = NULL;
	caselist_destroy(&app_state->caselist);
	memset(&app_state->caselist, 0, sizeof(caselist_t));
	show_case_info_window = false;
	show_slide_list_window = false;
}

void reload_global_caselist(app_state_t *app_state, const char *filename) {
	unload_all_images(app_state);
	reset_global_caselist(app_state);
	load_caselist(&app_state->caselist, filename);
}

bool32 load_caselist(caselist_t* caselist, const char* json_filename) {
	file_mem_t* caselist_file = platform_read_entire_file(json_filename);
	bool32 success = false;

	if (caselist_file) {
		JSON_Value* root_value = json_parse_string((const char*)caselist_file->data);
		if (json_value_get_type(root_value) != JSONArray) {
			// failed

		} else {
			// Set the 'working directory' of the case list to the folder the JSON file is located in.
			strncpy(caselist->folder_prefix, json_filename, sizeof(caselist->folder_prefix) - 1);
			char* prefix_end = (char*) one_past_last_slash(caselist->folder_prefix, sizeof(caselist->folder_prefix));
			ASSERT(prefix_end >= caselist->folder_prefix);
			*prefix_end = '\0';
			caselist->prefix_len = strlen(caselist->folder_prefix);

			caselist->json_root_value = root_value;

			JSON_Array* json_cases = json_value_get_array(root_value);
			caselist->case_count = json_array_get_count(json_cases);
			caselist->case_count = json_array_get_count(json_cases);
			if (caselist->case_count > 0) {
				caselist->cases = (case_t*) calloc(1, caselist->case_count * sizeof(case_t));
				caselist->names = (const char**) calloc(1, caselist->case_count * sizeof(void*));


				i32 num_cases_with_filenames = 0;
				for (i32 i = 0; i < caselist->case_count; ++i) {
					i32 curr_index = num_cases_with_filenames;
					case_t* the_case = caselist->cases + curr_index;
					JSON_Object* json_case_obj = json_array_get_object(json_cases, i);
					const char* image_filename = json_object_get_string(json_case_obj, "filename");
					if (image_filename) {
						++num_cases_with_filenames;
						the_case->filename = image_filename;
						the_case->name = json_object_get_string(json_case_obj, "name");
						caselist->names[curr_index] = the_case->name; // for ImGui
						the_case->clinical_context = json_object_get_string(json_case_obj, "clinical_context");
						the_case->diagnosis = json_object_get_string(json_case_obj, "diagnosis");
						the_case->notes = json_object_get_string(json_case_obj, "notes");
					}

				}
				caselist->num_cases_with_filenames = num_cases_with_filenames;


				success = true;
			}



		}


		free(caselist_file);
	}

	return success;
}

void caselist_destroy(caselist_t* caselist) {
	if (caselist) {
		if (caselist->json_root_value) {
			json_value_free(caselist->json_root_value);
			caselist->json_root_value = NULL;
		}
		if (caselist->cases) {
			free(caselist->cases);
			caselist->cases = NULL;
		}
	}

}
