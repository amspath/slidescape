/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2021  Pieter Valkema

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

/*
  Decoder for whole-slide image files in iSyntax format.

  This implementation is based on the documentation on the iSyntax format released by Philips:
  https://www.openpathology.philips.com/isyntax/

  See the following documents, and the accompanying source code samples:
  - "Fast Compression Method for Medical Images on the Web", by Bas Hulsken
    https://arxiv.org/abs/2005.08713
  - The description of the iSyntax image files:
    https://www.openpathology.philips.com/wp-content/uploads/isyntax/4522%20207%2043941_2020_04_24%20Pathology%20iSyntax%20image%20format.pdf

  This implementation does not require the Philips iSyntax SDK.
*/


#include "common.h"
#include "platform.h"
#include "intrinsics.h"

#include "isyntax.h"

#include "yxml.h"

#include "jpeg_decoder.h"
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <ctype.h>

// TODO: Implement recursive inverse wavelet transform
// TODO: Improve performance and stability
// TODO: Add ICC profiles support

#define PER_LEVEL_PADDING 3

#include "isyntax_dwt.c"


// Base64 decoder by Jouni Malinen, original:
// http://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c
// Performance comparison of base64 encoders/decoders:
// https://github.com/gaspardpetit/base64/

/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
static const unsigned char base64_table[65] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

unsigned char * base64_decode(const unsigned char *src, size_t len,
                              size_t *out_len)
{
	unsigned char dtable[256], *out, *pos, block[4], tmp;
	size_t i, count, olen;
	int pad = 0;

	memset(dtable, 0x80, 256);
	for (i = 0; i < sizeof(base64_table) - 1; i++)
		dtable[base64_table[i]] = (unsigned char) i;
	dtable['='] = 0;

	count = 0;
	for (i = 0; i < len; i++) {
		if (dtable[src[i]] != 0x80)
			count++;
	}

	if (count == 0 || count % 4)
		return NULL;

	olen = count / 4 * 3;
	pos = out = malloc(olen);
	if (out == NULL)
		return NULL;

	count = 0;
	for (i = 0; i < len; i++) {
		tmp = dtable[src[i]];
		if (tmp == 0x80)
			continue;

		if (src[i] == '=')
			pad++;
		block[count] = tmp;
		count++;
		if (count == 4) {
			*pos++ = (block[0] << 2) | (block[1] >> 4);
			*pos++ = (block[1] << 4) | (block[2] >> 2);
			*pos++ = (block[2] << 6) | block[3];
			count = 0;
			if (pad) {
				if (pad == 1)
					pos--;
				else if (pad == 2)
					pos -= 2;
				else {
					/* Invalid padding */
					free(out);
					return NULL;
				}
				break;
			}
		}
	}

	*out_len = pos - out;
	return out;
}
// end of base64 decoder.

// similar to atoi(), but also returning the string position so we can chain calls one after another.
static const char* atoi_and_advance(const char* str, i32* dest) {
	i32 num = 0;
	bool neg = false;
	while (isspace(*str)) ++str;
	if (*str == '-') {
		neg = true;
		++str;
	}
	while (isdigit(*str)) {
		num = 10*num + (*str - '0');
		++str;
	}
	if (neg) num = -num;
	*dest = num;
	return str;
}

static void parse_three_integers(const char* str, i32* first, i32* second, i32* third) {
	str = atoi_and_advance(str, first);
	str = atoi_and_advance(str, second);
	atoi_and_advance(str, third);
}

void isyntax_decode_base64_embedded_jpeg_file(isyntax_t* isyntax) {
	// stub
}

void isyntax_parse_ufsimport_child_node(isyntax_t* isyntax, u32 group, u32 element, char* value, u64 value_len) {

	switch(group) {
		default: {
			// unknown group
			console_print_verbose("Unknown group 0x%04x\n", group);
		} break;
		case 0x0008: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x002A: /*DICOM_ACQUISITION_DATETIME*/     {} break; // "20210101103030.000000"
				case 0x0070: /*DICOM_MANUFACTURER*/             {} break; // "PHILIPS"
				case 0x1090: /*DICOM_MANUFACTURERS_MODEL_NAME*/ {} break; // "UFS Scanner"
			}
		}; break;
		case 0x0018: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1000: /*DICOM_DEVICE_SERIAL_NUMBER*/     {} break; // "FMT<4-digit number>"
				case 0x1020: /*DICOM_SOFTWARE_VERSIONS*/        {} break; // "<versionnumber>" "<versionnumber>"
				case 0x1200: /*DICOM_DATE_OF_LAST_CALIBRATION*/ {} break; // "20210101"
				case 0x1201: /*DICOM_TIME_OF_LAST_CALIBRATION*/ {} break; // "100730"
			}
		} break;
		case 0x101D: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1007: /*PIIM_DP_SCANNER_RACK_NUMBER*/        {} break; // "[1..15]"
				case 0x1008: /*PIIM_DP_SCANNER_SLOT_NUMBER*/        {} break; // "[1..15]"
				case 0x1009: /*PIIM_DP_SCANNER_OPERATOR_ID*/        {} break; // "<Operator ID>"
				case 0x100A: /*PIIM_DP_SCANNER_CALIBRATION_STATUS*/ {} break; // "OK" or "NOT OK"

			}
		} break;
		case 0x301D: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1001: /*PIM_DP_UFS_INTERFACE_VERSION*/          {} break; // "5.0"
				case 0x1002: /*PIM_DP_UFS_BARCODE*/                    {} break; // "<base64-encoded barcode value>"
				case 0x1003: /*PIM_DP_SCANNED_IMAGES*/                    {

				} break;
				case 0x1010: /*PIM_DP_SCANNER_RACK_PRIORITY*/               {} break; // "<u16>"

			}
		} break;
	}
}

void isyntax_parse_scannedimage_child_node(isyntax_t* isyntax, u32 group, u32 element, char* value, u64 value_len) {

	// Parse metadata belong to one of the images in the file (either a WSI, LABELIMAGE or MACROIMAGE)

	isyntax_image_t* image = isyntax->parser.current_image;
	if (!image) {
		image = isyntax->parser.current_image = &isyntax->images[0];
	}

	switch(group) {
		default: {
			// unknown group
			console_print_verbose("Unknown group 0x%04x\n", group);
		} break;
		case 0x0008: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x2111: /*DICOM_DERIVATION_DESCRIPTION*/   {         // "PHILIPS UFS V%s | Quality=%d | DWT=%d | Compressor=%d"

				} break;
			}
		}; break;
		case 0x0028: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x0002: /*DICOM_SAMPLES_PER_PIXEL*/     {} break;
				case 0x0100: /*DICOM_BITS_ALLOCATED*/     {} break;
				case 0x0101: /*DICOM_BITS_STORED*/     {} break;
				case 0x0102: /*DICOM_HIGH_BIT*/     {} break;
				case 0x0103: /*DICOM_PIXEL_REPRESENTATION*/     {} break;
				case 0x2000: /*DICOM_ICCPROFILE*/     {} break;
				case 0x2110: /*DICOM_LOSSY_IMAGE_COMPRESSION*/     {} break;
				case 0x2112: /*DICOM_LOSSY_IMAGE_COMPRESSION_RATIO*/     {} break;
				case 0x2114: /*DICOM_LOSSY_IMAGE_COMPRESSION_METHOD*/     {} break; // "PHILIPS_DP_1_0"
			}
		} break;
		case 0x301D: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1004: /*PIM_DP_IMAGE_TYPE*/                     {         // "MACROIMAGE" or "LABELIMAGE" or "WSI"
					if ((strcmp(value, "MACROIMAGE") == 0)) {
						isyntax->macro_image_index = isyntax->parser.running_image_index;
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
						image->image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
					} else if ((strcmp(value, "LABELIMAGE") == 0)) {
						isyntax->label_image_index = isyntax->parser.running_image_index;
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
						image->image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
					} else if ((strcmp(value, "WSI") == 0)) {
						isyntax->wsi_image_index = isyntax->parser.running_image_index;
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_WSI;
						image->image_type = ISYNTAX_IMAGE_TYPE_WSI;
					}
				} break;
				case 0x1005: { /*PIM_DP_IMAGE_DATA*/
					size_t decoded_capacity = value_len;
					size_t decoded_len = 0;
					i32 last_char = value[value_len-1];
					if (last_char == '/') {
						value_len--; // The last character may cause the base64 decoding to fail if invalid
					}
					u8* decoded = base64_decode((u8*)value, value_len, &decoded_len);
					if (decoded) {
						i32 channels_in_file = 0;
#if 0
						// TODO: Why does this crash?
						image->pixels = jpeg_decode_image(decoded, decoded_len, &image->width, &image->height, &channels_in_file);
#else
						// stb_image.h
						image->pixels = stbi_load_from_memory(decoded, decoded_len, &image->width, &image->height, &channels_in_file, 4);
#endif
						free(decoded);
						// TODO: actually display the image
						DUMMY_STATEMENT;
					}
				} break;
				case 0x1013: /*DP_COLOR_MANAGEMENT*/                        {} break;
				case 0x1014: /*DP_IMAGE_POST_PROCESSING*/                   {} break;
				case 0x1015: /*DP_SHARPNESS_GAIN_RGB24*/                    {} break;
				case 0x1016: /*DP_CLAHE_CLIP_LIMIT_Y16*/                    {} break;
				case 0x1017: /*DP_CLAHE_NR_BINS_Y16*/                       {} break;
				case 0x1018: /*DP_CLAHE_CONTEXT_DIMENSION_Y16*/             {} break;
				case 0x1019: /*DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR*/    {} break;
				case 0x101A: /*DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL*/    {} break;
				case 0x101B: /*DP_WAVELET_QUANTIZER*/                       {} break;
				case 0x101C: /*DP_WAVELET_DEADZONE*/                        {} break;
				case 0x2000: /*UFS_IMAGE_GENERAL_HEADERS*/                  {} break;
				case 0x2001: /*UFS_IMAGE_NUMBER_OF_BLOCKS*/                 {} break;
				case 0x2002: /*UFS_IMAGE_DIMENSIONS_OVER_BLOCK*/            {} break;
				case 0x2003: /*UFS_IMAGE_DIMENSIONS*/                       {} break;
				case 0x2004: /*UFS_IMAGE_DIMENSION_NAME*/                   {} break;
				case 0x2005: /*UFS_IMAGE_DIMENSION_TYPE*/                   {} break;
				case 0x2006: /*UFS_IMAGE_DIMENSION_UNIT*/                   {} break;
				case 0x2007: /*UFS_IMAGE_DIMENSION_SCALE_FACTOR*/           {
					float mpp = atof(value);
					if (isyntax->parser.dimension_index == 0 /*x*/) {
						isyntax->mpp_x = mpp;
					} else if (isyntax->parser.dimension_index == 1 /*y*/) {
						isyntax->mpp_y = mpp;
					}
				} break;
				case 0x2008: /*UFS_IMAGE_DIMENSION_DISCRETE_VALUES_STRING*/ {} break;
				case 0x2009: /*UFS_IMAGE_BLOCK_HEADER_TEMPLATES*/           {} break;
				case 0x200A: /*UFS_IMAGE_DIMENSION_RANGES*/                 {} break;
				case 0x200B: /*UFS_IMAGE_DIMENSION_RANGE*/                  {
					isyntax_image_dimension_range_t range = {};
					parse_three_integers(value, &range.start, &range.step, &range.end);
					i32 step_nonzero = (range.step != 0) ? range.step : 1;
					range.numsteps = ((range.end + range.step) - range.start) / step_nonzero;
					if (isyntax->parser.data_object_flags & ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate) {
						isyntax_header_template_t* template = isyntax->header_templates + isyntax->parser.header_template_index;
						switch(isyntax->parser.dimension_index) {
							default: break;
							case 0: template->block_width = range.numsteps; break;
							case 1: template->block_height = range.numsteps; break;
							case 2: template->color_component = range.start; break;
							case 3: template->scale = range.start; break;
							case 4: template->waveletcoeff = (range.start == 0) ? 1 : 3; break;
						}
						DUMMY_STATEMENT;
					} else if (isyntax->parser.data_object_flags & ISYNTAX_OBJECT_UFSImageGeneralHeader) {
						switch(isyntax->parser.dimension_index) {
							default: break;
							case 0: {
								image->offset_x = range.start;
								image->width = range.numsteps;
							} break;
							case 1: {
								image->offset_y = range.start;
								image->height = range.numsteps;
							} break;
							case 2: break; // always 3 color channels ("Y" "Co" "Cg"), no need to check
							case 3: {
								image->level_count = range.numsteps;
								image->max_scale = range.numsteps - 1;
							} break;
							case 4: break; // always 4 wavelet coefficients ("LL" "LH" "HL" "HH"), no need to check
						}
						DUMMY_STATEMENT;
					}
					DUMMY_STATEMENT;
				} break;
				case 0x200C: /*UFS_IMAGE_DIMENSION_IN_BLOCK*/               {} break;
				case 0x200F: /*UFS_IMAGE_BLOCK_COMPRESSION_METHOD*/         {} break;
				case 0x2013: /*UFS_IMAGE_PIXEL_TRANSFORMATION_METHOD*/      {} break;
				case 0x2014: { /*UFS_IMAGE_BLOCK_HEADER_TABLE*/
					size_t decoded_capacity = value_len;
					size_t decoded_len = 0;
					i32 last_char = value[value_len-1];
#if 0
					FILE* test_out = fopen("test_b64.out", "wb");
					fwrite(value, value_len, 1, test_out);
					fclose(test_out);
#endif
					if (last_char == '/') {
						value_len--; // The last character may cause the base64 decoding to fail if invalid
						last_char = value[value_len-1];
					}
					while (last_char == '\n' || last_char == '\r' || last_char == ' ') {
						--value_len;
						last_char = value[value_len-1];
					}
					u8* decoded = base64_decode((u8*)value, value_len, &decoded_len);
					if (decoded) {
						image->block_header_table = decoded;
						image->block_header_size = decoded_len;

						u32 header_size = *(u32*) decoded + 0;
						u8* block_header_start = decoded + 4;
						dicom_tag_header_t sequence_element = *(dicom_tag_header_t*) (block_header_start);
						if (sequence_element.size == 40) {
							// We have a partial header structure, with 'Block Data Offset' and 'Block Size' missing (stored in Seektable)
							// Full block header size (including the sequence element) is 48 bytes
							u32 block_count = header_size / 48;
							u32 should_be_zero = header_size % 48;
							if (should_be_zero != 0) {
								// TODO: handle error condition
								DUMMY_STATEMENT;
							}

							image->codeblock_count = block_count;
							image->codeblocks = calloc(1, block_count * sizeof(isyntax_codeblock_t));
							image->header_codeblocks_are_partial = true;

							for (i32 i = 0; i < block_count; ++i) {
								isyntax_partial_block_header_t* header = ((isyntax_partial_block_header_t*)(block_header_start)) + i;
								isyntax_codeblock_t* codeblock = image->codeblocks + i;
								codeblock->x_coordinate = header->x_coordinate;
								codeblock->y_coordinate = header->y_coordinate;
								codeblock->color_component = header->color_component;
								codeblock->scale = header->scale;
								codeblock->coefficient = header->coefficient;
								codeblock->block_header_template_id = header->block_header_template_id;
								DUMMY_STATEMENT;
							}

						} else if (sequence_element.size == 72) {
							// We have the complete header structure. (Nothing stored in Seektable)
							u32 block_count = header_size / 80;
							u32 should_be_zero = header_size % 80;
							if (should_be_zero != 0) {
								// TODO: handle error condition
								DUMMY_STATEMENT;
							}

							image->codeblock_count = block_count;
							image->codeblocks = calloc(1, block_count * sizeof(isyntax_codeblock_t));
							image->header_codeblocks_are_partial = false;

							for (i32 i = 0; i < block_count; ++i) {
								isyntax_full_block_header_t* header = ((isyntax_full_block_header_t*)(block_header_start)) + i;
								isyntax_codeblock_t* codeblock = image->codeblocks + i;
								codeblock->x_coordinate = header->x_coordinate;
								codeblock->y_coordinate = header->y_coordinate;
								codeblock->color_component = header->color_component;
								codeblock->scale = header->scale;
								codeblock->coefficient = header->coefficient;
								codeblock->block_data_offset = header->block_data_offset; // extra
								codeblock->block_size = header->block_size; // extra
								codeblock->block_header_template_id = header->block_header_template_id;
								DUMMY_STATEMENT;
							}
						} else {
							// TODO: handle error condition
						}

						free(decoded);
					} else {
						//TODO: handle error condition
					}
				} break;

			}
		} break;
	}
}

