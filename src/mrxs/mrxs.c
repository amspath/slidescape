/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2024  Pieter Valkema

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
#include "mrxs.h"
#include "stringutils.h"
#include "listing.h"
#include "viewer.h" // for file_info_t and directory_info_t

#include <ctype.h> // for isspace()



static file_stream_t open_file_in_directory(const char* dirname, const char* filename) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s" PATH_SEP "%s.ini", dirname, filename);
    buffer[sizeof(buffer)-1] = '\0';
    return file_stream_open_for_reading(buffer);
}

static mem_t* read_entire_file_in_directory(const char* dirname, const char* filename) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s" PATH_SEP "%s", dirname, filename);
    buffer[sizeof(buffer)-1] = '\0';
    return platform_read_entire_file(buffer);
}

static void mrxs_slidedat_ini_parse_section_name(char* section_name, enum mrxs_section_enum* section, i32* layer, i32* level) {
    if (strncmp(section_name, "GENERAL", 7) == 0) {
        *section = MRXS_SECTION_GENERAL;
    } else if (strncmp(section_name, "HIERARCHICAL", 12) == 0) {
        *section = MRXS_SECTION_HIERARCHICAL;
    } else if (strncmp(section_name, "DATAFILE", 8) == 0) {
        *section = MRXS_SECTION_DATAFILE;
    } else {
        size_t section_name_len = strlen(section_name);
        char* next_name_part = NULL;
        if (strncmp(section_name, "LAYER_", 6) == 0) {
            next_name_part = find_next_token(section_name + 6, '_');
            if (next_name_part) {
                if (strncmp(next_name_part, "SECTION", 7) == 0) {
                    *section = MRXS_SECTION_LAYER_N_SECTION;
                } else if (strncmp(next_name_part, "LEVEL_", 6) == 0) {
                    *section = MRXS_SECTION_LAYER_N_LEVEL_N_SECTION;
                }
            } else {
                *section = MRXS_SECTION_UNKNOWN; // error condition
            }
        } else if (strncmp(section_name, "NONHIERLAYER_", 13) == 0) {
            next_name_part = find_next_token(section_name + 13, '_');
            if (next_name_part) {
                if (strncmp(next_name_part, "SECTION", 7) == 0) {
                    *section = MRXS_SECTION_NONHIERLAYER_N_SECTION;
                } else if (strncmp(next_name_part, "LEVEL_", 6) == 0) {
                    *section = MRXS_SECTION_NONHIERLAYER_N_LEVEL_N_SECTION;
                }
            } else {
                *section = MRXS_SECTION_UNKNOWN; // error condition
            }
        }
        if (next_name_part) {
            strip_character(next_name_part, '_');
            if (*section == MRXS_SECTION_LAYER_N_SECTION || *section == MRXS_SECTION_NONHIERLAYER_N_SECTION) {
                *layer = atoi(next_name_part);
            } else if (*section == MRXS_SECTION_LAYER_N_LEVEL_N_SECTION || *section == MRXS_SECTION_NONHIERLAYER_N_LEVEL_N_SECTION) {
                *layer = atoi(next_name_part);
                *level = atoi(next_name_part + 6);
            }
        }
    }
}

static inline const char* mrxs_string_pool_push(mrxs_t* mrxs, const char* s) {
   return (char*)mrxs->string_pool.data + memrw_string_pool_push(&mrxs->string_pool, s);
}

