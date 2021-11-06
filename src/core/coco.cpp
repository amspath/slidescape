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

#include "common.h"

#include "platform.h"
//#define SHEREDOM_JSON_IMPLEMENTATION
#include "json.h"
#include "stringutils.h"

#include "coco.h"
#include "viewer.h"

#include <time.h>



static void coco_copy_parsed_string(coco_t* coco, char* dest, json_string_s* payload_string) {
	size_t len = MIN(payload_string->string_size, COCO_MAX_FIELD-1);
	memcpy(dest, payload_string->string, len);
	dest[len] = '\0';
}

static void coco_parse_info(coco_t* coco, json_object_s* info) {
	console_print_verbose("[JSON] parsing info\n");
	json_object_element_s* element = info->start;
	while (element) {
		const char* element_name = element->name->string;
		if (element->value->type == json_type_string) {
			json_string_s* payload_string = (json_string_s*) element->value->payload;
			if (strcmp(element_name, "description") == 0) {
				coco_copy_parsed_string(coco, coco->info.description, payload_string);
			} else if (strcmp(element_name, "url") == 0) {
				coco_copy_parsed_string(coco, coco->info.url, payload_string);
			} else if (strcmp(element_name, "version") == 0) {
				coco_copy_parsed_string(coco, coco->info.version, payload_string);
			} else if (strcmp(element_name, "contributor") == 0) {
				coco_copy_parsed_string(coco, coco->info.contributor, payload_string);
			} else if (strcmp(element_name, "date_created") == 0) {
				coco_copy_parsed_string(coco, coco->info.date_created, payload_string);
			}
		} else if (element->value->type == json_type_number) {
			json_number_s* payload_number = (json_number_s*) element->value->payload;
			if (strcmp(element_name, "year") == 0) {
				coco->info.year = atoi(payload_number->number);
			}
		}


		element = element->next;
	}
}

static void coco_parse_licenses(coco_t* coco, json_array_s* info) {
	console_print_verbose("[JSON] parsing licenses\n");

	json_array_element_s* array_element = info->start;
	while (array_element) {
		if (array_element->value->type == json_type_object) {
			json_object_s* license_object = (json_object_s*)array_element->value->payload;
			json_object_element_s* element = license_object->start;
			coco_license_t* license = arraddnptr(coco->licenses, 1);
			memset(license, 0, sizeof(*license));
			++coco->license_count;
			while (element) {
				const char* element_name = element->name->string;
				if (element->value->type == json_type_string) {
					json_string_s* payload_string = (json_string_s*) element->value->payload;
					if (strcmp(element_name, "url") == 0) {
						coco_copy_parsed_string(coco, license->url, payload_string);
					} else if (strcmp(element_name, "name") == 0) {
						coco_copy_parsed_string(coco, license->name, payload_string);
					}
				} else if (element->value->type == json_type_number) {
					json_number_s* payload_number = (json_number_s*) element->value->payload;
					if (strcmp(element_name, "id") == 0) {
						license->id = atoi(payload_number->number);
					}
				}
				element = element->next;
			}
		}

		array_element = array_element->next;
	}
}

static void coco_parse_images(coco_t* coco, json_array_s* info) {
	console_print_verbose("[JSON] parsing images\n");

	json_array_element_s* array_element = info->start;
	while (array_element) {
		if (array_element->value->type == json_type_object) {
			json_object_s* image_object = (json_object_s*)array_element->value->payload;
			json_object_element_s* element = image_object->start;
			coco_image_t* image = arraddnptr(coco->images, 1);
			memset(image, 0, sizeof(*image));
			++coco->image_count;
			while (element) {
				const char* element_name = element->name->string;
				if (element->value->type == json_type_string) {
					json_string_s* payload_string = (json_string_s*) element->value->payload;
					if (strcmp(element_name, "file_name") == 0) {
						coco_copy_parsed_string(coco, image->file_name, payload_string);
					} else if (strcmp(element_name, "coco_url") == 0) {
						coco_copy_parsed_string(coco, image->coco_url, payload_string);
					} else if (strcmp(element_name, "flickr_url") == 0) {
						coco_copy_parsed_string(coco, image->flickr_url, payload_string);
					} else if (strcmp(element_name, "date_captured") == 0) {
						coco_copy_parsed_string(coco, image->date_captured, payload_string);
					}
				} else if (element->value->type == json_type_number) {
					json_number_s* payload_number = (json_number_s*) element->value->payload;
					if (strcmp(element_name, "id") == 0) {
						image->id = atoi(payload_number->number);
					} else if (strcmp(element_name, "license") == 0) {
						image->license = atoi(payload_number->number);
					} else if (strcmp(element_name, "width") == 0) {
						image->width = atoi(payload_number->number);
					} else if (strcmp(element_name, "height") == 0) {
						image->height = atoi(payload_number->number);
					}
				}
				element = element->next;
			}
		}

		array_element = array_element->next;
	}
}

