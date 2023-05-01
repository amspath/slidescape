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

#define WORK_QUEUE_IMPL
#include "work_queue.h"
#include "platform.h"
#include "intrinsics.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <semaphore.h>
#endif

work_queue_t work_queue_create(const char* semaphore_name, i32 entry_count) {
	work_queue_t queue = {0};

	i32 semaphore_initial_count = 0;
#if WINDOWS
	LONG maximum_count = 1e6; // realistically, we'd only get up to the number of worker threads, though
	queue.semaphore = CreateSemaphoreExA(0, semaphore_initial_count, maximum_count, semaphore_name, 0, SEMAPHORE_ALL_ACCESS);
#else
	queue.semaphore = sem_open(semaphore_name, O_CREAT, 0644, semaphore_initial_count);
#endif
	queue.entry_count = entry_count + 1; // add safety margin to detect when queue is about to overflow
	queue.entries = calloc(1, (entry_count + 1) * sizeof(work_queue_entry_t));
	return queue;
}

void work_queue_destroy(work_queue_t* queue) {
	if (queue->entries) {
		free(queue->entries);
		queue->entries = NULL;
	}
#if WINDOWS
	CloseHandle(queue->semaphore);
#else
	sem_close(queue->semaphore);
#endif
	queue->semaphore = NULL;
}

i32 work_queue_get_entry_count(work_queue_t* queue) {
	i32 count = queue->next_entry_to_submit - queue->next_entry_to_execute;
	while (count < 0) {
		count += queue->entry_count;
	}
	return count;
}

// TODO: add optional refcount increment
bool work_queue_submit(work_queue_t* queue, work_queue_callback_t callback, u32 task_identifier, void* userdata, size_t userdata_size) {
	if (!queue) {
		panic("work_queue_add_entry(): queue is NULL");
	}
	if (userdata_size > sizeof(((work_queue_entry_t*)0)->userdata)) {
		panic("work_queue_add_entry(): userdata_size overflows available space");
	}
	for (i32 tries = 0; tries < 1000; ++tries) {
		// Circular FIFO buffer
		i32 entry_to_submit = queue->next_entry_to_submit;
		i32 new_next_entry_to_submit = (queue->next_entry_to_submit + 1) % queue->entry_count;
		if (new_next_entry_to_submit == queue->next_entry_to_execute) {
			// TODO: fix multithreading problem: completion queue overflowing
			console_print_error("Warning: work queue is overflowing - job is cancelled\n");
			return false;
		}

		bool succeeded = atomic_compare_exchange(&queue->next_entry_to_submit,
		                                         new_next_entry_to_submit, entry_to_submit);
		if (succeeded) {
//		    console_print("exhange succeeded\n");
			work_queue_entry_t* entry = queue->entries + entry_to_submit;
			*entry = (work_queue_entry_t){ .callback = callback, .task_identifier = task_identifier };
			if (userdata_size > 0) {
				ASSERT(userdata);
				memcpy(entry->userdata, userdata, userdata_size);
			}
			write_barrier;
			entry->is_valid = true;
			write_barrier;
			atomic_increment(&queue->completion_goal);
			atomic_increment(&queue->start_goal);
//		    queue->next_entry_to_submit = new_next_entry_to_submit;
			semaphore_post(queue->semaphore);
			return true;
		} else {
			if (tries > 5) {
				console_print_error("exchange failed, retrying (try #%d)\n", tries);
			}
			continue;
		}

	}
	return false;
}

bool work_queue_submit_task(work_queue_t* queue, work_queue_callback_t callback, void* userdata, size_t userdata_size) {
	ASSERT(callback);
	return work_queue_submit(queue, callback, 0, userdata, userdata_size);
}

bool work_queue_submit_notification(work_queue_t* queue, u32 task_identifier, void* userdata, size_t userdata_size) {
	return work_queue_submit(queue, NULL, task_identifier, userdata, userdata_size);
}

