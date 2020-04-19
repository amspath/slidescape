#pragma once
#include "common.h"


#ifdef __cplusplus
extern "C" {
#endif


// prototypes
void init_networking();
void open_remote_slide(const char* hostname, i32 portno, const char* filename);

// globals
#if defined(TLSCLIENT_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif



#undef INIT
#undef extern


#ifdef __cplusplus
};
#endif

