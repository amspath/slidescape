#include "common.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif
#include "jpeglib.h"

static void on_error(j_common_ptr cinfo) {
	(*cinfo->err->output_message)(cinfo);
}

static void empty_impl(j_decompress_ptr cinfo) {

}

static void skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
	struct jpeg_source_mgr *src = cinfo->src;

	if (num_bytes > 0) {
		while (num_bytes > (long) src->bytes_in_buffer) {
			num_bytes -= (long) src->bytes_in_buffer;
			(void) (*src->fill_input_buffer)(cinfo);
		}
		src->next_input_byte += (size_t) num_bytes;
		src->bytes_in_buffer -= (size_t) num_bytes;
	}
}

static boolean fill_mem_input_buffer(j_decompress_ptr cinfo) {
	static const JOCTET mybuffer[4] = {
			(JOCTET) 0xFF, (JOCTET) JPEG_EOI, 0, 0
	};

	/* Insert a fake EOI marker */

	cinfo->src->next_input_byte = mybuffer;
	cinfo->src->bytes_in_buffer = 2;

	return TRUE;
}

void setup_jpeg_source(j_decompress_ptr cinfo, uint8_t *input_ptr, uint32_t input_length) {
	struct jpeg_source_mgr *src;

	if (cinfo->src == NULL) {
		cinfo->src = (struct jpeg_source_mgr *)
				(*cinfo->mem->alloc_small)((j_common_ptr) cinfo, JPOOL_PERMANENT,
				                           sizeof(struct jpeg_source_mgr));
	}

	src = cinfo->src;
	src->init_source = empty_impl;
	src->fill_input_buffer = fill_mem_input_buffer;
	src->skip_input_data = skip_input_data;
	src->resync_to_restart = jpeg_resync_to_restart;
	src->term_source = empty_impl;
	src->bytes_in_buffer = input_length;
	src->next_input_byte = input_ptr;
}

EMSCRIPTEN_KEEPALIVE
boolean decode_tile(uint8_t *table_ptr, uint32_t table_length, uint8_t *input_ptr, uint32_t input_length, uint8_t *output_ptr, bool32 is_YCbCr) {
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;

	// Setup error handling
	cinfo.err = jpeg_std_error(&jerr);
	jerr.error_exit = on_error;

	jpeg_create_decompress(&cinfo);

	// Load Jpeg table
	setup_jpeg_source(&cinfo, table_ptr, table_length);
	if (jpeg_read_header(&cinfo, FALSE) != JPEG_HEADER_TABLES_ONLY) {
		printf("Failed to load table\n");
		jpeg_destroy_decompress(&cinfo);
		return FALSE;
	}

	// Read tile data
	setup_jpeg_source(&cinfo, input_ptr, input_length);
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		printf("Failed to read header\n");
		jpeg_destroy_decompress(&cinfo);
		return FALSE;
	}

	cinfo.jpeg_color_space = is_YCbCr ? JCS_YCbCr : JCS_RGB;
	cinfo.out_color_space = JCS_RGB;

	jpeg_start_decompress(&cinfo);

	int row_width = cinfo.output_width;
	int target_row_stride = row_width * 4;
	int source_row_stride = row_width * cinfo.output_components;
	JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)
			((j_common_ptr) &cinfo, JPOOL_IMAGE, source_row_stride, 1);

	int source_offset;
	int target_offset;
	while (cinfo.output_scanline < cinfo.output_height) {
		(void) jpeg_read_scanlines(&cinfo, buffer, 1);

		for (int i = 0; i < row_width; i++) {
			source_offset = i * 3;
			target_offset = i * 4;

			// TODO: what to do here, BGRA or RGBA?
			output_ptr[target_offset + 0] = buffer[0][source_offset + 2];
			output_ptr[target_offset + 1] = buffer[0][source_offset + 1];
			output_ptr[target_offset + 2] = buffer[0][source_offset + 0];
			output_ptr[target_offset + 3] = 255;
		}

		output_ptr += target_row_stride;
	}

	(void) jpeg_finish_decompress(&cinfo);

	jpeg_destroy_decompress(&cinfo);

	return TRUE;
}

EMSCRIPTEN_KEEPALIVE
uint8_t *create_buffer(int size) {
	return malloc(size * sizeof(uint8_t));
}

EMSCRIPTEN_KEEPALIVE
void destroy_buffer(uint8_t *p) {
	free(p);
}
