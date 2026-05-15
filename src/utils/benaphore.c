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

#include "benaphore.h"


void benaphore_init(benaphore_t* benaphore) {
#ifdef _WIN32
	InitializeSRWLock(&benaphore->lock);
#else
	if (pthread_mutex_init(&benaphore->lock, NULL) != 0) {
		fatal_error("benaphore_init(): failed to initialize pthread mutex");
	}
#endif
}

void benaphore_destroy(benaphore_t* benaphore) {
#ifdef _WIN32
	(void)benaphore;
#else
	pthread_mutex_destroy(&benaphore->lock);
#endif
}

void benaphore_lock(benaphore_t* benaphore) {
#ifdef _WIN32
	AcquireSRWLockExclusive(&benaphore->lock);
#else
	pthread_mutex_lock(&benaphore->lock);
#endif
}

void benaphore_unlock(benaphore_t* benaphore) {
#ifdef _WIN32
	ReleaseSRWLockExclusive(&benaphore->lock);
#else
	pthread_mutex_unlock(&benaphore->lock);
#endif
}
