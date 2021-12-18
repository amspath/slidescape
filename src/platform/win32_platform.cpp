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

#include "common.h"
#include "win32_platform.h"
#include "viewer.h"
#include "gui.h"

wchar_t* win32_string_widen(const char* s, size_t len, wchar_t* buffer) {
	int characters_written = MultiByteToWideChar(CP_UTF8, 0, s, -1, buffer, len);
	if (characters_written > 0) {
		int last_character = MIN(characters_written, ((int)len)-1);
		buffer[last_character] = '\0';
	} else {
		win32_diagnostic("MultiByteToWideChar");
		buffer[0] = '\0';
	}
	return buffer;
}

char* win32_string_narrow(wchar_t* s, char* buffer, size_t buffer_size) {
	int bytes_written = WideCharToMultiByte(CP_UTF8, 0, s, -1, buffer, buffer_size, NULL, NULL);
	if (bytes_written > 0) {
		ASSERT(bytes_written < buffer_size);
		int last_byte = MIN(bytes_written, ((int)buffer_size)-1);
		buffer[last_byte] = '\0';
	} else {
		win32_diagnostic("WideCharToMultiByte");
		buffer[0] = '\0';
	}
	return buffer;
}

void win32_diagnostic(const char* prefix) {
	DWORD error_id = GetLastError();
	char* message_buffer;
	/*size_t size = */FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                                 NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
	console_print("%s: (error code 0x%x) %s\n", prefix, (u32)error_id, message_buffer);
	LocalFree(message_buffer);
}

void win32_diagnostic_verbose(const char* prefix) {
	DWORD error_id = GetLastError();
	char* message_buffer;
	/*size_t size = */FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                                 NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
	console_print_verbose("%s: (error code 0x%x) %s\n", prefix, (u32)error_id, message_buffer);
	LocalFree(message_buffer);
}

// Sources:
// https://stackoverflow.com/questions/291424/canonical-way-to-parse-the-command-line-into-arguments-in-plain-c-windows-api
// https://github.com/futurist/CommandLineToArgvA

/*************************************************************************
 * CommandLineToArgvA            [SHELL32.@]
 *
 * MODIFIED FROM https://www.winehq.org/ project
 * We must interpret the quotes in the command line to rebuild the argv
 * array correctly:
 * - arguments are separated by spaces or tabs
 * - quotes serve as optional argument delimiters
 *   '"a b"'   -> 'a b'
 * - escaped quotes must be converted back to '"'
 *   '\"'      -> '"'
 * - consecutive backslashes preceding a quote see their number halved with
 *   the remainder escaping the quote:
 *   2n   backslashes + quote -> n backslashes + quote as an argument delimiter
 *   2n+1 backslashes + quote -> n backslashes + literal quote
 * - backslashes that are not followed by a quote are copied literally:
 *   'a\b'     -> 'a\b'
 *   'a\\b'    -> 'a\\b'
 * - in quoted strings, consecutive quotes see their number divided by three
 *   with the remainder modulo 3 deciding whether to close the string or not.
 *   Note that the opening quote must be counted in the consecutive quotes,
 *   that's the (1+) below:
 *   (1+) 3n   quotes -> n quotes
 *   (1+) 3n+1 quotes -> n quotes plus closes the quoted string
 *   (1+) 3n+2 quotes -> n+1 quotes plus closes the quoted string
 * - in unquoted strings, the first quote opens the quoted string and the
 *   remaining consecutive quotes follow the above rule.
 */

