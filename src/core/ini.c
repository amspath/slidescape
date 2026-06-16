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
#include "platform.h"
#include "stringutils.h"
#include "memrw.h"
#include "intrinsics.h"

#include "ini.h"

typedef struct ini_save_async_task_t {
	char filename[1024];
	u8* data;
	u64 data_size;
	i32 generation;
} ini_save_async_task_t;

static platform_mutex_t ini_save_mutex = PLATFORM_MUTEX_INITIALIZER;
static volatile i32 ini_save_generation;

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
	memcpy(link, value, link_size);
	return value_changed;
}

bool ini_apply_option(ini_option_t* option, const char* value_string) {
	bool value_changed = false;
	switch(option->link_type) {
		default:
		case INI_LINK_VOID: {
			// nothing to do
		} break;
		case INI_LINK_INTEGER_SIGNED: {
			i64 value = atoll(value_string);
			switch(option->link_size) {
				case 1: { i8 v = (i8)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
				case 2: { i16 v = (i16)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
				case 4: { i32 v = (i32)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
				case 8: { i64 v = (i64)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
				default: break;
			}
		} break;
		case INI_LINK_INTEGER_UNSIGNED: {
			i64 value_raw = atoll(value_string);
			if (value_raw >= 0) {
				u64 value = (u64)value_raw;
				switch(option->link_size) {
					case 1: { u8 v = (u8)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
					case 2: { u16 v = (u16)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
					case 4: { u32 v = (u32)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
					case 8: { u64 v = (u64)value; value_changed = update_linked_value(option->link, &v, sizeof(v)); } break;
					default: break;
				}
			} else {
				// TODO: handle error condition: invalid input
			}
		} break;
		case INI_LINK_FLOAT: {
			float value = (float)atof(value_string);
			value_changed = update_linked_value(option->link, &value, sizeof(float));
		} break;
		case INI_LINK_BOOL: {
			bool value = false;
			if (strcasecmp(value_string, "true") == 0) {
				value = true;
			} else if (strcasecmp(value_string, "false") == 0) {
				value = false;
			} else {
				value = (bool)atoll(value_string);
			}
			value_changed = update_linked_value(option->link, &value, sizeof(bool));
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
	for (i32 section_index = 0; section_index < ini->section_count; ++section_index) {
		ini_section_t* section = ini->sections + section_index;
		for (i32 i = 0; i < section->option_count; ++i) {
			ini_option_t* option = section->options + i;
			if (option->has_entry) {
				ini_entry_t* entry = ini->entries + option->entry_index;
				const char* value_string = entry->value;
				bool value_changed = ini_apply_option(option, value_string);
				if (value_changed) {
					console_print_verbose("Applied option '%s = %s'\n", option->name, value_string);
				}
			}
		}
	}
}

void ini_begin_section(ini_t* ini, const char* section_name) {
	ini->current_section_name = section_name;
	bool match = false;
	for (i32 i = 0; i < ini->section_count; ++i) {
		ini_section_t* section = ini->sections + i;
		if (strncmp(section_name, section->name, INI_MAX_NAME) == 0) {
			match = true;
			ini->current_section_index = i;
			break;
		}
	}
	if (!match) {
		ini_section_t section = {0};
		copy_cstring(section.name, section_name, sizeof(section.name));
		arrput(ini->sections, section);
		ini->current_section_index = ini->section_count++;
	}
}


void ini_register_option(ini_t* ini, const char* name, u32 link_type, u32 link_size, void* link) {
	bool match = false;
	ini_section_t* section = ini->sections + ini->current_section_index;
	for (i32 i = 0; i < section->option_count; ++i) {
		ini_option_t* option = section->options + i;
		if (strncmp(name, option->name, INI_MAX_NAME) == 0) {
			match = true;
			option->link_type = link_type;
			option->link_size = link_size;
			option->link = link;
			option->has_entry = true;
			break;
		}
	}
	if (!match) {
		ini_option_t option = {0};
		copy_cstring(option.name, name, sizeof(option.name));
		option.link_type = link_type;
		option.link_size = link_size;
		option.link = link;
		option.has_entry = false;
		arrput(section->options, option);
		++section->option_count;
	}
}

void ini_register_i32(ini_t* ini, const char* name, i32* link) {
	ini_register_option(ini, name, INI_LINK_INTEGER_SIGNED, sizeof(i32), link);
}

void ini_register_bool(ini_t* ini, const char* name, bool* link) {
	ini_register_option(ini, name, INI_LINK_BOOL, sizeof(bool), link);
}

ini_entry_t ini_parse_line(char* line_string) {
	ini_entry_t result = {0};
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
			copy_cstring(result.name, name_start, MIN((size_t)name_len + 1, sizeof(result.name)));
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
			copy_cstring(result.name, line_string, MIN((size_t)name_len + 1, sizeof(result.name)));
			char* value_string = (equals_sign+1);
			i32 value_len = len - (value_string - line_string);
			i32 leading_whitespace = count_leading_whitespace(value_string, value_len);
			value_string += leading_whitespace;
			value_len -= leading_whitespace;
			ASSERT(value_len >= 0);
			copy_cstring(result.value, value_string, MIN((size_t)value_len + 1, sizeof(result.value)));
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

	ini_section_t null_section = {0}; // entries are placed here until the first 'real' section is defined
	arrput(ini->sections, null_section);
	++ini->section_count;
	ini_section_t* current_section = ini->sections;

	i32 running_section_index = 0;
	for (i32 line_index = 0; line_index < line_count; ++line_index) {
		ini_entry_t entry = ini_parse_line(lines[line_index]);
		entry.sparse_index = (line_index+1) * 10000; // ordering of entries with 'spaced out' indices, to allow for easy insertion later
		if (entry.type == INI_ENTRY_SECTION) {
			ini_section_t section = {0};
			section.highest_sparse_index = line_index * 10000;
			memcpy(section.name, entry.name, INI_MAX_NAME);
			arrput(ini->sections, section);
			++ini->section_count;
			++running_section_index;
			current_section = ini->sections + running_section_index;
		} else if (entry.type == INI_ENTRY_OPTION) {
			ini_option_t option = {0};
			memcpy(option.name, entry.name, INI_MAX_NAME);
			option.entry_index = ini->entry_count;
			option.sparse_index = entry.sparse_index;
			option.has_entry = true;
			arrput(current_section->options, option);
			++current_section->option_count;
		}
		entry.section_index = running_section_index;
		arrput(ini->entries, entry);
		++ini->entry_count;
	}
	free(lines);


	return ini;
}

ini_t* ini_load_from_file(const char* filename) {
	mem_t* file = platform_read_entire_file(filename);
	if (file) {
		ini_t* ini = ini_load((char*)file->data, file->len);
		free(file);
		return ini;
	} else {
		ini_t* ini = ini_load("", 0);
		return ini;
	}
}

void ini_option_get_value_string(ini_option_t* option, char* buf, size_t buf_size) {
	if (buf_size == 0) return;
	buf[0] = '\0';
	size_t maxstr = buf_size;
	if (option->link) {
		switch(option->link_type) {
			case INI_LINK_VOID: {

			} break;
			case INI_LINK_INTEGER_SIGNED: {
				i64 value = 0;
				switch(option->link_size) {
					default: break;
					case 1: value = *(i8*)option->link; break;
					case 2: value = *(i16*)option->link; break;
					case 4: value = *(i32*)option->link; break;
					case 8: value = *(i64*)option->link; break;
				}
				snprintf(buf, maxstr, "%lld", (long long)value);
			} break;
			case INI_LINK_INTEGER_UNSIGNED: {
				u64 value = 0;
				switch(option->link_size) {
					default: break;
					case 1: value = *(u8*)option->link; break;
					case 2: value = *(u16*)option->link; break;
					case 4: value = *(u32*)option->link; break;
					case 8: value = *(u64*)option->link; break;
				}
				snprintf(buf, maxstr, "%llu", (unsigned long long)value);
			} break;
			case INI_LINK_FLOAT: {
				float value = *(float*)option->link;
				snprintf(buf, maxstr, "%g", value);
			} break;
			case INI_LINK_BOOL: {
				bool value = *(bool*)option->link;
				snprintf(buf, maxstr, value ? "true" : "false");
			} break;
			case INI_LINK_STRING: {

			} break;
			case INI_LINK_CUSTOM: {

			} break;
		}
	}
	buf[buf_size - 1] = '\0';
}

static ini_option_t* ini_find_linked_option_for_entry(ini_t* ini, ini_entry_t* entry) {
	if (entry->section_index < 0 || entry->section_index >= ini->section_count) {
		return NULL;
	}
	ini_section_t* section = ini->sections + entry->section_index;
	for (i32 i = 0; i < section->option_count; ++i) {
		ini_option_t* option = section->options + i;
		if (option->has_entry && option->entry_index == (u32)(entry - ini->entries) && option->link) {
			return option;
		}
	}
	return NULL;
}

static bool ini_section_has_entry(ini_t* ini, i32 section_index) {
	if (section_index == 0) {
		return true;
	}
	for (i32 i = 0; i < ini->entry_count; ++i) {
		ini_entry_t* entry = ini->entries + i;
		if (entry->type == INI_ENTRY_SECTION && entry->section_index == section_index) {
			return true;
		}
	}
	return false;
}

static void ini_write_registered_options(memrw_t* out, ini_section_t* section, bool* written_options) {
	for (i32 option_index = 0; option_index < section->option_count; ++option_index) {
		ini_option_t* option = section->options + option_index;
		if (!option->link || written_options[option_index]) {
			continue;
		}
		char value[256];
		ini_option_get_value_string(option, value, sizeof(value));
		memrw_printf(out, "%s=%s\n", option->name, value);
		written_options[option_index] = true;
	}
}

static memrw_t ini_build_save_buffer(ini_t* ini) {
	memrw_t out = {0};
	memrw_init(&out, KILOBYTES(32));

	bool** written_by_section = NULL;
	for (i32 section_index = 0; section_index < ini->section_count; ++section_index) {
		ini_section_t* section = ini->sections + section_index;
		bool* written_options = NULL;
		if (section->option_count > 0) {
			written_options = (bool*)calloc(section->option_count, sizeof(bool));
		}
		arrput(written_by_section, written_options);
	}

	i32 active_section_index = 0;
	for (i32 i = 0; i < ini->entry_count; ++i) {
		ini_entry_t* entry = ini->entries + i;
		if (entry->type == INI_ENTRY_EMPTY_OR_COMMENT) {
			memrw_printf(&out, "%s\n", entry->value);
		} else if (entry->type == INI_ENTRY_SECTION) {
			if (active_section_index >= 0 && active_section_index < ini->section_count) {
				ini_write_registered_options(&out, ini->sections + active_section_index, written_by_section[active_section_index]);
			}
			active_section_index = entry->section_index;
			if (i > 0 && out.used_size > 0 && out.data[out.used_size - 1] != '\n') {
				memrw_putc('\n', &out);
			}
			memrw_printf(&out, "[%s]\n", entry->name);
		} else if (entry->type == INI_ENTRY_OPTION) {
			char value[256];
			ini_option_t* option = ini_find_linked_option_for_entry(ini, entry);
			if (option) {
				ini_option_get_value_string(option, value, sizeof(value));
				bool* written_options = written_by_section[entry->section_index];
				i32 section_option_count = ini->sections[entry->section_index].option_count;
				for (i32 option_index = 0; option_index < section_option_count; ++option_index) {
					if (option == ini->sections[entry->section_index].options + option_index) {
						written_options[option_index] = true;
						break;
					}
				}
			} else {
				copy_cstring(value, entry->value, sizeof(value));
			}
			memrw_printf(&out, "%s=%s\n", entry->name, value);
		}
	}

	for (i32 section_index = 0; section_index < ini->section_count; ++section_index) {
		ini_section_t* section = ini->sections + section_index;
		bool* written_options = written_by_section[section_index];
		if (section->option_count == 0) {
			continue;
		}
		bool has_unwritten_option = false;
		for (i32 option_index = 0; option_index < section->option_count; ++option_index) {
			ini_option_t* option = section->options + option_index;
			if (option->link && !written_options[option_index]) {
				has_unwritten_option = true;
				break;
			}
		}
		if (!has_unwritten_option) {
			continue;
		}
		if (out.used_size > 0 && out.data[out.used_size - 1] != '\n') {
			memrw_putc('\n', &out);
		}
		if (section_index != 0 && !ini_section_has_entry(ini, section_index)) {
			memrw_printf(&out, "[%s]\n", section->name);
		}
		ini_write_registered_options(&out, section, written_options);
	}

	for (i32 section_index = 0; section_index < arrlen(written_by_section); ++section_index) {
		free(written_by_section[section_index]);
	}
	arrfree(written_by_section);

	return out;
}

static void ini_write_buffer_to_file(const char* filename, u8* data, u64 data_size) {
	file_stream_t fp = file_stream_open_for_writing(filename);
	if (fp) {
		file_stream_write(data, data_size, fp);
		file_stream_close(fp);
	} else {
		console_print_error("Error: Could not save settings to '%s'\n", filename);
	}
}

static void ini_save_async_func(i32 logical_thread_index, void* userdata) {
	(void)logical_thread_index;
	ini_save_async_task_t* task = (ini_save_async_task_t*)userdata;
	platform_mutex_lock(&ini_save_mutex);
	if (task->generation == ini_save_generation) {
		ini_write_buffer_to_file(task->filename, task->data, task->data_size);
	}
	platform_mutex_unlock(&ini_save_mutex);
	free(task->data);
}

void ini_save_sync(ini_t* ini, const char* filename) {
	if (!ini) return;

	i32 generation = atomic_increment(&ini_save_generation);
	memrw_t out = ini_build_save_buffer(ini);
	platform_mutex_lock(&ini_save_mutex);
	if (generation == ini_save_generation) {
		ini_write_buffer_to_file(filename, out.data, out.used_size);
	}
	platform_mutex_unlock(&ini_save_mutex);
	memrw_destroy(&out);
}

void ini_save(ini_t* ini, const char* filename) {
	if (!ini) return;

	i32 generation = atomic_increment(&ini_save_generation);
	memrw_t out = ini_build_save_buffer(ini);
	ini_save_async_task_t task = {0};
	copy_cstring(task.filename, filename, sizeof(task.filename));
	task.data = out.data;
	task.data_size = out.used_size;
	task.generation = generation;

	if (thread_pool_submit_task(&global_thread_pool, ini_save_async_func, &task, sizeof(task))) {
		out.data = NULL;
		out.used_size = 0;
		out.capacity = 0;
	} else {
		platform_mutex_lock(&ini_save_mutex);
		if (generation == ini_save_generation) {
			ini_write_buffer_to_file(filename, out.data, out.used_size);
		}
		platform_mutex_unlock(&ini_save_mutex);
	}
	memrw_destroy(&out);
}
