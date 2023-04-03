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
#ifndef VIEWER_H
#define VIEWER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "platform.h"
#include "graphical_app.h"
#include "mathutils.h"
#include "arena.h"
#include "image.h"
#include "tiff.h"
#include "isyntax.h"
#include "dicom.h"
#include "openslide_api.h"
#include "caselist.h"
#include "annotation.h"


typedef enum viewer_file_type_enum {
	VIEWER_FILE_TYPE_UNKNOWN = 0,
	VIEWER_FILE_TYPE_SIMPLE_IMAGE,
	VIEWER_FILE_TYPE_TIFF,
	VIEWER_FILE_TYPE_NDPI,
	VIEWER_FILE_TYPE_DICOM,
	VIEWER_FILE_TYPE_ISYNTAX,
	VIEWER_FILE_TYPE_OPENSLIDE_COMPATIBLE,
	VIEWER_FILE_TYPE_XML,
	VIEWER_FILE_TYPE_JSON,
} viewer_file_type_enum;

typedef struct file_info_t {
	char filename[512];
    char dirname_with_trailing_slash[512];
    char basename[512];
	char ext[16];
	i64 filesize;
	viewer_file_type_enum type;
	bool is_valid;
	bool is_directory;
	bool is_regular_file;
	bool is_image;
	u8 header[256];
} file_info_t;

typedef struct directory_info_t {
	file_info_t* dicom_files; // array
	bool contains_dicom_files;
	bool contains_nondicom_images;
	bool is_valid;
} directory_info_t;

#define BYTES_PER_PIXEL 4

typedef enum filetype_hint_enum {
	FILETYPE_HINT_NONE = 0,
	FILETYPE_HINT_CASELIST,
	FILETYPE_HINT_ANNOTATIONS,
	FILETYPE_HINT_BASE_IMAGE,
	FILETYPE_HINT_OVERLAY,
} filetype_hint_enum;

typedef enum task_type_enum {
	TASK_NONE = 0,
	TASK_LOAD_TILE = 0,
} task_type_enum;

typedef enum load_tile_error_code_enum {
	LOAD_TILE_SUCCESS,
	LOAD_TILE_EMPTY,
	LOAD_TILE_READ_LOCAL_FAILED,
	LOAD_TILE_READ_REMOTE_FAILED,
} load_tile_error_code_enum;

typedef struct load_tile_task_t {
	i32 resource_id;
	image_t* image;
	tile_t* tile;
	i32 level;
	i32 tile_x;
	i32 tile_y;
	i32 priority;
	bool8 need_gpu_residency;
	bool8 need_keep_in_cache;
	work_queue_callback_t* completion_callback;
	work_queue_t* completion_queue;
    i32 refcount_to_decrement;
} load_tile_task_t;

typedef struct viewer_notify_tile_completed_task_t {
	u8* pixel_memory;
	i32 scale;
	i32 tile_index;
	i32 tile_width;
	i32 tile_height;
	i32 resource_id;
	bool want_gpu_residency;
} viewer_notify_tile_completed_task_t;


#define TILE_LOAD_BATCH_MAX 8

typedef struct load_tile_task_batch_t {
	i32 task_count;
	load_tile_task_t tile_tasks[TILE_LOAD_BATCH_MAX];
} load_tile_task_batch_t;

typedef struct scale_bar_t {
	char text[64];
	float max_width;
	float width;
	float height;
	v2f pos;
	v2f pos_max; // lower right corner
	v2f pos_center;
	v2f drag_start_offset;
	corner_enum corner;
	v2f pos_relative_to_corner;
	float text_x;
	bool enabled;
	bool initialized;
} scale_bar_t;


typedef enum entity_type_enum {
	ENTITY_SIMPLE_IMAGE = 1,
	ENTITY_TILED_IMAGE = 2,
} entity_type_enum;

typedef enum mouse_mode_enum {
	MODE_VIEW,
	MODE_INSERT,
	MODE_CREATE_SELECTION_BOX,
	MODE_DRAG_ANNOTATION_NODE,
	MODE_DRAG_SCALE_BAR,
} mouse_mode_enum;

typedef enum placement_tool_enum {
	TOOL_NONE,
	TOOL_CREATE_OUTLINE,
	TOOL_EDIT_EXISTING_COORDINATES,
	TOOL_CREATE_POINT,
	TOOL_CREATE_LINE,
	TOOL_CREATE_ARROW,
	TOOL_CREATE_FREEFORM,
	TOOL_CREATE_ELLIPSE,
	TOOL_CREATE_RECTANGLE,
	TOOL_CREATE_TEXT,
} placement_tool_enum;