static void coco_parse_annotations(coco_t* coco, json_array_s* info) {
	console_print_verbose("[JSON] parsing annotations\n");

	json_array_element_s* array_element = info->start;
	while (array_element) {
		if (array_element->value->type == json_type_object) {

			json_object_s* annotation_object = (json_object_s*)array_element->value->payload;
			json_object_element_s* element = annotation_object->start;
			coco_annotation_t* annotation = arraddnptr(coco->annotations, 1);
			memset(annotation, 0, sizeof(*annotation));
			++coco->annotation_count;
			while (element) {
				const char* element_name = element->name->string;
				// outer array (each element is a 'segmentation' associated with the annotation
				// For now, we assume that there is only a single segmentation
				// TODO: accept multiple segmentations per annotation
				if (element->value->type == json_type_array) {
					json_array_s* payload_array = (json_array_s*) element->value->payload;
					json_array_element_s* sub_array_element = payload_array->start;
					if (strcmp(element_name, "segmentation") == 0) {
						// [[x, y, x, y, x, y, ... ]]
						i32 coordinate_count = 0;
						v2f* coordinates = NULL; // sb
						// Assuming only a single element exists...
						/*while*/if (sub_array_element) {
							if (sub_array_element->value->type == json_type_array) {
								json_array_s* coordinate_array = (json_array_s*) sub_array_element->value->payload;
								json_array_element_s* coordinate_array_element = coordinate_array->start;
								i32 number_index = 0;
								v2f new_coord = {};
								while (coordinate_array_element) {
									i32 coordinate_index = number_index / 2;
									bool is_x = (number_index % 2) == 0; // X and Y coordinates are interleaved
									float number = 0.0;
									if (coordinate_array_element->value->type == json_type_number) {
										json_number_s* payload_number = (json_number_s*) coordinate_array_element->value->payload;
										number = strtof(payload_number->number, NULL);
									}
									if (is_x) {
										new_coord.x = number;
									} else {
										new_coord.y = number;
										arrput(coordinates, new_coord);
										++coordinate_count;
										new_coord = (v2f){}; // reset for next iteration
									}
									coordinate_array_element = coordinate_array_element->next;
									++number_index;
								}
							}
							sub_array_element = sub_array_element->next;
						}
						annotation->segmentation.coordinates = coordinates;
						annotation->segmentation.coordinate_count = coordinate_count;
					} else if (strcmp(element_name, "bbox") == 0) {
						i32 coordinate_index = 0;
						float coordinates[4] = {};
						while (sub_array_element && coordinate_index < 4) {
							if (sub_array_element->value->type == json_type_number) {
								json_number_s* payload_number = (json_number_s*) element->value->payload;
								coordinates[coordinate_index] = strtof(payload_number->number, NULL);
								++coordinate_index;
							}
							sub_array_element = sub_array_element->next;
						}
						annotation->bbox = *((rect2f*)coordinates);
					} else if (strcmp(element_name, "features") == 0) {
						//[id,value,id,value,...]
						i32 sub_element_index = 0;
						i32 feature_id = 0;
						while (sub_array_element && sub_element_index < (COCO_MAX_ANNOTATION_FEATURES * 2)) {
							if (sub_array_element->value->type == json_type_number) {
								json_number_s* payload_number = (json_number_s*) element->value->payload;
								if (sub_element_index % 2 == 0) {
									feature_id = atoi(payload_number->number);
								} else {
									annotation->features[feature_id] = strtof(payload_number->number, NULL);
								}
								++sub_element_index;
							}
							sub_array_element = sub_array_element->next;
						}
					}
				} else if (element->value->type == json_type_number) {
					json_number_s* payload_number = (json_number_s*) element->value->payload;
					if (strcmp(element_name, "id") == 0) {
						annotation->id = atoi(payload_number->number);
					} else if (strcmp(element_name, "category_id") == 0) {
						annotation->category_id = atoi(payload_number->number);
					} else if (strcmp(element_name, "image_id") == 0) {
						annotation->image_id = atoi(payload_number->number);
					} else if (strcmp(element_name, "area") == 0) {
						annotation->area = strtof(payload_number->number, NULL);
					}
				}
				element = element->next;
			}
		}

		array_element = array_element->next;
	}
}

