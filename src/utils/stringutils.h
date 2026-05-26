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

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

void strip_character(char* s, char character_to_strip);
char* trim_whitespace(char* s);
char* find_next_token(const char* s, char separator);
void dots_to_underscores(char* s, i32 max);
const char* one_past_last_slash(const char* s, i32 max);
const char* get_file_extension(const char* filename);
void replace_file_extension(char* filename, i32 max_len, const char* new_ext);
char** split_into_lines(char* buffer, size_t* num_lines);
size_t count_lines(char* buffer);
bool hex_digit_to_value(char c, u8* out_value);
size_t uri_percent_decode(const char* src, char* dest, size_t dest_size);

#ifdef __cplusplus
};
#endif
