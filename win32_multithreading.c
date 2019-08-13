#define USE_MINIMAL_SYSTEM_HEADER
#include "common.h"
#include "platform.h"
#include "win32_multithreading.h"
#include "intrinsics.h"

#include <stdio.h>

void add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata) {
	// Circular FIFO buffer
	i32 new_next_entry_to_submit = (queue->next_entry_to_submit + 1) % COUNT(queue->entries);
	ASSERT(new_next_entry_to_submit != queue->next_entry_to_execute);
	queue->entries[queue->next_entry_to_submit] = (work_queue_entry_t){ .data = userdata, .callback = callback };
	++queue->completion_goal;
	write_barrier;
	queue->next_entry_to_submit = new_next_entry_to_submit;
	ReleaseSemaphore(queue->semaphore_handle, 1, NULL);
}

work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue) {
	work_queue_entry_t result = {};

	i32 original_entry_to_execute = queue->next_entry_to_execute;
	i32 new_next_entry_to_execute = (original_entry_to_execute + 1) % COUNT(queue->entries);
	if (original_entry_to_execute != queue->next_entry_to_submit) {
		i32 entry_index = InterlockedCompareExchange((volatile long*) &queue->next_entry_to_execute,
		                                             new_next_entry_to_execute, original_entry_to_execute);
		if (entry_index == original_entry_to_execute) {
			result.data = queue->entries[entry_index].data;
			result.callback = queue->entries[entry_index].callback;
			result.is_valid = true;
			read_barrier;
		}
	}
	return result;
}

void win32_mark_queue_entry_completed(work_queue_t* queue) {
	InterlockedIncrement((volatile long*) &queue->completion_count);
}

bool32 do_worker_work(work_queue_t* queue, int logical_thread_index) {
	work_queue_entry_t entry = get_next_work_queue_entry(queue);
	if (entry.is_valid) {
		if (!entry.callback) panic();
		entry.callback(logical_thread_index, entry.data);
		win32_mark_queue_entry_completed(queue);
	}
	return entry.is_valid;
}


bool32 is_queue_work_in_progress(work_queue_t* queue) {
	bool32 result = (queue->completion_goal < queue->completion_count);
	return result;
}


void platform_wait_for_boolean_true(volatile bool32* value_ptr) {
	i32 value;
	for (;;) {
		value = InterlockedCompareExchange((volatile long*)value_ptr, 0, 0);
		if (value) return;
		Sleep(1);
	}
}