static void coco_parse_categories(coco_t* coco, json_array_s* info) {
	console_print_verbose("[JSON] parsing categories\n");

	json_array_element_s* array_element = info->start;
	while (array_element) {
		if (array_element->value->type == json_type_object) {
			json_object_s* category_object = (json_object_s*)array_element->value->payload;
			json_object_element_s* element = category_object->start;
			coco_category_t* category = arraddnptr(coco->categories, 1);
			memset(category, 0, sizeof(*category));
			++coco->category_count;
			while (element) {
				const char* element_name = element->name->string;
				if (element->value->type == json_type_string) {
					json_string_s* payload_string = (json_string_s*) element->value->payload;
					if (strcmp(element_name, "supercategory") == 0) {
						coco_copy_parsed_string(coco, category->supercategory, payload_string);
					} else if (strcmp(element_name, "name") == 0) {
						coco_copy_parsed_string(coco, category->name, payload_string);
					}
				} else if (element->value->type == json_type_number) {
					json_number_s* payload_number = (json_number_s*) element->value->payload;
					if (strcmp(element_name, "id") == 0) {
						category->id = atoi(payload_number->number);
					}
				} else if (element->value->type == json_type_array) {
					json_array_s* payload_array = (json_array_s*) element->value->payload;
					json_array_element_s* sub_array_element = payload_array->start;
					if (strcmp(element_name, "color") == 0) {
						i32 array_index = 0;
						while (sub_array_element) {
							if (sub_array_element->value->type == json_type_number) {
								json_number_s* payload_number = (json_number_s*) sub_array_element->value->payload;
								if (array_index < 4) {
									category->color.values[array_index] = atoi(payload_number->number);
								}
							}
							++array_index;
							sub_array_element = sub_array_element->next;
						}
					}
				}
				element = element->next;
			}
		}

		array_element = array_element->next;
	}
}

static void coco_parse_features(coco_t* coco, json_array_s* info) {
	console_print_verbose("[JSON] parsing features\n");

	json_array_element_s* array_element = info->start;
	while (array_element) {
		if (array_element->value->type == json_type_object) {
			json_object_s* feature_object = (json_object_s*)array_element->value->payload;
			json_object_element_s* element = feature_object->start;
			coco_feature_t* feature = arraddnptr(coco->features, 1);
			memset(feature, 0, sizeof(*feature));
			++coco->feature_count;
			while (element) {
				const char* element_name = element->name->string;
				if (element->value->type == json_type_string) {
					json_string_s* payload_string = (json_string_s*) element->value->payload;
					if (strcmp(element_name, "name") == 0) {
						coco_copy_parsed_string(coco, feature->name, payload_string);
					}
				} else if (element->value->type == json_type_number) {
					json_number_s* payload_number = (json_number_s*) element->value->payload;
					if (strcmp(element_name, "id") == 0) {
						feature->id = atoi(payload_number->number);
					} else if (strcmp(element_name, "category_id") == 0) {
						feature->category_id = atoi(payload_number->number);
						feature->restrict_to_group = true;
					}
				}
				element = element->next;
			}
		}

		array_element = array_element->next;
	}
}

bool open_coco(coco_t* coco, const char* json_source, size_t json_length) {
	bool success = false;

//	message_box(&global_app_state, "Trying to parse the JSON now");
	i64 timer_begin = get_clock();

	coco->original_filesize = json_length;

	if (json_source) {
		// NOTE: this may take a LONG time and use a LOT of memory, depending on the size of the JSON file.
		// TODO: execute on worker thread
		json_value_s* root = json_parse(json_source, json_length);
		if (root) {
			if (root->type != json_type_object) {
				// failed
			} else {
				json_object_s* object = (json_object_s*)root->payload;
				console_print_verbose("[JSON] Root object has length %d\n", object->length);
				ASSERT(object->length >= 1);
				json_object_element_s* element = object->start;
				coco->is_valid = true;
				while (element) {
					const char* element_name = element->name->string;
					json_object_s* payload_object = (json_object_s*)element->value->payload;
					json_array_s* payload_array = (json_array_s*)element->value->payload;

					if (element->value->type == json_type_object) {
						if (strcmp(element_name, "info") == 0) {
							coco_parse_info(coco, payload_object);
						}
					} else if (element->value->type == json_type_array) {
						if (strcmp(element_name, "licenses") == 0) {
							coco_parse_licenses(coco, payload_array);
						} else if (strcmp(element_name, "images") == 0) {
							coco_parse_images(coco, payload_array);
						} else if (strcmp(element_name, "annotations") == 0) {
							coco_parse_annotations(coco, payload_array);
						} else if (strcmp(element_name, "categories") == 0) {
							coco_parse_categories(coco, payload_array);
						} else if (strcmp(element_name, "features") == 0) {
							coco_parse_features(coco, payload_array);
						}
					}
					element = element->next;
				}




				console_print("Loaded COCO JSON in %g seconds\n", get_seconds_elapsed(timer_begin, get_clock()));
//				message_box(&global_app_state, "Finished parsing JSON");

			}
			free(root);
		} else {
			console_print_error("JSON parse error\n");
		}

	}

	return coco->is_valid;
}

