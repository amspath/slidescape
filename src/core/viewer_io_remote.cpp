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


void tiff_load_tile_batch_func(i32 logical_thread_index, void* userdata) {
	load_tile_task_batch_t* batch = (load_tile_task_batch_t*) userdata;
	load_tile_task_t* first_task = batch->tile_tasks;
	image_t* image = first_task->image;

	i32 refcount_decrement_amount = 0;
	for (i32 i = 0; i < batch->task_count; ++i) {
		load_tile_task_t *task = batch->tile_tasks + i;
		refcount_decrement_amount += task->refcount_to_decrement;
	}
	if (image->is_deleted) {
		// Early out to save time if the image was already closed/waiting for destruction
		atomic_subtract(&image->refcount, refcount_decrement_amount);
		return;
	}

	// Note: when the thread started up we allocated a large blob of memory for the thread to use privately
	// TODO: better/more explicit allocator (instead of some setting some hard-coded pointers)
//	thread_memory_t* thread_memory = (thread_memory_t*) thread_local_storage[logical_thread_index];
//	u8* temp_memory = (u8*) thread_memory->aligned_rest_of_thread_memory; //malloc(WSI_BLOCK_SIZE);
//	memset(temp_memory, 0xFF, WSI_BLOCK_SIZE);

	ASSERT(image->type == IMAGE_TYPE_WSI);
	if (image->backend == IMAGE_BACKEND_TIFF) {
		tiff_t* tiff = &image->tiff;

		if (tiff->is_remote) {

			i32 bytes_read = 0;
			i32 batch_size = batch->task_count;
			i64 chunk_offsets[TILE_LOAD_BATCH_MAX];
			i64 chunk_sizes[TILE_LOAD_BATCH_MAX];
			i64 total_read_size = 0;
			for (i32 i = 0; i < batch_size; ++i) {
				load_tile_task_t* task = batch->tile_tasks + i;

				i32 level = task->level;
				i32 tile_x = task->tile_x;
				i32 tile_y = task->tile_y;
				level_image_t* level_image = image->level_images + level;
				tile_t* tile = task->tile;
				i32 tile_index = tile_y * level_image->width_in_tiles + tile_x;
				tiff_ifd_t* level_ifd = tiff->level_images_ifd + level_image->pyramid_image_index;
				u64 tile_offset = level_ifd->tile_offsets[tile_index];
				u64 chunk_size = level_ifd->tile_byte_counts[tile_index];

				// It doesn't make sense to ask for empty tiles, this should never happen!
				ASSERT(tile_offset != 0);
				ASSERT(chunk_size != 0);

				chunk_offsets[i] = tile_offset;
				chunk_sizes[i] = chunk_size;
				total_read_size += chunk_size;
			}


//			u32 new_textures[TILE_LOAD_BATCH_MAX] = {0};

			// Note: First download everything, then decode and upload everything to the GPU.
			// It would be faster to pipeline this somehow.
			u8* read_buffer = download_remote_batch(tiff->location.hostname, tiff->location.portno,
			                                        tiff->location.filename,
			                                        chunk_offsets, chunk_sizes, batch_size, &bytes_read, logical_thread_index);
			if (read_buffer && bytes_read > 0) {
				i64 content_offset = find_end_of_http_headers(read_buffer, bytes_read);
				i64 content_length = bytes_read - content_offset;
				u8* content = read_buffer + content_offset;

				// TODO: better way to check the real content length?
				if (content_length >= total_read_size) {

					i64 chunk_offset_in_read_buffer = 0;
					for (i32 i = 0; i < batch_size; ++i) {
						load_tile_task_t* task = batch->tile_tasks + i;
						level_image_t* level_image = image->level_images + task->level;

						size_t pixel_memory_size = level_image->tile_width * level_image->tile_height * BYTES_PER_PIXEL;
						u8* pixel_memory = (u8*)malloc(pixel_memory_size);
						memset(pixel_memory, 0xFF, pixel_memory_size);

						u8* current_chunk = content + chunk_offset_in_read_buffer;
						chunk_offset_in_read_buffer += chunk_sizes[i];

						tiff_ifd_t* level_ifd = tiff->level_images_ifd + level_image->pyramid_image_index;
						u8* jpeg_tables = level_ifd->jpeg_tables;
						u64 jpeg_tables_length = level_ifd->jpeg_tables_length;

						if (content[0] == 0xFF && content[1] == 0xD9) {
							// JPEG stream is empty
						} else {
							if (jpeg_decode_tile(jpeg_tables, jpeg_tables_length, current_chunk, chunk_sizes[i],
							                     pixel_memory, (level_ifd->color_space == TIFF_PHOTOMETRIC_YCBCR))) {
//		                    console_print("thread %d: successfully decoded level %d, tile %d (%d, %d)\n", logical_thread_index, level, tile_index, tile_x, tile_y);
							} else {
								console_print_error("[thread %d] failed to decode level %d, tile (%d, %d)\n", logical_thread_index, task->level, task->tile_x, task->tile_y);
							}
						}

						viewer_notify_tile_completed_task_t completion_task = {};
						completion_task.resource_id = task->resource_id;
						completion_task.pixel_memory = pixel_memory;
						// TODO: check if we need to pass the tile height here too?
						completion_task.tile_width = level_image->tile_width;
						completion_task.scale = task->level;
						completion_task.tile_index = task->tile_y * level_image->width_in_tiles + task->tile_x;
						completion_task.want_gpu_residency = true;

						ASSERT(task->completion_callback);
						if (task->completion_callback) {
							task->completion_callback(logical_thread_index, &completion_task);
						}
						add_work_queue_entry(&global_completion_queue, task->completion_callback, &completion_task, sizeof(completion_task));

						//new_textures[i] = load_texture(pixel_memory, TILE_DIM, TILE_DIM, GL_BGRA);
					}

				}

			}

#if 0
			// Note: setting task->tile->texture to the texture handle lets the main thread know that the texture
			// is ready for use. However, the texture may still not *actually* be available until OpenGL has done its
			// magic, so to be 100% sure we need to call glFinish() before setting task->tile->texture
			// We only want to call glFinish() once, so we store the new texture handles temporarily while the
			// rest of the batch is still loading.
			// Better would be to flag the texture as 'ready for use' as soon as it's done, but I don't know
			// how we could query this / be notified of this. (maybe an optimization for later?)
			glFinish();
			write_barrier;
			for (i32 i = 0; i < batch_size; ++i) {
				load_tile_task_t* task = batch->tile_tasks + i;
				task->tile->texture = new_textures[i];
			}
#endif

			free(read_buffer);
		}

	}

    // NOTE: we guarantee existence of image_t until the jobs submitted from the main thread are done.
    // However, we will NOT wait for the completion queues to also be finished (usually the responsibility of the main thread).
    // This means that when we receive the completion tasks on the main thread, we have to check if the image is still valid.
    atomic_subtract(&image->refcount, refcount_decrement_amount);

}