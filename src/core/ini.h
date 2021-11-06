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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

enum ini_line_type_enum {
	INI_ENTRY_EMPTY_OR_COMMENT = 0,
	INI_ENTRY_SECTION = 1,
	INI_ENTRY_OPTION = 2,
};

enum ini_link_type_enum {
	INI_LINK_VOID = 0,
	INI_LINK_INTEGER_SIGNED = 1,
	INI_LINK_INTEGER_UNSIGNED = 2,
	INI_LINK_FLOAT = 3,
	INI_LINK_BOOL = 4,
	INI_LINK_STRING = 5,
	INI_LINK_CUSTOM = 6,
};

#define INI_MAX_NAME 64

typedef struct ini_option_t {
	u32 link_type;
	u32 link_size;
	void* link;
	char name[INI_MAX_NAME];
	u32 entry_index;
	u32 sparse_index;
	bool has_entry;
} ini_option_t;

typedef struct ini_section_t {
	char name[INI_MAX_NAME];
	u32 highest_sparse_index;
	u32 lowest_entry_index;
	u32 entry_count;
	ini_option_t* options;
	i32 option_count;
} ini_section_t;

typedef struct ini_entry_t {
	u32 sparse_index; // ordering of entries with 'spaced out' indices, to allow for easy insertion later
	u32 type;
	char name[INI_MAX_NAME];
	char value[256];
	const char* section;
	i32 section_index;
	u32 link_type;
	u32 link_size;
	void* link;
} ini_entry_t;

typedef struct ini_t {
	ini_entry_t* entries;
	i32 entry_count;
	ini_section_t* sections;
	i32 section_count;
	const char* current_section_name;
	i32 current_section_index;
} ini_t;


// Prototypes
void ini_apply(ini_t* ini);
void ini_begin_section(ini_t* ini, const char* section);
void ini_register_option(ini_t* ini, const char* name, u32 link_type, u32 link_size, void* link);
void ini_register_i32(ini_t* ini, const char* name, i32* link);
void ini_register_bool(ini_t* ini, const char* name, bool* link);
ini_t* ini_load_from_file(const char* filename);

#ifdef __cplusplus
}
#endif
