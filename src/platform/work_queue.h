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

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <semaphore.h>
#endif

typedef void (work_queue_callback_t)(int logical_thread_index, void* userdata);

typedef struct work_queue_entry_t {
	bool32 is_valid;
	work_queue_callback_t* callback;
	u8 userdata[128];
} work_queue_entry_t;

typedef struct work_queue_t {
#if WINDOWS
	HANDLE semaphore;
#else
	sem_t* semaphore;
#endif
	i32 volatile next_entry_to_submit;
	i32 volatile next_entry_to_execute;
	i32 volatile completion_count;
	i32 volatile completion_goal;
	i32 volatile start_count;
	i32 volatile start_goal;
	i32 entry_count;
	work_queue_entry_t* entries;
} work_queue_t;

work_queue_t create_work_queue(const char* semaphore_name, i32 entry_count);
void destroy_work_queue(work_queue_t* queue);
i32 get_work_queue_task_count(work_queue_t* queue);
bool add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata, size_t userdata_size);
work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue);
void mark_queue_entry_completed(work_queue_t* queue);
bool do_worker_work(work_queue_t* queue, int logical_thread_index);
bool is_queue_work_in_progress(work_queue_t* queue);
bool is_queue_work_waiting_to_start(work_queue_t* queue);
void drain_work_queue(work_queue_t* queue);
void dummy_work_queue_callback(int logical_thread_index, void* userdata);
void test_multithreading_work_queue();

#ifdef __cplusplus
}
#endif
