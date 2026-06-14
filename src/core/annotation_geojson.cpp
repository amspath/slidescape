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
#include "annotation.h"
#include "viewer.h"
#include "platform.h"
#include "stringutils.h"

#define SHEREDOM_JSON_IMPLEMENTATION
#include "json.h"

static void* geojson_json_alloc(void* user_data, size_t size) {
	(void)user_data;
	return malloc(size);
}

static bool json_string_equals(json_string_s* string, const char* cstr) {
	return string && strlen(cstr) == string->string_size && strncmp(string->string, cstr, string->string_size) == 0;
}

static json_object_element_s* json_object_find(json_object_s* object, const char* name) {
	if (!object) return NULL;
	for (json_object_element_s* element = object->start; element; element = element->next) {
		if (json_string_equals(element->name, name)) return element;
	}
	return NULL;
}

static json_value_s* json_object_get(json_object_s* object, const char* name) {
	json_object_element_s* element = json_object_find(object, name);
	return element ? element->value : NULL;
}

static json_object_s* json_object_get_object(json_object_s* object, const char* name) {
	json_value_s* value = json_object_get(object, name);
	return (value && value->type == json_type_object) ? (json_object_s*)value->payload : NULL;
}

static json_array_s* json_object_get_array(json_object_s* object, const char* name) {
	json_value_s* value = json_object_get(object, name);
	return (value && value->type == json_type_array) ? (json_array_s*)value->payload : NULL;
}

static json_string_s* json_object_get_string(json_object_s* object, const char* name) {
	json_value_s* value = json_object_get(object, name);
	return (value && value->type == json_type_string) ? (json_string_s*)value->payload : NULL;
}

static double json_number_to_double(json_value_s* value, bool* valid) {
	if (valid) *valid = false;
	if (!value || value->type != json_type_number) return 0.0;
	json_number_s* number = (json_number_s*)value->payload;
	if (valid) *valid = true;
	return atof(number->number);
}

static bool geojson_read_position(json_value_s* value, annotation_set_t* annotation_set, v2f* out) {
	if (!value || value->type != json_type_array) return false;
	json_array_s* array = (json_array_s*)value->payload;
	if (array->length < 2) return false;
	json_array_element_s* x_element = array->start;
	json_array_element_s* y_element = x_element ? x_element->next : NULL;
	bool x_valid = false;
	bool y_valid = false;
	double x = json_number_to_double(x_element ? x_element->value : NULL, &x_valid);
	double y = json_number_to_double(y_element ? y_element->value : NULL, &y_valid);
	if (!x_valid || !y_valid) return false;
	*out = V2F((float)x * annotation_set->mpp.x, (float)y * annotation_set->mpp.y);
	return true;
}

static void geojson_copy_string(char* dest, size_t dest_size, json_string_s* source) {
	if (!dest || dest_size == 0 || !source) return;
	size_t len = MIN(source->string_size, dest_size - 1);
	memcpy(dest, source->string, len);
	dest[len] = '\0';
}

static rgba_t geojson_parse_color(json_value_s* value, rgba_t fallback) {
	rgba_t result = fallback;
	if (!value) return result;
	if (value->type == json_type_array) {
		json_array_s* array = (json_array_s*)value->payload;
		json_array_element_s* element = array->start;
		u8* channels[] = { &result.r, &result.g, &result.b, &result.a };
		for (i32 i = 0; element && i < 4; ++i, element = element->next) {
			bool valid = false;
			double channel = json_number_to_double(element->value, &valid);
			if (valid) *channels[i] = (u8)CLAMP((int)channel, 0, 255);
		}
	} else if (value->type == json_type_number) {
		bool valid = false;
		u32 packed = (u32)json_number_to_double(value, &valid);
		if (valid) {
			result.r = (u8)((packed >> 16) & 0xff);
			result.g = (u8)((packed >> 8) & 0xff);
			result.b = (u8)(packed & 0xff);
			result.a = 255;
		}
	}
	return result;
}

