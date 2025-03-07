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
#include "jpeg_decoder.h"
#include "stb_image.h"
#include "miniz.h"

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

static void mrxs_slidedat_ini_parse_section_name(mrxs_t* mrxs, char* section_name, enum mrxs_section_enum* section, i32* layer, i32* level) {
    if (strncmp(section_name, "GENERAL", 7) == 0) {
        *section = MRXS_SECTION_GENERAL;
    } else if (strncmp(section_name, "HIERARCHICAL", 12) == 0) {
        *section = MRXS_SECTION_HIERARCHICAL;
    } else if (strncmp(section_name, "DATAFILE", 8) == 0) {
        *section = MRXS_SECTION_DATAFILE;
    } else {

	    // Check which (non)hier/val combination this section belongs to
	    for (i32 i = 0; i < mrxs->hier_count; ++i) {
		    mrxs_hier_t* hier = mrxs->hier + i;
		    if (!hier->is_ini_section_parsed && hier->section && strcmp(section_name, hier->section) == 0) {
			    *section = MRXS_SECTION_LAYER_N_SECTION;
				*layer = i;
				*level = -1;
			    hier->is_ini_section_parsed = true; // assume each section occurs only once, don't check twice
			    return;
		    }
		    for (i32 j = 0; j < hier->val_count; ++j) {
			    mrxs_hier_val_t* hier_val = hier->val + j;
			    if (!hier_val->is_ini_section_parsed && strcmp(section_name, hier_val->section) == 0) {
				    *section = MRXS_SECTION_LAYER_N_LEVEL_N_SECTION;
				    *layer = i;
				    *level = j;
				    hier_val->is_ini_section_parsed = true; // assume each section occurs only once, don't check twice
				    return;
			    }
		    }
	    }
	    for (i32 i = 0; i < mrxs->nonhier_count; ++i) {
		    mrxs_nonhier_t* nonhier = mrxs->nonhier + i;
		    if (!nonhier->is_ini_section_parsed && nonhier->section && strcmp(section_name, nonhier->section) == 0) {
			    *section = MRXS_SECTION_NONHIERLAYER_N_SECTION;
			    *layer = i;
			    *level = -1;
			    nonhier->is_ini_section_parsed = true; // assume each section occurs only once, don't check twice
			    return;
		    }
		    for (i32 j = 0; j < nonhier->val_count; ++j) {
			    mrxs_nonhier_val_t* nonhier_val = nonhier->val + j;
			    if (!nonhier_val->is_ini_section_parsed && strcmp(section_name, nonhier_val->section) == 0) {
				    *section = MRXS_SECTION_NONHIERLAYER_N_LEVEL_N_SECTION;
				    *layer = i;
				    *level = j;
				    nonhier_val->is_ini_section_parsed = true; // assume each section occurs only once, don't check twice
				    return;
			    }
		    }
	    }
    }
}

static inline const char* mrxs_string_pool_push(mrxs_t* mrxs, const char* s) {
   return (char*)mrxs->string_pool.data + memrw_string_pool_push(&mrxs->string_pool, s);
}

static enum mrxs_image_format_enum mrxs_ini_parse_image_format(const char* value) {
    if (strcmp(value, "JPEG") == 0) {
        return MRXS_IMAGE_FORMAT_JPEG;
    } else if (strcmp(value, "PNG") == 0) {
        return MRXS_IMAGE_FORMAT_PNG;
    } else if (strcmp(value, "BMP24") == 0) {
        return MRXS_IMAGE_FORMAT_BMP;
    } else {
        return MRXS_IMAGE_FORMAT_UNKNOWN;
    }
}

