#define USE_MINIMAL_SYSTEM_HEADER
#include "common.h"
#include "platform.h"

#include "openslide_api.h"

openslide_api openslide;
volatile bool32 is_openslide_available;
volatile bool32 is_openslide_loading_done;

#ifdef _WIN32
#include <stdio.h>

bool32 init_openslide() {
	i64 debug_start = get_clock();
	SetDllDirectoryA("openslide");
	HINSTANCE dll_handle = LoadLibraryA("libopenslide-0.dll");
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
		//TODO: diagnostic
		printf("Could not load libopenslide-0.dll\n");
		return false;
	}

}

#else
#error "Dynamic loading of OpenSlide is not yet supported on this platform"
#endif
