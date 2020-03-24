#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include "common.h"

void dots_to_underscores(char* s, i32 max);
const char* one_past_last_slash(const char* s, i32 max);
const char* get_file_extension(const char* filename);


#endif //STRINGUTILS_H
