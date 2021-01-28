/*
  Slideviewer, a whole-slide image viewer for digital pathology.
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

#pragma once
#include "common.h"
#include "parson.h"

#define SLIDE_MAX_PATH 512
#define SLIDE_MAX_META_STR 128

typedef struct {
	char base_filename[SLIDE_MAX_PATH];
	char block[SLIDE_MAX_META_STR];
	char stain[SLIDE_MAX_META_STR];
	const char* notes;
} slide_info_t;

typedef struct {
	const char *name;
	slide_info_t* slides;
	u32 slide_count;
	const char *clinical_context;
	const char *diagnosis;
	const char *notes;
} case_t;

typedef struct {
	u32 case_count;
	case_t *cases;
	const char** names;
	JSON_Value* json_root_value;
	bool32 is_remote;
	char folder_prefix[SLIDE_MAX_PATH]; // working directory
	u32 prefix_len;
} caselist_t;

typedef struct app_state_t app_state_t;
void reset_global_caselist(app_state_t* app_state);
void reload_global_caselist(app_state_t *app_state, const char *filename);
bool32 caselist_open_slide(app_state_t* app_state, caselist_t* caselist, slide_info_t* slide);
bool32 caselist_select_first_case(app_state_t* app_state, caselist_t* caselist);
bool32 load_caselist(caselist_t* caselist, const char* source, const char* caselist_name);
bool32 load_caselist_from_file(caselist_t* caselist, const char* json_filename);
bool32 load_caselist_from_remote(caselist_t* caselist, const char* hostname, i32 portno, const char* name);
void caselist_destroy(caselist_t* caselist);

// globals
#if defined(CASELIST_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

//extern caselist_t global_caselist;
//extern case_t* global_selected_case;

#undef INIT
#undef extern