bool isyntax_validate_dicom_attr(const char* expected, const char* observed) {
	bool ok = (strcmp(expected, observed) == 0);
	if (!ok) {
		console_print("iSyntax validation error: while reading DICOM metadata, expected '%s' but found '%s'\n", expected, observed);
	}
	return ok;
}

void isyntax_parser_init(isyntax_t* isyntax) {
	isyntax_parser_t* parser = &isyntax->parser;

	parser->initialized = true;

	parser->attrbuf_capacity = KILOBYTES(32);
	parser->contentbuf_capacity = MEGABYTES(8);

	parser->current_element_name = "";
	parser->attrbuf = malloc(parser->attrbuf_capacity); // TODO: free
	parser->attrbuf_end = parser->attrbuf + parser->attrbuf_capacity;
	parser->attrcur = NULL;
	parser->attrlen = 0;
	parser->contentbuf = malloc(parser->contentbuf_capacity); // TODO: free
	parser->contentcur = NULL;
	parser->contentlen = 0;

	parser->current_dicom_attribute_name[0] = '\0';
	parser->current_dicom_group_tag = 0;
	parser->current_dicom_element_tag = 0;
	parser->attribute_index = 0;
	parser->current_node_type = ISYNTAX_NODE_NONE;

	// XML parsing using the yxml library.
	// https://dev.yorhel.nl/yxml/man
	size_t yxml_stack_buffer_size = KILOBYTES(32);
	parser->x = (yxml_t*) malloc(sizeof(yxml_t) + yxml_stack_buffer_size);

	yxml_init(parser->x, parser->x + 1, yxml_stack_buffer_size);
}

const char* get_spaces(i32 length) {
	ASSERT(length >= 0);
	static const char spaces[] = "                                  ";
	i32 spaces_len = COUNT(spaces) - 1;
	i32 offset_from_end = MIN(spaces_len, length);
	i32 offset = spaces_len - offset_from_end;
	return spaces + offset;
}

void push_to_buffer_maybe_grow(u8** restrict dest, size_t* restrict dest_len, size_t* restrict dest_capacity, void* restrict src, size_t src_len) {
	ASSERT(dest && dest_len && dest_capacity && src);
	size_t old_len = *dest_len;
	size_t new_len = old_len + src_len;
	size_t capacity = *dest_capacity;
	if (new_len > capacity) {
		capacity = next_pow2(new_len);
		u8* new_ptr = (u8*)realloc(*dest, capacity);
		if (!new_ptr) panic();
		*dest = new_ptr;
		*dest_capacity = capacity;
	}
	memcpy(*dest + old_len, src, src_len);
	*dest_len = new_len;
}

