/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2026  Pieter Valkema

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