bool load_coco_from_file(coco_t* coco, const char* json_filename) {
	bool success = false;
	mem_t* coco_file = platform_read_entire_file(json_filename);

	if (coco_file) {
		success = open_coco(coco, (char*) coco_file->data, coco_file->len);
		if (success) {

		}

		free(coco_file);
	}
	return success;
}

static void coco_output_info(coco_t* coco, memrw_t* out) {
	char buf[4096];
	i32 len = snprintf(buf, sizeof(buf), "\"info\":{\"description\": \"%s\","
		        "\"url\":\"%s\","
				"\"version\":\"%s\","
                "\"year\":%d,"
                "\"contributor\":\"%s\","
                "\"date_created\":\"%s\"}", coco->info.description, coco->info.url, coco->info.version,
                                             coco->info.year, coco->info.contributor, coco->info.date_created);
	if (len > 0 && len < sizeof(buf)) {
		memrw_write(buf, out, len);
	}
}

static void coco_output_license(coco_license_t* license, memrw_t* out) {
	char buf[4096];
	i32 len = snprintf(buf, sizeof(buf), "{\"url\": \"%s\","
	                                     "\"id\": %d,"
	                                     "\"name\": \"%s\"}", license->url, license->id, license->name);
	if (len > 0 && len < sizeof(buf)) {
		memrw_write(buf, out, len);
	} else panic();
}

static void coco_output_licenses(coco_t* coco, memrw_t* out) {
	if (coco->license_count == 0) {
		memrw_write_literal("\"licenses\":[]", out);
	} else {
		memrw_write_literal("\"licenses\":[", out);
		i32 last_license_index = coco->license_count - 1;
		for (i32 license_index = 0; license_index < coco->license_count; ++license_index) {
			coco_license_t* license = coco->licenses + license_index;
			coco_output_license(license, out);
			if (license_index < last_license_index) {
				memrw_write_literal(",\n", out);
			}
		}
		memrw_write_literal("]", out);
	}
}

static void coco_output_image(coco_image_t* image, memrw_t* out) {
	char buf[4096];
	i32 len = snprintf(buf, sizeof(buf), "{\"license\": %d,"
                 "\"file_name\": \"%s\","
                 "\"coco_url\": \"%s\","
                 "\"height\": %d,"
                 "\"width\": %d,"
                 "\"date_captured\": \"%s\","
                 "\"flickr_url\": \"%s\","
                 "\"id\": %d}", image->license, image->file_name, image->coco_url, image->height,
                                image->width, image->date_captured, image->flickr_url, image->id);
	if (len > 0 && len < sizeof(buf)) {
		memrw_write(buf, out, len);
	} else panic();
}

static void coco_output_images(coco_t* coco, memrw_t* out) {
	if (coco->image_count == 0) {
		memrw_write_literal("\"images\":[]", out);
	} else {
		memrw_write_literal("\"images\":[\n", out);
		i32 last_image_index = coco->image_count - 1;
		for (i32 image_index = 0; image_index < coco->image_count; ++image_index) {
			coco_image_t* image = coco->images + image_index;
			coco_output_image(image, out);
			if (image_index < last_image_index) {
				memrw_write_literal(",", out);
			}
		}
		memrw_write_literal("\n]", out);
	}
}

static void coco_output_coordinate_in_array_with_fmt(const char* fmt, v2f coordinate, memrw_t* out) {
	char buf[64];
	i32 len = snprintf(buf, sizeof(buf), fmt, coordinate.x, coordinate.y);
	ASSERT(len > 0 && len < sizeof(buf));
	memrw_write(buf, out, len);
}

static void coco_output_segmentation(coco_segmentation_t* segmentation, memrw_t* out) {
	memrw_write_literal("[", out);
	i32 coordinate_count_minus_one = segmentation->coordinate_count - 1;
	for (i32 i = 0; i < coordinate_count_minus_one; ++i) {
		coco_output_coordinate_in_array_with_fmt("%g,%g,", segmentation->coordinates[i], out);
	}
	i32 last_coordinate_index = coordinate_count_minus_one;
	if (last_coordinate_index >= 0) {
		coco_output_coordinate_in_array_with_fmt("%g,%g", segmentation->coordinates[last_coordinate_index], out);
	}
	memrw_write_literal("]", out);
}