static i32 geojson_group_from_properties(annotation_set_t* annotation_set, json_object_s* properties) {
	i32 group_id = 0;
	json_object_s* classification = json_object_get_object(properties, "classification");
	if (classification) {
		json_string_s* name = json_object_get_string(classification, "name");
		if (name && name->string_size > 0) {
			char group_name[256] = {};
			geojson_copy_string(group_name, sizeof(group_name), name);
			group_id = find_annotation_group_or_create_if_not_found(annotation_set, group_name);
			annotation_group_t* group = annotation_set->stored_groups + group_id;
			json_value_s* color = json_object_get(classification, "color");
			group->color = geojson_parse_color(color, group->color);
		}
	} else {
		json_string_s* class_name = json_object_get_string(properties, "classification");
		if (class_name && class_name->string_size > 0) {
			char group_name[256] = {};
			geojson_copy_string(group_name, sizeof(group_name), class_name);
			group_id = find_annotation_group_or_create_if_not_found(annotation_set, group_name);
		}
	}
	return group_id;
}

static annotation_t* geojson_add_annotation(annotation_set_t* annotation_set, annotation_type_enum type, json_object_s* properties) {
	annotation_t annotation = {};
	annotation.type = type;
	annotation.group_id = geojson_group_from_properties(annotation_set, properties);
	annotation.color = annotation_set->stored_groups[annotation.group_id].color;
	json_string_s* name = json_object_get_string(properties, "name");
	if (name) {
		geojson_copy_string(annotation.name, sizeof(annotation.name), name);
	} else {
		annotation_set_automatic_name(&annotation, annotation_set->stored_annotation_count);
	}
	arrput(annotation_set->stored_annotations, annotation);
	++annotation_set->stored_annotation_count;
	arrput(annotation_set->active_annotation_indices, annotation_set->stored_annotation_count - 1);
	++annotation_set->active_annotation_count;
	return arrlastptr(annotation_set->stored_annotations);
}

static bool geojson_append_positions(annotation_set_t* annotation_set, annotation_t* annotation, json_array_s* coordinates) {
	bool any = false;
	for (json_array_element_s* element = coordinates ? coordinates->start : NULL; element; element = element->next) {
		v2f p = {};
		if (geojson_read_position(element->value, annotation_set, &p)) {
			arrput(annotation->coordinates, p);
			++annotation->coordinate_count;
			any = true;
		}
	}
	return any;
}

static void geojson_drop_duplicate_closing_coordinate(annotation_t* annotation) {
	if (annotation->coordinate_count >= 2) {
		v2f first = annotation->coordinates[0];
		v2f last = annotation->coordinates[annotation->coordinate_count - 1];
		if (first.x == last.x && first.y == last.y) {
			arrpop(annotation->coordinates);
			--annotation->coordinate_count;
		}
	}
}

static bool geojson_import_geometry(annotation_set_t* annotation_set, json_object_s* geometry, json_object_s* properties);

static bool geojson_import_point(annotation_set_t* annotation_set, json_value_s* coordinates_value, json_object_s* properties) {
	v2f p = {};
	if (!geojson_read_position(coordinates_value, annotation_set, &p)) return false;
	annotation_t* annotation = geojson_add_annotation(annotation_set, ANNOTATION_POINT, properties);
	arrput(annotation->coordinates, p);
	annotation->coordinate_count = 1;
	return true;
}

static bool geojson_import_line_string(annotation_set_t* annotation_set, json_array_s* coordinates, json_object_s* properties) {
	annotation_t* annotation = geojson_add_annotation(annotation_set, ANNOTATION_LINE, properties);
	if (!geojson_append_positions(annotation_set, annotation, coordinates) || annotation->coordinate_count < 2) {
		arrpop(annotation_set->stored_annotations);
		--annotation_set->stored_annotation_count;
		arrpop(annotation_set->active_annotation_indices);
		--annotation_set->active_annotation_count;
		return false;
	}
	return true;
}

