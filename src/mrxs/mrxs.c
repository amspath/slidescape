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


typedef struct mrxs_hier_entry_t {
    u32 image;
    u32 offset;
    u32 length;
    u32 file;
} mrxs_hier_entry_t;

typedef struct mrxs_nonhier_entry_t {
    u32 padding1;
    u32 padding2;
    u32 offset;
    u32 length;
    u32 file;
} mrxs_nonhier_entry_t;

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
    if (!mrxs->index_dat_filename || arrlen(mrxs->dat_filenames) == 0) {
        success = false;
    }
    return success;
}

bool mrxs_open_from_directory(mrxs_t* mrxs, file_info_t* file, directory_info_t* directory) {
    bool success = false;

    mem_t* slidedat_ini = read_entire_file_in_directory(file->filename, "Slidedat.ini");

    if (slidedat_ini) {
        if (mrxs_parse_slidedat_ini(mrxs, slidedat_ini)) {
            mem_t* index_dat = read_entire_file_in_directory(file->filename, mrxs->index_dat_filename);
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
                if (hier_root > 0 && hier_root < file->filesize) {
                    mem_seek(index_dat, hier_root);
                    u32* record_ptrs = malloc(mrxs->hier_count * sizeof(u32));
                    for (i32 i = 0; i < mrxs->hier_count; ++i) {
                        u32 record_ptr = record_ptrs[i];
                        mem_seek(index_dat, record_ptr);


                    }

                    free(record_ptrs);
                } else {
                    failed = true;
                    // TODO: error condition
                }

                if (!failed && nonhier_root > 0 && nonhier_root < file->filesize) {
                    mem_seek(index_dat, nonhier_root);
                    u32* record_ptrs = malloc(mrxs->nonhier_count * sizeof(u32));
                    for (i32 i = 0; i < mrxs->nonhier_count; ++i) {
                        u32 record_ptr = record_ptrs[i];

                    }

                    free(record_ptrs);
                } else {
                    failed = true;
                    // TODO: error condition
                }

                free(index_dat);
            }
        }
        free(slidedat_ini);
    }

//    file_stream_t fp = file_stream_open_for_reading(filename);
//    if (fp) {
//
//        file_stream_close(fp);
//    }

    return success;
}
