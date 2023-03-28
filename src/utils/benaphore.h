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


#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

#ifdef _WIN32
#include "windows.h"
#else
#include <semaphore.h>
#endif


typedef struct benaphore_t {
#ifdef _WIN32
	HANDLE semaphore;
#else
	sem_t* semaphore;
#endif
	volatile i32 counter;
} benaphore_t;

benaphore_t benaphore_create(void);
void benaphore_destroy(benaphore_t* benaphore);
void benaphore_lock(benaphore_t* benaphore);
void benaphore_unlock(benaphore_t* benaphore);

#ifdef __cplusplus
}
#endif
