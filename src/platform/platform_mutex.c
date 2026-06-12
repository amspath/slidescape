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

#include "platform_mutex.h"


void platform_mutex_init(platform_mutex_t* mutex) {
#ifdef _WIN32
	InitializeSRWLock(&mutex->lock);
#else
	if (pthread_mutex_init(&mutex->lock, NULL) != 0) {
		fatal_error("platform_mutex_init(): failed to initialize pthread mutex");
	}
#endif
}

void platform_mutex_destroy(platform_mutex_t* mutex) {
#ifdef _WIN32
	(void)mutex;
#else
	pthread_mutex_destroy(&mutex->lock);
#endif
}

void platform_mutex_lock(platform_mutex_t* mutex) {
#ifdef _WIN32
	AcquireSRWLockExclusive(&mutex->lock);
#else
	pthread_mutex_lock(&mutex->lock);
#endif
}

void platform_mutex_unlock(platform_mutex_t* mutex) {
#ifdef _WIN32
	ReleaseSRWLockExclusive(&mutex->lock);
#else
	pthread_mutex_unlock(&mutex->lock);
#endif
}