bool mrxs_parse_slidedat_ini(mrxs_t* mrxs, mem_t* slidedat_ini) {
    bool success = true;
    mrxs->string_pool = memrw_create(KILOBYTES(64));
    mrxs->string_pool.is_growing_disallowed = true; // we want stable string pointers, so the buffer can't realloc()
    size_t num_lines = 0;
    char** lines = split_into_lines((char*)slidedat_ini->data, &num_lines);
    if (lines) {
        enum mrxs_section_enum section = MRXS_SECTION_UNKNOWN;
        i32 layer = -1;
        i32 level = -1;
        for (i32 line_index = 0; line_index < num_lines; ++line_index) {
            char* line = lines[line_index];
            while (*(u8*)line >= 128) {
                ++line; // skip the first few characters which may be invalid
            }
            if (line[0] == '[') {
                char* section_name = line + 1;
                mrxs_slidedat_ini_parse_section_name(section_name, &section, &layer, &level);
            } else {
                char* value = find_next_token(line, '=');
                if (!value) {
                    // TODO: error condition
                    continue;
                }
                char* key = line;
                *(value-1) = '\0'; // remove '=' sign to zero-terminate key
                // strip trailing whitespace after key
                size_t key_len = strlen(key);
                if (key_len >= 1) {
                    for (i32 i = key_len - 1; i >= 0; --i) {
                        if (isspace(key[i])) {
                            key[i] = '\0';
                        } else {
                            break;
                        }
                    }
                }
                // strip preceding whitespace before value
                while(isspace(*value)) {
                    ++value;
                }

                switch(section) {
                    case MRXS_SECTION_GENERAL: {
                        if (strcmp(key, "IMAGENUMBER_X") == 0) {
                            mrxs->base_width_in_tiles = atoi(value);
                        } else if (strcmp(key, "IMAGENUMBER_Y") == 0) {
                            mrxs->base_height_in_tiles = atoi(value);
                        }
                    } break;
                    case MRXS_SECTION_HIERARCHICAL: {
                        if (strcmp(key, "INDEXFILE") == 0) {
                            mrxs->index_dat_filename = mrxs_string_pool_push(mrxs, value);
                        } else if (strcmp(key, "HIER_COUNT") == 0) {
                            mrxs->hier_count = atoi(value);
                            if (mrxs->hier == NULL) {
                                mrxs->hier = calloc(mrxs->hier_count, sizeof(mrxs_hier_t));
                            } else {
                                //TODO: error condition
                            }
                        } else if (strcmp(key, "NONHIER_COUNT") == 0) {
                            mrxs->nonhier_count = atoi(value);
                            if (mrxs->nonhier == NULL) {
                                mrxs->nonhier = calloc(mrxs->nonhier_count, sizeof(mrxs_nonhier_t));
                            } else {
                                //TODO: error condition
                            }
                        } else if (strncmp(key, "HIER_", 5) == 0) {
                            i32 hier_index = atoi(key + 5);
                            if (hier_index >= 0 && hier_index < mrxs->hier_count) {
                                mrxs_hier_t* hier = mrxs->hier + hier_index;
                                char* key_part2 = find_next_token(key + 5, '_');
                                if (strcmp(key_part2, "NAME") == 0) {
                                    if (strcmp(value, "Slide zoom level") == 0) {
                                        mrxs->slide_zoom_level_hier_index = hier_index;
                                        hier->name = MRXS_HIER_SLIDE_ZOOM_LEVEL;
                                    } else if (strcmp(value, "Slide filter level") == 0) {
                                        hier->name = MRXS_HIER_SLIDE_FILTER_LEVEL;
                                    } else if (strcmp(value, "Microscope focus level") == 0) {
                                        hier->name = MRXS_HIER_MICROSCOPE_FOCUS_LEVEL;
                                    } else if (strcmp(value, "Scan info layer") == 0) {
                                        hier->name = MRXS_HIER_SCAN_INFO_LAYER;
                                    }
                                } else if (strcmp(key_part2, "COUNT") == 0) {
                                    hier->count = atoi(value);
                                    hier->val = calloc(hier->count, sizeof(mrxs_hier_val_t));
                                } else if (strcmp(key_part2, "SECTION") == 0) {
                                    hier->section = mrxs_string_pool_push(mrxs, value);
                                } else if (strncmp(key_part2, "VAL_", 3) == 0) {
                                    i32 val_index = atoi(key_part2 + 4);
                                    if (val_index >= 0 && val_index < hier->count) {
                                        mrxs_hier_val_t* hier_val = hier->val + val_index;
                                        char* key_part3 = find_next_token(key_part2 + 4, '_');
                                        if (key_part3 == NULL) {
                                            hier_val->name = mrxs_string_pool_push(mrxs, value);
                                            if (strncmp(value, "ZoomLevel_", 10) == 0) {
                                                hier_val->type = MRXS_HIER_VAL_ZOOMLEVEL;
                                                hier_val->index = atoi(value + 10);
                                                if (hier_val->index >= 0 && hier_val->index < COUNT(mrxs->levels)) {
                                                    mrxs->levels[hier_val->index].hier_val_index = hier_val->index;
                                                }
                                            }
                                        } else if (strcmp(key_part3, "SECTION") == 0) {
                                            hier_val->section = mrxs_string_pool_push(mrxs, value);
                                            if (hier_val->type == MRXS_HIER_VAL_ZOOMLEVEL) {
                                                if (hier_val->index >= 0 && hier_val->index < COUNT(mrxs->levels)) {
                                                    mrxs->levels[hier_val->index].section_name = hier_val->section;
                                                }
                                            }
                                        }
                                    }
                                }

                            }
                        }
                    } break;
                    case MRXS_SECTION_DATAFILE: {
                        if (strcmp(key, "FILE_COUNT") == 0) {
                            mrxs->dat_count = atoi(value);
                            ASSERT(mrxs->dat_filenames == NULL);
                            if (mrxs->dat_filenames == NULL) {
                                mrxs->dat_filenames = calloc(mrxs->dat_count, sizeof(char*));
                            } else {
                                //todo: error condition
                            }
                        } else if (strncmp(key, "FILE_", 5) == 0) {
                            i32 file_index = atoi(key + 5);
                            if (file_index >= 0 && file_index < mrxs->dat_count && mrxs->dat_filenames) {
                                ASSERT(mrxs->dat_filenames[file_index] == NULL);
                                mrxs->dat_filenames[file_index] = (char*)mrxs->string_pool.data + memrw_string_pool_push(&mrxs->string_pool, value);
                            }
                        }
                    } break;

                    case MRXS_SECTION_UNKNOWN:
                    default: {

                    } break;
                }
            }
        }
        free(lines);
    }
    if (!mrxs->index_dat_filename || mrxs->dat_count == 0 || !mrxs->dat_filenames) {
        success = false;
    }
    return success;
}