work_queue_entry_t work_queue_get_next_entry(work_queue_t* queue) {
	work_queue_entry_t result = {0};

	i32 entry_to_execute = queue->next_entry_to_execute;
	i32 new_next_entry_to_execute = (entry_to_execute + 1) % queue->entry_count;

	// don't even try to execute a task if it is not yet submitted, or not yet fully submitted
	if ((entry_to_execute != queue->next_entry_to_submit) && (queue->entries[entry_to_execute].is_valid)) {
		bool succeeded = atomic_compare_exchange(&queue->next_entry_to_execute,
		                                         new_next_entry_to_execute, entry_to_execute);
		if (succeeded) {
			// We have dibs to execute this task!
			result = queue->entries[entry_to_execute];
			queue->entries[entry_to_execute].is_valid = false; // discourage competing threads (maybe not needed?)
			if (result.callback == NULL && result.task_identifier == 0) {
				console_print_error("Warning: encountered a work entry with a missing callback routine and/or task identifier (is this intended)?\n");
			}
			result.is_valid = true;
			read_barrier;
		}
	}

	return result;
}

void work_queue_mark_entry_completed(work_queue_t* queue) {
	atomic_increment(&queue->completion_count);
}

bool work_queue_do_work(work_queue_t* queue, int logical_thread_index) {
	work_queue_entry_t entry = work_queue_get_next_entry(queue);
	if (entry.is_valid) {
		atomic_decrement(&global_worker_thread_idle_count);
		atomic_increment(&queue->start_count);
		ASSERT(entry.callback);
		if (entry.callback) {
			// Simple way to keep track if we are executing a 'nested' task (i.e. executing a job while waiting to continue another job)
			++work_queue_call_depth;

			// Copy the user data (arguments for the call) onto the stack
			u8* userdata = alloca(sizeof(entry.userdata));
			memcpy(userdata, entry.userdata, sizeof(entry.userdata));

			// Ensure all the memory allocated on the thread's temp_arena will be released when the task completes
			temp_memory_t temp = begin_temp_memory_on_local_thread();

			// Execute the task
			entry.callback(logical_thread_index, userdata);

			release_temp_memory(&temp);
			--work_queue_call_depth;
		}
        work_queue_mark_entry_completed(queue);
		atomic_increment(&global_worker_thread_idle_count);
	}
	return entry.is_valid;
}


bool work_queue_is_work_in_progress(work_queue_t* queue) {
	// If we are checking the global work queue while running a task from that same queue, then we only want to know
	// whether any OTHER tasks are running. So in that case we need to subtract the call depth.
	i32 call_depth = (queue == &global_work_queue) ? work_queue_call_depth : 0;
	bool result = (queue->completion_goal - call_depth > queue->completion_count);
	return result;
}

bool work_queue_is_work_waiting_to_start(work_queue_t* queue) {
	bool result = (queue->start_goal > queue->start_count);
	return result;
}

void dummy_work_queue_callback(int logical_thread_index, void* userdata) {}

//#define TEST_THREAD_QUEUE
#ifdef TEST_THREAD_QUEUE
void echo_task_completed(int logical_thread_index, void* userdata) {
	console_print("thread %d completed: %s\n", logical_thread_index, (char*) userdata);
}

void echo_task(int logical_thread_index, void* userdata) {
	console_print("thread %d: %s\n", logical_thread_index, (char*) userdata);

	work_queue_submit_task(&global_completion_queue, echo_task_completed, userdata, strlen(userdata)+1);
}
#endif

void test_multithreading_work_queue() {
#ifdef TEST_THREAD_QUEUE
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"NULL entry", 11);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 0", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 1", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 2", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 3", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 4", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 5", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 6", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 7", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 8", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 9", 9);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 10", 10);
	work_queue_submit_task(&global_work_queue, echo_task, (void*)"string 11", 10);

//	while (is_queue_work_in_progress(&global_work_queue)) {
//		work_queue_do_work(&global_work_queue, 0);
//	}
	while (work_queue_is_work_in_progress(&global_work_queue) || work_queue_is_work_in_progress((&global_completion_queue))) {
		work_queue_do_work(&global_completion_queue, 0);
	}
#endif
}
