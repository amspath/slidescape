/*
  BSD 2-Clause License

  Copyright (c) 2019-2023, Pieter Valkema

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


typedef struct isyntax_streamer_tile_completed_task_t {
	u8* pixel_memory;
	i32 scale;
	i32 tile_index;
	i32 tile_width;
	i32 tile_height;
	i32 resource_id;
	bool want_gpu_residency;
} isyntax_streamer_tile_completed_task_t;

typedef struct isyntax_streamer_t {
//	image_t* image;
//	scene_t* scene;
	isyntax_t* isyntax;
	isyntax_image_t* wsi;
	i32 resource_id; // unique identifier associated with a single isyntax_t, use this to be able to discard any callbacks that still might come back after calling isyntax_destroy()
	v2f origin_offset;
	v2f camera_center;
	bounds2f camera_bounds;
	bounds2f crop_bounds;
	bool is_cropped;
//	zoom_state_t zoom;
	i32 zoom_level;
	work_queue_t* tile_completion_queue;
	work_queue_callback_t* tile_completion_callback;
	u32 tile_completion_task_identifier;

} isyntax_streamer_t;


void isyntax_begin_first_load(isyntax_streamer_t* streamer);
void isyntax_begin_load_tile(isyntax_streamer_t* streamer, i32 scale, i32 tile_x, i32 tile_y);


// globals
#if defined(VIEWER_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern bool is_tile_stream_task_in_progress;
extern bool is_tile_streamer_frame_boundary_passed;

#undef INIT
#undef extern



#ifdef __cplusplus
}
#endif