bool isyntax_parse_xml_header(isyntax_t* isyntax, char* xml_header, i64 chunk_length, bool is_last_chunk) {

	yxml_t* x = NULL;
	bool success = false;

	static bool paranoid_mode = true;

	isyntax_parser_t* parser = &isyntax->parser;

	if (!parser->initialized) {
		isyntax_parser_init(isyntax);
	}
	x = parser->x;

	if (0) { failed: cleanup:
		if (x) {
			free(x);
			parser->x = NULL;
		}
		if (parser->attrbuf) {
			free(parser->attrbuf);
			parser->attrbuf = NULL;
		}
		if (parser->contentbuf) {
			free(parser->contentbuf);
			parser->contentbuf = NULL;
		}
		return success;
	}



	// parse XML byte for byte

	char* doc = xml_header;
	for (i64 remaining_length = chunk_length; remaining_length > 0; --remaining_length, ++doc) {
		int c = *doc;
		if (c == '\0') {
			ASSERT(false); // this should never trigger
			break;
		}
		yxml_ret_t r = yxml_parse(x, c);
		if (r == YXML_OK) {
			continue; // nothing worthy of note has happened -> continue
		} else if (r < 0) {
			goto failed;
		} else if (r > 0) {
			// token
			switch(r) {
				case YXML_ELEMSTART: {
					// start of an element: '<Tag ..'
					isyntax_parser_node_t* parent_node = parser->node_stack + parser->node_stack_index;
					++parser->node_stack_index;
					isyntax_parser_node_t* node = parser->node_stack + parser->node_stack_index;
					memset(node, 0, sizeof(isyntax_parser_node_t));
					// Inherit group and element of parent node
					node->group = parent_node->group;
					node->element = parent_node->element;

					parser->contentcur = parser->contentbuf;
					*parser->contentcur = '\0';
					parser->contentlen = 0;
					parser->attribute_index = 0;
					if (strcmp(x->elem, "Attribute") == 0) {
						node->node_type = ISYNTAX_NODE_LEAF;
					} else if (strcmp(x->elem, "DataObject") == 0) {
						node->node_type = ISYNTAX_NODE_BRANCH;
						// push into the data object stack, to keep track of which type of DataObject we are parsing
						// (we need this information to restore state when the XML element ends)
						++parser->data_object_stack_index;
						parser->data_object_stack[parser->data_object_stack_index] = parent_node->element;
						// set relevant flag for which data object type we are now parsing
						u32 flags = parser->data_object_flags;
						switch(parent_node->element) {
							default: break;
							case 0:                                       flags |= ISYNTAX_OBJECT_DPUfsImport; break;
							case PIM_DP_SCANNED_IMAGES:                   flags |= ISYNTAX_OBJECT_DPScannedImage; break;
							case UFS_IMAGE_GENERAL_HEADERS:               flags |= ISYNTAX_OBJECT_UFSImageGeneralHeader; break;
							case UFS_IMAGE_BLOCK_HEADER_TEMPLATES:        flags |= ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate; break;
							case UFS_IMAGE_DIMENSIONS:                    flags |= ISYNTAX_OBJECT_UFSImageDimension; break;
							case UFS_IMAGE_DIMENSION_RANGES:              flags |= ISYNTAX_OBJECT_UFSImageDimensionRange; break;
							case DP_COLOR_MANAGEMENT:                     flags |= ISYNTAX_OBJECT_DPColorManagement; break;
							case DP_IMAGE_POST_PROCESSING:                flags |= ISYNTAX_OBJECT_DPImagePostProcessing; break;
							case DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR: flags |= ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor; break;
							case DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL: flags |= ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel; break;
						}
						parser->data_object_flags = flags;
					} else if (strcmp(x->elem, "Array") == 0) {
						node->node_type = ISYNTAX_NODE_ARRAY;
						console_print_verbose("%sArray\n", get_spaces(parser->node_stack_index));
					} else {
						node->node_type = ISYNTAX_NODE_NONE;
						console_print_verbose("%selement start: %s\n", get_spaces(parser->node_stack_index), x->elem);
					}
					parser->current_node_type = node->node_type;
					parser->current_node_has_children = false;
					parser->current_element_name = x->elem; // We need to remember this pointer, because it may point to something else at the YXML_ELEMEND state

				} break;
				case YXML_CONTENT: {
					// element content
					if (!parser->contentcur) break;

					// Load iSyntax block header table (and other large XML tags) greedily and bypass yxml parsing overhead
					if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
						u32 group = parser->current_dicom_group_tag;
						u32 element = parser->current_dicom_element_tag;
						isyntax_parser_node_t* node = parser->node_stack + parser->node_stack_index;
						node->group = group;
						node->element = element;
						bool need_skip = (group == 0x301D && element == 0x2014) || // UFS_IMAGE_BLOCK_HEADER_TABLE
								         (group == 0x301D && element == 0x1005) || // PIM_DP_IMAGE_DATA
										 (group == 0x0028 && element == 0x2000);   // DICOM_ICCPROFILE

					    if (need_skip) {
					    	parser->node_stack[parser->node_stack_index].has_base64_content = true;
							char* content_start = doc;
							char* pos = (char*)memchr(content_start, '<', remaining_length);
							if (pos) {
								i64 size = pos - content_start;
								push_to_buffer_maybe_grow((u8**)&parser->contentbuf, &parser->contentlen, &parser->contentbuf_capacity, content_start, size);
								parser->contentcur = parser->contentbuf + parser->contentlen;
//								console_print("iSyntax: skipped tag (0x%04x, 0x%04x) content length = %d\n", group, element, size);
								doc += (size-1); // skip to the next tag
								remaining_length -= (size-1);
								break;
							} else {
//								console_print("iSyntax: skipped tag (0x%04x, 0x%04x) content length = %d\n", group, element, remaining_length);
								push_to_buffer_maybe_grow((u8**)&parser->contentbuf, &parser->contentlen, &parser->contentbuf_capacity, content_start, remaining_length);
								parser->contentcur = parser->contentbuf + parser->contentlen;
								remaining_length = 0; // skip to the next chunk
								break;
							}
						}
					}

					char* tmp = x->data;
					while (*tmp && parser->contentlen < parser->contentbuf_capacity) {
						*(parser->contentcur++) = *(tmp++);
						++parser->contentlen;
						// too long content -> resize buffer
						if (parser->contentlen == parser->contentbuf_capacity) {
							size_t new_capacity = parser->contentbuf_capacity * 2;
							char* new_ptr = (char*)realloc(parser->contentbuf, new_capacity);
							if (!new_ptr) panic();
							parser->contentbuf = new_ptr;
							parser->contentcur = parser->contentbuf + parser->contentlen;
							parser->contentbuf_capacity = new_capacity;
//							console_print("isyntax_parse_xml_header(): XML content buffer overflow (resized buffer to %u)\n", new_capacity);
						}
					}

					*parser->contentcur = '\0';
				} break;
				case YXML_ELEMEND: {
					// end of an element: '.. />' or '</Tag>'

					if (parser->current_node_type == ISYNTAX_NODE_LEAF && !parser->current_node_has_children) {
						// Leaf node WITHOUT children.
						// In this case we didn't already parse the attributes at the YXML_ATTREND stage.
						// Now at the YXML_ELEMEND stage we can parse the complete tag at once (attributes + content).
						console_print_verbose("%sDICOM: %-40s (0x%04x, 0x%04x), size:%-8u = %s\n", get_spaces(parser->node_stack_index),
						                      parser->current_dicom_attribute_name,
						                      parser->current_dicom_group_tag, parser->current_dicom_element_tag, parser->contentlen, parser->contentbuf);

#if 0
						if (parser->node_stack[parser->node_stack_index].group == 0) {
							DUMMY_STATEMENT; // probably the group is 0 because this is a top level node.
						}
#endif

						if (parser->node_stack_index == 2) {
							isyntax_parse_ufsimport_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
							                        parser->contentbuf, parser->contentlen);
						} else {
							isyntax_parse_scannedimage_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
							                                   parser->contentbuf, parser->contentlen);
						}
					} else {
						// We have reached the end of a branch or array node, or a leaf node WITH children.
						// In this case, the attributes have already been parsed at the YXML_ATTREND stage.
						// Because their content is not text but child nodes, we do not need to touch the content buffer.
						const char* elem_name = NULL;
						if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
							// End of a leaf node WITH children.
							elem_name = "Attribute";
						} else if (parser->current_node_type == ISYNTAX_NODE_BRANCH) {
							elem_name = "DataObject";
							// pop data object stack
							u16 element = parser->data_object_stack[parser->data_object_stack_index];
							--parser->data_object_stack_index;
							// reset relevant data for the data object type we are now no longer parsing
							u32 flags = parser->data_object_flags;
							switch(element) {
								default: break;
								case 0:                                       flags &= ~ISYNTAX_OBJECT_DPUfsImport; break;
								case PIM_DP_SCANNED_IMAGES:                   flags &= ~ISYNTAX_OBJECT_DPScannedImage; break;
								case UFS_IMAGE_GENERAL_HEADERS: {
									flags &= ~ISYNTAX_OBJECT_UFSImageGeneralHeader;
									parser->dimension_index = 0;
								} break;
								case UFS_IMAGE_BLOCK_HEADER_TEMPLATES: {
									flags &= ~ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate;
									++parser->header_template_index;
									parser->dimension_index = 0;
								} break;
								case UFS_IMAGE_DIMENSIONS:                    flags &= ~ISYNTAX_OBJECT_UFSImageDimension; break;
								case UFS_IMAGE_DIMENSION_RANGES: {
									flags &= ~ISYNTAX_OBJECT_UFSImageDimensionRange;
									++parser->dimension_index;
								} break;
								case DP_COLOR_MANAGEMENT:                     flags &= ~ISYNTAX_OBJECT_DPColorManagement; break;
								case DP_IMAGE_POST_PROCESSING:                flags &= ~ISYNTAX_OBJECT_DPImagePostProcessing; break;
								case DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR: flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor; break;
								case DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL: flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel; break;
							}
							parser->data_object_flags = flags;
						} else if (parser->current_node_type == ISYNTAX_NODE_ARRAY) {
							elem_name = "Array";
						}

						console_print_verbose("%selement end: %s\n", get_spaces(parser->node_stack_index), elem_name);
					}

					// 'Pop' context back to parent node
					if (parser->node_stack_index > 0) {
						--parser->node_stack_index;
						parser->current_node_type = parser->node_stack[parser->node_stack_index].node_type;
						parser->current_node_has_children = parser->node_stack[parser->node_stack_index].has_children;
					} else {
						//TODO: handle error condition
						console_print_error("iSyntax XML error: closing element without matching start\n");
					}

				} break;
				case YXML_ATTRSTART: {
					// attribute: 'Name=..'
//				    console_print_verbose("attr start: %s\n", x->attr);
					parser->attrcur = parser->attrbuf;
					*parser->attrcur = '\0';
					parser->attrlen = 0;
				} break;
				case YXML_ATTRVAL: {
					// attribute value
				    //console_print_verbose("   attr val: %s\n", x->attr);
					if (!parser->attrcur) break;
					char* tmp = x->data;
					while (*tmp && parser->attrbuf < parser->attrbuf_end) {
						*(parser->attrcur++) = *(tmp++);
						++parser->attrlen;
						// too long content -> resize buffer
						if (parser->attrlen == parser->attrbuf_capacity) {
							size_t new_capacity = parser->attrbuf_capacity * 2;
							char* new_ptr = (char*)realloc(parser->attrbuf, new_capacity);
							if (!new_ptr) panic();
							parser->attrbuf = new_ptr;
							parser->attrcur = parser->attrbuf + parser->attrlen;
							parser->attrbuf_capacity = new_capacity;
//							console_print("isyntax_parse_xml_header(): XML attribute buffer overflow (resized buffer to %u)\n", new_capacity);
						}
					}
					*parser->attrcur = '\0';
				} break;
				case YXML_ATTREND: {
					// end of attribute '.."'
					if (parser->attrcur) {
						ASSERT(strlen(parser->attrbuf) == parser->attrlen);

						if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
							if (parser->attribute_index == 0 /* Name="..." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Name");
								size_t copy_size = MIN(parser->attrlen, sizeof(parser->current_dicom_attribute_name));
								memcpy(parser->current_dicom_attribute_name, parser->attrbuf, copy_size);
								i32 one_past_last_char = MIN(parser->attrlen, sizeof(parser->current_dicom_attribute_name)-1);
								parser->current_dicom_attribute_name[one_past_last_char] = '\0';
								DUMMY_STATEMENT;
							} else if (parser->attribute_index == 1 /* Group="0x...." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Group");
								parser->current_dicom_group_tag = strtoul(parser->attrbuf, NULL, 0);
								DUMMY_STATEMENT;
							} else if (parser->attribute_index == 2 /* Element="0x...." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Element");
								parser->current_dicom_element_tag = strtoul(parser->attrbuf, NULL, 0);
								DUMMY_STATEMENT;
							} else if (parser->attribute_index == 3 /* PMSVR="..." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "PMSVR");
								if (strcmp(parser->attrbuf, "IDataObjectArray") == 0) {
									// Leaf node WITH children.
									// Don't wait until YXML_ELEMEND to parse the attributes (this is the only opportunity we have!)
									parser->current_node_has_children = true;
									parser->node_stack[parser->node_stack_index].has_children = true;
									console_print_verbose("%sDICOM: %-40s (0x%04x, 0x%04x), array\n", get_spaces(parser->node_stack_index),
							                              parser->current_dicom_attribute_name,
									                      parser->current_dicom_group_tag, parser->current_dicom_element_tag);
									if (parser->node_stack_index == 2) { // At level of UfsImport
										isyntax_parse_ufsimport_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
										                        parser->contentbuf, parser->contentlen);
									} else {
										isyntax_parse_scannedimage_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
										                                   parser->contentbuf, parser->contentlen);
									}

								}
							}
						} else if (parser->current_node_type == ISYNTAX_NODE_BRANCH) {
							// A DataObject node is supposed to have one attribute "ObjectType"
							ASSERT(parser->attribute_index == 0);
							ASSERT(strcmp(x->attr, "ObjectType") == 0);
							console_print_verbose("%sDataObject %s = %s\n", get_spaces(parser->node_stack_index), x->attr, parser->attrbuf);
							if (strcmp(parser->attrbuf, "DPScannedImage") == 0) {
								// We started parsing a new image (which will be either a WSI, LABELIMAGE or MACROIMAGE).
								parser->current_image = isyntax->images + isyntax->image_count;
								++isyntax->image_count;
							}
						} else {
							console_print_verbose("%sattr %s = %s\n", get_spaces(parser->node_stack_index), x->attr, parser->attrbuf);
						}
						++parser->attribute_index;

					}
				} break;
				case YXML_PISTART:
				case YXML_PICONTENT:
				case YXML_PIEND:
					break; // processing instructions (uninteresting, skip)
				default: {
					console_print_error("yxml_parse(): unrecognized token (%d)\n", r);
					goto failed;
				}
			}
		}
	}

	success = true;
	if (is_last_chunk) {
		goto cleanup;
	} else {
		return success; // no cleanup yet, we will still need resources until the last header chunk is reached.
	}
}


// Convert between signed magnitude and two's complement
// https://stackoverflow.com/questions/21837008/how-to-convert-from-sign-magnitude-to-twos-complement
// N.B. This function is its own inverse (conversion works the other way as well)
static inline i16 signed_magnitude_to_twos_complement_16(u16 x) {
	u16 m = -(x >> 15);
	i16 result = (~m & x) | (((x & (u16)0x8000) - x) & m);
	return result;
}

static inline i32 twos_complement_to_signed_magnitude(u32 x) {
	u32 m = -(x >> 31);
	i32 result = (~m & x) | (((x & 0x80000000) - x) & m);
	return result;
}

static void signed_magnitude_to_twos_complement_16_block(u16* data, u32 len) {
#if defined(__SSE2__)
	// Fast SIMD version
	i32 i;
	for (i = 0; i < len; i += 8) {
		__m128i x = _mm_loadu_si128((__m128i*)(data + i));
		__m128i sign_masks = _mm_srai_epi16(x, 15); // 0x0000 if positive, 0xFFFF if negative
		__m128i maybe_positive = _mm_andnot_si128(sign_masks, x); // (~m & x)
		__m128i value_if_negative = _mm_sub_epi16(_mm_and_si128(x, _mm_set1_epi16(0x8000)), x); // (x & 0x8000) - x
		__m128i maybe_negative = _mm_and_si128(sign_masks, value_if_negative);
		__m128i result = _mm_or_si128(maybe_positive, maybe_negative);
		*(__m128i*)(data + i) = result;
	}
	if (i < len) {
		for (; i < len; ++i) {
			data[i] = signed_magnitude_to_twos_complement_16(data[i]);
		}
	}
#else
	// Slow version
	for (i32 i = 0; i < len; ++i) {
		data[i] = signed_magnitude_to_twos_complement_16(data[i]);
	}
#endif
	ASSERT(i == len);
}

// Convert a block of 16-bit signed integers to their absolute value
// Almost the same as the signed magnitude <-> twos complement conversion, except the sign bit is cleared at the end
static void convert_to_absolute_value_16_block(i16* data, u32 len) {
#if defined(__SSE2__)
	// Fast SIMD version
	i32 i;
	for (i = 0; i < len; i += 8) {
		__m128i x = _mm_loadu_si128((__m128i*)(data + i));
		__m128i sign_masks = _mm_srai_epi16(x, 15); // 0x0000 if positive, 0xFFFF if negative
		__m128i maybe_positive = _mm_andnot_si128(sign_masks, x); // (~m & x)
		__m128i value_if_negative = _mm_sub_epi16(_mm_and_si128(x, _mm_set1_epi16(0x8000)), x); // (x & 0x8000) - x
		__m128i maybe_negative = _mm_and_si128(sign_masks, value_if_negative);
		__m128i result = _mm_or_si128(maybe_positive, maybe_negative);
		result = _mm_and_si128(result, _mm_set1_epi16(0x7FFF)); // x &= 0x7FFF (clear sign bit)
		*(__m128i*)(data + i) = result;
	}
	if (i < len) {
		for (; i < len; ++i) {
			data[i] = signed_magnitude_to_twos_complement_16(data[i]) & 0x7FFF;
		}
	}
#else
		// Slow version
	for (i32 i = 0; i < len; ++i) {
		data[i] = signed_magnitude_to_twos_complement_16(data[i]) & 0x7FFF;
	}
#endif
	ASSERT(i == len);
}


void debug_convert_wavelet_coefficients_to_image2(icoeff_t* coefficients, i32 width, i32 height, const char* filename) {
	if (coefficients) {
		u8* decoded_8bit = (u8*)malloc(width*height);
		for (i32 i = 0; i < width * height; ++i) {
			u16 magnitude = 0x7FFF & (u16)twos_complement_to_signed_magnitude(coefficients[i]);
			decoded_8bit[i] = ATMOST(255, magnitude);
		}

		stbi_write_png(filename, width, height, 1, decoded_8bit, width);
		free(decoded_8bit);
	}
}

#if (DWT_COEFF_BITS==16)
static u32 wavelet_coefficient_to_color_value(icoeff_t coefficient) {
	u32 magnitude = ((u32)signed_magnitude_to_twos_complement_16(coefficient) & ~0x8000);
	return magnitude;
}
#else
static u32 wavelet_coefficient_to_color_value(icoeff_t coefficient) {
	u32 magnitude = ((u32)twos_complement_to_signed_magnitude(coefficient) & ~0x80000000);
	return magnitude;
}
#endif

static rgba_t ycocg_to_rgb(i32 Y, i32 Co, i32 Cg) {
	i32 tmp = Y - Cg/2;
	i32 G = tmp + Cg;
	i32 B = tmp - Co/2;
	i32 R = B + Co;
	return (rgba_t){ATMOST(255, R), ATMOST(255, G), ATMOST(255, B), 255};
}

static rgba_t ycocg_to_bgr(i32 Y, i32 Co, i32 Cg) {
	i32 tmp = Y - Cg/2;
	i32 G = tmp + Cg;
	i32 B = tmp - Co/2;
	i32 R = B + Co;
	return (rgba_t){ATMOST(255, B), ATMOST(255, G), ATMOST(255, R), 255};
}

void isyntax_wavelet_coefficients_to_rgb_tile(rgba_t* dest, icoeff_t* Y_coefficients, icoeff_t* Co_coefficients, icoeff_t* Cg_coefficients, i32 pixel_count) {
	for (i32 i = 0; i < pixel_count; ++i) {
		i32 Y = wavelet_coefficient_to_color_value(Y_coefficients[i]);
		dest[i] = ycocg_to_rgb(Y, Co_coefficients[i], Cg_coefficients[i]);
	}
}

void isyntax_wavelet_coefficients_to_bgr_tile(rgba_t* dest, icoeff_t* Y_coefficients, icoeff_t* Co_coefficients, icoeff_t* Cg_coefficients, i32 pixel_count) {
	for (i32 i = 0; i < pixel_count; ++i) {
		i32 Y = wavelet_coefficient_to_color_value(Y_coefficients[i]);
		dest[i] = ycocg_to_bgr(Y, Co_coefficients[i], Cg_coefficients[i]);
	}
}

#define DEBUG_OUTPUT_IDWT_STEPS_AS_PNG 0

void isyntax_idwt(icoeff_t* idwt, i32 quadrant_width, i32 quadrant_height, bool output_steps_as_png, const char* png_name) {
	i32 full_width = quadrant_width * 2;
	i32 full_height= quadrant_height * 2;
	i32 idwt_stride = full_width;

	if (output_steps_as_png) {
		char filename[512];
		snprintf(filename, sizeof(filename), "%s_step0.png", png_name);
		debug_convert_wavelet_coefficients_to_image2(idwt, full_width, full_height, filename);
	}

	// Horizontal pass
	opj_dwt_t h = {};
	size_t dwt_mem_size = (MAX(quadrant_width, quadrant_height)*2) * PARALLEL_COLS_53 * sizeof(icoeff_t);
	h.mem = (icoeff_t*)malloc(dwt_mem_size); // need _aligned_malloc? //TODO: use temporary memory
	h.sn = quadrant_width; // number of elements in low pass band
	h.dn = quadrant_width; // number of elements in high pass band
	h.cas = 1;

	for (i32 y = 0; y < full_height; ++y) {
		icoeff_t* input_row = idwt + y * idwt_stride;
		opj_idwt53_h(&h, input_row);
	}

	if (output_steps_as_png) {
		char filename[512];
		snprintf(filename, sizeof(filename), "%s_step1.png", png_name);
		debug_convert_wavelet_coefficients_to_image2(idwt, full_width, full_height, filename);
	}

	// Vertical pass
	opj_dwt_t v = {};
	v.mem = h.mem;
	v.sn = quadrant_height; // number of elements in low pass band
	v.dn = quadrant_height; // number of elements in high pass band
	v.cas = 1;

	i32 x;
	i32 last_x = full_width;
	for (x = 0; x + PARALLEL_COLS_53 <= last_x; x += PARALLEL_COLS_53) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, PARALLEL_COLS_53);
	}
	if (x < last_x) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, (last_x - x));
	}

	if (output_steps_as_png) {
		char filename[512];
		snprintf(filename, sizeof(filename), "%s_step2.png", png_name);
		debug_convert_wavelet_coefficients_to_image2(idwt, full_width, full_height, filename);
	}

	free(h.mem); // need _aligned_free?
}

static inline void get_offsetted_coeff_blocks(icoeff_t** ll_hl_lh_hh, i32 offset, isyntax_tile_channel_t* color_channel, i32 block_stride, icoeff_t* black_dummy_coeff, icoeff_t* white_dummy_coeff) {
	if (color_channel->coeff_ll) {
		ll_hl_lh_hh[0] = color_channel->coeff_ll + offset; //ll
	} else {
		ll_hl_lh_hh[0] = white_dummy_coeff;
	}
	if (color_channel->coeff_h) {
		ll_hl_lh_hh[1] = color_channel->coeff_h + offset; //hl
		ll_hl_lh_hh[2] = color_channel->coeff_h + block_stride + offset; //lh
		ll_hl_lh_hh[3] = color_channel->coeff_h + 2*block_stride + offset; //hh
	} else {
		ll_hl_lh_hh[1] = black_dummy_coeff;
		ll_hl_lh_hh[2] = black_dummy_coeff;
		ll_hl_lh_hh[3] = black_dummy_coeff;
	}

}



u32 isyntax_get_adjacent_tiles_mask(isyntax_level_t* level, i32 tile_x, i32 tile_y) {
	ASSERT(tile_x >= 0 && tile_y >= 0);
	ASSERT(tile_x < level->width_in_tiles && tile_y < level->height_in_tiles);
	// 9 bits, corresponding to the surrounding tiles:
	// 0x100 | 0x80 | 0x40
	// 0x20  | 0x10 | 8
	// 4     | 2    | 1
	u32 adj_tiles = 0x1FF; // all bits set
	if (tile_y == 0)                        adj_tiles &= ~(ISYNTAX_ADJ_TILE_TOP_LEFT | ISYNTAX_ADJ_TILE_TOP_CENTER | ISYNTAX_ADJ_TILE_TOP_RIGHT);
	if (tile_y == level->height_in_tiles-1) adj_tiles &= ~(ISYNTAX_ADJ_TILE_BOTTOM_LEFT | ISYNTAX_ADJ_TILE_BOTTOM_CENTER | ISYNTAX_ADJ_TILE_BOTTOM_RIGHT);
	if (tile_x == 0)                        adj_tiles &= ~(ISYNTAX_ADJ_TILE_TOP_LEFT | ISYNTAX_ADJ_TILE_CENTER_LEFT | ISYNTAX_ADJ_TILE_BOTTOM_LEFT);
	if (tile_x == level->width_in_tiles-1)  adj_tiles &= ~(ISYNTAX_ADJ_TILE_TOP_RIGHT | ISYNTAX_ADJ_TILE_CENTER_RIGHT | ISYNTAX_ADJ_TILE_BOTTOM_RIGHT);
	return adj_tiles;
}

u32 isyntax_get_adjacent_tiles_mask_with_missing_ll_coeff(isyntax_level_t* level, i32 tile_x, i32 tile_y) {
	u32 adjacent = isyntax_get_adjacent_tiles_mask(level, tile_x, tile_y);
	u32 mask = 0;
	if (adjacent & ISYNTAX_ADJ_TILE_TOP_LEFT) {
		isyntax_tile_t* tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x-1);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_TOP_LEFT;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_TOP_CENTER) {
		isyntax_tile_t* tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_TOP_CENTER;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_TOP_RIGHT) {
		isyntax_tile_t* tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x+1);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_TOP_RIGHT;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_CENTER_LEFT) {
		isyntax_tile_t* tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x-1);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_CENTER_LEFT;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_CENTER) {
		isyntax_tile_t* tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_CENTER;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_CENTER_RIGHT) {
		isyntax_tile_t* tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x+1);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_CENTER_RIGHT;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_BOTTOM_LEFT) {
		isyntax_tile_t* tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x-1);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_BOTTOM_LEFT;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_BOTTOM_CENTER) {
		isyntax_tile_t* tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_BOTTOM_CENTER;
	}
	if (adjacent & ISYNTAX_ADJ_TILE_BOTTOM_RIGHT) {
		isyntax_tile_t* tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x+1);
		if (!tile->has_ll) mask |= ISYNTAX_ADJ_TILE_BOTTOM_RIGHT;
	}
	return mask;
}

icoeff_t* isyntax_idwt_tile_for_color_channel(isyntax_t* isyntax, isyntax_image_t* wsi, i32 scale, i32 tile_x, i32 tile_y, i32 color) {
	isyntax_level_t* level = wsi->levels + scale;
	ASSERT(tile_x >= 0 && tile_x < level->width_in_tiles);
	ASSERT(tile_y >= 0 && tile_y < level->height_in_tiles);
	isyntax_tile_t* tile = level->tiles + tile_y * level->width_in_tiles + tile_x;
	isyntax_tile_channel_t* channel = tile->color_channels + color;

	u32 adj_tiles = isyntax_get_adjacent_tiles_mask(level, tile_x, tile_y);

	ASSERT(channel->neighbors_loaded == adj_tiles);
	if (channel->neighbors_loaded == adj_tiles) {

		// Prepare for stitching together the input image, with margins sampled from adjacent tiles for each quadrant
		i32 pad_l = ISYNTAX_IDWT_PAD_L;
		i32 pad_r = ISYNTAX_IDWT_PAD_R;
		i32 pad_l_plus_r = pad_l + pad_r;
//		ASSERT(sizeof(icoeff_t) * pad_amount == sizeof(u64)); // blit 64 bits == 4 pixels
		i32 block_width = isyntax->block_width;
		i32 block_height = isyntax->block_width;
		i32 quadrant_width = block_width + pad_l_plus_r;
		i32 quadrant_height = block_height + pad_l_plus_r;
		i32 full_width = 2 * quadrant_width;
		i32 full_height = 2 * quadrant_height;
		size_t idwt_buffer_size = full_width * full_height * sizeof(icoeff_t);
		icoeff_t* idwt = (icoeff_t*) calloc(1, idwt_buffer_size);

		i32 dest_stride = full_width;

		// fill upper left quadrant with white
		if (color == 0) {
			for (i32 x = 0; x < quadrant_width; ++x) {
				idwt[x] = 255;
			}
			for (i32 y = 1; y < quadrant_width; ++y) {
				memcpy(idwt + y * dest_stride, idwt, quadrant_width * sizeof(icoeff_t));
			}
		}
		icoeff_t* h_dummy_coeff = isyntax->black_dummy_coeff;
		icoeff_t* ll_dummy_coeff = (color == 0) ? isyntax->white_dummy_coeff : isyntax->black_dummy_coeff;


		i32 source_stride = block_width;
		i32 left_margin_source_x = block_width - pad_r;
		i32 top_margin_source_y = block_height - pad_r;
		size_t row_copy_size = block_width * sizeof(icoeff_t);
		size_t pad_l_copy_size = pad_l * sizeof(icoeff_t);
		size_t pad_r_copy_size = pad_r * sizeof(icoeff_t);

		i32 block_stride = block_width * block_height;
		i32 quadrant_offsets[4] = {0, quadrant_width, full_width * quadrant_height, full_width * quadrant_height + quadrant_width};
		icoeff_t* quadrants[4] = {};
		for (i32 i = 0; i < 4; ++i) {
			quadrants[i] = idwt + quadrant_offsets[i];
		}

		icoeff_t* ll_hl_lh_hh[4] = {};

		// Now do the stitching, with margins sampled from adjacent tiles for each quadrant
		// LL | HL
		// LH | HH

		// top left corner
		if (adj_tiles & ISYNTAX_ADJ_TILE_TOP_LEFT) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x-1);
			get_offsetted_coeff_blocks(ll_hl_lh_hh, (top_margin_source_y * source_stride) + left_margin_source_x,
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);

			for (i32 i = 0; i < 4; ++i) {
				icoeff_t* source = ll_hl_lh_hh[i];
				icoeff_t* dest = quadrants[i];
				for (i32 y = 0; y < pad_l; ++y) {
					memcpy(dest, source, pad_l_copy_size);
					source += source_stride;
					dest += dest_stride;
				}
			}

		}
		// top center
		if (adj_tiles & ISYNTAX_ADJ_TILE_TOP_CENTER) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y-1) * level->width_in_tiles + tile_x;
			get_offsetted_coeff_blocks(ll_hl_lh_hh, (top_margin_source_y * source_stride),
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + pad_l;
				for (i32 y = 0; y < pad_l; ++y) {
					memcpy(dest, source, row_copy_size);
					source += source_stride;
					dest += dest_stride;
				}
			}

		}
		// top right corner
		if (adj_tiles & ISYNTAX_ADJ_TILE_TOP_RIGHT) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y-1) * level->width_in_tiles + (tile_x+1);
			get_offsetted_coeff_blocks(ll_hl_lh_hh, (top_margin_source_y * source_stride),
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + pad_l + block_width;
				for (i32 y = 0; y < pad_l; ++y) {
					memcpy(dest, source, pad_r_copy_size);
					source += source_stride;
					dest += dest_stride;
				}
			}

		}
		// center left
		if (adj_tiles & ISYNTAX_ADJ_TILE_CENTER_LEFT) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x-1);
			get_offsetted_coeff_blocks(ll_hl_lh_hh, left_margin_source_x,
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + (pad_l * dest_stride);
				for (i32 y = 0; y < block_height; ++y) {
					memcpy(dest, source, pad_l_copy_size);
					dest += dest_stride;
					source += source_stride;
				}
			}

		}
		// center (main tile)
		if (adj_tiles & ISYNTAX_ADJ_TILE_CENTER) {
			get_offsetted_coeff_blocks(ll_hl_lh_hh, 0,
			                           channel, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + (pad_l * dest_stride) + pad_l;
				for (i32 y = 0; y < block_height; ++y) {
					memcpy(dest, source, row_copy_size);
					dest += dest_stride;
					source += source_stride;
				}
			}

		}
		// center right
		if (adj_tiles & ISYNTAX_ADJ_TILE_CENTER_RIGHT) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y) * level->width_in_tiles + (tile_x+1);
			get_offsetted_coeff_blocks(ll_hl_lh_hh, 0,
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + (pad_l * dest_stride) + pad_l + block_width;
				for (i32 y = 0; y < block_height; ++y) {
					memcpy(dest, source, pad_r_copy_size);
					dest += dest_stride;
					source += source_stride;
				}
			}

		}
		// bottom left corner
		if (adj_tiles & ISYNTAX_ADJ_TILE_BOTTOM_LEFT) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x-1);
			get_offsetted_coeff_blocks(ll_hl_lh_hh, left_margin_source_x,
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + ((pad_l + block_height) * dest_stride);
				for (i32 y = 0; y < pad_r; ++y) {
					memcpy(dest, source, pad_l_copy_size);
					source += source_stride;
					dest += dest_stride;
				}
			}

		}
		// bottom center
		if (adj_tiles & ISYNTAX_ADJ_TILE_BOTTOM_CENTER) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y+1) * level->width_in_tiles + tile_x;
			get_offsetted_coeff_blocks(ll_hl_lh_hh, 0,
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + ((pad_l + block_height) * dest_stride) + pad_l;
				for (i32 y = 0; y < pad_r; ++y) {
					memcpy(dest, source, row_copy_size);
					source += source_stride;
					dest += dest_stride;
				}
			}

		}
		// bottom right corner
		if (adj_tiles & ISYNTAX_ADJ_TILE_BOTTOM_RIGHT) {
			isyntax_tile_t* source_tile = level->tiles + (tile_y+1) * level->width_in_tiles + (tile_x+1);
			get_offsetted_coeff_blocks(ll_hl_lh_hh, 0,
			                           source_tile->color_channels + color, block_stride, h_dummy_coeff, ll_dummy_coeff);
			for (i32 i = 0; i < 4; ++i) {
				icoeff_t *source = ll_hl_lh_hh[i];
				icoeff_t *dest = quadrants[i] + ((pad_l + block_height) * dest_stride) + pad_l + block_width;
				for (i32 y = 0; y < pad_r; ++y) {
					memcpy(dest, source, pad_r_copy_size);
					source += source_stride;
					dest += dest_stride;
				}
			}

		}

		bool output_pngs = false;
		const char* debug_png = "debug_idwt_";
		/*
		if (scale == wsi->max_scale && tile_x == 1 && tile_y == 1 && color == 0) {
			output_pngs = true;
		}*/
		isyntax_idwt(idwt, quadrant_width, quadrant_height, output_pngs, debug_png);

		return idwt;
	}
	return NULL;
}

u32* isyntax_load_tile(isyntax_t* isyntax, isyntax_image_t* wsi, i32 scale, i32 tile_x, i32 tile_y) {
	isyntax_level_t* level = wsi->levels + scale;
	ASSERT(tile_x >= 0 && tile_x < level->width_in_tiles);
	ASSERT(tile_y >= 0 && tile_y < level->height_in_tiles);
	isyntax_tile_t* tile = level->tiles + tile_y * level->width_in_tiles + tile_x;
	i32 block_width = isyntax->block_width;
	i32 block_height = isyntax->block_height;
	size_t block_size = block_width * block_height * sizeof(icoeff_t);
	i32 first_valid_pixel = ISYNTAX_IDWT_FIRST_VALID_PIXEL;
	i32 idwt_width = 2 * (block_width + ISYNTAX_IDWT_PAD_L + ISYNTAX_IDWT_PAD_R);
	i32 idwt_height = 2 * (block_height + ISYNTAX_IDWT_PAD_L + ISYNTAX_IDWT_PAD_R);
	i32 idwt_stride = idwt_width;
	size_t row_copy_size = block_width * sizeof(icoeff_t);

	// TODO: check that all codeblocks are loaded

	icoeff_t* Y = NULL;
	icoeff_t* Co = NULL;
	icoeff_t* Cg = NULL;

	float elapsed_idwt = 0.0f;

	for (i32 color = 0; color < 3; ++color) {
		i64 start_idwt = get_clock();
		icoeff_t* idwt = isyntax_idwt_tile_for_color_channel(isyntax, wsi, scale, tile_x, tile_y, color);
		elapsed_idwt += get_seconds_elapsed(start_idwt, get_clock());
		ASSERT(idwt);
		switch(color) {
			case 0: Y = idwt; break;
			case 1: Co = idwt; break;
			case 2: Cg = idwt; break;
			default: break;
		}

		// Distribute result to child tiles
		if (scale > 0) {
			isyntax_level_t* next_level = wsi->levels + (scale - 1);
			isyntax_tile_t* child_top_left = next_level->tiles + (tile_y*2) * next_level->width_in_tiles + (tile_x*2);
			isyntax_tile_t* child_top_right = child_top_left + 1;
			isyntax_tile_t* child_bottom_left = child_top_left + next_level->width_in_tiles;
			isyntax_tile_t* child_bottom_right = child_bottom_left + 1;

			ASSERT(child_top_left->color_channels[color].coeff_ll == NULL);
			ASSERT(child_top_right->color_channels[color].coeff_ll == NULL);
			ASSERT(child_bottom_left->color_channels[color].coeff_ll == NULL);
			ASSERT(child_bottom_right->color_channels[color].coeff_ll == NULL);
			child_top_left->color_channels[color].coeff_ll = (icoeff_t*)malloc(block_size);
			child_top_right->color_channels[color].coeff_ll = (icoeff_t*)malloc(block_size);
			child_bottom_left->color_channels[color].coeff_ll = (icoeff_t*)malloc(block_size);
			child_bottom_right->color_channels[color].coeff_ll = (icoeff_t*)malloc(block_size);

			i32 dest_stride = block_width;
			// Blit top left child LL block
			{
				icoeff_t* dest = child_top_left->color_channels[color].coeff_ll;
				icoeff_t* source = idwt + (first_valid_pixel * idwt_stride) + first_valid_pixel;
				for (i32 y = 0; y < block_height; ++y) {
					memcpy(dest, source, row_copy_size);
					dest += dest_stride;
					source += idwt_stride;
				}
			}
			// Blit top right child LL block
			{
				icoeff_t* dest = child_top_right->color_channels[color].coeff_ll;
				icoeff_t* source = idwt + (first_valid_pixel * idwt_stride) + first_valid_pixel + block_width;
				for (i32 y = 0; y < block_height; ++y) {
					memcpy(dest, source, row_copy_size);
					dest += dest_stride;
					source += idwt_stride;
				}
			}
			// Blit bottom left child LL block
			{
				icoeff_t* dest = child_bottom_left->color_channels[color].coeff_ll;
				icoeff_t* source = idwt + ((first_valid_pixel + block_height) * idwt_stride) + first_valid_pixel;
				for (i32 y = 0; y < block_height; ++y) {
					memcpy(dest, source, row_copy_size);
					dest += dest_stride;
					source += idwt_stride;
				}
			}
			// Blit bottom right child LL block
			{
				icoeff_t* dest = child_bottom_right->color_channels[color].coeff_ll;
				icoeff_t* source = idwt + ((first_valid_pixel + block_height) * idwt_stride) + first_valid_pixel + block_width;
				for (i32 y = 0; y < block_height; ++y) {
					memcpy(dest, source, row_copy_size);
					dest += dest_stride;
					source += idwt_stride;
				}
			}

			if (color == 2) {
				child_top_left->has_ll = true;
				child_top_right->has_ll = true;
				child_bottom_left->has_ll = true;
				child_bottom_right->has_ll = true;
			}
		}
	}
	tile->is_loaded = true; // Meaning: it is now safe to start loading 'child' tiles of the next level

	// For the Y (luminance) color channel, we actually need the absolute value of the Y-channel wavelet coefficient.
	// (This doesn't hold for Co and Cg, those are are used directly as signed integers)
	convert_to_absolute_value_16_block(Y, idwt_width * idwt_height);

	// Reconstruct RGB image from separate color channels while cutting off margins
	i64 start = get_clock();
	i32 tile_width = block_width * 2;
	i32 tile_height = block_height * 2;
	u32* bgra = (u32*)malloc(tile_width * tile_height * sizeof(u32));
	for (i32 y = 0; y < tile_height; ++y) {
		i32 row_offset = ((first_valid_pixel + y) * idwt_stride) + first_valid_pixel;
		icoeff_t* row_Y = Y + row_offset;
		icoeff_t* row_Co = Co + row_offset;
		icoeff_t* row_Cg = Cg + row_offset;
		u32* dest = bgra + (y * tile_width);
		for (i32 x = 0; x < tile_width; ++x) {
			((rgba_t*)dest)[x] = ycocg_to_bgr(row_Y[x], row_Co[x], row_Cg[x]);
		}
	}

	float elapsed = get_seconds_elapsed(start, get_clock());
	console_print_verbose("load: scale=%d x=%d y=%d  idwt time =%g  rgb transform time=%g\n", scale, tile_x, tile_y, elapsed_idwt, elapsed);

	/*if (scale == wsi->max_scale && tile_x == 1 && tile_y == 1) {
		stbi_write_png("debug_dwt_output.png", tile_width, tile_height, 4, bgra, tile_width * 4);
	}*/

	free(Y);
	free(Co);
	free(Cg);

	return bgra;
}



icoeff_t* isyntax_idwt_tile(void* ll_block, i16* h_block, i32 block_width, i32 block_height, bool is_top_level, i32 which_color) {
	i16* hl = h_block;
	i16* lh = h_block + block_width * block_height;
	i16* hh = h_block + 2 * block_width * block_height;

	// Prepare the idwt buffer containing both LL and LH/Hl/HH coefficients.
	// LL | HL
	// LH | HH
	size_t idwt_buffer_size = block_width * block_height * 4 * sizeof(icoeff_t);
	icoeff_t* idwt = (icoeff_t*)malloc(idwt_buffer_size); // need _aligned_malloc?
	i32 idwt_stride = block_width * 2;
	i32 ll_stride = (is_top_level) ? block_width : block_width * 2;

	// Stitch the top quadrants (LL and HL) together
	if (is_top_level) {
		// Top level: LL/HL/LH/HH are all read directly from a codeblock (16-bit signed magnitude)
		for (i32 y = 0; y < block_height; ++y) {
			icoeff_t* pos = idwt + y * idwt_stride;
			u16* ll = ((u16*)ll_block) + y * ll_stride;
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = *ll++;
			}
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = *hl++;
			}
		}
	} else {
		// Recursive variant (non-top level):
		// LL is read from already transformed coefficients from the parent block (32-bit integers)
		// HL/LH/HH are read from codeblocks (16-bit signed magnitude)
		for (i32 y = 0; y < block_height; ++y) {
			icoeff_t* pos = idwt + y * idwt_stride;
			icoeff_t* ll = ((icoeff_t*)ll_block) + y * ll_stride;
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = *ll++;
			}
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = *hl++;
			}
		}
	}
	// Now add the lower quadrants (LH and HH)
	for (i32 y = 0; y < block_height; ++y) {
		icoeff_t* pos = idwt + (y + block_height) * idwt_stride;
		for (i32 x = 0; x < block_width; ++x) {
			*pos++ = *lh++;
		}
		for (i32 x = 0; x < block_width; ++x) {
			*pos++ = *hh++;
		}
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	char filename[512];
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d", which_color);
	isyntax_idwt(idwt, block_width, block_height, true, filename);
#else
	isyntax_idwt(idwt, block_width, block_height, false, NULL);
#endif

	return idwt;
}