typedef struct entity_t {
	u32 type;
	v2f pos;
	union {
		struct {
			image_t* image;
		} simple_image;
		struct {
			image_t* image;
		} tiled_image;
	};
} entity_t;

#define MAX_ENTITIES 1000


typedef struct zoom_state_t {
	float pos;
	i32 level;
	i32 notches;
	float notch_size;
	float pixel_width;
	float pixel_height;
	float screen_point_width;
	float downsample_factor;
	float base_pixel_width;
	float base_pixel_height;
} zoom_state_t;

typedef struct scene_t {
	rect2f viewport;
	v2f camera;
	v2f mouse;
	v2f right_clicked_pos;
    v2f left_clicked_pos;
	bounds2f camera_bounds;
	bounds2f tile_load_bounds;
	bool restrict_load_bounds;
	float r_minus_l;
	float t_minus_b;
	zoom_state_t zoom;
	bool8 need_zoom_reset;
	bool8 need_zoom_animation;
	v2f control;
	float time_since_control_start;
	v2f panning_velocity;
	v2f zoom_pivot;
	zoom_state_t zoom_target_state;
	v2f level_pixel_size;
	v4f clear_color;
	u32 entity_count;
	entity_t entities[MAX_ENTITIES];
	i32 active_layer;
	annotation_set_t annotation_set;
    annotation_set_template_t annotation_set_template;
	bool8 clicked;
	bool8 right_clicked;
	bool8 drag_started;
	bool8 drag_ended;
	bool8 is_dragging; // if mouse down: is this scene being dragged?
	bool8 suppress_next_click; // if mouse down: prevent release being counted as click
	bool8 viewport_changed;
	rect2f selection_box;
	bool8 has_selection_box;
	v2f drag_vector;
	v2f cumulative_drag_vector;
	bounds2f crop_bounds;
	bounds2i selection_pixel_bounds;
	bool8 can_export_region;
	bool8 is_cropped;
	v3f transparent_color;
	float transparent_tolerance;
	bool use_transparent_filter;
	scale_bar_t scale_bar;
	bool is_mpp_known;
	bool enable_grid;
	bool enable_annotations;
	bool8 initialized;
} scene_t;

typedef struct pixel_transfer_state_t {
	u32 pbo;
	u32 texture;
	i32 texture_width;
	i32 texture_height;
	bool8 need_finalization;
	void* userdata;
	bool8 initialized;
} pixel_transfer_state_t;


typedef struct tile_streamer_t {
	image_t* image;
	scene_t* scene;
	v2f origin_offset;
	v2f camera_center;
	bounds2f camera_bounds;
	bounds2f crop_bounds;
	bool is_cropped;
	zoom_state_t zoom;
} tile_streamer_t;


typedef enum command_enum {
	COMMAND_NONE,
	COMMAND_PRINT_VERSION,
	COMMAND_EXPORT,
} command_enum;

typedef enum command_export_error_enum {
	COMMAND_EXPORT_ERROR_NONE,
	COMMAND_EXPORT_ERROR_NO_ROI,
} command_export_error_enum;

typedef struct app_command_t app_command_t;
struct app_command_t {
	bool headless;
	bool exit_immediately;
	command_enum command;
	struct app_command_export_t {
		const char* roi;
		bool with_annotations;
		command_export_error_enum error;
	} export_command;
	const char** inputs; // array
};

typedef struct app_state_t {
	app_command_t command;
	u8* temp_storage_memory; // TODO: remove, use thread local temp storage instead
	arena_t temp_arena; // TODO: remove
	rect2i client_viewport;
	float display_scale_factor;
	float display_points_per_pixel;
	scene_t scene;
	v4f clear_color;
	float black_level;
	float white_level;
	image_t** loaded_images; // array
	i32 displayed_image;
	bool is_any_image_loaded;
	caselist_t caselist;
	case_t* selected_case;
	i32 selected_case_index;
	bool use_builtin_tiff_backend;
	bool use_image_adjustments;
	bool initialized;
	bool allow_idling_next_frame;
	mouse_mode_enum mouse_mode;
	placement_tool_enum mouse_tool;
	i64 last_frame_start;
	i64 frame_counter;
	float seconds_without_mouse_movement;
	i32 mouse_sensitivity;
	i32 keyboard_base_panning_speed;
	pixel_transfer_state_t pixel_transfer_states[32];
	u32 next_pixel_transfer_to_submit;
	window_handle_t main_window;
	bool is_window_title_set_for_image;
	input_t* input;
	i32* active_resources; // array
	bool is_export_in_progress;
	bool export_as_coco;
	bool enable_autosave;
    bool remember_annotation_groups_as_template;
	bool headless;
    char last_active_directory[512];
    char annotation_directory[512];
    bool is_annotation_directory_set;
} app_state_t;