static bool geojson_import_polygon_ring(annotation_set_t* annotation_set, json_array_s* ring, json_object_s* properties) {
	annotation_t* annotation = geojson_add_annotation(annotation_set, ANNOTATION_POLYGON, properties);
	if (!geojson_append_positions(annotation_set, annotation, ring)) {
		arrpop(annotation_set->stored_annotations);
		--annotation_set->stored_annotation_count;
		arrpop(annotation_set->active_annotation_indices);
		--annotation_set->active_annotation_count;
		return false;
	}
	geojson_drop_duplicate_closing_coordinate(annotation);
	if (annotation->coordinate_count < 3) {
		arrpop(annotation_set->stored_annotations);
		--annotation_set->stored_annotation_count;
		arrpop(annotation_set->active_annotation_indices);
		--annotation_set->active_annotation_count;
		return false;
	}
	return true;
}

static bool geojson_import_polygon(annotation_set_t* annotation_set, json_array_s* coordinates, json_object_s* properties) {
	json_array_element_s* exterior_ring = coordinates ? coordinates->start : NULL;
	if (!exterior_ring || exterior_ring->value->type != json_type_array) return false;
	return geojson_import_polygon_ring(annotation_set, (json_array_s*)exterior_ring->value->payload, properties);
}

static bool geojson_import_geometry(annotation_set_t* annotation_set, json_object_s* geometry, json_object_s* properties) {
	json_string_s* type_string = json_object_get_string(geometry, "type");
	json_value_s* coordinates_value = json_object_get(geometry, "coordinates");
	if (!type_string) return false;

	if (json_string_equals(type_string, "Point")) {
		return geojson_import_point(annotation_set, coordinates_value, properties);
	} else if (json_string_equals(type_string, "MultiPoint")) {
		json_array_s* coordinates = (coordinates_value && coordinates_value->type == json_type_array) ? (json_array_s*)coordinates_value->payload : NULL;
		bool any = false;
		for (json_array_element_s* element = coordinates ? coordinates->start : NULL; element; element = element->next) {
			any |= geojson_import_point(annotation_set, element->value, properties);
		}
		return any;
	} else if (json_string_equals(type_string, "LineString")) {
		json_array_s* coordinates = (coordinates_value && coordinates_value->type == json_type_array) ? (json_array_s*)coordinates_value->payload : NULL;
		return geojson_import_line_string(annotation_set, coordinates, properties);
	} else if (json_string_equals(type_string, "MultiLineString")) {
		json_array_s* lines = (coordinates_value && coordinates_value->type == json_type_array) ? (json_array_s*)coordinates_value->payload : NULL;
		bool any = false;
		for (json_array_element_s* element = lines ? lines->start : NULL; element; element = element->next) {
			if (element->value->type == json_type_array) {
				any |= geojson_import_line_string(annotation_set, (json_array_s*)element->value->payload, properties);
			}
		}
		return any;
	} else if (json_string_equals(type_string, "Polygon")) {
		json_array_s* coordinates = (coordinates_value && coordinates_value->type == json_type_array) ? (json_array_s*)coordinates_value->payload : NULL;
		return geojson_import_polygon(annotation_set, coordinates, properties);
	} else if (json_string_equals(type_string, "MultiPolygon")) {
		json_array_s* polygons = (coordinates_value && coordinates_value->type == json_type_array) ? (json_array_s*)coordinates_value->payload : NULL;
		bool any = false;
		for (json_array_element_s* element = polygons ? polygons->start : NULL; element; element = element->next) {
			if (element->value->type == json_type_array) {
				any |= geojson_import_polygon(annotation_set, (json_array_s*)element->value->payload, properties);
			}
		}
		return any;
	} else if (json_string_equals(type_string, "GeometryCollection")) {
		json_array_s* geometries = json_object_get_array(geometry, "geometries");
		bool any = false;
		for (json_array_element_s* element = geometries ? geometries->start : NULL; element; element = element->next) {
			if (element->value->type == json_type_object) {
				any |= geojson_import_geometry(annotation_set, (json_object_s*)element->value->payload, properties);
			}
		}
		return any;
	}

	console_print("Warning: unsupported GeoJSON geometry type '%.*s'; skipping feature.\n", (int)type_string->string_size, type_string->string);
	return false;
}

