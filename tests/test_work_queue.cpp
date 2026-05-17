#include "doctest.h"

#include "platform.h"
#include "work_queue.h"
#include "intrinsics.h"

typedef struct test_counter_task_t {
	i32 volatile* counter;
} test_counter_task_t;

static void increment_counter_task(int logical_thread_index, void* userdata) {
	(void)logical_thread_index;
	test_counter_task_t* task = (test_counter_task_t*)userdata;
	atomic_increment(task->counter);
}

static void ensure_test_thread_memory(void) {
	if (!threadlocal_thread_memory) {
		init_global_system_info(false);
		init_thread_memory(&global_system_info);
	}
}

TEST_CASE("work queue executes submitted tasks on caller thread") {
	ensure_test_thread_memory();

	work_queue_t queue = work_queue_create("/slidescape_test_work_queue", 8);
	i32 volatile counter = 0;
	test_counter_task_t task = {&counter};

	CHECK(work_queue_submit_task(&queue, increment_counter_task, &task, sizeof(task)));
	CHECK(work_queue_submit_task(&queue, increment_counter_task, &task, sizeof(task)));
	CHECK(work_queue_submit_task(&queue, increment_counter_task, &task, sizeof(task)));

	while (work_queue_is_work_in_progress(&queue)) {
		CHECK(work_queue_do_work(&queue));
	}

	CHECK(counter == 3);
	CHECK(queue.start_count == 3);
	CHECK(queue.completion_count == 3);

	work_queue_destroy(&queue);
}

TEST_CASE("work queue helpers tolerate absent optional queue") {
	CHECK_FALSE(work_queue_do_work(NULL));
	CHECK_FALSE(work_queue_is_work_in_progress(NULL));
	CHECK_FALSE(work_queue_is_work_waiting_to_start(NULL));
}

TEST_CASE("thread pool runs work and can be destroyed") {
	ensure_test_thread_memory();

	init_global_system_info(false);
	system_info_t old_system_info = global_system_info;
	global_system_info.suggested_total_thread_count = 3;

	thread_pool_t pool = {};
	init_thread_pool(&pool, 32, true, false, NULL);

	i32 volatile counter = 0;
	test_counter_task_t task = {&counter};
	for (i32 i = 0; i < 16; ++i) {
		REQUIRE(work_queue_submit_task(pool.queue, increment_counter_task, &task, sizeof(task)));
	}

	thread_pool_wait_for_completion(&pool);
	CHECK(counter == 16);
	CHECK(pool.queue->completion_count == 16);

	thread_pool_destroy(&pool);
	CHECK(pool.initialized == 0);
	CHECK(pool.queue == NULL);
	CHECK(pool.high_priority_queue == NULL);

	global_system_info = old_system_info;
}

TEST_CASE("thread pool without high priority queue can be reused after destroy") {
	ensure_test_thread_memory();

	init_global_system_info(false);
	system_info_t old_system_info = global_system_info;
	global_system_info.suggested_total_thread_count = 2;

	for (i32 iteration = 0; iteration < 2; ++iteration) {
		thread_pool_t pool = {};
		init_thread_pool(&pool, 8, false, false, NULL);
		REQUIRE(pool.initialized);
		CHECK(pool.high_priority_queue == NULL);

		i32 volatile counter = 0;
		test_counter_task_t task = {&counter};
		REQUIRE(work_queue_submit_task(pool.queue, increment_counter_task, &task, sizeof(task)));
		thread_pool_wait_for_completion(&pool);
		CHECK(counter == 1);

		thread_pool_destroy(&pool);
		thread_pool_destroy(&pool);
		CHECK(pool.initialized == 0);
	}

	global_system_info = old_system_info;
}

TEST_CASE("thread pool helpers submit normal and high priority work") {
	ensure_test_thread_memory();

	init_global_system_info(false);
	system_info_t old_system_info = global_system_info;
	global_system_info.suggested_total_thread_count = 2;

	thread_pool_t pool = {};
	init_thread_pool(&pool, 8, true, false, NULL);

	i32 volatile counter = 0;
	test_counter_task_t task = {&counter};
	REQUIRE(thread_pool_submit_task(&pool, increment_counter_task, &task, sizeof(task)));
	REQUIRE(thread_pool_submit_high_priority_task(&pool, increment_counter_task, &task, sizeof(task)));
	CHECK(thread_pool_is_work_waiting_to_start(&pool));

	while (thread_pool_is_work_in_progress(&pool)) {
		if (!thread_pool_do_work(&pool)) {
			platform_sleep(1);
		}
	}
	CHECK(counter == 2);
	CHECK(pool.queue->completion_count == 1);
	CHECK(pool.high_priority_queue->completion_count == 1);

	thread_pool_destroy(&pool);

	global_system_info = old_system_info;
}
