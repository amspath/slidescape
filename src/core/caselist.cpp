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

#include "common.h"

#include "platform.h"
#define SHEREDOM_JSON_IMPLEMENTATION
#include "json.h"
#include "stringutils.h"

#define CASELIST_IMPL
#include "viewer.h"
#include "caselist.h"
#include "gui.h"
#include "remote.h"


void reset_global_caselist(app_state_t* app_state) {
	app_state->selected_case = NULL;
	app_state->selected_case_index = 0;
	caselist_destroy(&app_state->caselist);
	memset(&app_state->caselist, 0, sizeof(caselist_t));
	show_slide_list_window = false;
}

void reload_global_caselist(app_state_t *app_state, const char *filename) {
	unload_all_images(app_state);
	reset_global_caselist(app_state);
	load_caselist_from_file(&app_state->caselist, filename);
}

bool32 caselist_open_slide(app_state_t* app_state, caselist_t* caselist, slide_info_t* slide) {
	bool32 success = false;
	if (slide->base_filename[0] != '\0') {
		if (caselist->is_remote) {
			success = open_remote_slide(app_state, remote_hostname, atoi(remote_port), slide->base_filename);
		} else {

			// If the SLIDES_DIR environment variable is set, load slides from there
			char path_buffer[2048] = {};
			i32 path_len = snprintf(path_buffer, sizeof(path_buffer), "%s%s", caselist->folder_prefix,
			                        slide->base_filename);

			if (!file_exists(path_buffer)) {
				const char* ext = get_file_extension(slide->base_filename);
				// TODO: search for files with this pattern
				path_len += snprintf(path_buffer + path_len, sizeof(path_buffer) - path_len, ".tiff");
			}

			unload_all_images(app_state);
			file_info_t file = viewer_get_file_info(path_buffer);
			image_t image = load_image_from_file(app_state, &file, NULL, 0);
			add_image(app_state, image, true, false);
			success = image.is_valid;
		}
	}
	return success;
}

bool32 caselist_select_first_case(app_state_t* app_state, caselist_t* caselist) {
	bool32 success = false;
	case_t* first_case = caselist->cases;
	app_state->selected_case = first_case;
	app_state->selected_case_index = 0;
	if (first_case && first_case->slides) {
		success = caselist_open_slide(app_state, caselist, first_case->slides);
	}
	return success;
}

static void caselist_copy_parsed_string(char* dest, json_string_s* payload_string, size_t maxstr) {
	size_t len = MIN(payload_string->string_size, maxstr);
	memcpy(dest, payload_string->string, len);
	dest[len] = '\0';
}

void caselist_parse_slides(caselist_t* caselist, case_t* the_case, json_array_s* slides_array) {
	the_case->slide_count = slides_array->length;
	the_case->slides = (slide_info_t*)(calloc(the_case->slide_count, sizeof(slide_info_t)));

	json_array_element_s* slide_array_element = slides_array->start;
	i32 slide_index = 0;
	while (slide_array_element) {
		if (slide_array_element->value->type == json_type_object) {
			slide_info_t* slide = the_case->slides + slide_index;
			json_object_s* slide_obj = (json_object_s*)slide_array_element->value->payload;
			json_object_element_s* element = slide_obj->start;
			while (element) {
				const char* element_name = element->name->string;
				if (element->value->type == json_type_string) {
					json_string_s* payload_string = (json_string_s*) element->value->payload;
					if (strcmp(element_name, "filename") == 0) {
						caselist_copy_parsed_string(slide->base_filename, payload_string, sizeof(slide->base_filename)-1);
					} else if (strcmp(element_name, "block") == 0) {
						caselist_copy_parsed_string(slide->block, payload_string, sizeof(slide->block)-1);
					} else if (strcmp(element_name, "stain") == 0) {
						caselist_copy_parsed_string(slide->stain, payload_string, sizeof(slide->stain)-1);
					} else if (strcmp(element_name, "notes") == 0) {
						slide->notes = payload_string->string;
					}
				}

				element = element->next;
			}

		}

		slide_array_element->next = slide_array_element;
	}
}