bool load_geojson_annotations(app_state_t* app_state, const char* filename) {
	annotation_set_t* annotation_set = &app_state->scene.annotation_set;
	mem_t* file = platform_read_entire_file(filename);
	if (!file) return false;

	bool success = false;
	i64 start = get_clock();
	json_value_s* root = json_parse_ex((const char*)file->data, file->len, json_parse_flags_default, geojson_json_alloc, NULL, NULL);
	json_object_s* root_object = NULL;
	json_string_s* root_type = NULL;
	json_array_s* features = NULL;
	i32 imported_count = 0;
	i32 skipped_count = 0;
	if (!root) {
		console_print_error("GeoJSON parse error: '%s' is not valid JSON.\n", filename);
		goto cleanup;
	}
	if (root->type != json_type_object) {
		console_print_error("GeoJSON parse error: root value must be an object.\n");
		goto cleanup;
	}

	root_object = (json_object_s*)root->payload;
	root_type = json_object_get_string(root_object, "type");
	if (!json_string_equals(root_type, "FeatureCollection")) {
		console_print_error("GeoJSON parse error: expected root type FeatureCollection.\n");
		goto cleanup;
	}

	features = json_object_get_array(root_object, "features");
	if (!features) {
		console_print_error("GeoJSON parse error: FeatureCollection has no features array.\n");
		goto cleanup;
	}

	for (json_array_element_s* feature_element = features->start; feature_element; feature_element = feature_element->next) {
		if (feature_element->value->type != json_type_object) {
			++skipped_count;
			continue;
		}
		json_object_s* feature = (json_object_s*)feature_element->value->payload;
		json_string_s* feature_type = json_object_get_string(feature, "type");
		if (!json_string_equals(feature_type, "Feature")) {
			++skipped_count;
			continue;
		}
		json_object_s* geometry = json_object_get_object(feature, "geometry");
		json_object_s* properties = json_object_get_object(feature, "properties");
		if (!properties) properties = (json_object_s*)json_null;
		if (geometry && geojson_import_geometry(annotation_set, geometry, properties)) {
			++imported_count;
		} else {
			++skipped_count;
		}
	}

	copy_cstring(annotation_set->annotation_filename, filename, sizeof(annotation_set->annotation_filename));
	annotation_set->preferred_output_format = ANNOTATION_FILE_FORMAT_GEOJSON;
	annotation_set->annotations_were_loaded_from_file = true;
	success = imported_count > 0;
	console_print_verbose("Loaded GeoJSON annotations in %g seconds (%d imported, %d skipped).\n", get_seconds_elapsed(start, get_clock()), imported_count, skipped_count);

cleanup:
	if (root) free(root);
	free(file);
	return success;
}

static void geojson_write_escaped_string(FILE* fp, const char* string) {
	fputc('"', fp);
	for (const unsigned char* c = (const unsigned char*)string; c && *c; ++c) {
		switch (*c) {
			case '"': fputs("\\\"", fp); break;
			case '\\': fputs("\\\\", fp); break;
			case '\b': fputs("\\b", fp); break;
			case '\f': fputs("\\f", fp); break;
			case '\n': fputs("\\n", fp); break;
			case '\r': fputs("\\r", fp); break;
			case '\t': fputs("\\t", fp); break;
			default:
				if (*c < 0x20) fprintf(fp, "\\u%04x", *c);
				else fputc(*c, fp);
				break;
		}
	}
	fputc('"', fp);
}

