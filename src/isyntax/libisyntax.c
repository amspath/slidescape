/*
  BSD 2-Clause License

  Copyright (c) 2019-2025, Pieter Valkema

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

// To compile without implementing stb_printf.h:
// #define LIBISYNTAX_NO_STB_SPRINTF_IMPLEMENTATION

// To compile without implementing stb_image.h.:
// #define LIBISYNTAX_NO_STB_IMAGE_IMPLEMENTATION

// To compile without implementing thread pool routines (in case you want to supply your own):
// #define LIBISYNTAX_NO_THREAD_POOL_IMPLEMENTATION

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef LIBISYNTAX_NO_STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_IMPLEMENTATION
#endif
#include "stb_sprintf.h"

#ifndef LIBISYNTAX_NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

#include "common.h"
#include "platform.h"
#include "intrinsics.h"

#include "libisyntax.h"
#include "isyntax.h"
#include "isyntax_reader.h"
#include <math.h>

#define CHECK_LIBISYNTAX_OK(_libisyntax_call) do { \
    isyntax_error_t result = _libisyntax_call;     \
    ASSERT(result == LIBISYNTAX_OK);               \
} while(0);


#ifndef LIBISYNTAX_NO_THREAD_POOL_IMPLEMENTATION

static platform_thread_info_t thread_infos[MAX_THREAD_COUNT];



// Routines for initializing the global thread pool

#if WINDOWS
#include "win32_utils.h"

_Noreturn DWORD WINAPI thread_proc(void* parameter) {
	platform_thread_info_t* thread_info = (platform_thread_info_t*) parameter;
	i64 init_start_time = get_clock();

	atomic_increment(&global_worker_thread_idle_count);

	init_thread_memory(thread_info->logical_thread_index, &global_system_info);
	thread_memory_t* thread_memory = local_thread_memory;

	for (i32 i = 0; i < MAX_ASYNC_IO_EVENTS; ++i) {
		thread_memory->async_io_events[i] = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (!thread_memory->async_io_events[i]) {
			win32_diagnostic("CreateEvent");
		}
	}
//	console_print("Thread %d reporting for duty (init took %.3f seconds)\n", thread_info->logical_thread_index, get_seconds_elapsed(init_start_time, get_clock()));

	for (;;) {
		if (thread_info->logical_thread_index > global_active_worker_thread_count) {
			// Worker is disabled, do nothing
			Sleep(100);
			continue;
		}
		if (!work_queue_is_work_in_progress(thread_info->queue)) {
			Sleep(1);
			WaitForSingleObjectEx(thread_info->queue->semaphore, 1, FALSE);
		}
        work_queue_do_work(thread_info->queue, thread_info->logical_thread_index);
	}
}

static void init_thread_pool() {
	init_thread_memory(0, &global_system_info);

    int total_thread_count = global_system_info.suggested_total_thread_count;
	global_worker_thread_count = total_thread_count - 1;
	global_active_worker_thread_count = global_worker_thread_count;

	global_work_queue = work_queue_create("/worksem", 1024); // Queue for newly submitted tasks
	global_completion_queue = work_queue_create("/completionsem", 1024); // Message queue for completed tasks

	// NOTE: the main thread is considered thread 0.
	for (i32 i = 1; i < total_thread_count; ++i) {
		platform_thread_info_t thread_info = { .logical_thread_index = i, .queue = &global_work_queue};
		thread_infos[i] = thread_info;

		DWORD thread_id;
		HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, thread_infos + i, 0, &thread_id);
		CloseHandle(thread_handle);

	}


}

#else

#include <pthread.h>

static void* worker_thread(void* parameter) {
    platform_thread_info_t* thread_info = (platform_thread_info_t*) parameter;

//	fprintf(stderr, "Hello from thread %d\n", thread_info->logical_thread_index);

    init_thread_memory(thread_info->logical_thread_index, &global_system_info);
	atomic_increment(&global_worker_thread_idle_count);

	for (;;) {
		if (thread_info->logical_thread_index > global_active_worker_thread_count) {
			// Worker is disabled, do nothing
			platform_sleep(100);
			continue;
		}
        if (!work_queue_is_work_waiting_to_start(thread_info->queue)) {
            //platform_sleep(1);
            sem_wait(thread_info->queue->semaphore);
            if (thread_info->logical_thread_index > global_active_worker_thread_count) {
                // Worker is disabled, do nothing
                platform_sleep(100);
                continue;
            }
        }
        work_queue_do_work(thread_info->queue, thread_info->logical_thread_index);
    }

    return 0;
}

static void init_thread_pool() {
	init_thread_memory(0, &global_system_info);
    global_worker_thread_count = global_system_info.suggested_total_thread_count - 1;
    global_active_worker_thread_count = global_worker_thread_count;

	global_work_queue = work_queue_create("/worksem", 1024); // Queue for newly submitted tasks
	global_completion_queue = work_queue_create("/completionsem", 1024); // Message queue for completed tasks

    pthread_t threads[MAX_THREAD_COUNT] = {};

    // NOTE: the main thread is considered thread 0.
    for (i32 i = 1; i < global_system_info.suggested_total_thread_count; ++i) {
        thread_infos[i] = (platform_thread_info_t){ .logical_thread_index = i, .queue = &global_work_queue};

        if (pthread_create(threads + i, NULL, &worker_thread, (void*)(&thread_infos[i])) != 0) {
            fprintf(stderr, "Error creating thread\n");
        }

    }

    test_multithreading_work_queue();


}

#endif

#endif //LIBISYNTAX_NO_THREAD_POOL_IMPLEMENTATION

// TODO(avirodov): int may be too small for some counters later on.
// TODO(avirodov): should make a flag to turn counters off, they may have overhead.
// TODO(avirodov): struct? move to isyntax.h/.c?
// TODO(avirodov): debug api?
#define DBGCTR_COUNT(_counter) atomic_increment(&_counter)
i32 volatile dbgctr_init_thread_pool_counter = 0;
i32 volatile dbgctr_init_global_mutexes_created = 0;

static benaphore_t* libisyntax_get_global_mutex() {
    static benaphore_t libisyntax_global_mutex;
    static i32 volatile init_status = 0; // 0 - not initialized, 1 - being initialized, 2 - done initializing.

    // Quick path for already initialized scenario.
    read_barrier;
    if (init_status == 2) {
        return &libisyntax_global_mutex;
    }

    // We need to establish a global mutex, and this is nontrivial as mutex primitives available don't allow static
    // initialization (more discussion in https://github.com/amspath/libisyntax/issues/16).
    if (atomic_compare_exchange(&init_status, 1, 0)) {
        // We get to do the initialization
        libisyntax_global_mutex = benaphore_create();
        DBGCTR_COUNT(dbgctr_init_global_mutexes_created);
        init_status = 2;
        write_barrier;
    } else {
        // Wait until the other thread finishes initialization. Since we don't have a mutex, spinlock is
        // the best we can do here. It should be a very short critical section.
        do { read_barrier; } while(init_status < 2);
    }

    return &libisyntax_global_mutex;
}

isyntax_error_t libisyntax_init() {
    // Lock-unlock to ensure that all parallel calls to libisyntax_init() wait for the actual initialization to complete.
    benaphore_lock(libisyntax_get_global_mutex());
    static bool libisyntax_global_init_complete = false;

    if (libisyntax_global_init_complete == false) {
#ifndef LIBISYNTAX_NO_THREAD_POOL_IMPLEMENTATION
        // Actual initialization.
        get_system_info(false);
        DBGCTR_COUNT(dbgctr_init_thread_pool_counter);
        init_thread_pool();
#endif
        libisyntax_global_init_complete = true;
    }
    benaphore_unlock(libisyntax_get_global_mutex());
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_open(const char* filename, enum libisyntax_open_flags_t flags, isyntax_t** out_isyntax) {
    // Note(avirodov): intentionally not changing api of isyntax_open. We can do that later if needed and reduce
    // the size/count of wrappers.
    isyntax_t* result = malloc(sizeof(isyntax_t));
    memset(result, 0, sizeof(*result));

    bool success = isyntax_open(result, filename, flags);
    if (success) {
        *out_isyntax = result;
        return LIBISYNTAX_OK;
    } else {
        free(result);
        return LIBISYNTAX_FATAL;
    }
}

void libisyntax_close(isyntax_t* isyntax) {
    isyntax_destroy(isyntax);
    free(isyntax);
}

int32_t libisyntax_get_tile_width(const isyntax_t* isyntax) {
    return isyntax->tile_width;
}

int32_t libisyntax_get_tile_height(const isyntax_t* isyntax) {
    return isyntax->tile_height;
}

const isyntax_image_t* libisyntax_get_wsi_image(const isyntax_t* isyntax) {
    return isyntax->images + isyntax->wsi_image_index;
}

const isyntax_image_t* libisyntax_get_label_image(const isyntax_t* isyntax) {
	return isyntax->images + isyntax->label_image_index;
}

const isyntax_image_t* libisyntax_get_macro_image(const isyntax_t* isyntax) {
	return isyntax->images + isyntax->label_image_index;
}

const char* libisyntax_get_barcode(const isyntax_t* isyntax) {
	return isyntax->barcode;
}

int32_t libisyntax_image_get_level_count(const isyntax_image_t* image) {
    return image->level_count;
}

const isyntax_level_t* libisyntax_image_get_level(const isyntax_image_t* image, int32_t index) {
    return &image->levels[index];
}

int32_t libisyntax_level_get_scale(const isyntax_level_t* level) {
    return level->scale;
}

int32_t libisyntax_level_get_width_in_tiles(const isyntax_level_t* level) {
    return level->width_in_tiles;
}

int32_t libisyntax_level_get_height_in_tiles(const isyntax_level_t* level) {
    return level->height_in_tiles;
}

int32_t libisyntax_level_get_width(const isyntax_level_t* level) {
	return level->width;
}

int32_t libisyntax_level_get_height(const isyntax_level_t* level) {
	return level->height;
}

float libisyntax_level_get_mpp_x(const isyntax_level_t* level) {
	return level->um_per_pixel_x;
}

float libisyntax_level_get_mpp_y(const isyntax_level_t* level) {
	return level->um_per_pixel_y;
}

isyntax_error_t libisyntax_cache_create(const char* debug_name_or_null, int32_t cache_size,
                                        isyntax_cache_t** out_isyntax_cache)
{
    isyntax_cache_t* cache_ptr = malloc(sizeof(isyntax_cache_t));
    memset(cache_ptr, 0, sizeof(*cache_ptr));
    tile_list_init(&cache_ptr->cache_list, debug_name_or_null);
    cache_ptr->target_cache_size = cache_size;
    cache_ptr->mutex = benaphore_create();

    // Note: rest of initialization is deferred to the first injection, as that is where we will know the block size.

    *out_isyntax_cache = cache_ptr;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_cache_inject(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax) {
    // TODO(avirodov): consider refactoring implementation to another file, here and in destroy.
    if (isyntax->ll_coeff_block_allocator != NULL || isyntax->h_coeff_block_allocator != NULL) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }

    if (!isyntax_cache->h_coeff_block_allocator->is_valid || !isyntax_cache->ll_coeff_block_allocator->is_valid) {
        // Shouldn't ever partially initialize.
        ASSERT(!isyntax_cache->h_coeff_block_allocator->is_valid);
        ASSERT(!isyntax_cache->ll_coeff_block_allocator->is_valid);

        isyntax_cache->allocator_block_width = isyntax->block_width;
        isyntax_cache->allocator_block_height = isyntax->block_height;
        size_t ll_coeff_block_size = isyntax->block_width * isyntax->block_height * sizeof(icoeff_t);
        size_t block_allocator_maximum_capacity_in_blocks = GIGABYTES(32) / ll_coeff_block_size;
        size_t ll_coeff_block_allocator_capacity_in_blocks = block_allocator_maximum_capacity_in_blocks / 4;
        size_t h_coeff_block_size = ll_coeff_block_size * 3;
        size_t h_coeff_block_allocator_capacity_in_blocks = ll_coeff_block_allocator_capacity_in_blocks * 3;
        isyntax_cache->ll_coeff_block_allocator = malloc(sizeof(block_allocator_t));
        isyntax_cache->h_coeff_block_allocator = malloc(sizeof(block_allocator_t));
        *isyntax_cache->ll_coeff_block_allocator = block_allocator_create(ll_coeff_block_size, ll_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
        *isyntax_cache->h_coeff_block_allocator = block_allocator_create(h_coeff_block_size, h_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
        isyntax_cache->is_block_allocator_owned = true;
    }

    if (isyntax_cache->allocator_block_width != isyntax->block_width ||
            isyntax_cache->allocator_block_height != isyntax->block_height) {
        return LIBISYNTAX_FATAL; // Not implemented, see todo in libisyntax.h.
    }

    isyntax->ll_coeff_block_allocator = isyntax_cache->ll_coeff_block_allocator;
    isyntax->h_coeff_block_allocator = isyntax_cache->h_coeff_block_allocator;
    isyntax->is_block_allocator_owned = false;
    return LIBISYNTAX_OK;
}

void libisyntax_cache_destroy(isyntax_cache_t* isyntax_cache) {
    if (isyntax_cache->is_block_allocator_owned) {
        if (isyntax_cache->ll_coeff_block_allocator->is_valid) {
            block_allocator_destroy(isyntax_cache->ll_coeff_block_allocator);
        }
        if (isyntax_cache->h_coeff_block_allocator->is_valid) {
            block_allocator_destroy(isyntax_cache->h_coeff_block_allocator);
        }
    }

    benaphore_destroy(&isyntax_cache->mutex);
    free(isyntax_cache);
}

// TODO(pvalkema): should we allow passing a stride for the pixels_buffer, to allow blitting into buffers
//  that are not exactly the height/width of the region?
isyntax_error_t libisyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache,
                                     int32_t level, int64_t tile_x, int64_t tile_y,
                                     uint32_t* pixels_buffer, int32_t pixel_format) {
    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }
    // TODO(avirodov): additional vaidations, e.g. tile_x >= 0 && tile_x < isyntax...[level]...->width_in_tiles.

    // TODO(avirodov): if isyntax_cache is null, we can support using allocators that are in isyntax object,
    //  if is_init_allocators = 1 when created. Not sure is needed.
    isyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, pixels_buffer, pixel_format);
    return LIBISYNTAX_OK;
}

#define PER_LEVEL_PADDING 3

isyntax_error_t libisyntax_read_region(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache, int32_t level,
                                       int64_t x, int64_t y, int64_t width, int64_t height, uint32_t* pixels_buffer,
                                       int32_t pixel_format) {

    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }

    // Get the level
    ASSERT(level < isyntax->images[0].level_count);
    isyntax_level_t* current_level = &isyntax->images[0].levels[level];

    // TODO(pvalkema): check if this still needs adjustment
    int32_t num_levels = isyntax->images[0].level_count;
    int32_t offset = ((PER_LEVEL_PADDING << num_levels) - PER_LEVEL_PADDING) >> level;

    x += offset;
    y += offset;

    int32_t tile_width = isyntax->tile_width;
    int32_t tile_height = isyntax->tile_height;

    int64_t start_tile_x;
    int64_t end_tile_x;
    int64_t x_remainder;
    int64_t x_remainder_last;

	// Round down to the next lower multiple of tile_width, even when x < 0
	start_tile_x = (x >= 0) ? x / tile_width : (x - tile_width + 1) / tile_width;
	end_tile_x = ((x + width - 1) >= 0) ? (x + width - 1) / tile_width : ((x + width - 1) - tile_width + 1) / tile_width;

	// Normalize the remainder into [0, tile_width - 1], even for negative x.
	x_remainder = ((x % tile_width) + tile_width) % tile_width;
	x_remainder_last = (((x + width - 1) % tile_width) + tile_width) % tile_width;

    int64_t start_tile_y;
    int64_t end_tile_y;
    int64_t y_remainder;
    int64_t y_remainder_last;

	// Round down to the next lower multiple of tile_height, even when y < 0
	start_tile_y = (y >= 0) ? y / tile_height : (y - tile_height + 1) / tile_height;
	end_tile_y = ((y + height - 1) >= 0) ? (y + height - 1) / tile_height : ((y + height - 1) - tile_height + 1) / tile_height;

	// Normalize the remainder into [0, tile_height - 1], even for negative y.
	y_remainder = ((y % tile_height) + tile_height) % tile_height;
	y_remainder_last = (((y + height - 1) % tile_height) + tile_height) % tile_height;

    // Allocate memory for tile pixels (will reuse for consecutive libisyntax_tile_read() calls)
    uint32_t* tile_pixels = (uint32_t*)malloc(tile_width * tile_height * sizeof(uint32_t));

    // Read tiles and copy the relevant portion of each tile to the region
    for (int64_t tile_y = start_tile_y; tile_y <= end_tile_y; ++tile_y) {
        for (int64_t tile_x = start_tile_x; tile_x <= end_tile_x; ++tile_x) {
            // Calculate the portion of the tile to be copied
            int64_t src_x = (tile_x == start_tile_x) ? x_remainder : 0;
            int64_t src_y = (tile_y == start_tile_y) ? y_remainder : 0;
            int64_t dest_x = (tile_x == start_tile_x) ? 0 : (tile_x - start_tile_x) * tile_width - x_remainder;
            int64_t dest_y = (tile_y == start_tile_y) ? 0 : (tile_y - start_tile_y) * tile_height - y_remainder;
            int64_t copy_width = (tile_x == end_tile_x) ? x_remainder_last - src_x + 1 : tile_width - src_x;
            int64_t copy_height = (tile_y == end_tile_y) ? y_remainder_last - src_y + 1 : tile_height - src_y;

            // Read tile
            CHECK_LIBISYNTAX_OK(libisyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, tile_pixels, pixel_format));

            // Copy the relevant portion of the tile to the region
            for (int64_t i = 0; i < copy_height; ++i) {
                int64_t dest_index = (dest_y + i) * width + dest_x;
                int64_t src_index = (src_y + i) * tile_width + src_x;
                memcpy((pixels_buffer) + dest_index,
                       tile_pixels + src_index,
                       copy_width * sizeof(uint32_t));
            }
        }
    }

    free(tile_pixels);

    return LIBISYNTAX_OK;
}

// TODO(pvalkema): remove this / only support returning compressed JPEG buffer and leave decompression to caller?
static isyntax_error_t libisyntax_read_associated_image(isyntax_t* isyntax, isyntax_image_t* image, int32_t* width, int32_t* height,
                                                        uint32_t** pixels_buffer, int32_t pixel_format) {
    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }
    uint32_t* pixels = (uint32_t*)isyntax_get_associated_image_pixels(isyntax, image, pixel_format);
    // NOTE: the width and height are only known AFTER the decoding.
    if (width) *width = image->width;
    if (height) *height = image->height;
    if (pixels_buffer) *pixels_buffer = pixels;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_read_label_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                            uint32_t** pixels_buffer, int32_t pixel_format) {
    isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
    return libisyntax_read_associated_image(isyntax, label_image, width, height, pixels_buffer, pixel_format);
}

isyntax_error_t libisyntax_read_macro_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                            uint32_t** pixels_buffer, int32_t pixel_format) {
    isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
    return libisyntax_read_associated_image(isyntax, macro_image, width, height, pixels_buffer, pixel_format);
}

static isyntax_error_t libisyntax_read_assocatiated_image_jpeg(isyntax_t* isyntax, isyntax_image_t* image, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    ASSERT(jpeg_buffer);
    ASSERT(jpeg_size);
    u8* jpeg_compressed = isyntax_get_associated_image_jpeg(isyntax, image, jpeg_size);
    if (jpeg_compressed) {
        *jpeg_buffer = jpeg_compressed;
        return LIBISYNTAX_OK;
    } else {
        return LIBISYNTAX_FATAL;
    }
}

isyntax_error_t libisyntax_read_label_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
    return libisyntax_read_assocatiated_image_jpeg(isyntax, label_image, jpeg_buffer, jpeg_size);
}

isyntax_error_t libisyntax_read_macro_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
    return libisyntax_read_assocatiated_image_jpeg(isyntax, macro_image, jpeg_buffer, jpeg_size);
}

isyntax_error_t libisyntax_read_icc_profile(isyntax_t* isyntax, isyntax_image_t* image, uint8_t** icc_profile_buffer, uint32_t* icc_profile_size) {
    ASSERT(icc_profile_buffer);
    ASSERT(icc_profile_size);
    u8* icc_profile_compressed = isyntax_get_icc_profile(isyntax, image, icc_profile_size);
    if (icc_profile_compressed) {
        *icc_profile_buffer = icc_profile_compressed;
        return LIBISYNTAX_OK;
    } else {
        return LIBISYNTAX_FATAL;
    }
}
