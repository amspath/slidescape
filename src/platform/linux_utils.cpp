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

#include "common.h"
#include "platform.h"

int platform_stat(const char* filename, struct stat* st) {
	return stat(filename, st);
}

file_stream_t file_stream_open_for_reading(const char* filename) {
	FILE* handle = fopen64(filename, "rb");
	return handle;
}

file_stream_t file_stream_open_for_writing(const char* filename) {
	FILE* handle = fopen64(filename, "wb");
	return handle;
}

i64 file_stream_read(void* dest, size_t bytes_to_read, file_stream_t file_stream) {
	size_t bytes_read = fread(dest, 1, bytes_to_read, file_stream);
	return bytes_read;
}

void file_stream_write(void* source, size_t bytes_to_write, file_stream_t file_stream) {
	size_t ret = fwrite(source, 1, bytes_to_write, file_stream);
}

i64 file_stream_get_filesize(file_stream_t file_stream) {
	struct stat st;
	if (fstat(fileno(file_stream), &st) == 0) {
		i64 filesize = st.st_size;
		return filesize;
	} else {
		return 0;
	}
}

i64 file_stream_get_pos(file_stream_t file_stream) {
	fpos_t prev_read_pos = {0}; // NOTE: fpos_t may be a struct!
	int ret = fgetpos64(file_stream, &prev_read_pos); // for restoring the file position later
	ASSERT(ret == 0); (void)ret;
#ifdef _FPOSOFF
	return _FPOSOFF(prev_read_pos);
#else
	STATIC_ASSERT(sizeof(off_t) == 8);
	// Somehow, it is unclear what is the 'correct' way to convert an fpos_t to a simple integer?
	return *(i64*)(&prev_read_pos);
#endif
}

bool file_stream_set_pos(file_stream_t file_stream, i64 offset) {
	fpos_t pos = {offset};
	int ret = fsetpos64(file_stream, &pos);
	return (ret == 0);
}

void file_stream_close(file_stream_t file_stream) {
	fclose(file_stream);
}

file_handle_t open_file_handle_for_simultaneous_access(const char* filename) {
	file_handle_t fd = open(filename, O_RDONLY);
	if (fd == -1) {
		console_print_error("Error: Could not reopen file for asynchronous I/O\n");
		return 0;
	} else {
		return fd;
	}
}

void file_handle_close(file_handle_t file_handle) {
	if (file_handle) {
		close(file_handle);
	}
}

size_t file_handle_read_at_offset(void* dest, file_handle_t file_handle, u64 offset, size_t bytes_to_read) {
	size_t bytes_read = pread(file_handle, dest, bytes_to_read, offset);
	return bytes_read;
}