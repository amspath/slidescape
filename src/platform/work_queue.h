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

#include "common.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <semaphore.h>
#endif

typedef void (work_queue_callback_t)(int logical_thread_index, void* userdata);

typedef struct work_queue_entry_t {
	bool32 is_valid;
	u32 task_identifier;
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

work_queue_t work_queue_create(const char* semaphore_name, i32 entry_count);
void work_queue_destroy(work_queue_t* queue);
i32 work_queue_get_entry_count(work_queue_t* queue);
bool work_queue_submit_task(work_queue_t* queue, work_queue_callback_t callback, void* userdata, size_t userdata_size);
bool work_queue_submit_notification(work_queue_t* queue, u32 task_identifier, void* userdata, size_t userdata_size);
bool work_queue_submit(work_queue_t* queue, work_queue_callback_t callback, u32 task_identifier, void* userdata, size_t userdata_size);
work_queue_entry_t work_queue_get_next_entry(work_queue_t* queue);
void work_queue_mark_entry_completed(work_queue_t* queue);
bool work_queue_do_work(work_queue_t* queue, int logical_thread_index);
bool work_queue_is_work_in_progress(work_queue_t* queue);
bool work_queue_is_work_waiting_to_start(work_queue_t* queue);
void dummy_work_queue_callback(int logical_thread_index, void* userdata);
void test_multithreading_work_queue();


// globals
#if defined(WORK_QUEUE_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern THREAD_LOCAL i32 work_queue_call_depth;
extern work_queue_t global_work_queue;
extern i32 global_worker_thread_idle_count;


#undef INIT
#undef extern



#ifdef __cplusplus
}
#endif