LPSTR* WINAPI CommandLineToArgvA(LPSTR lpCmdline, int* numargs)
{
	DWORD argc;
	LPSTR  *argv;
	LPSTR s;
	LPSTR d;
	LPSTR cmdline;
	int qcount,bcount;

	if(!numargs || *lpCmdline==0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return NULL;
	}

	/* --- First count the arguments */
	argc=1;
	s=lpCmdline;
	/* The first argument, the executable path, follows special rules */
	if (*s=='"')
	{
		/* The executable path ends at the next quote, no matter what */
		s++;
		while (*s)
			if (*s++=='"')
				break;
	}
	else
	{
		/* The executable path ends at the next space, no matter what */
		while (*s && *s!=' ' && *s!='\t')
			s++;
	}
	/* skip to the first argument, if any */
	while (*s==' ' || *s=='\t')
		s++;
	if (*s)
		argc++;

	/* Analyze the remaining arguments */
	qcount=bcount=0;
	while (*s)
	{
		if ((*s==' ' || *s=='\t') && qcount==0)
		{
			/* skip to the next argument and count it if any */
			while (*s==' ' || *s=='\t')
				s++;
			if (*s)
				argc++;
			bcount=0;
		}
		else if (*s=='\\')
		{
			/* '\', count them */
			bcount++;
			s++;
		}
		else if (*s=='"')
		{
			/* '"' */
			if ((bcount & 1)==0)
				qcount++; /* unescaped '"' */
			s++;
			bcount=0;
			/* consecutive quotes, see comment in copying code below */
			while (*s=='"')
			{
				qcount++;
				s++;
			}
			qcount=qcount % 3;
			if (qcount==2)
				qcount=0;
		}
		else
		{
			/* a regular character */
			bcount=0;
			s++;
		}
	}

	/* Allocate in a single lump, the string array, and the strings that go
	 * with it. This way the caller can make a single LocalFree() call to free
	 * both, as per MSDN.
	 */
	argv=(LPSTR*)LocalAlloc(LMEM_FIXED, (argc+1)*sizeof(LPSTR)+(strlen(lpCmdline)+1)*sizeof(char));
	if (!argv)
		return NULL;
	cmdline=(LPSTR)(argv+argc+1);
	strcpy(cmdline, lpCmdline);

	/* --- Then split and copy the arguments */
	argv[0]=d=cmdline;
	argc=1;
	/* The first argument, the executable path, follows special rules */
	if (*d=='"')
	{
		/* The executable path ends at the next quote, no matter what */
		s=d+1;
		while (*s)
		{
			if (*s=='"')
			{
				s++;
				break;
			}
			*d++=*s++;
		}
	}
	else
	{
		/* The executable path ends at the next space, no matter what */
		while (*d && *d!=' ' && *d!='\t')
			d++;
		s=d;
		if (*s)
			s++;
	}
	/* close the executable path */
	*d++=0;
	/* skip to the first argument and initialize it if any */
	while (*s==' ' || *s=='\t')
		s++;
	if (!*s)
	{
		/* There are no parameters so we are all done */
		argv[argc]=NULL;
		*numargs=argc;
		return argv;
	}

	/* Split and copy the remaining arguments */
	argv[argc++]=d;
	qcount=bcount=0;
	while (*s)
	{
		if ((*s==' ' || *s=='\t') && qcount==0)
		{
			/* close the argument */
			*d++=0;
			bcount=0;

			/* skip to the next one and initialize it if any */
			do {
				s++;
			} while (*s==' ' || *s=='\t');
			if (*s)
				argv[argc++]=d;
		}
		else if (*s=='\\')
		{
			*d++=*s++;
			bcount++;
		}
		else if (*s=='"')
		{
			if ((bcount & 1)==0)
			{
				/* Preceded by an even number of '\', this is half that
				 * number of '\', plus a quote which we erase.
				 */
				d-=bcount/2;
				qcount++;
			}
			else
			{
				/* Preceded by an odd number of '\', this is half that
				 * number of '\' followed by a '"'
				 */
				d=d-bcount/2-1;
				*d++='"';
			}
			s++;
			bcount=0;
			/* Now count the number of consecutive quotes. Note that qcount
			 * already takes into account the opening quote if any, as well as
			 * the quote that lead us here.
			 */
			while (*s=='"')
			{
				if (++qcount==3)
				{
					*d++='"';
					qcount=0;
				}
				s++;
			}
			if (qcount==2)
				qcount=0;
		}
		else
		{
			/* a regular character */
			*d++=*s++;
			bcount=0;
		}
	}
	*d='\0';
	argv[argc]=NULL;
	*numargs=argc;

	return argv;
}