static void coco_output_annotation(coco_t* coco, coco_annotation_t* annotation, memrw_t* out) {
	char buf[4096];
	// Part 1: everything before the segmentation field
	i32 len = snprintf(buf, sizeof(buf), "{\"id\":%d,"
	                                     "\"category_id\":%d,"
	                                     "\"iscrowd\":0,"
									     "\"segmentation\":[", annotation->id, annotation->category_id);
	if (len > 0 && len < sizeof(buf)) {
		memrw_write(buf, out, len);
	} else panic();
	// Part 2: the segmentation field
#if 0
	i32 segmentation_count = 1;
	for (i32 i = 0; i < segmentation_count; ++i) {

	}
#endif
	coco_segmentation_t* segmentation = &annotation->segmentation;
	coco_output_segmentation(segmentation, out);

	// Part 2: the feature data
	memrw_write_literal("],\"features\":[", out);
	bool need_comma = false;
	for (i32 feature_index = 0; feature_index < coco->feature_count; ++feature_index) {
		coco_feature_t* coco_feature = coco->features + feature_index;
		if ((!coco_feature->restrict_to_group) || (annotation->category_id == coco_feature->category_id)) {
			i32 id = coco_feature->id;
			if (id >= 0 && id < COCO_MAX_ANNOTATION_FEATURES) {
				if (need_comma) {
					memrw_write_literal(",", out);
				}
				len = snprintf(buf, sizeof(buf), "%d,%g", coco_feature->id, annotation->features[coco_feature->id]);
				ASSERT(len > 0 && len < sizeof(buf));
				memrw_write(buf, out, len);
				need_comma = true;
			} else {
				// TODO: support unconstrained IDs
				console_print_error("coco_output_annotation(): feature IDs > %d not supported\n", coco_feature->id);
			}
		}
	}

	// Part 3: everything after
	len = snprintf(buf, sizeof(buf), "],\"image_id\":%d,"
	                                 "\"area\":%g,"
	                                 "\"bbox\":[%g,%g,%g,%g]}", annotation->image_id, annotation->area,
	                                    annotation->bbox.x, annotation->bbox.y, annotation->bbox.w, annotation->bbox.h);
	if (len > 0 && len < sizeof(buf)) {
		memrw_write(buf, out, len);
	} else panic();


}

static void coco_output_annotations(coco_t* coco, memrw_t* out) {
	if (coco->annotation_count == 0) {
		memrw_write_literal("\"annotations\":[]", out);
	} else {
		memrw_write_literal("\"annotations\":[\n", out);
		i32 last_annotation_index = coco->annotation_count - 1;
		for (i32 annotation_index = 0; annotation_index < coco->annotation_count; ++annotation_index) {
			coco_annotation_t* annotation = coco->annotations + annotation_index;
			coco_output_annotation(coco, annotation, out);
			if (annotation_index < last_annotation_index) {
				memrw_write_literal(",\n", out);
			}
		}
		memrw_write_literal("\n]", out);
	}
}

static void coco_output_category(coco_category_t* category, memrw_t* out) {
	char buf[4096];
	i32 len = snprintf(buf, sizeof(buf), "{\"supercategory\":\"%s\","
	                                     "\"id\":%d,"
	                                     "\"name\":\"%s\","
										 "\"color\":[%d,%d,%d]}",
										 category->supercategory, category->id, category->name,
										 category->color.r, category->color.g, category->color.b);
	if (len > 0 && len < sizeof(buf)) {
		memrw_write(buf, out, len);
	} else panic();
}

static void coco_output_categories(coco_t* coco, memrw_t* out) {
	if (coco->category_count == 0) {
		memrw_write_literal("\"categories\":[]", out);
	} else {
		memrw_write_literal("\"categories\":[\n", out);
		i32 last_category_index = coco->category_count - 1;
		for (i32 category_index = 0; category_index < coco->category_count; ++category_index) {
			coco_category_t* category = coco->categories + category_index;
			coco_output_category(category, out);
			if (category_index < last_category_index) {
				memrw_write_literal(",\n", out);
			}
		}
		memrw_write_literal("\n]", out);
	}
}

static void coco_output_feature(coco_feature_t* feature, memrw_t* out) {
	char buf[4096];
	i32 len = snprintf(buf, sizeof(buf), "{\"id\":%d,"
										 "\"name\":\"%s\"",
										 feature->id, feature->name);
	if (len > 0 && len < sizeof(buf)) {
		memrw_write(buf, out, len);
	} else panic();

	// If the category_id JSON field is not present, that means the feature is not restricted to a group
	if (feature->restrict_to_group) {
		len = snprintf(buf, sizeof(buf), ",\"category_id\":%d", feature->category_id);
		ASSERT(len > 0 && len < sizeof(buf));
		memrw_write(buf, out, len);
	}
	memrw_write_literal("}", out);
}

static void coco_output_features(coco_t* coco, memrw_t* out) {
	if (coco->feature_count == 0) {
		memrw_write_literal("\"features\":[]", out);
	} else {
		memrw_write_literal("\"features\":[\n", out);
		i32 last_feature_index = coco->feature_count - 1;
		for (i32 feature_index = 0; feature_index < coco->feature_count; ++feature_index) {
			coco_feature_t* feature = coco->features + feature_index;
			coco_output_feature(feature, out);
			if (feature_index < last_feature_index) {
				memrw_write_literal(",\n", out);
			}
		}
		memrw_write_literal("\n]", out);
	}
}

