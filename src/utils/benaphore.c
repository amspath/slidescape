/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2023  Pieter Valkema

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

#include "benaphore.h"

#include "intrinsics.h"

// Based on:
// https://preshing.com/20120226/roll-your-own-lightweight-mutex/


benaphore_t benaphore_create(void) {
	benaphore_t result = {0};
#ifdef _WIN32
	result.semaphore = CreateSemaphore(NULL, 0, 1, NULL);
#else
	static i32 counter = 1;
	char semaphore_name[64];
	i32 c = atomic_increment(&counter);
	snprintf(semaphore_name, sizeof(semaphore_name)-1, "/benaphore%d", c);
	result.semaphore = sem_open(semaphore_name, O_CREAT, 0644, 0);
#endif
	return result;
}

void benaphore_destroy(benaphore_t* benaphore) {
#ifdef _WIN32
	CloseHandle(benaphore->semaphore);
#else
	sem_close(benaphore->semaphore);
#endif
}

void benaphore_lock(benaphore_t* benaphore) {
	if (atomic_increment(&benaphore->counter) > 1) {
#ifdef _WIN32
		WaitForSingleObject(benaphore->semaphore, INFINITE);
#else
		sem_wait(benaphore->semaphore);
#endif
	}
}

void benaphore_unlock(benaphore_t* benaphore) {
	if (atomic_decrement(&benaphore->counter) > 0) {
#ifdef _WIN32
		ReleaseSemaphore(benaphore->semaphore, 1, NULL);
#else
		sem_post(benaphore->semaphore);
#endif
	}
}