static void geojson_write_position(FILE* fp, annotation_set_t* annotation_set, v2f p) {
	fprintf(fp, "[%g, %g]", p.x / annotation_set->mpp.x, p.y / annotation_set->mpp.y);
}

static void geojson_write_properties(FILE* fp, annotation_set_t* annotation_set, annotation_t* annotation) {
	fprintf(fp, "\n      \"properties\": {\n        \"objectType\": \"annotation\"");
	if (annotation->name[0] != '\0') {
		fprintf(fp, ",\n        \"name\": ");
		geojson_write_escaped_string(fp, annotation->name);
	}
	if (annotation->group_id > 0 && annotation->group_id < annotation_set->stored_group_count) {
		annotation_group_t* group = annotation_set->stored_groups + annotation->group_id;
		fprintf(fp, ",\n        \"classification\": {\"name\": ");
		geojson_write_escaped_string(fp, group->name);
		fprintf(fp, ", \"color\": [%u, %u, %u]}", group->color.r, group->color.g, group->color.b);
	}
	fprintf(fp, "\n      }");
}

static void geojson_write_feature(FILE* fp, annotation_set_t* annotation_set, annotation_t* annotation) {
	const char* geometry_type = "Polygon";
	if (annotation->type == ANNOTATION_POINT) geometry_type = "Point";
	else if (annotation->type == ANNOTATION_LINE) geometry_type = "LineString";

	fprintf(fp, "    {\n      \"type\": \"Feature\",\n      \"geometry\": {\n        \"type\": \"%s\",\n        \"coordinates\": ", geometry_type);
	if (annotation->type == ANNOTATION_POINT) {
		if (annotation->coordinate_count > 0) geojson_write_position(fp, annotation_set, annotation->coordinates[0]);
		else fprintf(fp, "[]");
	} else if (annotation->type == ANNOTATION_LINE) {
		fprintf(fp, "[");
		for (i32 i = 0; i < annotation->coordinate_count; ++i) {
			if (i > 0) fprintf(fp, ", ");
			geojson_write_position(fp, annotation_set, annotation->coordinates[i]);
		}
		fprintf(fp, "]");
	} else {
		fprintf(fp, "[[");
		for (i32 i = 0; i < annotation->coordinate_count; ++i) {
			if (i > 0) fprintf(fp, ", ");
			geojson_write_position(fp, annotation_set, annotation->coordinates[i]);
		}
		if (annotation->coordinate_count > 0) {
			fprintf(fp, ", ");
			geojson_write_position(fp, annotation_set, annotation->coordinates[0]);
		}
		fprintf(fp, "]]");
	}
	fprintf(fp, "\n      },");
	geojson_write_properties(fp, annotation_set, annotation);
	fprintf(fp, "\n    }");
}

void save_geojson_annotations(annotation_set_t* annotation_set, const char* filename_out) {
	ASSERT(annotation_set);
	ASSERT(filename_out);
	while (!atomic_compare_exchange(&annotation_set->is_saving_in_progress, 1, 0)) {
		console_print("save_geojson_annotations(): failed to get an exclusive lock on the annotation set, retrying...\n");
		platform_sleep(100);
	}

	FILE* fp = fopen(filename_out, "wb");
	if (fp) {
		fprintf(fp, "{\n  \"type\": \"FeatureCollection\",\n  \"features\": [\n");
		for (i32 annotation_index = 0; annotation_index < annotation_set->active_annotation_count; ++annotation_index) {
			annotation_t* annotation = get_active_annotation(annotation_set, annotation_index);
			if (annotation_index > 0) fprintf(fp, ",\n");
			geojson_write_feature(fp, annotation_set, annotation);
		}
		fprintf(fp, "\n  ]\n}\n");
		fclose(fp);
	}
	annotation_set->is_saving_in_progress = false;
}
