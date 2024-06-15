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
#include "remote.h"
#include "memrw.h"

#include "json.h"


web_api_binding_t slide_score_api_get_tile_server_bindings_template[] = {
        {"cookiePart", offsetof(slide_score_get_tile_server_result_t, cookie_part), FIELD_TYPE_STRING_256CHARS},
        {"urlPart", offsetof(slide_score_get_tile_server_result_t, url_part), FIELD_TYPE_STRING_256CHARS},
        {"expiresOn", offsetof(slide_score_get_tile_server_result_t, expires_on), FIELD_TYPE_STRING_256CHARS},
};
web_api_result_descriptor_t slide_score_api_get_tile_server_result_descriptor = {
        slide_score_api_get_tile_server_bindings_template, COUNT(slide_score_api_get_tile_server_bindings_template)
};

web_api_binding_t slide_score_api_get_image_metadata_bindings_template[] = {
        {"level0TileWidth", offsetof(slide_score_get_image_metadata_result_t, tile_width),       FIELD_TYPE_I32},
        {"level0TileHeight", offsetof(slide_score_get_image_metadata_result_t, tile_height),     FIELD_TYPE_I32},
        {"osdTileSize", offsetof(slide_score_get_image_metadata_result_t, osd_tile_size),        FIELD_TYPE_I32},
        {"mppX", offsetof(slide_score_get_image_metadata_result_t, mpp_x),                       FIELD_TYPE_FLOAT},
        {"mppX", offsetof(slide_score_get_image_metadata_result_t, mpp_y),                       FIELD_TYPE_FLOAT},
        {"objectivePower", offsetof(slide_score_get_image_metadata_result_t, objective_power),   FIELD_TYPE_FLOAT},
        {"backgroundColor", offsetof(slide_score_get_image_metadata_result_t, background_color), FIELD_TYPE_I32},
        {"levelCount", offsetof(slide_score_get_image_metadata_result_t, level_count),           FIELD_TYPE_I32},
        {"zLayerCount", offsetof(slide_score_get_image_metadata_result_t, z_layer_count),        FIELD_TYPE_I32},
        {"level0Width", offsetof(slide_score_get_image_metadata_result_t, level_0_width),        FIELD_TYPE_I32},
        {"level0Height", offsetof(slide_score_get_image_metadata_result_t, level_0_height),      FIELD_TYPE_I32},
        {"fileName", offsetof(slide_score_get_image_metadata_result_t, filename),                FIELD_TYPE_STRING_256CHARS},
};
web_api_result_descriptor_t slide_score_api_get_image_metadata_descriptor = {
        slide_score_api_get_image_metadata_bindings_template, COUNT(slide_score_api_get_image_metadata_bindings_template)
};


static const web_api_result_descriptor_t* slide_score_api_result_descriptors[SLIDE_SCORE_API_LAST] = {
        [SLIDE_SCORE_API_GET_IMAGE_METADATA] = &slide_score_api_get_image_metadata_descriptor,
        [SLIDE_SCORE_API_GET_TILE_SERVER] = &slide_score_api_get_tile_server_result_descriptor,
};

