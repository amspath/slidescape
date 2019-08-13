//#define USE_MINIMAL_SYSTEM_HEADER
#include "common.h"
#include "platform.h"
#include "win32_platform.h"

#include <stdio.h>
#include <sys/stat.h>

int g_argc;
char** g_argv;

i64 performance_counter_frequency;
bool32 is_sleep_granular;

void win32_diagnostic(const char* prefix) {
	DWORD error_id = GetLastError();
	char* message_buffer;
	/*size_t size = */FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                                 NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
	printf("%s: (error code 0x%x) %s\n", prefix, (u32)error_id, message_buffer);
	LocalFree(message_buffer);
}

void win32_message_box(HWND window_handle, const char* message) {
	MessageBoxA(window_handle, message, "Slideviewer", MB_ICONERROR);
}


// Timer-related procedures
void init_timer() {
	LARGE_INTEGER perf_counter_frequency_result;
	QueryPerformanceFrequency(&perf_counter_frequency_result);
	performance_counter_frequency = perf_counter_frequency_result.QuadPart;
	// Make Sleep() more granular
	UINT desired_scheduler_granularity_ms = 1;
	is_sleep_granular = (timeBeginPeriod(desired_scheduler_granularity_ms) == TIMERR_NOERROR);
}

i64 get_clock() {
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return result.QuadPart;
}

float get_seconds_elapsed(i64 start, i64 end) {
	return (float)(end - start) / (float)performance_counter_frequency;
}


u8* platform_alloc(size_t size) {
	u8* result = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!result) {
		printf("Error: memory allocation failed!\n");
		panic();
	}
}

file_mem_t* platform_read_entire_file(const char* filename) {
	file_mem_t* result = NULL;
	FILE* fp = fopen(filename, "rb");
	if (fp) {
		struct stat st;
		if (fstat(fileno(fp), &st) == 0) {
			i64 filesize = st.st_size;
			if (filesize > 0) {
				size_t allocation_size = filesize + sizeof(result->len) + 1;
				result = malloc(allocation_size);
				if (result) {
					((u8*)result)[allocation_size-1] = '\0';
					result->len = filesize;
					size_t bytes_read = fread(result->data, 1, filesize, fp);
					if (bytes_read != filesize) {
						panic();
					}
				}
			}
		}
		fclose(fp);
	}
	return result;
}