HANDLE win32_open_overlapped_file_handle(const char* filename) {
	// NOTE: Using the FILE_FLAG_NO_BUFFERING flag *might* be faster, but I am not actually noticing a speed increase,
	// so I am keeping it turned off for now.
	size_t filename_len = strlen(filename) + 1;
	wchar_t* wide_filename = win32_string_widen(filename, filename_len, (wchar_t*) alloca(2 * filename_len));
	HANDLE handle = CreateFileW(wide_filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	                            FILE_ATTRIBUTE_NORMAL | /*FILE_FLAG_SEQUENTIAL_SCAN |*/
	                            /*FILE_FLAG_NO_BUFFERING |*/ FILE_FLAG_OVERLAPPED,
	                            NULL);
	return handle;
}

file_handle_t open_file_handle_for_simultaneous_access(const char* filename) {
	return win32_open_overlapped_file_handle(filename);
}

size_t win32_overlapped_read(thread_memory_t* thread_memory, HANDLE file_handle, void* dest, u32 read_size, i64 offset) {
	// We align reads to 4K boundaries, so that file handles can be used with the FILE_FLAG_NO_BUFFERING flag.
	// See: https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering
	i64 aligned_offset = offset & ~(KILOBYTES(4)-1);
	i64 align_delta = offset - aligned_offset;
	ASSERT(align_delta >= 0);

	i64 end_byte = offset + read_size;
	i64 raw_read_size = end_byte - aligned_offset;
	ASSERT(raw_read_size >= 0);

	i64 bytes_to_read_in_last_sector = raw_read_size % KILOBYTES(4);
	if (bytes_to_read_in_last_sector > 0) {
		raw_read_size += KILOBYTES(4) - bytes_to_read_in_last_sector;
	}

	// TODO: detect if read size does not fit in the arena, and allocate memory if needed
	temp_memory_t temp_memory = begin_temp_memory(&thread_memory->temp_arena);
	u8* temp_dest = (u8*)arena_push_size(&thread_memory->temp_arena, raw_read_size);

	// To submit an async I/O request on Win32, we need to fill in an OVERLAPPED structure with the
	// offset in the file where we want to do the read operation
	LARGE_INTEGER offset_ = {.QuadPart = (i64)aligned_offset};
	OVERLAPPED overlapped = {};
	overlapped.Offset = offset_.LowPart;
	overlapped.OffsetHigh = (DWORD)offset_.HighPart;
	overlapped.hEvent = thread_memory->async_io_events[0];
	ResetEvent(thread_memory->async_io_events[0]); // reset the event to unsignaled state

	if (!ReadFile(file_handle, temp_dest, raw_read_size, NULL, &overlapped)) {
		DWORD error = GetLastError();
		if (error != ERROR_IO_PENDING) {
			win32_diagnostic("ReadFile");
			end_temp_memory(&temp_memory);
			return 0;
		}
	}

	// Wait for the result of the I/O operation (blocking, because we specify bWait=TRUE)
	DWORD bytes_read = 0;
	if (!GetOverlappedResult(file_handle, &overlapped, &bytes_read, TRUE)) {
		win32_diagnostic("GetOverlappedResult");
	}
	// This should not be strictly necessary, but do it just in case GetOverlappedResult exits early (paranoia)
	if(WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0) {
		win32_diagnostic("WaitForSingleObject");
	}

	memcpy(dest, temp_dest + align_delta, read_size);

	end_temp_memory(&temp_memory);
	return read_size;
}

size_t file_handle_read_at_offset(void* dest, file_handle_t file_handle, u64 offset, size_t bytes_to_read) {
	size_t bytes_read = win32_overlapped_read(local_thread_memory, file_handle, dest, bytes_to_read, offset);
	return bytes_read;
}


file_stream_t file_stream_open_for_reading(const char* filename) {
//	console_print_verbose("Attempting CreateFile() for reading...\n");

	size_t filename_len = strlen(filename) + 1;
	wchar_t* wide_filename = win32_string_widen(filename, filename_len, (wchar_t*) alloca(2 * filename_len));
	HANDLE handle = CreateFileW(wide_filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	                            FILE_ATTRIBUTE_NORMAL /* | FILE_FLAG_SEQUENTIAL_SCAN */
			/*| FILE_FLAG_NO_BUFFERING |*/ /* | FILE_FLAG_OVERLAPPED*/,
			                    NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		win32_diagnostic_verbose("CreateFile");
		return 0;
	} else {
		return handle;
	}
}