// Example codeblock order for a 'chunk' in the file:
// x        y       color   scale   coeff   offset      size    header_template_id
// 66302	66302	0	    8	    1	    850048253	8270	18
// 65918	65918	0	    7	    1	    850056531	17301	19
// 98686	65918	0	    7	    1	    850073840	14503	19
// 65918	98686	0	    7	    1	    850088351	8	    19
// 98686	98686	0	    7	    1	    850088367	8	    19
// 65726	65726	0	    6	    1	    850088383	26838	20
// 82110	65726	0	    6	    1	    850115229	11215	20
// 98494	65726	0	    6	    1	    850126452	6764	20
// 114878	65726	0	    6	    1	    850133224	25409	20
// 65726	82110	0	    6	    1	    850158641	21369	20
// 82110	82110	0	    6	    1	    850180018	8146	20
// 98494	82110	0	    6	    1	    850188172	4919	20
// 114878	82110	0	    6	    1	    850193099	19908	20
// 65726	98494	0	    6	    1	    850213015	8	    20
// 82110	98494	0	    6	    1	    850213031	8	    20
// 98494	98494	0	    6	    1	    850213047	8	    20
// 114878	98494	0	    6	    1	    850213063	8	    20
// 65726	114878	0	    6	    1	    850213079	8	    20
// 82110	114878	0	    6	    1	    850213095	8	    20
// 98494	114878	0	    6	    1	    850213111	8	    20
// 114878	114878	0	    6	    1	    850213127	8	    20
// 66558	66558	0	    8	    0	    850213143	5558	21    <- LL codeblock