bool mrxs_read_index_dat_slide_zoom_level(mrxs_t* mrxs, mem_t* index_dat, mrxs_hier_val_t* hier_val) {
	i32 scale = hier_val->index;
	if (!(scale >= 0 && scale < mrxs->level_count)) {
		return false;
	}
	mrxs_level_t* level = mrxs->levels + scale;

	i32 page_count = 0; // the first page doesn't count, it always has 0 entries
	for (;;) {
		u32 entry_count = 0;
		u32 next_ptr = 0;
		mem_read(&entry_count, index_dat, 4);
		mem_read(&next_ptr, index_dat, 4);
		if (entry_count > 0) {
			// read entries
			for (i32 j = 0; j < entry_count; ++j) {
				mrxs_hier_entry_t entry = {};
				mem_read(&entry, index_dat, sizeof(entry));
				i32 tile_index_x = entry.image % mrxs->base_width_in_tiles;
				i32 tile_index_y = entry.image / mrxs->base_width_in_tiles;
				tile_index_x >>= scale;
				tile_index_y >>= scale;
				if (tile_index_x < level->width_in_tiles && tile_index_y < level->height_in_tiles) {
					mrxs_tile_t* tile = level->tiles + tile_index_y * level->width_in_tiles + tile_index_x;
					tile->hier_entry = entry;
					DUMMY_STATEMENT;
				} else {
					DUMMY_STATEMENT;
				}
				DUMMY_STATEMENT;
			}
		}
		if (next_ptr != 0 && next_ptr < index_dat->len) {
			mem_seek(index_dat, next_ptr);
			++page_count;
		} else {
			break; // last page reached
		}
	}
	return true;
}

