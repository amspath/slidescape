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

#include "common.h"
#include "stringutils.h"

#include <string.h>

void dots_to_underscores(char* s, i32 max) {
	for (char* pos = s; pos < s + max; ++pos) {
		char c = *pos;
		if (c == '\0') break;
		if (c == '.') *pos = '_';
	}
}

const char* one_past_last_slash(const char* s, i32 max) {
	i32 len = strnlen(s, max - 1);
	i32 stripped_len = 0;
	const char* pos = s + len - 1;
	for (; pos >= s; --pos) {
		char c = *pos;
		if (c == '/')  {
			pos += 1; // gone back one too far
			break;
		} else {
			++stripped_len;
		}
	}
	const char* result = pos;
	ASSERT(stripped_len > 0 && stripped_len <= len);
	return result;
}

const char* get_file_extension(const char* filename) {
	size_t len = strlen(filename);
	const char* ext = filename + len;
	for (const char* pos = ext - 1; pos >= filename; --pos) {
		if (*pos == '.') {
			ext = pos + 1;
			break;
		}
	}
	return ext;
}
