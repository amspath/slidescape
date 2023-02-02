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

#pragma once

// forward declarations
typedef struct image_t image_t;
typedef struct annotation_set_t annotation_set_t;

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COCO_MAX_FIELD 512
#define COCO_MAX_ANNOTATION_FEATURES 64

typedef struct coco_info_t {
	char description[COCO_MAX_FIELD];
	char url[COCO_MAX_FIELD];
	char version[COCO_MAX_FIELD];
	i32 year;
	char contributor[COCO_MAX_FIELD];
	char date_created[COCO_MAX_FIELD];
} coco_info_t;

typedef struct coco_license_t {
	char url[COCO_MAX_FIELD];
	i32 id;
	char name[COCO_MAX_FIELD];
} coco_license_t;

typedef struct coco_image_t {
	i32 id;
	i32 license;
	char coco_url[COCO_MAX_FIELD];
	char flickr_url[COCO_MAX_FIELD];
	i32 width;
	i32 height;
	char file_name[COCO_MAX_FIELD];
	char date_captured[COCO_MAX_FIELD];
} coco_image_t;

typedef struct coco_segmentation_t {
	v2f* coordinates;
	i32 coordinate_count;
} coco_segmentation_t;

typedef struct coco_annotation_t {
	i32 id;
	i32 category_id;
	coco_segmentation_t segmentation;
	i32 image_id;
	float area;
	rect2f bbox;
	float features[COCO_MAX_ANNOTATION_FEATURES];
} coco_annotation_t;

typedef struct coco_category_t {
	char supercategory[COCO_MAX_FIELD];
	i32 id;
	char name[COCO_MAX_FIELD];
	rgba_t color;
} coco_category_t;

typedef struct coco_feature_t {
	i32 id;
	i32 category_id;
	bool restrict_to_group;
	char name[COCO_MAX_FIELD];
} coco_feature_t;

typedef struct coco_t {
	size_t original_filesize;
	coco_info_t info;
	coco_license_t* licenses;
	i32 license_count;
	coco_image_t* images;
	i32 image_count;
	coco_annotation_t* annotations;
	i32 annotation_count;
	coco_category_t* categories;
	i32 category_count;
	coco_feature_t* features;
	i32 feature_count;
	i32 main_license_id;
	i32 main_category_id;
	i32 main_image_id;
	bool is_valid;
} coco_t;


bool open_coco(coco_t* coco, const char* json_source, size_t json_length);
bool load_coco_from_file(coco_t* coco, const char* json_filename);
void coco_init_main_image(coco_t* coco, image_t* image);
void coco_transfer_annotations_from_annotation_set(coco_t* coco, annotation_set_t* annotation_set);
void coco_transfer_annotations_to_annotation_set(coco_t* coco, annotation_set_t* annotation_set);
memrw_t save_coco(coco_t* coco);
void coco_destroy(coco_t* coco);
coco_t coco_create_empty();

#ifdef __cplusplus
}
#endif
