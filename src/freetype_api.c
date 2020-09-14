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
#include "platform.h"

#ifndef FT2BUILD_H_
#include <ft2build.h>
#include FT_FREETYPE_H          // <freetype/freetype.h>
#include FT_MODULE_H            // <freetype/ftmodapi.h>
#include FT_GLYPH_H             // <freetype/ftglyph.h>
#include FT_SYNTHESIS_H         // <freetype/ftsynth.h>
#endif

#define FREETYPE_API_IMPL
#include "freetype_api.h"

#ifdef _WIN32
#include <windows.h>
#include "win32_main.h" // for win32_diagnostic()

bool init_freetype() {
	i64 debug_start = get_clock();
	HINSTANCE dll_handle = LoadLibraryA("freetype.dll");
	if (!dll_handle) {
		SetDllDirectoryA("freetype");
		dll_handle = LoadLibraryA("freetype.dll");
		SetDllDirectoryA(NULL);
	}
	if (dll_handle) {

#define GET_PROC(proc) if (!(freetype.proc = (void*) GetProcAddress(dll_handle, #proc))) goto failed;
		GET_PROC(FT_New_Memory_Face);
		GET_PROC(FT_Select_Charmap);
		GET_PROC(FT_Request_Size);
		GET_PROC(FT_Get_Char_Index);
		GET_PROC(FT_Load_Glyph);
		GET_PROC(FT_GlyphSlot_Embolden);
		GET_PROC(FT_GlyphSlot_Oblique);
		GET_PROC(FT_Render_Glyph);
		GET_PROC(FT_Done_Face);
		GET_PROC(FT_New_Library);
		GET_PROC(FT_Add_Default_Modules);
		GET_PROC(FT_Done_Library);
#undef GET_PROC

		printf("Initialized FreeType in %g seconds.\n", get_seconds_elapsed(debug_start, get_clock()));
		return true;

	} else failed: {
		//win32_diagnostic("LoadLibraryA");
		printf("FreeType not available: could not load freetype.dll\n");
		return false;
	}

}

#else
#error "Dynamic loading of FreeType is not yet supported on this platform"
#endif