// The above pattern repeats for the other 2 color channels (1 and 2).
// The LL codeblock is only present at the highest scales.

void isyntax_decompress_codeblock_in_chunk(isyntax_codeblock_t* codeblock, i32 block_width, i32 block_height, u8* chunk, u64 chunk_base_offset) {
	i64 offset_in_chunk = codeblock->block_data_offset - chunk_base_offset;
	ASSERT(offset_in_chunk >= 0);
	codeblock->data = chunk + offset_in_chunk;
	codeblock->decoded = isyntax_hulsken_decompress(codeblock, block_width, block_height, 1);
}




// Read between 57 and 64 bits (7 bytes + 1-8 bits) from a bitstream (least significant bit first).
// Requires that at least 7 safety bytes are present at the end of the stream (don't trigger a segmentation fault)!
static inline u64 bitstream_lsb_read(u8* buffer, u32 pos) {
	u64 raw = *(u64*)(buffer + pos / 8);
	raw >>= pos % 8;
	return raw;
}

static inline u64 bitstream_lsb_read_advance(u8* buffer, i32* bits_read, i32 bits_to_read) {
	u64 raw = *(u64*)(buffer + (*bits_read / 8));
	raw >>= (*bits_read / 8);
	*bits_read += bits_to_read;
	return raw;
}