void coco_transfer_annotations_from_annotation_set(coco_t* coco, annotation_set_t* annotation_set) {
	// Transfer groups (categories)
	arrsetlen(coco->categories, annotation_set->active_group_count);
	memset(coco->categories, 0, annotation_set->active_group_count * sizeof(coco_category_t));
	coco->category_count = annotation_set->active_group_count;
	for (i32 i = 0; i < annotation_set->active_group_count; ++i) {
		annotation_group_t* group = annotation_set->stored_groups + annotation_set->active_group_indices[i];
		coco_category_t* category = coco->categories + i;
		category->id = i;
		snprintf(category->name, MIN(sizeof(group->name), sizeof(category->name)), group->name);
		category->color = group->color;
	}

	// Transfer features
	arrsetlen(coco->features, annotation_set->active_feature_count);
	memset(coco->features, 0, annotation_set->active_feature_count * sizeof(coco_feature_t));
	coco->feature_count = annotation_set->active_feature_count;
	for (i32 i = 0; i < annotation_set->active_feature_count; ++i) {
		annotation_feature_t* feature = annotation_set->stored_features + annotation_set->active_feature_indices[i];
		coco_feature_t* coco_feature = coco->features + i;
		coco_feature->id = i;
		snprintf(coco_feature->name, MIN(sizeof(feature->name), sizeof(coco_feature->name)), feature->name);
		coco_feature->category_id = feature->group_id;
		coco_feature->restrict_to_group = feature->restrict_to_group;
	}

	// TODO: be less stupid about memory allocation
	// save dynamic arrays for coordinates, so we can reallocate them later
	/*v2f** saved_coordinate_arrays = (v2f**)alloca(annotation_set->active_annotation_count * sizeof(v2f*));
	i32 saved_count = sb_count()
	memset(saved_coordinate_arrays, 0, annotation_set->active_annotation_count * sizeof(v2f*));
	for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
		saved_coordinate_arrays[i] = coco->annotations[i].segmentation.coordinates;
	}*/
	// reset/reallocate space for annotations
	for (i32 i = 0; i < arrlen(coco->annotations); ++i) {
		coco_annotation_t* coco_annotation = coco->annotations + i;
		arrfree(coco_annotation->segmentation.coordinates);
	}
	arrsetlen(coco->annotations, annotation_set->active_annotation_count);
	memset(coco->annotations, 0, annotation_set->active_annotation_count * sizeof(coco_annotation_t));
	coco->annotation_count = annotation_set->active_annotation_count;

	for (i32 i = 0; i < annotation_set->active_annotation_count; ++i) {
		annotation_t* annotation = annotation_set->stored_annotations + annotation_set->active_annotation_indices[i];
		coco_annotation_t* coco_annotation = coco->annotations + i;
		v2f* saved_coordinates_storage = coco_annotation->segmentation.coordinates;
		memset(coco_annotation, 0, sizeof(coco_annotation_t));
		coco_annotation->id = i;
		coco_annotation->category_id = annotation->group_id;
		coco_annotation->segmentation.coordinates = saved_coordinates_storage;
		// reset/reallocate space for coordinates
		arrsetlen(coco_annotation->segmentation.coordinates, annotation->coordinate_count);
		if (annotation->coordinate_count > 0) {
			ASSERT(coco_annotation->segmentation.coordinates != NULL);
			v2f* coordinates = annotation_set->coordinates + annotation->first_coordinate;
			coco_annotation->segmentation.coordinate_count = annotation->coordinate_count;

			v2f mpp = annotation_set->mpp;
			if (!(mpp.x > 0.0f && mpp.y > 0.0f)) {
				ASSERT(!"mpp invalid or not initialized");
				mpp = (v2f){1.0f, 1.0f}; // prevent divide by zero
			}
			for (i32 j = 0; j < annotation->coordinate_count; ++j) {
				v2f* coordinate = coordinates + j;
				v2f* coco_coordinate = coco_annotation->segmentation.coordinates + j;
				*coco_coordinate = (v2f) {coordinate->x / mpp.x, coordinate->y / mpp.y};
			}
		}
		ASSERT(MAX_ANNOTATION_FEATURES == COCO_MAX_ANNOTATION_FEATURES);
		ASSERT(sizeof(coco_annotation->features) == sizeof(annotation->features));
		memcpy(coco_annotation->features, annotation->features, sizeof(annotation->features));
	}
}

