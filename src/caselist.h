/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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




typedef struct {
	const char *name;
	const char *filename;
	const char *clinical_context;
	const char *diagnosis;
	const char *notes;
} case_t;

typedef struct {
	u32 case_count;
	u32 num_cases_with_filenames;
	case_t *cases;
	const char** names;
	JSON_Value* json_root_value;
	bool32 is_remote;
	char folder_prefix[512]; // working directory
	u32 prefix_len;
} caselist_t;

typedef struct app_state_t app_state_t;
void reset_global_caselist(app_state_t* app_state);
void reload_global_caselist(app_state_t *app_state, const char *filename);
bool32 load_caselist(caselist_t* caselist, const char* json_filename);
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




