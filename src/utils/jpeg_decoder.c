#include "common.h"

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


// https://www.ridgesolutions.ie/index.php/2019/12/10/libjpeg-example-encode-jpeg-to-memory-buffer-instead-of-file/



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
	cinfo.out_color_space = JCS_EXT_BGRA;

	jpeg_start_decompress(&cinfo);

	int row_width = cinfo.output_width;
	int target_row_stride = row_width * 4;
	int source_row_stride = row_width * cinfo.output_components;
	JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)
			((j_common_ptr) &cinfo, JPOOL_IMAGE, source_row_stride, 1);

	while (cinfo.output_scanline < cinfo.output_height) {
		(void) jpeg_read_scanlines(&cinfo, buffer, 1);
		memcpy(output_ptr, buffer[0], target_row_stride);
		output_ptr += target_row_stride;
	}

	(void) jpeg_finish_decompress(&cinfo);

	jpeg_destroy_decompress(&cinfo);

	return TRUE;
}

u8* jpeg_decode_image(u8* input_ptr, u32 input_length, i32* width, i32* height, i32 *channels_in_file) {
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;

	// Setup error handling
	cinfo.err = jpeg_std_error(&jerr);
	jerr.error_exit = on_error;

	jpeg_create_decompress(&cinfo);

	// Read tile data
	setup_jpeg_source(&cinfo, input_ptr, input_length);
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		printf("Failed to read header\n");
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}

	cinfo.out_color_space = JCS_EXT_BGRA;

	jpeg_start_decompress(&cinfo);

	int row_width = cinfo.output_width;
	int target_row_stride = row_width * cinfo.output_components;
	size_t output_size = target_row_stride * cinfo.output_height;
	u8* output_buffer = malloc(output_size);

	while (cinfo.output_scanline < cinfo.output_height) {
		u8* output_pos = output_buffer + (cinfo.output_scanline) * target_row_stride;
		u8* buffer_array[1] = { output_pos };
		i32 ret = jpeg_read_scanlines(&cinfo, buffer_array, 1);
	}

	if (width) *width = cinfo.output_width;
	if (height) *height = cinfo.output_height;
	if (channels_in_file) *channels_in_file = cinfo.output_components;

	(void) jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	return output_buffer;
}

EMSCRIPTEN_KEEPALIVE
uint8_t *create_buffer(int size) {
	return libc_malloc(size * sizeof(uint8_t));
}

EMSCRIPTEN_KEEPALIVE
void destroy_buffer(uint8_t *p) {
	free(p);
}

void jpeg_encode_tile(u8* pixels, i32 width, i32 height, i32 quality, u8** tables_buffer, u32* tables_size_ptr,
                      u8** jpeg_buffer, u32* jpeg_size_ptr, bool use_rgb) {
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);

	jpeg_create_compress(&cinfo);
	cinfo.image_width = width;
	cinfo.image_height = height;

	cinfo.input_components = 4;
	cinfo.in_color_space = JCS_EXT_BGRA;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	if (use_rgb) {
		jpeg_set_colorspace(&cinfo, JCS_RGB);
	}

	if (tables_buffer) {
		jpeg_mem_dest(&cinfo, tables_buffer, (unsigned long*) tables_size_ptr); // libjpeg-turbo will allocate the buffer
		jpeg_write_tables(&cinfo);
	} else {
		jpeg_suppress_tables(&cinfo, TRUE);
	}

	if (jpeg_buffer) {
		jpeg_mem_dest(&cinfo, jpeg_buffer, (unsigned long*) jpeg_size_ptr); // libjpeg-turbo will allocate the buffer
		jpeg_start_compress(&cinfo, FALSE);

		i32 row_stride = width * cinfo.input_components;
		JSAMPROW row_pointer[1];
		while (cinfo.next_scanline < cinfo.image_height) {
			row_pointer[0] = pixels + (cinfo.next_scanline * row_stride);
			jpeg_write_scanlines(&cinfo, row_pointer, 1);
		}

		jpeg_finish_compress(&cinfo);
	}

	jpeg_destroy_compress(&cinfo);

}

void jpeg_encode_image(u8* pixels, i32 width, i32 height, i32 quality, u8** jpeg_buffer, u32* jpeg_size_ptr) {
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);

	jpeg_create_compress(&cinfo);
	cinfo.image_width = width;
	cinfo.image_height = height;

	cinfo.input_components = 4;
	cinfo.in_color_space = JCS_EXT_BGRA;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);

	if (jpeg_buffer) {
		jpeg_mem_dest(&cinfo, jpeg_buffer, (unsigned long*) jpeg_size_ptr); // libjpeg-turbo will allocate the buffer
		jpeg_start_compress(&cinfo, TRUE);

		i32 row_stride = width * cinfo.input_components;
		JSAMPROW row_pointer[1];
		while (cinfo.next_scanline < cinfo.image_height) {
			row_pointer[0] = pixels + (cinfo.next_scanline * row_stride);
			jpeg_write_scanlines(&cinfo, row_pointer, 1);
		}

		jpeg_finish_compress(&cinfo);
	}

	jpeg_destroy_compress(&cinfo);
}

