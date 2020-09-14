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

#define OPENSLIDE_API_IMPL
#include "openslide_api.h"

#ifdef _WIN32
#include <windows.h>
#include "win32_main.h" // for win32_diagnostic()

bool32 init_openslide() {
	i64 debug_start = get_clock();
	HINSTANCE dll_handle = LoadLibraryA("libopenslide-0.dll");
	if (!dll_handle) {
		SetDllDirectoryA("openslide");
		dll_handle = LoadLibraryA("libopenslide-0.dll");
		SetDllDirectoryA(NULL);
	}
	if (dll_handle) {

#define GET_PROC(proc) if (!(openslide.proc = (void*) GetProcAddress(dll_handle, #proc))) goto failed;
		GET_PROC(openslide_detect_vendor);
		GET_PROC(openslide_open);
		GET_PROC(openslide_get_level_count);
		GET_PROC(openslide_get_level0_dimensions);
		GET_PROC(openslide_get_level_dimensions);
		GET_PROC(openslide_get_level_downsample);
		GET_PROC(openslide_get_best_level_for_downsample);
		GET_PROC(openslide_read_region);
		GET_PROC(openslide_close);
		GET_PROC(openslide_get_error);
		GET_PROC(openslide_get_property_names);
		GET_PROC(openslide_get_property_value);
		GET_PROC(openslide_get_associated_image_names);
		GET_PROC(openslide_get_associated_image_dimensions);
		GET_PROC(openslide_read_associated_image);
		GET_PROC(openslide_get_version);
#undef GET_PROC

		printf("Initialized OpenSlide in %g seconds.\n", get_seconds_elapsed(debug_start, get_clock()));
		return true;

	} else failed: {
		//win32_diagnostic("LoadLibraryA");
		printf("OpenSlide not available: could not load libopenslide-0.dll\n");
		return false;
	}

}

#else
#error "Dynamic loading of OpenSlide is not yet supported on this platform"
#endif
