#pragma once

typedef struct file_mem_t {
	size_t len;
	u8 data[0];
} file_mem_t;

extern int g_argc;
extern char** g_argv;

extern i64 performance_counter_frequency;
extern bool32 is_sleep_granular;

// Platform specific function prototypes
void init_timer();
i64 get_clock();
float get_seconds_elapsed(i64 start, i64 end);

u8* platform_alloc(size_t size); // required to be zeroed by the platform
file_mem_t* platform_read_entire_file(const char* filename);

void platform_wait_for_boolean_true(volatile bool32* value_ptr);

void mouse_show();
void mouse_hide();



