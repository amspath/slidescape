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
