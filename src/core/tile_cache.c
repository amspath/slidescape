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

#include "tile_cache.h"

tile_cache_t* tile_cache_create(image_t* image) {
	if (!image) {
		return NULL;
	}

	tile_cache_t* cache = (tile_cache_t*)calloc(1, sizeof(tile_cache_t));
	if (!cache) {
		return NULL;
	}
	cache->image = image;
	platform_mutex_init(&cache->lock);
	cache->lock_initialized = true;

	for (i32 level = 0; level < image->level_count; ++level) {
		level_image_t* level_image = image->level_images + level;
		if (level_image->exists && level_image->tile_count > 0) {
			cache->level_tiles[level] = (tile_cache_tile_t*)calloc(level_image->tile_count, sizeof(tile_cache_tile_t));
			if (!cache->level_tiles[level]) {
				tile_cache_destroy(cache);
				return NULL;
			}
		}
	}

	return cache;
}

tile_cache_t* tile_cache_get_or_create(image_t* image) {
	if (!image) {
		return NULL;
	}
	if (!image->tile_cache) {
		image->tile_cache = tile_cache_create(image);
	}
	return image->tile_cache;
}

void tile_cache_destroy(tile_cache_t* cache) {
	if (!cache) {
		return;
	}

	for (i32 level = 0; level < COUNT(cache->level_tiles); ++level) {
		tile_cache_tile_t* tiles = cache->level_tiles[level];
		if (tiles) {
			image_t* image = cache->image;
			i32 tile_count = (image && level < image->level_count) ? image->level_images[level].tile_count : 0;
			for (i32 tile_index = 0; tile_index < tile_count; ++tile_index) {
				tile_cache_tile_t* tile = tiles + tile_index;
				if (tile->pixels) {
					free(tile->pixels);
					tile->pixels = NULL;
				}
			}
			free(tiles);
			cache->level_tiles[level] = NULL;
		}
	}

	if (cache->lock_initialized) {
		platform_mutex_destroy(&cache->lock);
		cache->lock_initialized = false;
	}
	free(cache);
}

tile_cache_tile_t* tile_cache_get_tile_state(image_t* image, i32 level, i32 tile_index) {
	tile_cache_t* cache = image ? image->tile_cache : NULL;
	if (!cache || level < 0 || level >= COUNT(cache->level_tiles)) {
		return NULL;
	}
	if (level >= image->level_count) {
		return NULL;
	}
	level_image_t* level_image = image->level_images + level;
	if (tile_index < 0 || tile_index >= level_image->tile_count) {
		return NULL;
	}
	return cache->level_tiles[level] ? cache->level_tiles[level] + tile_index : NULL;
}
