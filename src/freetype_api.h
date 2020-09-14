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

#ifndef FT2BUILD_H_
#include <ft2build.h>
#include FT_FREETYPE_H          // <freetype/freetype.h>
#include FT_MODULE_H            // <freetype/ftmodapi.h>
#include FT_GLYPH_H             // <freetype/ftglyph.h>
#include FT_SYNTHESIS_H         // <freetype/ftsynth.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


typedef struct _freetype freetype_t;
typedef struct freetype_api {
	FT_Error  (__stdcall *FT_New_Memory_Face)(FT_Library library, const FT_Byte* file_base, FT_Long file_size, FT_Long face_index, FT_Face        *aface );
	FT_Error  (__stdcall *FT_Select_Charmap)(FT_Face face, FT_Encoding encoding);
	FT_Error  (__stdcall *FT_Request_Size)(FT_Face face, FT_Size_Request req);
	FT_UInt   (__stdcall *FT_Get_Char_Index)(FT_Face face, FT_ULong charcode);
	FT_Error  (__stdcall *FT_Load_Glyph)(FT_Face face, FT_UInt glyph_index, FT_Int32 load_flags);
	void      (__stdcall *FT_GlyphSlot_Embolden)(FT_GlyphSlot slot);
	void      (__stdcall *FT_GlyphSlot_Oblique)(FT_GlyphSlot slot);
	FT_Error  (__stdcall *FT_Render_Glyph)(FT_GlyphSlot slot, FT_Render_Mode render_mode);
	FT_Error  (__stdcall *FT_Done_Face)(FT_Face face);
	FT_Error  (__stdcall *FT_New_Library)(FT_Memory memory, FT_Library *alibrary);
	void      (__stdcall *FT_Add_Default_Modules)(FT_Library library);
	FT_Error  (__stdcall *FT_Done_Library)(FT_Library library);
} freetype_api;


// prototypes
bool init_freetype();

// globals
#if defined(FREETYPE_API_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern freetype_api freetype;
extern bool is_freetype_available;
extern bool is_freetype_loading_done;

#undef INIT
#undef extern


#ifdef __cplusplus
}
#endif