// partly adapted from stb_image.h
#define HUFFMAN_FAST_BITS 11   // optimal value may depend on various factors, CPU cache etc.

// Lookup table for (1 << n) - 1
static const u16 size_bitmasks[17]={0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};

typedef struct huffman_t {
	u16 fast[1 << HUFFMAN_FAST_BITS];
	u16 code[256];
	u8  size[256];
	u16 nonfast_symbols[256];
	u16 nonfast_code[256+7]; // extra safety bytes for SIMD vector operations
	u16 nonfast_size[256];
	u16 nonfast_size_masks[256+7]; // extra safety bytes for SIMD vector operations
} huffman_t;

void save_code_in_huffman_fast_lookup_table(huffman_t* h, u32 code, u32 code_width, u8 symbol) {
	ASSERT(code_width <= HUFFMAN_FAST_BITS);
	i32 duplicate_bits = HUFFMAN_FAST_BITS - code_width;
	for (u32 i = 0; i < (1 << duplicate_bits); ++i) {
		u32 address = (i << code_width) | code;
		h->fast[address] = symbol;
	}
}

u32 max_code_size;
u32 symbol_counts[256];
u64 fast_count;
u64 nonfast_count;

i16* isyntax_hulsken_decompress(isyntax_codeblock_t* codeblock, i32 block_width, i32 block_height, i32 compressor_version) {
	ASSERT(compressor_version == 1 || compressor_version == 2);

	// Read the header information stored in the codeblock.
	// The layout varies depending on the version of the compressor used (version 1 or 2).
	// All integers are stored little-endian, least-significant bit first.
	//
	// Version 1 layout:
	//   uint32 : serialized length (in bytes)
	//   uint8 : zero run symbol
	//   uint8 : zero run counter size (in bits)
	// Version 2 layout:
	//   coeff_count (== 1 or 3) * coeff_bit_depth bits : 1 or 3 bitmasks, indicating which bitplanes are present
	//   uint8 : zero run symbol
	//   uint8 : zero run counter size (in bits)
	//   (variable length) : bitplane seektable (contains offsets to each of the bitplanes)

	// After the header section, the rest of the codeblock contains a Huffman tree, followed by a Huffman-coded
	// message of 8-bit Huffman symbols, interspersed with 'zero run' symbols (for run-length encoding of zeroes).

	i32 coeff_count = (codeblock->coefficient == 1) ? 3 : 1;
	i32 coeff_bit_depth = 16; // fixed value for iSyntax

	// Early out if dummy/empty block
	if (codeblock->block_size <= 8) {
		size_t coeff_buffer_size = coeff_count * block_width * block_height * sizeof(i16);
		i16* coeff_buffer = (i16*)calloc(1, coeff_buffer_size);
		return coeff_buffer;
	}

	i32 bits_read = 0;
	i32 block_size_in_bits = codeblock->block_size * 8;
	i64 serialized_length = 0; // In v1: stored in the first 4 bytes. In v2: derived calculation.
	u32 bitmasks[3] = { 0x000FFFF, 0x000FFFF, 0x000FFFF }; // default in v1: all ones, can be overridden later
	i32 total_mask_bits = coeff_bit_depth * coeff_count;
	u8* byte_pos = codeblock->data;
	if (compressor_version == 1) {
		serialized_length = *(u32*)byte_pos;
		byte_pos += 4;
		bits_read += 4*8;
	} else {
		if (coeff_count == 1) {
			bitmasks[0] = *(u16*)(byte_pos);
			byte_pos += 2;
			bits_read += 2*8;
			total_mask_bits = _popcnt32(bitmasks[0]);
		} else if (coeff_count == 3) {
			bitmasks[0] = *(u16*)(byte_pos);
			bitmasks[1] = *(u16*)(byte_pos+2);
			bitmasks[2] = *(u16*)(byte_pos+4);
			byte_pos += 6;
			bits_read += 6*8;
			total_mask_bits = _popcnt32(bitmasks[0]) + _popcnt32(bitmasks[1]) + _popcnt32(bitmasks[2]);
		} else {
			panic();
		}
		serialized_length = total_mask_bits * (block_width * block_height / 8);
	}
	u8 zerorun_symbol = *(u8*)byte_pos++;
	bits_read += 8;
	u8 zero_counter_size = *(u8*)byte_pos++;
	bits_read += 8;

	if (compressor_version >= 2) {
		// read bitplane seektable
		i32 stored_bit_plane_count = total_mask_bits;
		u32* bitplane_offsets = alloca(stored_bit_plane_count * sizeof(u32));
		i32 bitplane_ptr_bits = (i32)(log2f(serialized_length)) + 5;
		for (i32 i = 0; i < stored_bit_plane_count; ++i) {
			bitplane_offsets[i] = bitstream_lsb_read_advance(codeblock->data, &bits_read, bitplane_ptr_bits);
		}
	}

	// Read Huffman table
	huffman_t huffman = {};
	memset(huffman.fast, 0x80, sizeof(huffman.fast));
	memset(huffman.nonfast_size_masks, 0xFF, sizeof(huffman.nonfast_size_masks));
	u32 fast_mask = (1 << HUFFMAN_FAST_BITS) - 1;
	{
		i32 code_size = 0;
		u32 code = 0;
		i32 nonfast_symbol_index = 0;
		do {
			// Read a chunk of bits large enough to 'always' have the whole Huffman code, followed by the 8-bit symbol.
			// A blob of 57-64 bits is more than sufficient for a Huffman code of at most 16 bits.
			// The bitstream is organized least significant bit first (treat as one giant little-endian integer).
			// To read bits in the stream, look at the lowest bit positions. To advance the stream, shift right.
			i32 bits_to_advance = 1;
			u64 blob = bitstream_lsb_read(codeblock->data, bits_read); // gives back between 57 and 64 bits.

			// 'Descend' into the tree until we hit a leaf node.
			bool is_leaf = blob & 1;
			// TODO: intrinsic?
			while (!is_leaf) {
				++bits_to_advance;
				blob >>= 1;
				is_leaf = (blob & 1);
				++code_size;
			}
			blob >>= 1;

			// Read 8-bit Huffman symbol
			u8 symbol = (u8)(blob);
			huffman.code[symbol] = code;
			huffman.size[symbol] = code_size;

			if (code_size <= HUFFMAN_FAST_BITS) {
				// We can accelerate decoding of small Huffman codes by storing them in a lookup table.
				// However, for the longer codes this becomes inefficient so in those cases we need another method.
				save_code_in_huffman_fast_lookup_table(&huffman, code, code_size, symbol);
				++fast_count;
			} else {
				// TODO: how to make this faster?
				u32 prefix = code & fast_mask;
				u16 old_fast_data = huffman.fast[prefix];
				u8 old_lowest_symbol_index = old_fast_data & 0xFF;
				u8 new_lowest_symbol_index = MIN(old_lowest_symbol_index, nonfast_symbol_index);
				huffman.fast[prefix] = 256 + new_lowest_symbol_index;
				huffman.nonfast_symbols[nonfast_symbol_index] = symbol;
				huffman.nonfast_code[nonfast_symbol_index] = code;
				huffman.nonfast_size[nonfast_symbol_index] = code_size;
				huffman.nonfast_size_masks[nonfast_symbol_index] = size_bitmasks[code_size];
				++nonfast_symbol_index;
				++nonfast_count;
			}
			if (code_size > max_code_size) {
				max_code_size = code_size;
//			    console_print("found the biggest code size: %d\n", code_size);
			}
			symbol_counts[symbol]++;

			bits_to_advance += 8;
			bits_read += bits_to_advance;

			// traverse back up the tree: find last zero -> flip to one
			if (code_size == 0) {
				break; // already done; this happens if there is only a root node, no leaves
			}
			u32 code_high_bit = (1 << (code_size - 1));
			bool found_zero = (~code) & code_high_bit;
			while (!found_zero) {
				--code_size;
				if (code_size == 0) break;
				code &= code_high_bit - 1;
				code_high_bit >>= 1;
				found_zero = (~code) & code_high_bit;
			}
			code |= code_high_bit;
		} while(code_size > 0);
	}

	// Decode the message
	u8* decompressed_buffer = (u8*)malloc(serialized_length);

	u32 zerorun_code = huffman.code[zerorun_symbol];
	u32 zerorun_code_size = huffman.size[zerorun_symbol];
	if (zerorun_code_size == 0) zerorun_code_size = 1; // handle special case of the 'empty' Huffman tree (root node is leaf node)
	u32 zerorun_code_mask = (1 << zerorun_code_size) - 1;

	u32 zero_counter_mask = (1 << zero_counter_size) - 1;
	i32 decompressed_length = 0;
	while (bits_read < block_size_in_bits) {
		if (decompressed_length >= serialized_length) {
			break; // done
		}
		i32 symbol = 0;
		i32 code_size = 1;
		u64 blob = bitstream_lsb_read(codeblock->data, bits_read);
		u32 fast_index = blob & fast_mask;
		u16 c = huffman.fast[fast_index];
		if (c <= 255) {
			// Lookup the symbol directly.
			symbol = c;
			code_size = huffman.size[symbol];
		} else {
			// TODO: super naive and slow implementation, accelerate this!
			bool match = false;
			u8 lowest_possible_symbol_index = c & 0xFF;

#if  !((defined(__SSE2__) && defined(__AVX__)))
			for (i32 i = lowest_possible_symbol_index; i < 256; ++i) {
				u8 test_size = huffman.nonfast_size[i];
				u16 test_code = huffman.nonfast_code[i];
				if ((blob & size_bitmasks[test_size]) == test_code) {
					// match
					code_size = test_size;
					symbol = huffman.nonfast_symbols[i];
					match = true;
					break;
				}
			}
#else
			// SIMD version using SSE2, very slightly faster than the version above
			// (Need to compile with AVX enabled, otherwise unaligned loads will make it slower)
			// NOTE: Probably not a bottleneck, the loop below (nearly) always finishes after one iteration.
			i32 the_symbol = 0;
			for (i32 i = lowest_possible_symbol_index; i < 256; i += 8) {
				__m128i size_mask = _mm_loadu_si128((__m128i*)(huffman.nonfast_size_masks + i));
				__m128i code = _mm_loadu_si128((__m128i*)(huffman.nonfast_code + i));
				__m128i test = _mm_set1_epi16((u16)blob);
				test = _mm_and_si128(test, size_mask);
				__m128i hit = _mm_cmpeq_epi16(test, code);
				u32 hit_mask = _mm_movemask_epi8(hit);
				if (hit_mask) {
					u32 first_bit = bit_scan_forward(hit_mask);
					i32 symbol_index = i + first_bit / 2;
					symbol = huffman.nonfast_symbols[symbol_index];
					code_size = huffman.nonfast_size[symbol_index];
					match = true;
//					if (symbol_index - i > 1) console_print_verbose("diff=%d, i=%d, symbol_index=%d\n", symbol_index - i, i, symbol_index);
					break;
				}
			}
			DUMMY_STATEMENT;
			if (the_symbol != symbol) {
				DUMMY_STATEMENT;
			}
#endif
			if (!match) {
				DUMMY_STATEMENT;
			}
		}

		if (code_size == 0) code_size = 1; // handle special case of the 'empty' Huffman tree (root node is leaf node)

		blob >>= code_size;
		bits_read += code_size;

		// Handle run-length encoding of zeroes
		if (symbol == zerorun_symbol) {
			u32 numzeroes = blob & zero_counter_mask;
			bits_read += zero_counter_size;
			// A 'zero run' with length of zero means that this is not a zero run after all, but rather
			// the 'escaped' zero run symbol itself which should be outputted.
			if (numzeroes > 0) {
				if (compressor_version == 2) ++numzeroes; // v2 stores actual count minus one
				if (decompressed_length + numzeroes >= serialized_length) {
					// Reached the end, terminate
					memset(decompressed_buffer + decompressed_length, 0, MIN(serialized_length - decompressed_length, numzeroes));
					decompressed_length += numzeroes;
					break;
				}
				// If the next Huffman symbol is also the zero run symbol, then their counters actually refer to the same zero run.
				// Basically, each extra zero run symbol expands the 'zero counter' bit depth, i.e.:
				//   n zero symbols -> depth becomes n * counter_bits
				u32 total_zero_counter_size = zero_counter_size;
				for(;;) {
					// Peek ahead in the bitstream, grab any additional zero run symbols, and recalculate numzeroes.
					blob = bitstream_lsb_read(codeblock->data, bits_read);
					u8 next_code = (blob & zerorun_code_mask);
					if (next_code == zerorun_code) {
						// The zero run continues
						blob >>= zerorun_code_size;
						u32 counter_extra_bits = blob & zero_counter_mask;
						if (compressor_version == 2) ++counter_extra_bits; // v2 stores actual count minus one
						numzeroes <<= zero_counter_size;
						numzeroes |= (counter_extra_bits);
						total_zero_counter_size += zero_counter_size;
						bits_read += zerorun_code_size + zero_counter_size;
						if (decompressed_length + numzeroes >= serialized_length) {
							break; // Reached the end, terminate
						}
					} else {
						break; // no next zero run symbol, the zero run is finished
					}
				}

				i32 bytes_to_write = MIN(serialized_length - decompressed_length, numzeroes);
				ASSERT(bytes_to_write > 0);
				memset(decompressed_buffer + decompressed_length, 0, bytes_to_write);
				decompressed_length += numzeroes;
			} else {
				// This is not a 'zero run' after all, but an escaped symbol. So output the symbol.
				decompressed_buffer[decompressed_length++] = symbol;
			}
		} else {
			decompressed_buffer[decompressed_length++] = symbol;
		}

	}

	if (serialized_length != decompressed_length) {
		ASSERT(!"size mismatch");
		console_print("iSyntax: size mismatch in block %d (size=%d): expected %d observed %d\n",
				codeblock->block_data_offset, codeblock->block_size, serialized_length, decompressed_length);
	}
	codeblock->decompressed_size = decompressed_length;

	i32 bytes_per_bitplane = (block_width * block_height) / 8;
	if (compressor_version == 1) {

		i32 bytes_per_sample = 2; // ((coeff_bit_depth+7)/8);
		i32 expected_bitmask_bits = (decompressed_length*8) / (block_width * block_height);

		// try to deduce the number of coefficients without knowing the header information
		// TODO: this should not be necessary! Remove this code once we reliably know coeff_count from the header information
		i32 extra_bits = (decompressed_length*8) % (block_width * block_height);
		if (extra_bits > 0) {
			if (coeff_count != 1 && extra_bits == 1*16) {
				coeff_count = 1;
			} else if (coeff_count != 3 && extra_bits == 3*16) {
				coeff_count = 3;
			}
			total_mask_bits = coeff_bit_depth * coeff_count;
		}

		// If there are empty bitplanes: bitmasks stored at end of data
		u64 expected_length = total_mask_bits * bytes_per_bitplane;
		if (decompressed_length < expected_length) {
			if (coeff_count == 1) {
				bitmasks[0] = *(u16*)(decompressed_buffer + decompressed_length - 2);
				total_mask_bits = _popcnt32(bitmasks[0]);
			} else if (coeff_count == 3) {
				byte_pos = decompressed_buffer + decompressed_length - 6;
				bitmasks[0] = *(u16*)(byte_pos);
				bitmasks[1] = *(u16*)(byte_pos+2);
				bitmasks[2] = *(u16*)(byte_pos+4);
				total_mask_bits = _popcnt32(bitmasks[0]) + _popcnt32(bitmasks[1]) + _popcnt32(bitmasks[2]);
			} else {
				panic(); // TODO: fail condition
			}
			expected_length = (total_mask_bits * block_width * block_height) / 8 + (coeff_count * 2);
			ASSERT(decompressed_length == expected_length);
		}
	}

	// unpack bitplanes
	i32 compressed_bitplane_index = 0;
	size_t coeff_buffer_size = coeff_count * block_width * block_height * sizeof(u16);
	u16* coeff_buffer = (u16*)malloc(coeff_buffer_size); // need _aligned_malloc?
	u16* final_coeff_buffer = (u16*)malloc(coeff_buffer_size); // this copy will have the snake-order reshuffling undone
	memset(coeff_buffer, 0, coeff_buffer_size);
	memset(final_coeff_buffer, 0, coeff_buffer_size);

	for (i32 coeff_index = 0; coeff_index < coeff_count; ++coeff_index) {
		u16 bitmask = bitmasks[coeff_index];
		u16* current_coeff_buffer = coeff_buffer + (coeff_index * (block_width * block_height));
		u16* current_final_coeff_buffer = final_coeff_buffer + (coeff_index * (block_width * block_height));

		i32 bit = 0;
		while (bitmask) {
			if (bitmask & 1) {
				ASSERT((block_width * block_height % 8) == 0);
				u8* bitplane = decompressed_buffer + (compressed_bitplane_index * bytes_per_bitplane);
				for (i32 i = 0; i < block_width * block_height; i += 8) {
					i32 j = i/8;
					// What is the order bitplanes are stored in, actually??
					i32 shift_amount = (bit == 0) ? 15 : bit - 1; // bitplanes are stored sign, lsb ... msb
//					i32 shift_amount = 15 - bit; // bitplanes are stored sign, msb ... lsb
					u8 b = bitplane[j];
					if (b == 0) continue;
#if !defined(__SSE2__)
					// TODO: SIMD stuff; this step is SLOW
					current_coeff_buffer[i+0] |= ((b >> 0) & 1) << shift_amount;
					current_coeff_buffer[i+1] |= ((b >> 1) & 1) << shift_amount;
					current_coeff_buffer[i+2] |= ((b >> 2) & 1) << shift_amount;
					current_coeff_buffer[i+3] |= ((b >> 3) & 1) << shift_amount;
					current_coeff_buffer[i+4] |= ((b >> 4) & 1) << shift_amount;
					current_coeff_buffer[i+5] |= ((b >> 5) & 1) << shift_amount;
					current_coeff_buffer[i+6] |= ((b >> 6) & 1) << shift_amount;
					current_coeff_buffer[i+7] |= ((b >> 7) & 1) << shift_amount;
#else
					// This SIMD implementation is ~20% faster compared to the simple version above.
					// Can it be made faster?
					__m128i* dst = (__m128i*) (current_coeff_buffer+i);
					uint64_t t = bswap_64(((0x8040201008040201ULL*b) & 0x8080808080808080ULL) >> 7);
					__m128i v_t = _mm_set_epi64x(0, t);
					__m128i array_of_bools = _mm_unpacklo_epi8(v_t, _mm_setzero_si128());
					__m128i masks = _mm_slli_epi16(array_of_bools, shift_amount);
					__m128i result = _mm_or_si128(*dst, masks);
					*dst = result;
#endif
					DUMMY_STATEMENT;
				}
				++compressed_bitplane_index;
			}
			bitmask >>= 1;
			++bit;

		}

		if (bit > 0) {
			// Reshuffle snake-order
			i32 area_stride_x = block_width / 4;
			for (i32 area4x4_index = 0; area4x4_index < ((block_width * block_height) / 16); ++area4x4_index) {
				i32 area_base_index = area4x4_index * 16;
				i32 area_x = (area4x4_index % area_stride_x) * 4;
				i32 area_y = (area4x4_index / area_stride_x) * 4;

				u64 area_y0 = *(u64*)&current_coeff_buffer[area_base_index];
				u64 area_y1 = *(u64*)&current_coeff_buffer[area_base_index+4];
				u64 area_y2 = *(u64*)&current_coeff_buffer[area_base_index+8];
				u64 area_y3 = *(u64*)&current_coeff_buffer[area_base_index+12];

				*(u64*)(current_final_coeff_buffer + (area_y+0) * block_width + area_x) = area_y0;
				*(u64*)(current_final_coeff_buffer + (area_y+1) * block_width + area_x) = area_y1;
				*(u64*)(current_final_coeff_buffer + (area_y+2) * block_width + area_x) = area_y2;
				*(u64*)(current_final_coeff_buffer + (area_y+3) * block_width + area_x) = area_y3;
			}

			// Convert signed magnitude to twos complement (ex. 0x8002 becomes -2)
			signed_magnitude_to_twos_complement_16_block(current_final_coeff_buffer, block_width * block_height);
		}

	}

	free(coeff_buffer); // need _aligned_free?
	free(decompressed_buffer);
//	_aligned_free(final_coeff_buffer);



	return (i16*)final_coeff_buffer;

}

