/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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


bool add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata) {
	for (i32 tries = 0; tries < 1000; ++tries) {
		// Circular FIFO buffer
		i32 entry_to_submit = queue->next_entry_to_submit;
		i32 new_next_entry_to_submit = (queue->next_entry_to_submit + 1) % COUNT(queue->entries);
		if (new_next_entry_to_submit == queue->next_entry_to_execute) {
			console_print_error("Warning: work queue is overflowing - job is cancelled\n");
			return false;
		}

		bool succeeded = atomic_compare_exchange(&queue->next_entry_to_submit,
		                                         new_next_entry_to_submit, entry_to_submit);
		if (succeeded) {
//		    console_print("exhange succeeded\n");
			queue->entries[entry_to_submit] = (work_queue_entry_t){ .data = userdata, .callback = callback };
			write_barrier;
			queue->entries[entry_to_submit].is_valid = true;
			write_barrier;
			atomic_increment(&queue->completion_goal);
//		    queue->next_entry_to_submit = new_next_entry_to_submit;
#if WINDOWS
			ReleaseSemaphore(queue->semaphore, 1, NULL);
#else
			sem_post(queue->semaphore);
#endif
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
	work_queue_entry_t result = {};

	i32 entry_to_execute = queue->next_entry_to_execute;
	i32 new_next_entry_to_execute = (entry_to_execute + 1) % COUNT(queue->entries);

	// don't even try to execute a task if it is not yet submitted, or not yet fully submitted
	if ((entry_to_execute != queue->next_entry_to_submit) && (queue->entries[entry_to_execute].is_valid)) {
		bool succeeded = atomic_compare_exchange(&queue->next_entry_to_execute,
		                                         new_next_entry_to_execute, entry_to_execute);
		if (succeeded) {
			// We have dibs to execute this task!
			queue->entries[entry_to_execute].is_valid = false; // discourage competing threads (maybe not needed?)
			result.data = queue->entries[entry_to_execute].data;
			result.callback = queue->entries[entry_to_execute].callback;
			if (!result.callback) {
				console_print_error("Error: encountered a work entry with a missing callback routine\n");
				panic();
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
		if (!entry.callback) panic();
		entry.callback(logical_thread_index, entry.data);
		mark_queue_entry_completed(queue);
	}
	return entry.is_valid;
}


bool is_queue_work_in_progress(work_queue_t* queue) {
	bool result = (queue->completion_goal > queue->completion_count);
	return result;
}

//#define TEST_THREAD_QUEUE
#ifdef TEST_THREAD_QUEUE
void echo_task_completed(int logical_thread_index, void* userdata) {
	console_print("thread %d completed: %s\n", logical_thread_index, (char*) userdata);
}

void echo_task(int logical_thread_index, void* userdata) {
	console_print("thread %d: %s\n", logical_thread_index, (char*) userdata);

	add_work_queue_entry(&global_completion_queue, echo_task_completed, userdata);
}
#endif

void test_multithreading_work_queue() {
#ifdef TEST_THREAD_QUEUE
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"NULL entry");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 0");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 1");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 2");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 3");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 4");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 5");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 6");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 7");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 8");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 9");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 10");
	add_work_queue_entry(&global_work_queue, echo_task, (void*)"string 11");

//	while (is_queue_work_in_progress(&global_work_queue)) {
//		do_worker_work(&global_work_queue, 0);
//	}
	while (is_queue_work_in_progress(&global_work_queue) || is_queue_work_in_progress((&global_completion_queue))) {
		do_worker_work(&global_completion_queue, 0);
	}
#endif
}