static const char* slide_score_api_names[SLIDE_SCORE_API_LAST] = {
        [SLIDE_SCORE_API_SCORES] = "Scores",
        [SLIDE_SCORE_API_GET_STUDIES_UPDATED] = "GetStudiesUpdated",
        [SLIDE_SCORE_API_STUDIES] = "Studies",
        [SLIDE_SCORE_API_IMAGES] = "Images",
        [SLIDE_SCORE_API_CASES] = "Cases",
        [SLIDE_SCORE_API_QUESTIONS] = "Questions",
        [SLIDE_SCORE_API_GET_SLIDE_PATH] = "GetSlidePath",
        [SLIDE_SCORE_API_GET_SLIDE_DESCRIPTION] = "GetSlideDescription",
        [SLIDE_SCORE_API_GET_SLIDE_DETAILS] = "GetSlideDetails",
        [SLIDE_SCORE_API_GET_CASE_DESCRIPTION] = "GetCaseDescription",
        [SLIDE_SCORE_API_PUBLISH] = "Publish",
        [SLIDE_SCORE_API_UNPUBLISH] = "Unpublish",
        [SLIDE_SCORE_API_DOWNLOAD_SLIDE] = "DownloadSlide",
        [SLIDE_SCORE_API_IS_SLIDE_OUT_OF_FOCUS] = "IsSlideOutOfFocus",
        [SLIDE_SCORE_API_UPSERT_DOMAIN] = "UpsertDomain",
        [SLIDE_SCORE_API_GET_DOMAIN_FOR_STUDY] = "GetDomainForStudy",
        [SLIDE_SCORE_API_SET_DOMAIN_FOR_STUDY] = "SetDomainForStudy",
        [SLIDE_SCORE_API_GENERATE_STUDENT_ACCOUNT] = "GenerateStudentAccount",
        [SLIDE_SCORE_API_GENERATE_LOGIN_LINK] = "GenerateLoginLink",
        [SLIDE_SCORE_API_GENERATE_SLIDE_FILE_URL] = "GenerateSlideFileURL",
        [SLIDE_SCORE_API_CREATE_STUDY] = "CreateStudy",
        [SLIDE_SCORE_API_UPDATE_STUDY] = "UpdateStudy",
        [SLIDE_SCORE_API_REQUEST_UPLOAD] = "RequestUpload",
        [SLIDE_SCORE_API_FINISH_UPLOAD] = "FinishUpload",
        [SLIDE_SCORE_API_ADD_SLIDE] = "AddSlide",
        [SLIDE_SCORE_API_UPLOAD_RESULTS] = "UploadResults",
        [SLIDE_SCORE_API_CONVERT_SCORE_VALUE_TO_ANNO2] = "ConvertScoreValueToAnno2",
        [SLIDE_SCORE_API_CREATE_ANNO2] = "CreateAnno2",
        [SLIDE_SCORE_API_SET_SLIDE_RESOLUTION] = "SetSlideResolution",
        [SLIDE_SCORE_API_SET_SLIDE_DESCRIPTION] = "SetSlideDescription",
        [SLIDE_SCORE_API_UPDATE_SLIDE_NAME] = "UpdateSlideName",
        [SLIDE_SCORE_API_UPDATE_CASE_NAME] = "UpdateCaseName",
        [SLIDE_SCORE_API_UPDATE_SLIDE_PATH] = "UpdateSlidePath",
        [SLIDE_SCORE_API_SET_IS_ARCHIVED] = "SetIsArchived",
        [SLIDE_SCORE_API_UPDATE_TMA_CORE_SIZE] = "UpdateTMACoreSize",
        [SLIDE_SCORE_API_DELETE_SLIDE] = "DeleteSlide",
        [SLIDE_SCORE_API_GET_QUPATH_TOKENS_FOR_STUDY] = "GetQupathTokensForStudy",
        [SLIDE_SCORE_API_ADD_CASE] = "AddCase",
        [SLIDE_SCORE_API_SET_IMAGE_CASE] = "SetImageCase",
        [SLIDE_SCORE_API_REMOVE_CASE] = "RemoveCase",
        [SLIDE_SCORE_API_DELETE_STUDY] = "DeleteStudy",
        [SLIDE_SCORE_API_UNDELETE_STUDY] = "UndeleteStudy",
        [SLIDE_SCORE_API_EXPORT_ASAP_ANNOTATIONS] = "ExportASAPAnnotations",
        [SLIDE_SCORE_API_UPLOAD_ASAP_ANNOTATIONS] = "UploadASAPAnnotations",
        [SLIDE_SCORE_API_GET_SCREENSHOT] = "GetScreenshot",
        [SLIDE_SCORE_API_SEND_EMAIL] = "SendEmail",
        [SLIDE_SCORE_API_ADD_USER] = "AddUser",
        [SLIDE_SCORE_API_REMOVE_USER] = "RemoveUser",
        [SLIDE_SCORE_API_CREATE_TMA_MAP] = "CreateTMAMap",
        [SLIDE_SCORE_API_SET_SLIDE_TMA_MAP] = "SetSlideTMAMap",
        [SLIDE_SCORE_API_ADD_QUESTION] = "AddQuestion",
        [SLIDE_SCORE_API_UPDATE_QUESTION] = "UpdateQuestion",
        [SLIDE_SCORE_API_REMOVE_QUESTION] = "RemoveQuestion",
        [SLIDE_SCORE_API_REIMPORT] = "Reimport",
        [SLIDE_SCORE_API_GET_IMAGE_METADATA] = "GetImageMetadata",
        [SLIDE_SCORE_API_GET_TILE_SERVER] = "GetTileServer",
        [SLIDE_SCORE_API_GET_TOKEN_EXPIRY] = "GetTokenExpiry",
        [SLIDE_SCORE_API_I_ENDPOINT] = "",
        [SLIDE_SCORE_API_GET_RAW_TILE] = "GetRawTile",
};




void web_api_call_destroy(web_api_call_t* call) {
    memrw_destroy(&call->url);
    memrw_destroy(&call->request);
}