//  prototypes
void add_image(app_state_t* app_state, image_t* image, bool need_zoom_reset, bool need_image_registration);
void unload_all_images(app_state_t* app_state);
bool load_generic_file(app_state_t* app_state, const char* filename, u32 filetype_hint);
image_t* load_image_from_file(app_state_t* app_state, file_info_t* file, directory_info_t* directory, u32 filetype_hint);
void load_tile_func(i32 logical_thread_index, void* userdata);
void load_openslide_wsi(wsi_t* wsi, const char* filename);
void unload_openslide_wsi(wsi_t* wsi);
bool was_button_pressed(button_state_t* button);
bool was_button_released(button_state_t* button);
bool was_key_pressed(input_t* input, i32 keycode);
bool is_key_down(input_t* input, i32 keycode);
void init_app_state(app_state_t* app_state, app_command_t command);
void autosave(app_state_t* app_state, bool force_ignore_delay);
void request_tiles(image_t* image, load_tile_task_t* wishlist, i32 tiles_to_load);
void scene_update_camera_pos(scene_t* scene, v2f pos);
void viewer_switch_tool(app_state_t* app_state, placement_tool_enum tool);
void viewer_update_and_render(app_state_t* app_state, input_t* input, i32 client_width, i32 client_height, float delta_time);
void do_after_scene_render(app_state_t* app_state, input_t* input);

// viewer_opengl.cpp
u32 load_texture(void* pixels, i32 width, i32 height, u32 pixel_format);
void unload_texture(u32 texture);
void init_opengl_stuff(app_state_t* app_state);
void upload_tile_on_worker_thread(image_t* image, void* tile_pixels, i32 scale, i32 tile_index, i32 tile_width, i32 tile_height);

// viewer_io_file.cpp
const char* get_active_directory(app_state_t* app_state);
const char* get_annotation_directory(app_state_t* app_state);
void set_annotation_directory(app_state_t* app_state, const char* path);
void viewer_upload_already_cached_tile_to_gpu(int logical_thread_index, void* userdata);
void viewer_notify_load_tile_completed(int logical_thread_index, void* userdata);
file_info_t viewer_get_file_info(const char* filename);

// viewer_io_remote.cpp
void tiff_load_tile_batch_func(i32 logical_thread_index, void* userdata);

// viewer_options.cpp
void viewer_init_options(app_state_t* app_state);

// viewer_commandline.cpp
app_command_t app_parse_commandline(int argc, const char** argv);
void app_command_execute_immediately(app_command_t* app_command);
int app_command_execute(app_state_t* app_state);

// isyntax_streamer.cpp
void isyntax_stream_image_tiles(tile_streamer_t* tile_streamer, isyntax_t* isyntax);
void isyntax_begin_stream_image_tiles(tile_streamer_t* tile_streamer);

// scene.cpp
void zoom_update_pos(zoom_state_t* zoom, float pos);
void init_zoom_state(zoom_state_t* zoom, float zoom_position, float notch_size, float base_pixel_width, float base_pixel_height);
void init_scene(app_state_t *app_state, scene_t *scene);
v2f scene_mouse_pos(scene_t* scene);
void update_scale_bar(scene_t* scene, scale_bar_t* scale_bar);
void draw_scale_bar(scale_bar_t* scale_bar);
void draw_grid(scene_t* scene);
void draw_selection_box(scene_t* scene);

// globals
#if defined(VIEWER_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern app_state_t global_app_state;

extern i64 zoom_in_key_hold_down_start_time;
extern i64 zoom_in_key_times_zoomed_while_holding;
extern i64 zoom_out_key_hold_down_start_time;
extern i64 zoom_out_key_times_zoomed_while_holding;
extern bool prefer_integer_zoom INIT(= false);
extern bool use_fast_rendering INIT(= false); // optimize for performance for e.g. remote desktop
extern i32 global_lowest_scale_to_render INIT(= 0);
extern i32 global_highest_scale_to_render INIT(= 16);

extern bool window_start_maximized INIT(=true);
extern i32 desired_window_width INIT(=1280);
extern i32 desired_window_height INIT(=720);
extern bool draw_macro_image_in_background;
extern bool draw_label_image_in_background; // TODO: implement

extern bool is_tile_stream_task_in_progress;
extern bool is_tile_streamer_frame_boundary_passed;

extern i32 global_next_resource_id INIT(= 1000);

extern float global_tiff_export_progress; // TODO: change to task-local variable?

extern bool is_dicom_available;
extern bool is_dicom_loading_done;
extern bool is_openslide_available;
extern bool is_openslide_loading_done;

#undef INIT
#undef extern


#ifdef __cplusplus
}
#endif

#endif //VIEWER_H