void coco_transfer_annotations_to_annotation_set(coco_t* coco, annotation_set_t* annotation_set) {


	// TODO: transfer meta info, licenses, images
	annotation_set->coco.info = coco->info;

	// Transfer licenses
	if (coco->license_count > 0) {
		ASSERT(arrlen(coco->licenses) == coco->license_count);
		arrsetlen(annotation_set->coco.licenses, arrlen(coco->licenses));
		ASSERT(annotation_set->coco.licenses != NULL);
		if (annotation_set->coco.licenses != NULL) {
			memcpy(annotation_set->coco.licenses, coco->licenses, coco->license_count * sizeof(coco_license_t));
			annotation_set->coco.license_count = coco->license_count;
		}
	}

	// Transfer images
	if (coco->image_count > 0) {
		ASSERT(arrlen(coco->images) == coco->image_count);
		arrsetlen(annotation_set->coco.images, arrlen(coco->images));
		ASSERT(annotation_set->coco.images != NULL);
		if (annotation_set->coco.images != NULL) {
			memcpy(annotation_set->coco.images, coco->images, coco->image_count * sizeof(coco_image_t));
			annotation_set->coco.image_count = coco->image_count;
		}
	}

	// Transfer groups (categories)
	i32 group_count = coco->category_count;
	arrsetlen(annotation_set->stored_groups, group_count);
	memset(annotation_set->stored_groups, 0, group_count * sizeof(annotation_group_t));
	annotation_set->stored_group_count = group_count;
	arrsetlen(annotation_set->active_group_indices, group_count);
	annotation_set->active_group_count = group_count;
	for (i32 i = 0; i < group_count; ++i) {
		annotation_set->active_group_indices[i] = i;
		coco_category_t* category = coco->categories + i;
		annotation_group_t* group = annotation_set->stored_groups + i;
		group->id = category->id;
		group->color = category->color;
		strncpy(group->name, category->name, MIN(sizeof(group->name), sizeof(category->name)));
	}

	// Transfer features
	i32 feature_count = coco->feature_count;
	arrsetlen(annotation_set->stored_features, feature_count);
	memset(annotation_set->stored_features, 0, feature_count * sizeof(annotation_feature_t));
	annotation_set->stored_feature_count = feature_count;
	arrsetlen(annotation_set->active_feature_indices, feature_count);
	annotation_set->active_feature_count = feature_count;
	for (i32 i = 0; i < feature_count; ++i) {
		annotation_set->active_feature_indices[i] = i;
		coco_feature_t* coco_feature = coco->features + i;
		annotation_feature_t* feature = annotation_set->stored_features + i;
		feature->group_id = coco_feature->category_id; // TODO: lookup in hash table?
		feature->restrict_to_group = coco_feature->restrict_to_group;
		feature->id = coco_feature->id;
		strncpy(feature->name, coco_feature->name, MIN(sizeof(feature->name), sizeof(coco_feature->name)));
	}

	// Transfer annotations
	i32 annotation_count = coco->annotation_count;
	arrsetlen(annotation_set->stored_annotations, annotation_count);
	memset(annotation_set->stored_annotations, 0, annotation_count * sizeof(annotation_t));
	annotation_set->stored_annotation_count = annotation_count;
	arrsetlen(annotation_set->active_annotation_indices, annotation_count);
	annotation_set->active_annotation_count = annotation_count;
	i32 total_coordinate_count = 0; // count/accumulate while iterating over annotations below
	for (i32 i = 0; i < annotation_count; ++i) {
		annotation_set->active_annotation_indices[i] = i;
		coco_annotation_t* coco_annotation = coco->annotations + i;
		annotation_t* annotation = annotation_set->stored_annotations + i;
		annotation->group_id = coco_annotation->category_id;
		annotation->first_coordinate = total_coordinate_count;
		annotation->coordinate_count = coco_annotation->segmentation.coordinate_count;
		annotation->coordinate_capacity = annotation->coordinate_count;
		if (annotation->coordinate_count > 0) {
			annotation->has_coordinates = true;
			if (annotation->coordinate_count == 1) {
				annotation->type = ANNOTATION_POINT;
			} else if (annotation->coordinate_count == 2) {
				annotation->type = ANNOTATION_LINE;
			} else {
				annotation->type = ANNOTATION_POLYGON;
			}
		}
		ASSERT(MAX_ANNOTATION_FEATURES == COCO_MAX_ANNOTATION_FEATURES);
		ASSERT(sizeof(coco_annotation->features) == sizeof(annotation->features));
		memcpy(annotation->features, coco_annotation->features, sizeof(annotation->features));

		total_coordinate_count += coco_annotation->segmentation.coordinate_count;
	}

	// Transfer coordinates
	arrsetlen(annotation_set->coordinates, total_coordinate_count); // allocate coordinates in bulk
	memset(annotation_set->coordinates, 0, total_coordinate_count * sizeof(v2f));
	annotation_set->coordinate_count = total_coordinate_count;
	i32 running_coordinate_index = 0;
	for (i32 i = 0; i < annotation_count; ++i) {
		coco_annotation_t* coco_annotation = coco->annotations + i;
		annotation_t* annotation = annotation_set->stored_annotations + i;
		v2f* annotation_coordinates = annotation_set->coordinates + annotation->first_coordinate;
		for (i32 j = 0; j < annotation->coordinate_count; ++j) {
			v2f c = coco_annotation->segmentation.coordinates[j];
			annotation_coordinates[j] = (v2f){annotation_set->mpp.x * c.x, annotation_set->mpp.y * c.y};
		}
		running_coordinate_index += coco_annotation->segmentation.coordinate_count;
	}
}

