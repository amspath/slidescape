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

#include "common.h"
#include "platform.h"
#include "stringutils.h"

#include "ini.h"

i32 count_leading_whitespace(const char* str, size_t len) {
	i32 count = 0;
	for(;;) {
		int c = *str++;
		if (c == ' ' || c == '\t') {
			++count;
			if (len-- > 0) {
				continue;
			}
		}
		break;
	}
	return count;
}

i32 count_whitespace_reverse(const char* str, size_t len) {
	i32 count = 0;
	while (len > 0) {
		--len;
		--str;
		int c = *str;
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			++count;
			continue;
		} else {
			break;
		}
	}
	return count;
}

static bool update_linked_value(void* link, void* value, size_t link_size) {
	bool value_changed = (memcmp(link, value, link_size) != 0);
	memcpy(link, value, link_size); // NOTE: assumes little-endian
	return value_changed;
}

bool ini_apply_option(ini_entry_t* entry) {
	bool value_changed = false;
	switch(entry->link_type) {
		default:
		case INI_LINK_VOID: {
			// nothing to do
		} break;
		case INI_LINK_INTEGER_SIGNED: {
			i64 value = atoll(entry->value);
			value_changed = update_linked_value(entry->link, &value, entry->link_size);
		} break;
		case INI_LINK_INTEGER_UNSIGNED: {
			i64 value_raw = atoll(entry->value);
			if (value_raw >= 0) {
				u64 value = (i64)value_raw;
				value_changed = update_linked_value(entry->link, &value, entry->link_size);
			} else {
				// TODO: handle error condition: invalid input
			}
		} break;
		case INI_LINK_FLOAT: {
			float value = (float)atof(entry->value);
			value_changed = update_linked_value(entry->link, &value, sizeof(float));
		} break;
		case INI_LINK_BOOL: {
			bool value = false;
			if (strcasecmp(entry->value, "true") == 0) {
				value = true;
			} else if (strcasecmp(entry->value, "false") == 0) {
				value = false;
			} else {
				value = (bool)atoll(entry->value);
			}
			value_changed = update_linked_value(entry->link, &value, sizeof(bool));
		} break;
		case INI_LINK_STRING: {
			ASSERT(!"not implemented");
		} break;
		case INI_LINK_CUSTOM: {
			ASSERT(!"not implemented");
		} break;
	}
	return value_changed;
}

void ini_apply(ini_t* ini) {
	for (i32 i = 0; i < ini->entry_count; ++i) {
		ini_entry_t* entry = ini->entries + i;
		if (entry->type != INI_ENTRY_OPTION) continue;
		bool value_changed = ini_apply_option(entry);
		if (value_changed) {
			console_print("Applied option '%s = %s'\n", entry->name, entry->value);
		}
	}
}

void ini_begin_section(ini_t* ini, const char* section) {
	ini->current_section = section;
	// TODO: if section doesn't exist: insert new section
	bool match = false;
	for (i32 i = 0; i < ini->entry_count; ++i) {
		ini_entry_t* entry = ini->entries + i;
		if (entry->type != INI_ENTRY_SECTION) continue;
		if (strncmp(section, entry->name, sizeof(entry->name)-1) == 0) {
			match = true;
			break;
		}
	}
	if (!match) {

	}
}


void ini_register_option(ini_t* ini, const char* name, u32 link_type, u32 link_size, void* link) {
	bool match = false;
	for (i32 i = 0; i < ini->entry_count; ++i) {
		ini_entry_t* entry = ini->entries + i;
		if (entry->type != INI_ENTRY_OPTION) continue;
		if (strncmp(name, entry->name, sizeof(entry->name)-1) == 0) {
			match = true;
			entry->link_type = link_type;
			entry->link_size = link_size;
			entry->link = link;
			break;
		}
	}
	if (!match) {
		// TODO: add new option, not yet present in INI file.
	}
}

void ini_register_i32(ini_t* ini, const char* name, i32* link) {
	ini_register_option(ini, name, INI_LINK_INTEGER_SIGNED, sizeof(i32), link);
}

void ini_register_bool(ini_t* ini, const char* name, bool* link) {
	ini_register_option(ini, name, INI_LINK_BOOL, sizeof(bool), link);
}