bool32 load_caselist(caselist_t* caselist, const char* json_source, size_t json_length, const char* caselist_name) {

	bool32 success = false;

	if (json_source) {
		json_value_s* root = json_parse(json_source, json_length);
		if (root->type != json_type_array) {
			// failed

		} else {
			caselist->json_root_value = root;

			// The base element of the cases file is an unnamed array; each element is a case
			json_array_s* cases_array = (json_array_s*)root->payload;
			caselist->case_count = (u32)cases_array->length;

			if (caselist->case_count > 0) {
				caselist->cases = (case_t*) calloc(1, caselist->case_count * sizeof(case_t));
				caselist->names = (const char**) calloc(1, caselist->case_count * sizeof(void*));

				json_array_element_s* case_element = cases_array->start;
				i32 case_index = 0;
				while (case_element) {
					bool case_has_slides = false;
					if (case_element->value->type == json_type_object) {
						case_t* the_case = caselist->cases + case_index;
						json_object_s* case_obj = (json_object_s*)case_element->value->payload;

						json_object_element_s* element = case_obj->start;
						while (element) {
							const char* element_name = element->name->string;
							if (element->value->type == json_type_string) {
								json_string_s* payload_string = (json_string_s*) element->value->payload;
								if (strcmp(element_name, "name") == 0) {
									the_case->name = payload_string->string;
								} else if (strcmp(element_name, "clinical_context") == 0) {
									the_case->clinical_context = payload_string->string;
								} else if (strcmp(element_name, "diagnosis") == 0) {
									the_case->diagnosis = payload_string->string;
								} else if (strcmp(element_name, "notes") == 0) {
									the_case->notes = payload_string->string;
								} else if (strcmp(element_name, "filename") == 0) {
									// Note: this element is mutually exclusive with the slides array (filename present means only a single slide!)
									if (!case_has_slides) {
										the_case->slide_count = 1;
										the_case->slides = (slide_info_t*)calloc(1, sizeof(slide_info_t));
										slide_info_t* slide = &the_case->slides[0];
										caselist_copy_parsed_string(slide->base_filename, payload_string, sizeof(slide->base_filename)-1);
										case_has_slides = true;
									} else {
										console_print_error("Caselist parsing error: found a slide filename for case %d, but it already has slides!\n", case_index);
									}
								}
							} else if (element->value->type == json_type_array) {
								json_array_s* payload_array = (json_array_s*) element->value->payload;
								if (strcmp(element_name, "slides") == 0) {
									// Note: the slides array mutually exclusive with the filename element (slides array means there could be any number of slides)
									if (!case_has_slides) {
										caselist_parse_slides(caselist, the_case, payload_array);
										case_has_slides = true;
									} else {
										console_print_error("Caselist parsing error: found a slides array for case %d, but it already has a slide!\n", case_index);
									}

								}
							}
							element = element->next;
						}
					}
					case_element = case_element->next;
					++case_index;
				}

				// TODO: make case names mutable
				for (i32 i = 0; i < caselist->case_count; ++i) {
					case_t* the_case = caselist->cases + case_index;
					if (the_case->name == NULL || the_case->name[0] == '\0') {
						the_case->name = "(unnamed)";
					}
				}

				success = true;

			}
		}
	}
	return success;
}

bool32 load_caselist_from_file(caselist_t* caselist, const char* json_filename) {
	bool32 success = false;
	mem_t* caselist_file = platform_read_entire_file(json_filename);

	if (caselist_file) {
		// Set the 'working directory' of the case list to the folder the JSON file is located in.
		strncpy(caselist->folder_prefix, json_filename, sizeof(caselist->folder_prefix) - 1);
		char* prefix_end = (char*) one_past_last_slash(caselist->folder_prefix, sizeof(caselist->folder_prefix));
		ASSERT(prefix_end >= caselist->folder_prefix);
		*prefix_end = '\0';
		caselist->prefix_len = strlen(caselist->folder_prefix);

		caselist->is_remote = false;
		success = load_caselist(caselist, (char*) caselist_file->data, caselist_file->len, json_filename);
		free(caselist_file);
	}
	return success;
}

bool32 load_caselist_from_remote(caselist_t* caselist, const char* hostname, i32 portno, const char* name) {
	bool32 success = false;

	i32 bytes_read = 0;
	u8* json_file = download_remote_caselist(hostname, portno, name, &bytes_read); // load from remote
	if (json_file) {
		size_t json_length = strlen((char*)json_file);
		if ((i32)json_length != bytes_read) {
			console_print_verbose("Warning: json_length is %d bytes, while bytes_read is %d\n", json_length, bytes_read);
		}
		caselist->is_remote = true;
		success = load_caselist(caselist, (char*) json_file, json_length, name);
		free(json_file);
	}

	return success;
}


void caselist_destroy(caselist_t* caselist) {
	if (caselist) {
		if (caselist->json_root_value) {
			free(caselist->json_root_value);
			caselist->json_root_value = NULL;
		}
		if (caselist->cases) {
			for (i32 i = 0; i < caselist->case_count; ++i) {
				case_t* the_case = caselist->cases + i;
				if (the_case->slides) {
					for (i32 j = 0; j < the_case->slide_count; ++j) {
						// nothing to do here
					}
					free(the_case->slides);
				}
			}

			free(caselist->cases);
			caselist->cases = NULL;
		}
	}

}