bool mrxs_parse_slidedat_ini(mrxs_t* mrxs, mem_t* slidedat_ini) {
    i64 start = get_clock();
	bool success = true;
    mrxs->string_pool = memrw_create(slidedat_ini->len);
    mrxs->string_pool.is_growing_disallowed = true; // we want stable string pointers, so the buffer can't realloc()
    size_t num_lines = 0;
    char** lines = split_into_lines((char*)slidedat_ini->data, &num_lines);
    if (lines) {
        enum mrxs_section_enum section = MRXS_SECTION_UNKNOWN;
        i32 layer = -1;
        i32 level = -1;
		char section_name[128] = "";
        for (i32 line_index = 0; line_index < num_lines; ++line_index) {
            char* line = lines[line_index];
            while (*(u8*)line >= 128) {
                ++line; // skip the first few characters which may be invalid
            }
            if (line[0] == '[') {
				// Section name: strip [ and ] characters from the string, then parse further
				char* pos = line + 1;
				while (isspace(*pos)) {
					++pos;
				}
				strncpy(section_name, pos, sizeof(section_name));
                size_t len = strlen(section_name);
				pos = section_name + len - 1;
				while (pos > section_name) {
					if (*pos == ']') {
						*pos = '\0';
						break;
					}
					--pos;
				}
                mrxs_slidedat_ini_parse_section_name(mrxs, section_name, &section, &layer, &level);
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
                        } else if (strcmp(key, "CURRENT_SLIDE_VERSION") == 0) {
                            mrxs->slide_version_major = atoi(value);
                            if (value[1] == '.' && value[2] != '\0') {
                                mrxs->slide_version_minor = atoi(value + 2);
                            }
                        } else if (strcmp(key, "CameraImageDivisionsPerSide") == 0) {
                            mrxs->camera_image_divisions_per_slide = atoi(value);
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
                                    hier->val_count = atoi(value);
                                    hier->val = calloc(hier->val_count, sizeof(mrxs_hier_val_t));
                                } else if (strcmp(key_part2, "SECTION") == 0) {
                                    hier->section = mrxs_string_pool_push(mrxs, value);
                                } else if (strncmp(key_part2, "VAL_", 3) == 0) {
                                    i32 val_index = atoi(key_part2 + 4);
                                    if (val_index >= 0 && val_index < hier->val_count) {
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
                        } else if (strncmp(key, "NONHIER_", 8) == 0) {
                            i32 nonhier_index = atoi(key + 8);
                            if (nonhier_index >= 0 && nonhier_index < mrxs->nonhier_count) {
                                mrxs_nonhier_t* nonhier = mrxs->nonhier + nonhier_index;
                                char* key_part2 = find_next_token(key + 8, '_');
                                if (strcmp(key_part2, "NAME") == 0) {
                                    if (strcmp(value, "Scan data layer") == 0) {
                                        nonhier->name = MRXS_NONHIER_SCAN_DATA_LAYER;
                                    } else if (strcmp(value, "StitchingLayer") == 0) {
                                        nonhier->name = MRXS_NONHIER_STITCHING_LAYER;
                                    } else if (strcmp(value, "StitchingIntensityLayer") == 0) {
                                        nonhier->name = MRXS_NONHIER_STITCHING_INTENSITY_LAYER;
                                    } else if (strcmp(value, "VIMSLIDE_HISTOGRAM_DATA") == 0) {
                                        nonhier->name = MRXS_NONHIER_VIMSLIDE_HISTOGRAM_DATA;
                                    }
                                } else if (strcmp(key_part2, "COUNT") == 0) {
                                    nonhier->val_count = atoi(value);
                                    nonhier->val = calloc(nonhier->val_count, sizeof(mrxs_nonhier_val_t));
                                } else if (strcmp(key_part2, "SECTION") == 0) {
                                    nonhier->section = mrxs_string_pool_push(mrxs, value);
                                } else if (strncmp(key_part2, "VAL_", 3) == 0) {
                                    i32 val_index = atoi(key_part2 + 4);
                                    if (val_index >= 0 && val_index < nonhier->val_count) {
                                        mrxs_nonhier_val_t* nonhier_val = nonhier->val + val_index;
                                        char* key_part3 = find_next_token(key_part2 + 4, '_');
                                        if (key_part3 == NULL) {
                                            nonhier_val->name = mrxs_string_pool_push(mrxs, value);
                                            if (nonhier->name == MRXS_NONHIER_SCAN_DATA_LAYER) {
                                                if (strcmp(value, "ScanDataLayer_ScanMap") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANDATALAYER_SCANMAP;
                                                } else if (strcmp(value, "ScanDataLayer_XMLInfoHeader") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANDATALAYER_XMLINFOHEADER;
                                                } else if (strcmp(value, "ScanDataLayer_SlideThumbnail") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANDATALAYER_SLIDETHUMBNAIL;
                                                } else if (strcmp(value, "ScanDataLayer_SlideBarcode") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANDATALAYER_SLIDEBARCODE;
                                                } else if (strcmp(value, "ScanDataLayer_SlidePreview") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANDATALAYER_SLIDEPREVIEW;
                                                } else if (strcmp(value, "ScanDataLayer_StagePositionMap") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANDATALAYER_STAGEPOSITIONMAP;
                                                } else if (strcmp(value, "ScanDataLayer_Empty") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANDATALAYER_EMPTY;
                                                } else if (strcmp(value, "ProfileXMLHeader") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_PROFILEXMLHEADER;
                                                } else if (strcmp(value, "ProfileXML") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_PROFILEXML;
                                                } else if (strcmp(value, "ScannedFOVsMap") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_SCANNEDFOVSMAP;
                                                }
                                            } else if (nonhier->name == MRXS_NONHIER_STITCHING_LAYER) {
                                                if (strcmp(value, "DataLevel_V1.0") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_DATALEVEL_V1_0;
                                                }
                                            } else if (nonhier->name == MRXS_NONHIER_STITCHING_INTENSITY_LAYER) {
                                                if (strcmp(value, "StitchingIntensityLevel") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_STITCHING_INTENSITY_LEVEL;
                                                }
                                            } else if (nonhier->name == MRXS_NONHIER_VIMSLIDE_HISTOGRAM_DATA) {
                                                if (strcmp(value, "default") == 0) {
                                                    nonhier_val->type = MRXS_NONHIER_VAL_VIMSLIDE_HISTOGRAM_DATA_DEFAULT;
                                                }
                                            }

                                        } else if (strcmp(key_part3, "SECTION") == 0) {
                                            nonhier_val->section = mrxs_string_pool_push(mrxs, value);
                                        } else if (strcmp(key_part3, "IMAGENUMBER_X") == 0) {
                                            nonhier_val->imagenumber_x = atoi(value);
                                        } else if (strcmp(key_part3, "IMAGENUMBER_Y") == 0) {
                                            nonhier_val->imagenumber_y = atoi(value);
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


					case MRXS_SECTION_LAYER_N_LEVEL_N_SECTION: {
						ASSERT(layer != -1 && level != -1);
						if (layer != -1 && level != -1) {
							mrxs_hier_t* hier = mrxs->hier + layer;
							mrxs_hier_val_t* hier_val = hier->val + level;

							if (hier_val->type == MRXS_HIER_VAL_ZOOMLEVEL) {
								mrxs_level_t* mrxs_level = mrxs->levels + hier_val->index;
								if (strcmp(key, "DIGITIZER_WIDTH") == 0) {
									mrxs_level->tile_width = atoi(value);
								} else if (strcmp(key, "DIGITIZER_HEIGHT") == 0) {
									mrxs_level->tile_height = atoi(value);
								} else if (strcmp(key, "MICROMETER_PER_PIXEL_X") == 0) {
									mrxs_level->um_per_pixel_x = atof(value);
								} else if (strcmp(key, "MICROMETER_PER_PIXEL_Y") == 0) {
									mrxs_level->um_per_pixel_y = atof(value);
								} else if (strcmp(key, "IMAGE_FILL_COLOR_BGR") == 0) {
									mrxs_level->image_fill_color_bgr = atoi(value);
								} else if (strcmp(key, "IMAGE_FORMAT") == 0) {
                                    mrxs_level->image_format = mrxs_ini_parse_image_format(value);
								}
							}
						}
					} break;

                    case MRXS_SECTION_NONHIERLAYER_N_LEVEL_N_SECTION: {
                        ASSERT(layer != -1 && level != -1);
                        if (layer != -1 && level != -1) {
                            mrxs_nonhier_t* nonhier = mrxs->nonhier + layer;
                            mrxs_nonhier_val_t* nonhier_val = nonhier->val + level;

                            if (nonhier_val->type == MRXS_NONHIER_VAL_SCANDATALAYER_SCANMAP) {
                                if (strcmp(key, "SCANMAP_IMAGE_TYPE") == 0) {
                                    mrxs->scanmap_image.format = mrxs_ini_parse_image_format(value);
                                } else if (strcmp(key, "SCANMAP_IMAGE_WIDTH") == 0) {
                                    mrxs->scanmap_image.width = atoi(value);
                                } else if (strcmp(key, "SCANMAP_IMAGE_HEIGHT") == 0) {
                                    mrxs->scanmap_image.height = atof(value);
                                }
                            } else if (nonhier_val->type == MRXS_NONHIER_VAL_SCANDATALAYER_STAGEPOSITIONMAP) {
                                if (strcmp(key, "STAGEPOSMAP_IMAGE_TYPE") == 0) {
                                    mrxs->stageposmap_image.format = mrxs_ini_parse_image_format(value);
                                } else if (strcmp(key, "STAGEPOSMAP_IMAGE_WIDTH") == 0) {
                                    mrxs->stageposmap_image.width = atoi(value);
                                } else if (strcmp(key, "STAGEPOSMAP_IMAGE_HEIGHT") == 0) {
                                    mrxs->stageposmap_image.height = atof(value);
                                }
                            } else if (nonhier_val->type == MRXS_NONHIER_VAL_SCANDATALAYER_SLIDETHUMBNAIL) {
                                if (strcmp(key, "THUMBNAIL_IMAGE_TYPE") == 0) {
                                    mrxs->thumbnail_image.format = mrxs_ini_parse_image_format(value);
                                } else if (strcmp(key, "THUMBNAIL_IMAGE_WIDTH") == 0) {
                                    mrxs->thumbnail_image.width = atoi(value);
                                } else if (strcmp(key, "THUMBNAIL_IMAGE_HEIGHT") == 0) {
                                    mrxs->thumbnail_image.height = atof(value);
                                }
                            } else if (nonhier_val->type == MRXS_NONHIER_VAL_SCANDATALAYER_SLIDEBARCODE) {
                                if (strcmp(key, "BARCODE_IMAGE_TYPE") == 0) {
                                    mrxs->barcode_image.format = mrxs_ini_parse_image_format(value);
                                } else if (strcmp(key, "BARCODE_IMAGE_WIDTH") == 0) {
                                    mrxs->barcode_image.width = atoi(value);
                                } else if (strcmp(key, "BARCODE_IMAGE_HEIGHT") == 0) {
                                    mrxs->barcode_image.height = atof(value);
                                }
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

	mrxs_level_t* base_level = mrxs->levels + 0;
	if (base_level->um_per_pixel_x > 0.0f && base_level->um_per_pixel_y > 0.0f) {
		mrxs->is_mpp_known = true;
		mrxs->mpp_x = base_level->um_per_pixel_x;
		mrxs->mpp_y = base_level->um_per_pixel_y;
	} else {
		mrxs->is_mpp_known = false;
		mrxs->mpp_x = 1.0f;
		mrxs->mpp_y = 1.0f;
	}
	mrxs->tile_width = base_level->tile_width;
	mrxs->tile_height = base_level->tile_height;

	console_print_verbose("Parsing Slidedat.ini took %g seconds.\n", get_seconds_elapsed(start, get_clock()));

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

bool mrxs_read_stitching_intensity_level(mrxs_t* mrxs, mem_t* index_dat, mrxs_nonhier_val_t* nonhier_val) {
    i32 page_count = 0; // the first page doesn't count, it always has 0 entries
    for (;;) {
        u32 entry_count = 0;
        u32 next_ptr = 0;
        mem_read(&entry_count, index_dat, 4);
        mem_read(&next_ptr, index_dat, 4);
        if (entry_count > 0) {
            // read entries
            for (i32 j = 0; j < entry_count; ++j) {
                mrxs_nonhier_entry_t entry = {};
                mem_read(&entry, index_dat, sizeof(entry));
                mrxs->stitching_intensity_layer_entry = entry;
                DUMMY_STATEMENT;
                return true; // only one (relevant) entry is expected
            }
        }
        if (next_ptr != 0 && next_ptr < index_dat->len) {
            mem_seek(index_dat, next_ptr);
            ++page_count;
        } else {
            break; // last page reached
        }
    }
    return false;
}

bool mrxs_load_slide_position_file(mrxs_t* mrxs) {
    mrxs_nonhier_entry_t entry = mrxs->stitching_intensity_layer_entry;
    bool success = false;
    if (entry.length == 0 || !mrxs->dat_file_handles) {
        return false;
    }
    file_handle_t file_handle = mrxs->dat_file_handles[entry.file];
    if (file_handle) {
        temp_memory_t temp = begin_temp_memory_on_local_thread();
        u8* compressed_data = (u8*)arena_push_size(temp.arena, entry.length);
        size_t bytes_read = file_handle_read_at_offset(compressed_data, file_handle, entry.offset, entry.length);
        if (bytes_read == entry.length) {
            size_t out_len = 0;
            int flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
            mrxs_slide_position_t* position_file = tinfl_decompress_mem_to_heap(compressed_data, entry.length, &out_len, flags);
            ASSERT(sizeof(mrxs_slide_position_t) == 9);
            if (out_len % sizeof(mrxs_slide_position_t) == 0) {
                mrxs->camera_positions = position_file;
                mrxs->camera_position_count = out_len / sizeof(mrxs_slide_position_t);
                success = true;
            } else {
                libc_free(position_file); // error: length not a multiple
            }
        }
        release_temp_memory(&temp);
    }

    /*if (success) {
        for (i32 i = 0; i < mrxs->camera_position_count; ++i) {
            mrxs_slide_position_t position = mrxs->camera_positions[i];
            printf("camera position %d: flag=%d, x=%d, y=%d\n", i, position.flag, position.x, position.y);
        }
    }*/

    return success;
}

bool mrxs_open_from_directory(mrxs_t* mrxs, file_info_t* file, directory_info_t* directory) {
    bool success = false;

	i64 start = get_clock();
    mem_t* slidedat_ini = read_entire_file_in_directory(file->full_filename, "Slidedat.ini");

    if (slidedat_ini) {
        if (mrxs_parse_slidedat_ini(mrxs, slidedat_ini)) {
            mem_t* index_dat = read_entire_file_in_directory(file->full_filename, mrxs->index_dat_filename);
	        if (index_dat) {
		        i64 index_parse_start = get_clock();
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
					mrxs->level_count = hier_zoom_levels->val_count;
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
		                for (i32 val_index = 0; val_index < hier->val_count; ++val_index, ++record_index) {
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

                    // There is one record stored for each HIER_i_VAL_j combination (all in a flat array)
                    i32 record_index = 0;
                    for (i32 nonhier_index = 0; nonhier_index < mrxs->nonhier_count; ++nonhier_index) {
                        mrxs_nonhier_t* nonhier = mrxs->nonhier + nonhier_index;
                        for (i32 val_index = 0; val_index < nonhier->val_count; ++val_index, ++record_index) {
                            mrxs_nonhier_val_t* nonhier_val = nonhier->val + val_index;

                            mem_seek(index_dat, nonhier_root + record_index * sizeof(u32));
                            u32 record_ptr = 0;
                            mem_read(&record_ptr, index_dat, 4);
                            mem_seek(index_dat, record_ptr);
                            if (nonhier->name == MRXS_NONHIER_STITCHING_INTENSITY_LAYER && nonhier_val->type == MRXS_NONHIER_VAL_STITCHING_INTENSITY_LEVEL) {
                                if (!mrxs_read_stitching_intensity_level(mrxs, index_dat, nonhier_val)) {
                                    goto label_failed;
                                }
                            }

                        }
                    }

					success = true;
                } else {
                    failed = true;
                    // TODO: error condition
                }

                free(index_dat);

		        console_print_verbose("Parsing index took %g seconds.\n", get_seconds_elapsed(index_parse_start, get_clock()));
            }
        }
        free(slidedat_ini);
    }
	i64 clock_index_loaded = get_clock();
	console_print_verbose("Slidedat.ini and index loaded in %g seconds.\n", get_seconds_elapsed(start, clock_index_loaded));

	if (success) {
		ASSERT(mrxs->dat_filenames && mrxs->dat_count > 0);
		mrxs->dat_file_handles = malloc(mrxs->dat_count * sizeof(file_handle_t));
		//TODO: measure performance, maybe move to worker threads?
		for (i32 i = 0; i < mrxs->dat_count; ++i) {
			const char* dat_filename = mrxs->dat_filenames[i];
			char full_dat_filename[512];
			snprintf(full_dat_filename, sizeof(full_dat_filename), "%s" PATH_SEP "%s", file->full_filename, dat_filename);
			full_dat_filename[sizeof(full_dat_filename) - 1] = '\0';
			file_handle_t file_handle = open_file_handle_for_simultaneous_access(full_dat_filename);
			if (!file_handle) {
				success = false;
				console_print_error("Error: Could not open file for asynchronous I/O: %s\n", dat_filename);
				break;
			}
			mrxs->dat_file_handles[i] = file_handle;
		}

        console_print_verbose("Opening file handles to %d dat files took %g seconds.\n", mrxs->dat_count, get_seconds_elapsed(clock_index_loaded, get_clock()));

        // Read slide position file
        mrxs_load_slide_position_file(mrxs);
    }


    return success;
}

u8* mrxs_decode_image_to_bgra(u8* compressed_data, size_t compressed_length, enum mrxs_image_format_enum image_format,
        i32 expected_width, i32 expected_height) {
    u8* result = NULL;
    if (image_format == MRXS_IMAGE_FORMAT_JPEG) {
        // JPEG compression
        i32 width = 0;
        i32 height = 0;
        i32 channels_in_file = 0;
        u8* pixels = jpeg_decode_image(compressed_data, compressed_length, &width, &height, &channels_in_file);
        if (pixels && width == expected_width && height == expected_height && channels_in_file == 4) {
            // success
            result = pixels;
        } else {
            if (!pixels) {
                console_print_error("mrxs_decode_tile_to_bgra(): JPEG decoding error\n");
            } else if (width != expected_width && height != expected_height) {
                console_print_error("mrxs_decode_tile_to_bgra(): unexpected tile width/height; expected (%d, %d), got (%d, %d)\n", expected_width, width, expected_height, height);
            } else if (channels_in_file != 4) {
                console_print_error("mrxs_decode_tile_to_bgra(): expected JPEG channels in file to be 4, got %d\n", channels_in_file);
            }
            if (pixels) free(pixels);
            result = NULL;
        }
    } else if (image_format == MRXS_IMAGE_FORMAT_BMP || image_format == MRXS_IMAGE_FORMAT_PNG) {
        // Decode image using stb_image.h
        i32 width = 0;
        i32 height = 0;
        i32 channels_in_file = 0;
        u32* pixels = (u32*)stbi_load_from_memory(compressed_data, compressed_length, &width, &height, &channels_in_file, 4);
        if (pixels && width == expected_width && height == expected_height) {
            // success
            // swap RGBA to BGRA
            i32 pixel_count = width * height;
            for (i32 i = 0; i < pixel_count; ++i) {
                // assume little-endian order: 0xAABBGGRR
                // TODO: big-endian compatibility
                u32 pixel = pixels[i];
                u32 new_pixel = (pixel & 0xFF00FF00) | ((pixel & 0xFF)<<16) | ((pixel>>16) & 0xFF);
                pixels[i] = new_pixel;
            }
            result = (u8*)pixels;
        } else {
            if (!pixels) {
                console_print_error("mrxs_decode_image_to_bgra(): decoding error in stbi_load_from_memory(), image format = %d\n", image_format);
            } else if (width != expected_width && height != expected_height) {
                console_print_error("mrxs_decode_image_to_bgra(): unexpected tile width/height; expected (%d, %d), got (%d, %d)\n", expected_width, width, expected_height, height);
            }
            if (pixels) free(pixels);
            result = NULL;
        }
    } else {
        console_print_error("mrxs_decode_image_to_bgra(): unknown or unsupported image format: %d\n", image_format);
    }
    return result;
}

u8* mrxs_decode_tile_to_bgra(mrxs_t* mrxs, i32 level, i32 tile_index) {
	u8* result = NULL;
	if (level >= 0 && level < mrxs->level_count) {
		mrxs_level_t* mrxs_level = mrxs->levels + level;
		if (tile_index >= 0 && tile_index < mrxs_level->width_in_tiles * mrxs_level->height_in_tiles) {
			mrxs_tile_t* tile = mrxs_level->tiles + tile_index;
			mrxs_hier_entry_t hier_entry = tile->hier_entry;
			if (mrxs->dat_file_handles && hier_entry.file < mrxs->dat_count) {
				file_handle_t file_handle = mrxs->dat_file_handles[hier_entry.file];
				if (file_handle) {
                    temp_memory_t temp = begin_temp_memory_on_local_thread();
					u8* compressed_tile_data = (u8*)arena_push_size(temp.arena, hier_entry.length);
					size_t bytes_read = file_handle_read_at_offset(compressed_tile_data, file_handle, hier_entry.offset, hier_entry.length);
					if (bytes_read == hier_entry.length) {
                        result = mrxs_decode_image_to_bgra(compressed_tile_data, hier_entry.length, mrxs_level->image_format, mrxs->tile_width, mrxs->tile_height);
					}
                    release_temp_memory(&temp);
				}
			}
		}
	}
	return result;
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
    if (mrxs->camera_positions) {
        libc_free(mrxs->camera_positions);
    }
	memset(mrxs, 0, sizeof(mrxs_t));
}