ini_entry_t ini_parse_line(char* line_string) {
	ini_entry_t result = {};
	result.type = INI_ENTRY_EMPTY_OR_COMMENT; // default behavior: ignore line
	size_t len = strlen(line_string);
	if (len == 0 || line_string[0] == ';') {
		// empty line or INI comment
		return result;
	}
	if (line_string[0] == '[' && len >= 2) {
		// INI section
		char* name_start = line_string + 1;
		char* close_bracket = memchr(name_start, ']', len);
		if (close_bracket) {
			result.type = INI_ENTRY_SECTION;
			i32 name_len = close_bracket - name_start;
			ASSERT(name_len >= 0);
			strncpy(result.name, name_start, MIN(name_len, sizeof(result.name)-1));
			return result;
		} else {
			return result; // invalid, ignore line
		}
	} else {
		// INI option
		char* equals_sign = memchr(line_string, '=', len);
		if (equals_sign) {
			result.type = INI_ENTRY_OPTION;
			i32 name_len = equals_sign - line_string;
			name_len -= count_whitespace_reverse(equals_sign, name_len);
			ASSERT(name_len >= 0);
			strncpy(result.name, line_string, MIN(name_len, sizeof(result.name)-1));
			char* value_string = (equals_sign+1);
			i32 value_len = len - (value_string - line_string);
			i32 leading_whitespace = count_leading_whitespace(value_string, value_len);
			value_string += leading_whitespace;
			value_len -= leading_whitespace;
			ASSERT(value_len >= 0);
			strncpy(result.value, value_string, MIN(value_len, sizeof(result.value)-1));
			return result;
		} else {
			return result;// invalid, ignore line
		}
	}
}

ini_t* ini_load(char* ini_string, size_t len) {
	if (!ini_string) {
		ini_string = "";
		len = 0;
	}
	ini_t* ini = calloc(1, sizeof(ini_t));

	size_t line_count = 0;
	char** lines = split_into_lines(ini_string, &line_count);

	for (i32 line_index = 0; line_index < line_count; ++line_index) {
		ini_entry_t entry = ini_parse_line(lines[line_index]);
		entry.sparse_index = (line_index+1) * 10000; // ordering of entries with 'spaced out' indices, to allow for easy insertion later
		sb_push(ini->entries, entry);
		++ini->entry_count;
	}
	free(lines);


	return ini;
}

ini_t* ini_load_from_file(const char* filename) {
	mem_t* file = platform_read_entire_file(filename);
	char* data = file ? (char*)file->data : NULL;
	ini_t* ini = ini_load(data, file->len);
	free(file);
	return ini;
}

void ini_sync_value_string(ini_entry_t* entry) {
	size_t maxstr = sizeof(entry->value)-1;
	if (entry->link) {
		switch(entry->link_type) {
			case INI_LINK_VOID: {

			} break;
			case INI_LINK_INTEGER_SIGNED: {
				i64 value = 0;
				switch(entry->link_size) {
					default: break;
					case 1: value = *(i8*)entry->link; break;
					case 2: value = *(i16*)entry->link; break;
					case 4: value = *(i32*)entry->link; break;
					case 8: value = *(i64*)entry->link; break;
				}
				snprintf(entry->value, maxstr, "%lld", value);
			} break;
			case INI_LINK_INTEGER_UNSIGNED: {
				u64 value = 0;
				switch(entry->link_size) {
					default: break;
					case 1: value = *(u8*)entry->link; break;
					case 2: value = *(u16*)entry->link; break;
					case 4: value = *(u32*)entry->link; break;
					case 8: value = *(u64*)entry->link; break;
				}
				snprintf(entry->value, maxstr, "%llu", value);
			} break;
			case INI_LINK_FLOAT: {
				float value = *(float*)entry->link;
				snprintf(entry->value, maxstr, "%f", value);
			} break;
			case INI_LINK_BOOL: {
				float value = *(bool*)entry->link;
				snprintf(entry->value, maxstr, value ? "true" : "false");
			} break;
			case INI_LINK_STRING: {

			} break;
			case INI_LINK_CUSTOM: {

			} break;
		}
	}
}

void ini_save(ini_t* ini, const char* filename) {
	if (!ini) return;

	FILE* fp = fopen(filename, "w");

	for (i32 i = 0; i < ini->entry_count; ++i) {
		ini_entry_t* entry = ini->entries + i;
		if (entry->type == INI_ENTRY_EMPTY_OR_COMMENT) {
			fprintf(fp, "%s\n", entry->value);
		} else if (entry->type == INI_ENTRY_SECTION) {
			fprintf(fp, "[%s]\n", entry->name);
		} else if (entry->type == INI_ENTRY_OPTION) {
			ini_sync_value_string(entry);
			fprintf(fp, "%s=%s\n", entry->name, entry->value);
		}
	}

	fclose(fp);
}