memrw_t save_coco(coco_t* coco) {
	i64 timer_begin = get_clock();

	size_t out_size = MEGABYTES(1);
	if (coco->original_filesize > out_size) {
		out_size = next_pow2(coco->original_filesize);
	}
	memrw_t out = memrw_create(out_size);

	memrw_write_literal("{\n", &out);
	coco_output_info(coco, &out);
	memrw_write_literal(",\n", &out);
	coco_output_licenses(coco, &out);
	memrw_write_literal(",\n", &out);
	coco_output_images(coco, &out);
	memrw_write_literal(",\n", &out);
	coco_output_annotations(coco, &out);
	memrw_write_literal(",\n", &out);
	coco_output_categories(coco, &out);
	memrw_write_literal(",\n", &out);
	coco_output_features(coco, &out);
	memrw_write_literal("\n}\n", &out);

	return out;

	/*FILE* fp = fopen("coco_test_out.json", "wb");
	if (fp) {
		fwrite(out.data, out.used_size, 1, fp);
		fclose(fp);
	}

	console_print("JSON saved to file in %g seconds\n", get_seconds_elapsed(timer_begin, get_clock()));*/
}

i32 coco_add_new_license(coco_t* coco) {
	coco_license_t new_license = {};
	i32 highest_id = -1;
	for (i32 i = 0; i < coco->license_count; ++i) {
		coco_license_t* license = coco->licenses + i;
		if (license->id > highest_id) {
			highest_id = license->id;
		}
	}
	new_license.id = highest_id + 1;
	arrput(coco->licenses, new_license);
	++coco->license_count;
	return new_license.id;
}

i32 coco_add_new_category(coco_t* coco) {
	coco_category_t new_category = {};
	i32 highest_id = -1;
	for (i32 i = 0; i < coco->category_count; ++i) {
		coco_category_t* category = coco->categories + i;
		if (category->id > highest_id) {
			highest_id = category->id;
		}
	}
	new_category.id = highest_id + 1;
	arrput(coco->categories, new_category);
	++coco->category_count;
	return new_category.id;
}

i32 coco_add_new_image(coco_t* coco) {
	coco_image_t new_image = {};
	i32 highest_id = -1;
	for (i32 i = 0; i < coco->image_count; ++i) {
		coco_image_t* image = coco->images + i;
		if (image->id > highest_id) {
			highest_id = image->id;
		}
	}
	new_image.id = highest_id + 1;
	arrput(coco->images, new_image);
	++coco->image_count;
	return new_image.id;
}

coco_t coco_create_empty() {
	coco_t coco = {};
	snprintf(coco.info.description, COCO_MAX_FIELD, "New dataset");

	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	snprintf(coco.info.date_created, COCO_MAX_FIELD, "%d/%02d/%02d",
			 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
	coco.info.year = tm->tm_year + 1900;

//	coco.main_license_id = coco_add_new_license(&coco);
//	coco.main_category_id = coco_add_new_category(&coco);
//	coco.main_image_id = coco_add_new_image(&coco);

	coco.is_valid = true;

	return coco;
}

void coco_init_main_image(coco_t* coco, image_t* image) {
	if (coco->license_count == 0) {
		coco->main_license_id = coco_add_new_license(coco);
	}
	if (coco->image_count == 0) {
		coco->main_image_id = coco_add_new_image(coco);
	}
	coco_image_t* coco_image = coco->images + 0;
	snprintf(coco_image->file_name, COCO_MAX_FIELD, image->name);
	coco_image->width = image->width_in_pixels;
	coco_image->height = image->height_in_pixels;
}


void coco_destroy(coco_t* coco) {
	coco->is_valid = false;
	arrfree(coco->licenses);
	arrfree(coco->images);
	arrfree(coco->categories);
	arrfree(coco->features);
	for (i32 i = 0; i < coco->annotation_count; ++i) {
		coco_annotation_t* annotation = coco->annotations + i;
		arrfree(annotation->segmentation.coordinates);
	}
	arrfree(coco->annotations);
}