web_api_call_t slide_score_build_api_call(slide_score_client_t* ss, slide_score_api_enum api, const char** par_names, const char** par_values, i32 par_count) {
    web_api_call_t call = {};
    if (api <= SLIDE_SCORE_API_UNKNOWN || api >= SLIDE_SCORE_API_LAST) {
        return call; // invalid
    }
    if (api == SLIDE_SCORE_API_I_ENDPOINT) {
        // stub
    } else {
        memrw_init(&call.url, 512);
        memrw_printf(&call.url, "/Api/%s", slide_score_api_names[api]);
        for (i32 i = 0; i < par_count; ++i) {
            char prefix = (i == 0) ? '?' : '&';
            memrw_putc(prefix, &call.url);
            memrw_write_string(par_names[i], &call.url);
            memrw_putc('=', &call.url);
            memrw_write_string_urlencode(par_values[i], &call.url);
        }
        memrw_putc(0, &call.url); // zero terminate
        call.is_valid = true;
    }

    // Build the HTTP request
    memrw_init(&call.request, 4092);


    // If the API token is provided, add it to the HTTP headers
    char token_header_string[4196] = "";
    if (ss->api_key[0] != '\0') {
        snprintf(token_header_string, sizeof(token_header_string), "Authorization: Bearer %s\r\n", ss->api_key);
    } else {
        token_header_string[0] = '\0';
    }

    static const char requestfmt[] =
            "GET %s HTTP/1.1\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7\r\n"
            //            "Accept-Encoding: gzip, deflate, br\r\n"
            "Accept-Language: en,nl;q=0.9,en-US;q=0.8,af;q=0.7\r\n%s"
            "Cache-Control: max-age=0\r\n"
            "Connection: close\r\n"
            "Host: %s\r\n"
            "Upgrade-Insecure-Requests: 1\r\n\r\n";
    memrw_printf(&call.request, requestfmt, call.url.data, token_header_string, ss->server_name);
    memrw_putc(0, &call.request); // zero terminate
//    i32 request_len = (i32)strlen((char*)call.request.data);

    return call;
}

void web_api_populate_struct_with_field(const char* field_name, const char* value, web_api_binding_t* bindings, i32 binding_count, slide_score_api_result_t* api_result) {
    u8* result_struct_generic = (u8*)&api_result->result_generic;
    for (i32 i = 0; i < binding_count; ++i) {
        web_api_binding_t* binding = bindings + i;
        if (!binding->already_filled) {
            if (strcmp(binding->name, field_name) == 0) {
                switch(binding->field_type) {
                    case FIELD_TYPE_I32: {
                        *(i32*)((result_struct_generic)+binding->offset) = atoi(value);
                    } break;
                    case FIELD_TYPE_FLOAT: {
                        *(float*)((result_struct_generic)+binding->offset) = atof(value);
                    } break;
                    case FIELD_TYPE_STRING_256CHARS: {
                        strncpy((((char*)result_struct_generic)+binding->offset), value, 255);
                    } break;
                    default: break;
                }
                binding->already_filled = true;
                return;
            }
        }
    }

}

slide_score_api_result_t debug_slide_score_api_handle_response(const char* json, size_t json_length, slide_score_api_enum api) {
    bool success = false;
    bool api_reported_success = false;

    const web_api_result_descriptor_t* result_descriptor = slide_score_api_result_descriptors[api];

    size_t bindings_template_size = result_descriptor->binding_count * sizeof(web_api_binding_t);
    web_api_binding_t* bindings = alloca(bindings_template_size);
    memcpy(bindings, result_descriptor->bindings_template, bindings_template_size);
    slide_score_api_result_t parsed_api_result = {};
    parsed_api_result.api = api;

    struct json_value_s* root = json_parse(json, json_length);
    if (root) {
        if (root->type == json_type_object) {
            struct json_object_s* object = (struct json_object_s*)root->payload;
            struct json_object_element_s* element = object->start;
            while (element) {
                const char* element_name = element->name->string;
                // TODO: refactor code duplication
                enum json_type_e value_type = element->value->type;
                if (value_type == json_type_number || value_type == json_type_string) {
                    const char* value = ((struct json_string_s *)(element->value->payload))->string;
                    web_api_populate_struct_with_field(element_name, value, bindings,
                                                       result_descriptor->binding_count, &parsed_api_result);
                }
                // TODO: handle nested objects, arrays
                // Note: the metadata objects only exists for the GetTileMetadata call?
                if (strcmp(element_name, "metadata") == 0 && element->value->type == json_type_object) {
                    struct json_object_s* metadata_object = (struct json_object_s*)element->value->payload;
                    struct json_object_element_s* metadata_element = metadata_object->start;

                    while (metadata_element) {
                        const char* metadata_element_name = metadata_element->name->string;
                        enum json_type_e metadata_value_type = metadata_element->value->type;
                        if (metadata_value_type == json_type_number || metadata_value_type == json_type_string) {
                            const char* value = ((struct json_string_s *)(metadata_element->value->payload))->string;
                            web_api_populate_struct_with_field(metadata_element_name, value, bindings,
                                                               result_descriptor->binding_count, &parsed_api_result);
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
    parsed_api_result.success = success;
    return parsed_api_result;
}


void slide_score_get_image_metadata(slide_score_client_t* ss, i32 image_id) {
    char image_id_str[16];
    snprintf(image_id_str, sizeof(image_id_str), "%d", image_id);
    web_api_call_t call = slide_score_build_api_call(ss, SLIDE_SCORE_API_GET_IMAGE_METADATA, (const char **)"imageId", (const char **) image_id_str, 1);
    if (call.is_valid) {
//        do_http_request()
    }

    char url[512];
//    snprintf("https://")
}

