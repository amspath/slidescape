#pragma once

typedef void (work_queue_callback_t)(int logical_thread_index, void* userdata);

typedef struct work_queue_entry_t {
	void* data;
	work_queue_callback_t* callback;
	bool32 is_valid;
} work_queue_entry_t;

typedef struct work_queue_t work_queue_t;

typedef struct work_queue_t {
	HANDLE semaphore_handle;
	i32 volatile next_entry_to_submit;
	i32 volatile next_entry_to_execute;
	i32 volatile completion_count;
	i32 volatile completion_goal;
	work_queue_entry_t entries[256];
} work_queue_t;

typedef struct thread_info_t {
	i32 logical_thread_index;
	work_queue_t* queue;
} thread_info_t;

#define MAX_THREAD_COUNT 16

void add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata);
bool32 is_queue_work_in_progress(work_queue_t* queue);
bool32 do_worker_work(work_queue_t* queue, int logical_thread_index);