void debug_read_codeblock_from_file(isyntax_codeblock_t* codeblock, FILE* fp) {
	if (fp && !codeblock->data) {
		codeblock->data = calloc(1, codeblock->block_size + 8); // TODO: pool allocator
		fseeko64(fp, codeblock->block_data_offset, SEEK_SET);
		fread(codeblock->data, codeblock->block_size, 1, fp);

#if 0
		char out_filename[512];
		snprintf(out_filename, 512, "codeblocks/%d.bin", codeblock->block_data_offset);
		FILE* out = fopen(out_filename, "wb");
		if (out) {
			fwrite(codeblock->data, codeblock->block_size, 1, out);
			fclose(out);
		}
#endif
	}
}

static inline i32 get_first_valid_coef_pixel(i32 scale) {
	i32 result = (PER_LEVEL_PADDING << scale) - (PER_LEVEL_PADDING - 1);
	return result;
}

static inline i32 get_first_valid_ll_pixel(i32 scale) {
	i32 result = get_first_valid_coef_pixel(scale) + (1 << scale);
	return result;
}

i32 isyntax_get_chunk_codeblocks_per_color_for_level(i32 level, bool has_ll) {
	i32 rel_level = level % 3;
	i32 codeblock_count;
	if (rel_level == 0) {
		codeblock_count = 1;
	} else if (rel_level == 1) {
		codeblock_count = 1 + 4;
	} else {
		codeblock_count = 1 + 4 + 16;
	}
	if (has_ll) ++codeblock_count;
	return codeblock_count;
}


static void test_output_block_header(isyntax_image_t* wsi_image) {
	FILE* test_block_header_fp = fopen("test_block_header.csv", "wb");
	if (test_block_header_fp) {
		fprintf(test_block_header_fp, "x_coordinate,y_coordinate,color_component,scale,coefficient,block_data_offset,block_data_size,block_header_template_id\n");

		for (i32 i = 0; i < wsi_image->codeblock_count; i += 1/*21*3*/) {
			isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
			fprintf(test_block_header_fp, "%d,%d,%d,%d,%d,%d,%d,%d\n",
//			        codeblock->x_adjusted,
//			        codeblock->y_adjusted,
			        codeblock->x_coordinate - wsi_image->offset_x,
			        codeblock->y_coordinate - wsi_image->offset_y,
			        codeblock->color_component,
			        codeblock->scale,
			        codeblock->coefficient,
			        codeblock->block_data_offset,
			        codeblock->block_size,
			        codeblock->block_header_template_id);

//			if (codeblock->scale == wsi_image->level_count-1) {
//				i+=3; // skip extra LL blocks;
//			}
		}

		fclose(test_block_header_fp);
	}
}

