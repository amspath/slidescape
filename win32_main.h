#ifndef WIN32_MAIN
#define WIN32_MAIN

// todo: fix this (make opaque structure?)
#include "windows.h"

#define BYTES_PER_PIXEL 4

typedef struct {
	union {
		struct {
			BITMAPINFO bitmapinfo;
		} win32;
	};
	void* memory;
	size_t memory_size;
	int width;
	int height;
	int pitch;
} surface_t;

typedef struct {
	int width;
	int height;
} win32_window_dimension_t;


typedef struct work_queue_t {
	HANDLE semaphore_handle;
	i32 volatile next_entry_to_submit;
	i32 volatile next_entry_to_execute;
	i32 volatile completion_count;
	i32 volatile completion_goal;
	work_queue_entry_t entries[256];
} work_queue_t;


typedef struct win32_thread_info_t {
	i32 logical_thread_index;
	work_queue_t* queue;
} win32_thread_info_t;

#define MAX_THREAD_COUNT 128



#endif
