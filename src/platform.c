#include "common.h"

#include <stdio.h>
#include <sys/stat.h>

#include "platform.h"


mem_t* platform_allocate_mem_buffer(size_t capacity) {
	size_t allocation_size = sizeof(mem_t) + capacity + 1;
	mem_t* result = (mem_t*) malloc(allocation_size);
	result->len = 0;
	result->capacity = capacity;
	return result;
}

mem_t* platform_read_entire_file(const char* filename) {
	mem_t* result = NULL;
	FILE* fp = fopen(filename, "rb");
	if (fp) {
		struct stat st;
		if (fstat(fileno(fp), &st) == 0) {
			i64 filesize = st.st_size;
			if (filesize > 0) {
				size_t allocation_size = sizeof(mem_t) + filesize + 1;
				result = (mem_t*) malloc(allocation_size);
				if (result) {
					((u8*)result)[allocation_size-1] = '\0';
					result->len = filesize;
					result->capacity = filesize;
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

bool file_exists(const char* filename) {
	return (access(filename, F_OK) != -1);
}
