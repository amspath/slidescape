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
#include "slide_score.h"

#include "json.h"

typedef struct slide_score_image_server_t {
    slide_score_image_metadata_t metadata;
} slide_score_image_server_t;

typedef struct slide_score_tile_server_t {
    char cookie_part[256];
    char url_part[256];
    char expires_on[256];
} slide_score_tile_server_t;

json_api_binding_t slide_score_api_get_tile_server_bindings_template[] = {
        {"cookiePart", offsetof(slide_score_tile_server_t, cookie_part), FIELD_TYPE_STRING_256CHARS},
        {"urlPart", offsetof(slide_score_tile_server_t, url_part), FIELD_TYPE_STRING_256CHARS},
        {"expiresOn", offsetof(slide_score_tile_server_t, expires_on), FIELD_TYPE_STRING_256CHARS},
};

json_api_binding_t slide_score_api_get_image_metadata_bindings_template[] = {
        {"level0TileWidth", offsetof(slide_score_image_metadata_t, tile_width), FIELD_TYPE_I32},
        {"level0TileHeight", offsetof(slide_score_image_metadata_t, tile_height), FIELD_TYPE_I32},
        {"osdTileSize", offsetof(slide_score_image_metadata_t, osd_tile_size), FIELD_TYPE_I32},
        {"mppX", offsetof(slide_score_image_metadata_t, mpp_x), FIELD_TYPE_FLOAT},
        {"mppX", offsetof(slide_score_image_metadata_t, mpp_y), FIELD_TYPE_FLOAT},
        {"objectivePower", offsetof(slide_score_image_metadata_t, objective_power), FIELD_TYPE_FLOAT},
        {"backgroundColor", offsetof(slide_score_image_metadata_t, background_color), FIELD_TYPE_I32},
        {"levelCount", offsetof(slide_score_image_metadata_t, level_count), FIELD_TYPE_I32},
        {"zLayerCount", offsetof(slide_score_image_metadata_t, z_layer_count), FIELD_TYPE_I32},
        {"level0Width", offsetof(slide_score_image_metadata_t, level_0_width), FIELD_TYPE_I32},
        {"level0Height", offsetof(slide_score_image_metadata_t, level_0_height), FIELD_TYPE_I32},
        {"fileName", offsetof(slide_score_image_metadata_t, filename), FIELD_TYPE_STRING_256CHARS},
};

void slide_score_generate_api_url(char* buf, size_t buf_size, slide_score_api_enum api, const char** par_names, const char** par_values, i32 par_count) {
    //TODO: stub
}

void json_api_populate_struct_with_field(const char* field_name, const char* value, json_api_binding_t* bindings, i32 binding_count, slide_score_image_metadata_t* metadata) {
    for (i32 i = 0; i < binding_count; ++i) {
        json_api_binding_t* binding = bindings + i;
        if (!binding->already_filled) {
            if (strcmp(binding->name, field_name) == 0) {
                switch(binding->field_type) {
                    case FIELD_TYPE_I32: {
                        *(i32*)(((u8*)metadata)+binding->offset) = atoi(value);
                    } break;
                    case FIELD_TYPE_FLOAT: {
                        *(float*)(((u8*)metadata)+binding->offset) = atof(value);
                    } break;
                    case FIELD_TYPE_STRING_256CHARS: {
                        strncpy((((char*)metadata)+binding->offset), value, 255);
                    } break;
                    default: break;
                }
                binding->already_filled = true;
                return;
            }
        }
    }

}

bool debug_slide_score_api_handle_response(const char* json, size_t json_length) {
    bool success = false;
    bool api_reported_success = false;

    slide_score_api_enum api = SLIDE_SCORE_API_GET_IMAGE_METADATA;
    json_api_binding_t* bindings = alloca(sizeof(slide_score_api_get_image_metadata_bindings_template));
    i32 bindings_count = COUNT(slide_score_api_get_image_metadata_bindings_template);
    memcpy(bindings, slide_score_api_get_image_metadata_bindings_template, sizeof(slide_score_api_get_image_metadata_bindings_template));
    slide_score_image_metadata_t metadata = {};

    struct json_value_s* root = json_parse(json, json_length);
    if (root) {
        if (root->type == json_type_object) {
            struct json_object_s* object = (struct json_object_s*)root->payload;
            struct json_object_element_s* element = object->start;
            while (element) {
                const char* element_name = element->name->string;
                if (strcmp(element_name, "success") == 0) {
                    api_reported_success = json_value_is_true(element->value);
                } else if (strcmp(element_name, "metadata") == 0 && element->value->type == json_type_object) {
                    struct json_object_s* metadata_object = (struct json_object_s*)element->value->payload;
                    struct json_object_element_s* metadata_element = metadata_object->start;



                    while (metadata_element) {
                        const char* metadata_element_name = metadata_element->name->string;
                        enum json_type_e value_type = metadata_element->value->type;
                        if (value_type == json_type_number || value_type == json_type_string) {
                            const char* value = ((struct json_string_s *)(metadata_element->value->payload))->string;
                            json_api_populate_struct_with_field(metadata_element_name, value, bindings, bindings_count, &metadata);
                        }

                        metadata_element = metadata_element->next;
                    }

                }
                element = element->next;
            }

        } else {
            DUMMY_STATEMENT;
        }


        success = api_reported_success;
    }
    return success;
}

