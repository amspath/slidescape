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

#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct slide_score_image_metadata_t {
    i32 tile_width;
    i32 tile_height;
    i32 osd_tile_size;
    float mpp_x;
    float mpp_y;
    float objective_power;
    i32 background_color;
    i32 level_count;
    i32 z_layer_count;
    i32 level_0_width;
    i32 level_0_height;
    char filename[256];

} slide_score_image_metadata_t;

typedef enum field_type_enum {
    FIELD_TYPE_UNKNOWN = 0,
    FIELD_TYPE_I32,
    FIELD_TYPE_FLOAT,
    FIELD_TYPE_STRING_256CHARS,
} field_type_enum;

typedef enum slide_score_api_enum {
    SLIDE_SCORE_API_UNKNOWN = 0,
    SLIDE_SCORE_API_SCORES,
    SLIDE_SCORE_API_GET_STUDIES_UPDATED,
    SLIDE_SCORE_API_STUDIES,
    SLIDE_SCORE_API_IMAGES,
    SLIDE_SCORE_API_CASES,
    SLIDE_SCORE_API_QUESTIONS,
    SLIDE_SCORE_API_GET_SLIDE_PATH,
    SLIDE_SCORE_API_GET_SLIDE_DESCRIPTION,
    SLIDE_SCORE_API_GET_SLIDE_DETAILS,
    SLIDE_SCORE_API_GET_CASE_DESCRIPTION,
    SLIDE_SCORE_API_PUBLISH,
    SLIDE_SCORE_API_UNPUBLISH,
    SLIDE_SCORE_API_DOWNLOAD_SLIDE,
    SLIDE_SCORE_API_IS_SLIDE_OUT_OF_FOCUS,
    SLIDE_SCORE_API_UPSERT_DOMAIN,
    SLIDE_SCORE_API_GET_DOMAIN_FOR_STUDY,
    SLIDE_SCORE_API_SET_DOMAIN_FOR_STUDY,
    SLIDE_SCORE_API_GENERATE_STUDENT_ACCOUNT,
    SLIDE_SCORE_API_GENERATE_LOGIN_LINK,
    SLIDE_SCORE_API_GENERATE_SLIDE_FILE_URL,
    SLIDE_SCORE_API_CREATE_STUDY,
    SLIDE_SCORE_API_UPDATE_STUDY,
    SLIDE_SCORE_API_REQUEST_UPLOAD,
    SLIDE_SCORE_API_FINISH_UPLOAD,
    SLIDE_SCORE_API_ADD_SLIDE,
    SLIDE_SCORE_API_UPLOAD_RESULTS,
    SLIDE_SCORE_API_CONVERT_SCORE_VALUE_TO_ANNO2,
    SLIDE_SCORE_API_CREATE_ANNO2,
    SLIDE_SCORE_API_SET_SLIDE_RESOLUTION,
    SLIDE_SCORE_API_SET_SLIDE_DESCRIPTION,
    SLIDE_SCORE_API_UPDATE_SLIDE_NAME,
    SLIDE_SCORE_API_UPDATE_CASE_NAME,
    SLIDE_SCORE_API_UPDATE_SLIDE_PATH,
    SLIDE_SCORE_API_SET_IS_ARCHIVED,
    SLIDE_SCORE_API_UPDATE_TMA_CORE_SIZE,
    SLIDE_SCORE_API_DELETE_SLIDE,
    SLIDE_SCORE_API_GET_QUPATH_TOKENS_FOR_STUDY,
    SLIDE_SCORE_API_ADD_CASE,
    SLIDE_SCORE_API_SET_IMAGE_CASE,
    SLIDE_SCORE_API_REMOVE_CASE,
    SLIDE_SCORE_API_DELETE_STUDY,
    SLIDE_SCORE_API_UNDELETE_STUDY,
    SLIDE_SCORE_API_EXPORT_ASAP_ANNOTATIONS,
    SLIDE_SCORE_API_UPLOAD_ASAP_ANNOTATIONS,
    SLIDE_SCORE_API_GET_SCREENSHOT,
    SLIDE_SCORE_API_SEND_EMAIL,
    SLIDE_SCORE_API_ADD_USER,
    SLIDE_SCORE_API_REMOVE_USER,
    SLIDE_SCORE_API_CREATE_TMA_MAP,
    SLIDE_SCORE_API_SET_SLIDE_TMA_MAP,
    SLIDE_SCORE_API_ADD_QUESTION,
    SLIDE_SCORE_API_UPDATE_QUESTION,
    SLIDE_SCORE_API_REMOVE_QUESTION,
    SLIDE_SCORE_API_REIMPORT,
    SLIDE_SCORE_API_GET_IMAGE_METADATA,
    SLIDE_SCORE_API_GET_TILE_SERVER,
    SLIDE_SCORE_API_GET_TOKEN_EXPIRY,
    SLIDE_SCORE_API_I_ENDPOINT,
    SLIDE_SCORE_API_GET_RAW_TILE,
} slide_score_api_enum;

typedef struct json_api_binding_t {
    const char* name;
    i64 offset;
    field_type_enum field_type;
    bool already_filled;
} json_api_binding_t;

bool debug_slide_score_api_handle_response(const char* json, size_t json_length);


#ifdef __cplusplus
}
#endif