file_stream_t file_stream_open_for_writing(const char* filename) {
//	console_print_verbose("Attempting CreateFile()...\n");

	size_t filename_len = strlen(filename) + 1;
	wchar_t* wide_filename = win32_string_widen(filename, filename_len, (wchar_t*) alloca(2 * filename_len));
	HANDLE handle = CreateFileW(wide_filename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	                            FILE_ATTRIBUTE_NORMAL /* | FILE_FLAG_SEQUENTIAL_SCAN */
			/*| FILE_FLAG_NO_BUFFERING |*/ /* | FILE_FLAG_OVERLAPPED*/,
			                    NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		win32_diagnostic("CreateFile");
		return 0;
	} else {
		return handle;
	}
}

i64 file_stream_read(void* dest, size_t bytes_to_read, file_stream_t file_stream) {
	DWORD bytes_read;
//	console_print_verbose("Attempting ReadFile()...\n");
//	OVERLAPPED overlapped = {};
	if (!ReadFile(file_stream, dest, bytes_to_read, &bytes_read, NULL)) {
		win32_diagnostic("ReadFile");
		return 0;
	} else {
//		console_print_verbose("ReadFile(): %d bytes read\n", bytes_read);
		return (i64)bytes_read;
	}
}

void file_stream_write(void* source, size_t bytes_to_write, file_stream_t file_stream) {
	DWORD bytes_written;
	if (!WriteFile(file_stream, source, bytes_to_write, &bytes_written, NULL)) {
		win32_diagnostic("WriteFile");
	}
}

i64 file_stream_get_filesize(file_stream_t file_stream) {
	LARGE_INTEGER filesize = {};
	if (!GetFileSizeEx(file_stream, &filesize)) {
		win32_diagnostic("GetFileSizeEx");
	}
	return filesize.QuadPart;
}

i64 file_stream_get_pos(file_stream_t file_stream) {
	LARGE_INTEGER file_position = {};
	LARGE_INTEGER distance_to_move = {};
	if (!SetFilePointerEx(file_stream, distance_to_move, &file_position, FILE_CURRENT)) {
		win32_diagnostic("SetFilePointerEx");
	}
	return file_position.QuadPart;
}

bool file_stream_set_pos(file_stream_t file_stream, i64 offset) {
	LARGE_INTEGER new_file_pointer = {};
	new_file_pointer.QuadPart = offset;
	if (!SetFilePointerEx(file_stream, new_file_pointer, NULL, FILE_BEGIN)) {
		win32_diagnostic("SetFilePointerEx");
		return false;
	} else {
		return true;
	}
}

void file_stream_close(file_stream_t file_stream) {
	if (!CloseHandle(file_stream)) {
		win32_diagnostic("CloseHandle");
	}
}

u8* platform_alloc(size_t size) {
	u8* result = (u8*) VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!result) {
		console_print("Error: memory allocation failed!\n");
		panic();
	}
	return result;
}

void platform_sleep(u32 ms) {
	Sleep(ms);
}

void message_box(app_state_t* app_state, const char* message) {
	MessageBoxA(app_state->main_window, message, "Slidescape", MB_ICONERROR);
}

// Window related procecures

void set_window_title(window_handle_t window, const char* title) {
	size_t len = strlen(title) + 1;
	wchar_t* wide_title = win32_string_widen(title, len, (wchar_t*) alloca(2 * len));
	SetWindowTextW(window, wide_title);
}

void reset_window_title(window_handle_t window) {
	SetWindowTextA(window, "Slidescape");
}

POINT stored_mouse_pos;

void mouse_hide() {
	if (!cursor_hidden && !gui_want_capture_mouse) {
		GetCursorPos(&stored_mouse_pos);
		ShowCursor(0);
		cursor_hidden = true;
	}
}

void mouse_show() {
	if (cursor_hidden) {
		// TODO: mouse move asymptotically to the window corners?
//		SetCursorPos(stored_mouse_pos.x, stored_mouse_pos.y);
		ShowCursor(1);
		cursor_hidden = false;
	}
}

const char* get_default_save_directory() {
	return "";
};

