#pragma once

#include "common.h"

typedef struct {
	size_t len;
	u8 data[0];
} file_mem_t;


extern int g_argc;
extern char** g_argv;

// Platform specific function prototypes

i64 get_clock();
float get_seconds_elapsed(i64 start, i64 end);

u8* platform_alloc(size_t size); // required to be zeroed by the platform
file_mem_t* platform_read_entire_file(const char* filename);

void mouse_show();
void mouse_hide();