bool isyntax_open(isyntax_t* isyntax, const char* filename) {

	ASSERT(isyntax);
	memset(isyntax, 0, sizeof(*isyntax));

	int ret = 0; (void)ret;
	FILE* fp = fopen64(filename, "rb");
	bool success = false;
	if (fp) {
		struct stat st;
		if (fstat(fileno(fp), &st) == 0) {
			isyntax->filesize = st.st_size;

			// https://www.openpathology.philips.com/wp-content/uploads/isyntax/4522%20207%2043941_2020_04_24%20Pathology%20iSyntax%20image%20format.pdf
			// Layout of an iSyntax file:
			// XML Header | End of Table (EOT) marker, 3 bytes "\r\n\x04" | Seektable (optional) | Codeblocks

			// Read the XML header
			// We don't know the length of the XML header, so we just read 'enough' data in a chunk and hope that we get it
			// (and if not, read some more data until we get it)

			i64 load_begin = get_clock();
			i64 io_begin = get_clock();
			i64 io_ticks_elapsed = 0;
			i64 parse_begin = get_clock();
			i64 parse_ticks_elapsed = 0;

			size_t read_size = MEGABYTES(1);
			char* read_buffer = malloc(read_size);
			size_t bytes_read = fread(read_buffer, 1, read_size, fp);
			io_ticks_elapsed += (get_clock() - io_begin);

			if (bytes_read < 3) goto fail_1;
			bool are_there_bytes_left = (bytes_read == read_size);

			// find EOT candidates, 3 bytes "\r\n\x04"
			i64 header_length = 0;
			i64 isyntax_data_offset = 0; // Offset of either the Seektable, or the Codeblocks segment of the iSyntax file.
			i64 bytes_read_from_data_offset_in_last_chunk = 0;

			i32 chunk_index = 0;
			for (;; ++chunk_index) {
//				console_print("iSyntax: reading XML header chunk %d\n", chunk_index);
				i64 chunk_length = 0;
				bool match = false;
				char* pos = read_buffer;
				i64 offset = 0;
				char* marker = (char*)memchr(read_buffer, '\x04', bytes_read);
				if (marker) {
					offset = marker - read_buffer;
					match = true;
					chunk_length = offset;
					header_length += chunk_length;
					isyntax_data_offset = header_length + 1;
					i64 data_offset_in_last_chunk = offset + 1;
					bytes_read_from_data_offset_in_last_chunk = (i64)bytes_read - data_offset_in_last_chunk;
				}
				if (match) {
					// We found the end of the XML header. This is the last chunk to process.
					if (!(header_length > 0 && header_length < isyntax->filesize)) goto fail_1;

					parse_begin = get_clock();
					isyntax_parse_xml_header(isyntax, read_buffer, chunk_length, true);
					parse_ticks_elapsed += (get_clock() - parse_begin);

//					console_print("iSyntax: the XML header is %u bytes, or %g%% of the total file size\n", header_length, (float)((float)header_length * 100.0f) / isyntax->filesize);
//					console_print("   I/O time: %g seconds\n", get_seconds_elapsed(0, io_ticks_elapsed));
//					console_print("   Parsing time: %g seconds\n", get_seconds_elapsed(0, parse_ticks_elapsed));
//					console_print("   Total loading time: %g seconds\n", get_seconds_elapsed(load_begin, get_clock()));
					break;
				} else {
					// We didn't find the end of the XML header. We need to read more chunks to find it.
					// (Or, we reached the end of the file unexpectedly, which is an error.)
					chunk_length = read_size;
					header_length += chunk_length;
					if (are_there_bytes_left) {

						parse_begin = get_clock();
						isyntax_parse_xml_header(isyntax, read_buffer, chunk_length, false);
						parse_ticks_elapsed += (get_clock() - parse_begin);

						io_begin = get_clock();
						bytes_read = fread(read_buffer, 1, read_size, fp); // read the next chunk
						io_ticks_elapsed += (get_clock() - io_begin);

						are_there_bytes_left = (bytes_read == read_size);
						continue;
					} else {
						console_print_error("iSyntax parsing error: didn't find the end of the XML header (unexpected end of file)\n");
						goto fail_1;
					}
				}
			}

			if (isyntax->mpp_x <= 0.0f) {
				isyntax->mpp_x = 0.25f; // should usually be 0.25; zero or below can never be right
			}
			if (isyntax->mpp_y <= 0.0f) {
				isyntax->mpp_y = 0.25f;
			}

			isyntax->block_width = isyntax->header_templates[0].block_width;
			isyntax->block_height = isyntax->header_templates[0].block_height;
			isyntax->tile_width = isyntax->block_width * 2; // tile dimension AFTER inverse wavelet transform
			isyntax->tile_height = isyntax->block_height * 2;

			isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;
			if (wsi_image->image_type == ISYNTAX_IMAGE_TYPE_WSI) {

				i64 block_width = isyntax->block_width;
				i64 block_height = isyntax->block_height;
				i64 tile_width = isyntax->tile_width;
				i64 tile_height = isyntax->tile_height;

				i64 num_levels = wsi_image->level_count;
				ASSERT(num_levels >= 1);
//				i64 padding = (PER_LEVEL_PADDING << level_count) - PER_LEVEL_PADDING;
				i64 grid_width = ((wsi_image->width + (block_width << num_levels) - 1) / (block_width << num_levels)) << (num_levels - 1);
				i64 grid_height = ((wsi_image->height + (block_height << num_levels) - 1) / (block_height << num_levels)) << (num_levels - 1);

				i64 h_coeff_tile_count = 0; // number of tiles with LH/HL/HH coefficients
				i64 base_level_tile_count = grid_height * grid_width;
				for (i32 i = 0; i < wsi_image->level_count; ++i) {
					isyntax_level_t* level = wsi_image->levels + i;
					level->tile_count = base_level_tile_count >> (i * 2);
					h_coeff_tile_count += level->tile_count;
					level->scale = i;
					level->width_in_tiles = grid_width >> i;
					level->height_in_tiles = grid_height >> i;
					level->downsample_factor = (float)(1 << i);
					level->um_per_pixel_x = isyntax->mpp_x * level->downsample_factor;
					level->um_per_pixel_y = isyntax->mpp_y * level->downsample_factor;
					level->x_tile_side_in_um = tile_width * level->um_per_pixel_x;
					level->y_tile_side_in_um = tile_height * level->um_per_pixel_y;
				}
				// The highest level has LL tiles in addition to LH/HL/HH tiles
				i64 ll_coeff_tile_count = base_level_tile_count >> ((num_levels - 1) * 2);
				i64 total_coeff_tile_count = h_coeff_tile_count + ll_coeff_tile_count;
				i64 total_codeblock_count = total_coeff_tile_count * 3; // for 3 color channels

				for (i32 i = 0; i < wsi_image->codeblock_count; ++i) {
					isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;

					// Calculate adjusted codeblock coordinates so that they fit the origin of the image
					codeblock->x_adjusted = (i32)codeblock->x_coordinate - wsi_image->offset_x;
					codeblock->y_adjusted = (i32)codeblock->y_coordinate - wsi_image->offset_y;

					// Calculate the block ID (= index into the seektable)
					// adapted from extract_block_header.py
					bool is_ll = codeblock->coefficient == 0;
					u32 block_id = 0;
					i32 maxscale = is_ll ? codeblock->scale + 1 : codeblock->scale;
					for (i32 scale = 0; scale < maxscale; ++scale) {
						block_id += wsi_image->levels[scale].tile_count;
					}

					i32 offset;
					if (is_ll) {
						offset = get_first_valid_ll_pixel(codeblock->scale);
					} else {
						offset = get_first_valid_coef_pixel(codeblock->scale);
					}
					i32 x = codeblock->x_adjusted - offset;
					i32 y = codeblock->y_adjusted - offset;
					codeblock->x_adjusted = x;
					codeblock->y_adjusted = y;
					codeblock->block_x = x / (tile_width << codeblock->scale);
					codeblock->block_y = y / (tile_height << codeblock->scale);

					i32 grid_stride = grid_width >> codeblock->scale;
					block_id += codeblock->block_y * grid_stride + codeblock->block_x;

					i32 tiles_per_color = total_coeff_tile_count;
					block_id += codeblock->color_component * tiles_per_color;
					codeblock->block_id = block_id;
					DUMMY_STATEMENT;
				}

				io_begin = get_clock(); // for performance measurement
				fseeko64(fp, isyntax_data_offset, SEEK_SET);
				if (wsi_image->header_codeblocks_are_partial) {
					// The seektable is required to be present, because the block header table did not contain all information.
					dicom_tag_header_t seektable_header_tag = {};
					fread(&seektable_header_tag, sizeof(dicom_tag_header_t), 1, fp);

					io_ticks_elapsed += (get_clock() - io_begin);
					parse_begin = get_clock();

					if (seektable_header_tag.group == 0x301D && seektable_header_tag.element == 0x2015) {
						i32 seektable_size = seektable_header_tag.size;
						if (seektable_size < 0) {
							// We need to guess the size...
							ASSERT(wsi_image->codeblock_count > 0);
							seektable_size = sizeof(isyntax_seektable_codeblock_header_t) * wsi_image->codeblock_count;
						}
						isyntax_seektable_codeblock_header_t* seektable =
								(isyntax_seektable_codeblock_header_t*) malloc(seektable_size);
						fread(seektable, seektable_size, 1, fp);

						// Now fill in the missing data.
						// NOTE: The number of codeblock entries in the seektable is much greater than the number of
						// codeblocks that *actually* exist in the file. This means that we have to discard many of
						// the seektable entries.
						// Luckily, we can easily identify the entries that need to be discarded.
						// (They have the data offset (and data size) set to 0.)
						i32 seektable_entry_count = seektable_size / sizeof(isyntax_seektable_codeblock_header_t);

						for (i32 i = 0; i < wsi_image->codeblock_count; ++i) {
							isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
							if (codeblock->block_id > seektable_entry_count) {
								ASSERT(!"block ID out of bounds");
								continue;
							}
							isyntax_seektable_codeblock_header_t* seektable_entry = seektable + codeblock->block_id;
							ASSERT(seektable_entry->block_data_offset_header.group == 0x301D);
							ASSERT(seektable_entry->block_data_offset_header.element == 0x2010);
							codeblock->block_data_offset = seektable_entry->block_data_offset;
							codeblock->block_size = seektable_entry->block_size;

#if 0
							// Debug test:
							// Decompress codeblocks in the seektable.
							if (1 || i == wsi_image->codeblock_count_per_color-1) {
								// Only parse 'non-empty'/'background' codeblocks
								if (codeblock->block_size > 8 /*&& codeblock->block_data_offset == 129572464*/) {
									debug_read_codeblock_from_file(codeblock, fp);
									isyntax_header_template_t* template = isyntax->header_templates + codeblock->block_header_template_id;
									if (template->waveletcoeff != 3) {
										DUMMY_STATEMENT;
									}
									if (i % 1000 == 0) {
										console_print_verbose("reading codeblock %d\n", i);
									}
									i16* decompressed = isyntax_hulsken_decompress(codeblock, isyntax->block_width, isyntax->block_height, 1);
									if (decompressed) {
#if 0
										FILE* out = fopen("hulskendecompressed4.raw", "wb");
										if(out) {
											fwrite(decompressed, codeblock->decompressed_size, 1, out);
											fclose(out);
										}
#endif
										_aligned_free(decompressed);
									}
								}
							}
#endif

						}
#if 0
						test_output_block_header(wsi_image);
#endif

						// Allocate enough space for the maximum number of codeblock 'chunks' we can expect
						// (the actual number of chunks may be lower, because some tiles might not exist)
						i32 max_possible_chunk_count = 0;
						for (i32 scale = 0; scale <= wsi_image->max_scale; ++scale) {
							if ((scale + 1) % 3 == 0 || scale == wsi_image->max_scale) {
								isyntax_level_t* level = wsi_image->levels + scale;
								max_possible_chunk_count += level->tile_count;
							}
						}
						wsi_image->data_chunks = (isyntax_data_chunk_t*) calloc(1, max_possible_chunk_count * sizeof(isyntax_data_chunk_t));


						// Create tables for spatial lookup of codeblocks and codeblock chunks from tile coordinates
						for (i32 i = 0; i < wsi_image->level_count; ++i) {
							isyntax_level_t* level = wsi_image->levels + i;
							// NOTE: Tile entry with codeblock_index == 0 will mean there is no codeblock for this tile (empty/background)
							level->tiles = (isyntax_tile_t*) calloc(1, level->tile_count * sizeof(isyntax_tile_t));
						}
						i32 current_chunk_codeblock_index = 0;
						i32 next_chunk_codeblock_index = 0;
						i32 data_chunk_index = 0;
						for (i32 i = 0; i < wsi_image->codeblock_count; ++i) {
							isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
							if (codeblock->color_component != 0) {
								// Don't let color channels 1 and 2 overwrite what was already set
								i = next_chunk_codeblock_index; // skip ahead
								codeblock = wsi_image->codeblocks + i;
								if (i >= wsi_image->codeblock_count) break;
							}
							// Keep track of where we are in the 'chunk' of codeblocks
							if (i == next_chunk_codeblock_index) {
								// This codeblock is the top of a new chunk
								i32 chunk_codeblock_count_per_color;
								if (codeblock->scale == wsi_image->max_scale) {
									chunk_codeblock_count_per_color = isyntax_get_chunk_codeblocks_per_color_for_level(codeblock->scale, true);
								} else {
									chunk_codeblock_count_per_color = 21;
								}
								current_chunk_codeblock_index = i;
								next_chunk_codeblock_index = i + (chunk_codeblock_count_per_color * 3);

								isyntax_data_chunk_t* chunk = wsi_image->data_chunks + data_chunk_index;
								chunk->offset = codeblock->block_data_offset;
								chunk->top_codeblock_index = current_chunk_codeblock_index;
								chunk->codeblock_count_per_color = chunk_codeblock_count_per_color;
								chunk->scale = codeblock->scale;
								++wsi_image->data_chunk_count;
								++data_chunk_index;
							}
							isyntax_level_t* level = wsi_image->levels + codeblock->scale;
							i32 tile_index = codeblock->block_y * level->width_in_tiles + codeblock->block_x;
							ASSERT(tile_index < level->tile_count);
							level->tiles[tile_index].exists = true;
							level->tiles[tile_index].codeblock_index = i;
							level->tiles[tile_index].codeblock_chunk_index = current_chunk_codeblock_index;
							level->tiles[tile_index].data_chunk_index = data_chunk_index;

						}



						parse_ticks_elapsed += (get_clock() - parse_begin);
//						console_print("iSyntax: the seektable is %u bytes, or %g%% of the total file size\n", seektable_size, (float)((float)seektable_size * 100.0f) / isyntax->filesize);
//						console_print("   I/O time: %g seconds\n", get_seconds_elapsed(0, io_ticks_elapsed));
//						console_print("   Parsing time: %g seconds\n", get_seconds_elapsed(0, parse_ticks_elapsed));
						isyntax->loading_time = get_seconds_elapsed(load_begin, get_clock());
					} else {
						// TODO: error
					}
				}
			} else {
				// TODO: error
			}


			// TODO: further implement iSyntax support
			success = true;

			fail_1:
			free(read_buffer);
		}
		fclose(fp);

		if (success) {
#if WINDOWS
			// TODO: make async I/O platform agnostic
			// TODO: set FILE_FLAG_NO_BUFFERING for maximum performance (but: need to align read requests to page size...)
			// http://vec3.ca/using-win32-asynchronous-io/
			isyntax->file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			                                         FILE_ATTRIBUTE_NORMAL | /*FILE_FLAG_SEQUENTIAL_SCAN |*/
			                                         /*FILE_FLAG_NO_BUFFERING |*/ FILE_FLAG_OVERLAPPED,
			                                         NULL);
#else
			isyntax->file_handle = open(filename, O_RDONLY);
				if (isyntax->file_handle == -1) {
					console_print_error("Error: Could not reopen file for asynchronous I/O\n");
					return false;
				} else {
					// success
				}

#endif
		}
	}
	return success;
}

void isyntax_destroy(isyntax_t* isyntax) {
	for (i32 image_index = 0; image_index < isyntax->image_count; ++image_index) {
		isyntax_image_t* isyntax_image = isyntax->images + image_index;
		if (isyntax_image->image_type == ISYNTAX_IMAGE_TYPE_WSI) {
			if (isyntax_image->codeblocks) {
				free(isyntax_image->codeblocks);
				isyntax_image->codeblocks = NULL;
			}
			if (isyntax_image->data_chunks) {
				free(isyntax_image->data_chunks);
				isyntax_image->data_chunks = NULL;
			}
			for (i32 i = 0; i < isyntax_image->level_count; ++i) {
				isyntax_level_t* level = isyntax_image->levels + i;
				if (level->tiles) {
					free(level->tiles);
					level->tiles = NULL;
				}
			}
		}
	}
#if WINDOWS
	if (isyntax->file_handle) {
		CloseHandle(isyntax->file_handle);
	}
#else
	if (isyntax->file_handle) {
		close(isyntax->file_handle);
	}
#endif
}
