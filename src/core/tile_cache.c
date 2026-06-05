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

void tile_cache_pin_cpu_tile(image_t* image, i32 level, i32 tile_index, u32 demand_flags) {
	tile_cache_t* cache = tile_cache_get_or_create(image);
	if (!cache) {
		return;
	}
	tile_cache_tile_t* tile = tile_cache_get_tile_state(image, level, tile_index);
	if (!tile) {
		return;
	}
	++tile->cpu_pin_count;
	tile->demand_mask |= demand_flags | TILE_CACHE_DEMAND_CPU_RESIDENCY;
}

void tile_cache_unpin_cpu_tile(image_t* image, i32 level, i32 tile_index, u32 demand_flags) {
	tile_cache_tile_t* tile = tile_cache_get_tile_state(image, level, tile_index);
	if (!tile) {
		return;
	}
	if (tile->cpu_pin_count > 0) {
		--tile->cpu_pin_count;
	}
	if (tile->cpu_pin_count == 0) {
		tile->demand_mask &= ~(demand_flags | TILE_CACHE_DEMAND_CPU_RESIDENCY);
		tile_cache_release_cpu_pixels_if_unpinned(image, level, tile_index);
	}
}

bool tile_cache_tile_has_cpu_pixels(image_t* image, i32 level, i32 tile_index) {
	tile_cache_tile_t* tile = tile_cache_get_tile_state(image, level, tile_index);
	return tile && tile->cpu_resident && tile->pixels;
}

bool tile_cache_tile_is_cpu_pinned(image_t* image, i32 level, i32 tile_index) {
	tile_cache_tile_t* tile = tile_cache_get_tile_state(image, level, tile_index);
	return tile && tile->cpu_pin_count > 0;
}

u8* tile_cache_get_cpu_pixels(image_t* image, i32 level, i32 tile_index) {
	tile_cache_tile_t* tile = tile_cache_get_tile_state(image, level, tile_index);
	if (!tile || !tile->cpu_resident) {
		return NULL;
	}
	tile->last_cpu_access_time = get_clock();
	return tile->pixels;
}

void tile_cache_store_cpu_pixels(image_t* image, i32 level, i32 tile_index, u8* pixels) {
	if (!pixels) {
		return;
	}
	tile_cache_t* cache = tile_cache_get_or_create(image);
	if (!cache) {
		free(pixels);
		return;
	}
	tile_cache_tile_t* tile = tile_cache_get_tile_state(image, level, tile_index);
	if (!tile) {
		free(pixels);
		return;
	}
	if (tile->pixels && tile->pixels != pixels) {
		free(pixels);
		return;
	}
	tile->pixels = pixels;
	tile->cpu_resident = true;
	tile->last_cpu_access_time = get_clock();
}

void tile_cache_release_cpu_pixels_if_unpinned(image_t* image, i32 level, i32 tile_index) {
	tile_cache_tile_t* tile = tile_cache_get_tile_state(image, level, tile_index);
	if (!tile || tile->cpu_pin_count > 0) {
		return;
	}
	if (tile->pixels) {
		free(tile->pixels);
		tile->pixels = NULL;
	}
	tile->cpu_resident = false;
}
