/*
  BSD 2-Clause License

  Copyright (c) 2019-2026, Pieter Valkema

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

#ifdef _WIN32
#include "windows.h"
#else
#include <pthread.h>
#endif


typedef struct platform_mutex_t {
#ifdef _WIN32
	SRWLOCK lock;
#else
	pthread_mutex_t lock;
#endif
} platform_mutex_t;

#ifdef _WIN32
#define PLATFORM_MUTEX_INITIALIZER { SRWLOCK_INIT }
#else
#define PLATFORM_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }
#endif

void platform_mutex_init(platform_mutex_t* mutex);
void platform_mutex_destroy(platform_mutex_t* mutex);
void platform_mutex_lock(platform_mutex_t* mutex);
void platform_mutex_unlock(platform_mutex_t* mutex);

#ifdef __cplusplus
}
#endif