bool mrxs_open_from_directory(mrxs_t* mrxs, file_info_t* file, directory_info_t* directory) {
    bool success = false;

    mem_t* slidedat_ini = read_entire_file_in_directory(file->full_filename, "Slidedat.ini");

    if (slidedat_ini) {
        if (mrxs_parse_slidedat_ini(mrxs, slidedat_ini)) {
            mem_t* index_dat = read_entire_file_in_directory(file->full_filename, mrxs->index_dat_filename);
            if (index_dat) {
                char version[6] = {};
                char slide_id[33] = {};
                u32 hier_root = 0;
                u32 nonhier_root = 0;
                mem_read(version, index_dat, 5);
                mem_read(slide_id, index_dat, 32);
                mem_read(&hier_root, index_dat, 4);
                mem_read(&nonhier_root, index_dat, 4);

                bool failed = false;
                if (hier_root > 0 && hier_root < index_dat->len) {

					// Initialize some basic stuff
					mrxs_hier_t* hier_zoom_levels = mrxs->hier + mrxs->slide_zoom_level_hier_index;
					mrxs->level_count = hier_zoom_levels->count;
					for (i32 i = 0; i < mrxs->level_count; ++i) {
						mrxs_level_t* level = mrxs->levels + i;
						level->width_in_tiles = (mrxs->base_width_in_tiles + (1 << i) - 1) >> i;
						level->height_in_tiles = (mrxs->base_height_in_tiles + (1 << i) - 1) >> i;
						u32 tile_count = level->width_in_tiles * level->height_in_tiles;
						level->tiles = calloc(tile_count, sizeof(mrxs_tile_t ));
					}

	                // There is one record stored for each HIER_i_VAL_j combination (all in a flat array)
					i32 record_index = 0;
	                for (i32 hier_index = 0; hier_index < mrxs->hier_count; ++hier_index) {
		                mrxs_hier_t* hier = mrxs->hier + hier_index;
		                for (i32 val_index = 0; val_index < hier->count; ++val_index, ++record_index) {
			                mrxs_hier_val_t* hier_val = hier->val + val_index;

			                mem_seek(index_dat, hier_root + record_index * sizeof(u32));
			                u32 record_ptr = 0;
			                mem_read(&record_ptr, index_dat, 4);
			                mem_seek(index_dat, record_ptr);
							if (hier->name == MRXS_HIER_SLIDE_ZOOM_LEVEL && hier_val->type == MRXS_HIER_VAL_ZOOMLEVEL) {
								if (!mrxs_read_index_dat_slide_zoom_level(mrxs, index_dat, hier_val)) {
									goto label_failed;
								}
							}

		                }
	                }

                } else {
					label_failed:
					failed = true;
                    // TODO: error condition
                }

                if (!failed && nonhier_root > 0 && nonhier_root < index_dat->len) {
                    mem_seek(index_dat, nonhier_root);
//                    u32* record_ptrs = malloc(mrxs->nonhier_count * sizeof(u32));
//	                mem_read(record_ptrs, index_dat, mrxs->nonhier_count * sizeof(u32));
//                    for (i32 i = 0; i < mrxs->nonhier_count; ++i) {
//                        u32 record_ptr = record_ptrs[i];
//
//                    }
//
//                    free(record_ptrs);
					success = true;
                } else {
                    failed = true;
                    // TODO: error condition
                }

                free(index_dat);
            }
        }
        free(slidedat_ini);
    }

	if (success) {
		ASSERT(mrxs->dat_filenames && mrxs->dat_count > 0);
		mrxs->dat_file_handles = malloc(mrxs->dat_count * sizeof(file_handle_t));
		//TODO: measure performance, maybe move to worker threads?
		for (i32 i = 0; i < mrxs->dat_count; ++i) {
			const char* dat_filename = mrxs->dat_filenames[i];
			file_handle_t file_handle = open_file_handle_for_simultaneous_access(dat_filename);
			if (!file_handle) {
				success = false;
				console_print_error("Error: Could not open file for asynchronous I/O: %s\n", dat_filename);
				break;
			}
			mrxs->dat_file_handles[i] = file_handle;
		}
	}


    return success;
}

// Set the work queue to submit parallel jobs to
// TODO(pvalkema): rethink this?
void mrxs_set_work_queue(mrxs_t* mrxs, work_queue_t* queue) {
	mrxs->work_submission_queue = queue;
}

void mrxs_destroy(mrxs_t* mrxs) {
	while (mrxs->refcount > 0) {
		platform_sleep(1);
		if (mrxs->work_submission_queue) {
			work_queue_do_work(mrxs->work_submission_queue, 0);
		} else {
			static bool already_printed = false;
			if (!already_printed) {
				console_print_error("mrxs_destroy(): work_submission_queue not set; refcount = %d, waiting to reach 0\n", mrxs->refcount);
				already_printed = true;
			}
		}
	}
	if (mrxs->dat_file_handles) {
		for (i32 i = 0; i < mrxs->dat_count; ++i) {
			file_handle_t file_handle = mrxs->dat_file_handles[i];
			if (file_handle) {
				file_handle_close(file_handle);
			}
		}
		free(mrxs->dat_file_handles);
	}
	memrw_destroy(&mrxs->string_pool);
	if (mrxs->dat_filenames) {
		free(mrxs->dat_filenames);
	}
	if (mrxs->hier) {
		free(mrxs->hier);
	}
	if (mrxs->nonhier) {
		free(mrxs->nonhier);
	}
	memset(mrxs, 0, sizeof(mrxs_t));
}
