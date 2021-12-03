/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2021  Pieter Valkema

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

#if !WINDOWS
#include <semaphore.h>
#endif

i32 get_work_queue_task_count(work_queue_t* queue) {
	i32 count = queue->next_entry_to_submit - queue->next_entry_to_execute;
	while (count < 0) {
		count += COUNT(queue->entries);
	}
	return count;
}

bool add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata, size_t userdata_size) {
	if (userdata_size > sizeof(((work_queue_entry_t*)0)->userdata)) {
		ASSERT(!"userdata_size overflows available space");
		panic();
	}
	for (i32 tries = 0; tries < 1000; ++tries) {
		// Circular FIFO buffer
		i32 entry_to_submit = queue->next_entry_to_submit;
		i32 new_next_entry_to_submit = (queue->next_entry_to_submit + 1) % COUNT(queue->entries);
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
			*entry = (work_queue_entry_t){ .callback = callback };
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

work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue) {
	work_queue_entry_t result = {0};

	i32 entry_to_execute = queue->next_entry_to_execute;
	i32 new_next_entry_to_execute = (entry_to_execute + 1) % COUNT(queue->entries);

	// don't even try to execute a task if it is not yet submitted, or not yet fully submitted
	if ((entry_to_execute != queue->next_entry_to_submit) && (queue->entries[entry_to_execute].is_valid)) {
		bool succeeded = atomic_compare_exchange(&queue->next_entry_to_execute,
		                                         new_next_entry_to_execute, entry_to_execute);
		if (succeeded) {
			// We have dibs to execute this task!
			result = queue->entries[entry_to_execute];
			queue->entries[entry_to_execute].is_valid = false; // discourage competing threads (maybe not needed?)
			if (!result.callback) {
				console_print_error("Error: encountered a work entry with a missing callback routine\n");
				ASSERT(!"invalid code path");
			}
			result.is_valid = true;
			read_barrier;
		}
	}

	return result;
}

void mark_queue_entry_completed(work_queue_t* queue) {
	atomic_increment(&queue->completion_count);
}

bool do_worker_work(work_queue_t* queue, int logical_thread_index) {
	work_queue_entry_t entry = get_next_work_queue_entry(queue);
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

			// Execute the task
			entry.callback(logical_thread_index, userdata);
			--work_queue_call_depth;
		}
		mark_queue_entry_completed(queue);
		atomic_increment(&global_worker_thread_idle_count);
	}
	return entry.is_valid;
}


bool is_queue_work_in_progress(work_queue_t* queue) {
	bool result = (queue->completion_goal - work_queue_call_depth > queue->completion_count);
	return result;
}

bool is_queue_work_waiting_to_start(work_queue_t* queue) {
	bool result = (queue->start_goal > queue->start_count);
	return result;
}

// NOTE: only use this on the main thread!
void drain_work_queue(work_queue_t* queue) {
	while (is_queue_work_in_progress(&global_work_queue)) {
		do_worker_work(&global_work_queue, 0);
	}
}

//#define TEST_THREAD_QUEUE
#ifdef TEST_THREAD_QUEUE
void echo_task_completed(int logical_thread_index, void* userdata) {
	console_print("thread %d completed: %s\n", logical_thread_index, (char*) userdata);
}

void echo_task(int logical_thread_index, void* userdata) {
	console_print("thread %d: %s\n", logical_thread_index, (char*) userdata);

	add_work_queue_entry(&global_completion_queue, echo_task_completed, userdata, strlen(userdata)+1);
}
#endif

void test_multithreading_work_queue() {
#ifdef TEST_THREAD_QUEUE
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"NULL entry", 11);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 0", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 1", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 2", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 3", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 4", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 5", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 6", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 7", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 8", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 9", 9);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 10", 10);
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 11", 10);

//	while (is_queue_work_in_progress(&global_work_queue)) {
//		do_worker_work(&global_work_queue, 0);
//	}
	while (is_queue_work_in_progress(&global_work_queue) || is_queue_work_in_progress((&global_completion_queue))) {
		do_worker_work(&global_completion_queue, 0);
	}
#endif
}