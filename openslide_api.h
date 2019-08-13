#pragma once

typedef struct _openslide openslide_t;
typedef struct openslide_api {
	const char*  (APIENTRY *openslide_detect_vendor)(const char *filename);
	openslide_t* (APIENTRY *openslide_open)(const char *filename);
	int32_t      (APIENTRY *openslide_get_level_count)(openslide_t *osr);
	void         (APIENTRY *openslide_get_level0_dimensions)(openslide_t *osr, int64_t *w, int64_t *h);
	void         (APIENTRY *openslide_get_level_dimensions)(openslide_t *osr, int32_t level, int64_t *w, int64_t *h);
	double       (APIENTRY *openslide_get_level_downsample)(openslide_t *osr, int32_t level);
	int32_t      (APIENTRY *openslide_get_best_level_for_downsample)(openslide_t *osr, double downsample);
	void         (APIENTRY *openslide_read_region)(openslide_t *osr, uint32_t *dest, int64_t x, int64_t y, int32_t level, int64_t w, int64_t h);
	void         (APIENTRY *openslide_close)(openslide_t *osr);
	const char * (APIENTRY *openslide_get_error)(openslide_t *osr);
	const char * const *(APIENTRY *openslide_get_property_names)(openslide_t *osr);
	const char * (APIENTRY *openslide_get_property_value)(openslide_t *osr, const char *name);
	const char * const *(APIENTRY *openslide_get_associated_image_names)(openslide_t *osr);
	void         (APIENTRY *openslide_get_associated_image_dimensions)(openslide_t *osr, const char *name, int64_t *w, int64_t *h);
	void         (APIENTRY *openslide_read_associated_image)(openslide_t *osr, const char *name, uint32_t *dest);
	const char * (APIENTRY *openslide_get_version)(void);
} openslide_api;

#define OPENSLIDE_PROPERTY_NAME_COMMENT "openslide.comment"
#define OPENSLIDE_PROPERTY_NAME_VENDOR "openslide.vendor"
#define OPENSLIDE_PROPERTY_NAME_QUICKHASH1 "openslide.quickhash-1"
#define OPENSLIDE_PROPERTY_NAME_BACKGROUND_COLOR "openslide.background-color"
#define OPENSLIDE_PROPERTY_NAME_OBJECTIVE_POWER "openslide.objective-power"
#define OPENSLIDE_PROPERTY_NAME_MPP_X "openslide.mpp-x"
#define OPENSLIDE_PROPERTY_NAME_MPP_Y "openslide.mpp-y"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_X "openslide.bounds-x"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_Y "openslide.bounds-y"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_WIDTH "openslide.bounds-width"
#define OPENSLIDE_PROPERTY_NAME_BOUNDS_HEIGHT "openslide.bounds-height"

extern openslide_api openslide;
extern volatile bool32 is_openslide_available;
extern volatile bool32 is_openslide_loading_done;

bool32 init_openslide();
