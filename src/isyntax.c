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

#include "common.h"
#include "platform.h"

#include "isyntax.h"

#include "yxml.h"

// TODO: Reading the header in chunks
// TODO: Parse array nodes properly
// TODO: Record relevant metadata in the isyntax_t structure
// TODO: Add base64 decoding routines
// --> used in the XML header for barcode, label/macro JPEG data, image block header structure, ICC profiles
// TODO: Parse seektable
// TODO: Figure out codeblocks packaging scheme and decompression
// TODO: Identify spatial location of codeblocks
// TODO: Inverse discrete wavelet transform
// TODO: Colorspace post-processing (convert YCoCg to RGB)
// TODO: Add ICC profiles support

void isyntax_decode_base64_embedded_jpeg_file(isyntax_t* isyntax) {
	// stub
}

void isyntax_parse_ufsimport_child_node(isyntax_t* isyntax, u32 group, u32 element, const char* value, u64 value_len) {

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
				case 0x1003: /*PIM_DP_SCANNED_IMAGES*/                    {} break;
				case 0x1010: /*PIM_DP_SCANNER_RACK_PRIORITY*/               {} break; // "<u16>"

			}
		} break;
	}
}

void isyntax_parse_scannedimage_child_node(isyntax_t* isyntax, u32 group, u32 element, const char* value, u64 value_len) {

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
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
						isyntax->macro_image.image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
						isyntax->parser.current_image = &isyntax->macro_image;
					} else if ((strcmp(value, "LABELIMAGE") == 0)) {
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
						isyntax->label_image.image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
						isyntax->parser.current_image = &isyntax->label_image;
					} else if ((strcmp(value, "WSI") == 0)) {
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_WSI;
						isyntax->wsi_image.image_type = ISYNTAX_IMAGE_TYPE_WSI;
						isyntax->parser.current_image = &isyntax->wsi_image;
					}
				} break;
				case 0x1005: /*PIM_DP_IMAGE_DATA*/                          {} break;
				case 0x1015: /*DP_SHARPNESS_GAIN_RGB24*/                    {} break;
				case 0x1016: /*DP_CLAHE_CLIP_LIMIT_Y16*/                    {} break;
				case 0x1017: /*DP_CLAHE_NR_BINS_Y16*/                       {} break;
				case 0x1018: /*DP_CLAHE_CONTEXT_DIMENSION_Y16*/             {} break;
				case 0x101B: /*DP_WAVELET_QUANTIZER*/                       {} break;
				case 0x101C: /*DP_WAVELET_DEADZONE*/                        {} break;
				case 0x2001: /*UFS_IMAGE_NUMBER_OF_BLOCKS*/                 {} break;
				case 0x2002: /*UFS_IMAGE_DIMENSIONS_OVER_BLOCK*/            {} break;
				case 0x2004: /*UFS_IMAGE_DIMENSION_NAME*/                   {} break;
				case 0x2005: /*UFS_IMAGE_DIMENSION_TYPE*/                   {} break;
				case 0x2006: /*UFS_IMAGE_DIMENSION_UNIT*/                   {} break;
				case 0x2007: /*UFS_IMAGE_DIMENSION_SCALE_FACTOR*/           {} break;
				case 0x2008: /*UFS_IMAGE_DIMENSION_DISCRETE_VALUES_STRING*/ {} break;
				case 0x200B: /*UFS_IMAGE_DIMENSION_RANGE*/                  {} break;
				case 0x200C: /*UFS_IMAGE_DIMENSION_IN_BLOCK*/               {} break;
				case 0x200F: /*UFS_IMAGE_BLOCK_COMPRESSION_METHOD*/         {} break;
				case 0x2013: /*UFS_IMAGE_PIXEL_TRANSFORMATION_METHOD*/      {} break;
				case 0x2014: /*UFS_IMAGE_BLOCK_HEADER_TABLE*/               {} break;

			}
		} break;
	}
}

// TODO: parse array nodes properly
void isyntax_parse_attribute(isyntax_t* isyntax, u32 group, u32 element, const char* value, u64 value_len) {

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
				case 0x2111: /*DICOM_DERIVATION_DESCRIPTION*/   {         // "PHILIPS UFS V%s | Quality=%d | DWT=%d | Compressor=%d"

				} break;
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
				case 0x1004: /*PIM_DP_IMAGE_TYPE*/                     {         // "MACROIMAGE" or "LABELIMAGE" or "WSI"
					if ((strcmp(value, "MACROIMAGE") == 0)) {
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
						isyntax->macro_image.image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
						isyntax->parser.current_image = &isyntax->macro_image;
					} else if ((strcmp(value, "LABELIMAGE") == 0)) {
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
						isyntax->label_image.image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
						isyntax->parser.current_image = &isyntax->label_image;
					} else if ((strcmp(value, "WSI") == 0)) {
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_WSI;
						isyntax->wsi_image.image_type = ISYNTAX_IMAGE_TYPE_WSI;
						isyntax->parser.current_image = &isyntax->wsi_image;
					}
				} break;
				case 0x1005: /*PIM_DP_IMAGE_DATA*/                          {} break;
				case 0x1010: /*PIM_DP_SCANNER_RACK_PRIORITY*/               {} break; // "<u16>"
				case 0x1015: /*DP_SHARPNESS_GAIN_RGB24*/                    {} break;
				case 0x1016: /*DP_CLAHE_CLIP_LIMIT_Y16*/                    {} break;
				case 0x1017: /*DP_CLAHE_NR_BINS_Y16*/                       {} break;
				case 0x1018: /*DP_CLAHE_CONTEXT_DIMENSION_Y16*/             {} break;
				case 0x101B: /*DP_WAVELET_QUANTIZER*/                       {} break;
				case 0x101C: /*DP_WAVELET_DEADZONE*/                        {} break;
				case 0x2001: /*UFS_IMAGE_NUMBER_OF_BLOCKS*/                 {} break;
				case 0x2002: /*UFS_IMAGE_DIMENSIONS_OVER_BLOCK*/            {} break;
				case 0x2004: /*UFS_IMAGE_DIMENSION_NAME*/                   {} break;
				case 0x2005: /*UFS_IMAGE_DIMENSION_TYPE*/                   {} break;
				case 0x2006: /*UFS_IMAGE_DIMENSION_UNIT*/                   {} break;
				case 0x2007: /*UFS_IMAGE_DIMENSION_SCALE_FACTOR*/           {} break;
				case 0x2008: /*UFS_IMAGE_DIMENSION_DISCRETE_VALUES_STRING*/ {} break;
				case 0x200B: /*UFS_IMAGE_DIMENSION_RANGE*/                  {} break;
				case 0x200C: /*UFS_IMAGE_DIMENSION_IN_BLOCK*/               {} break;
				case 0x200F: /*UFS_IMAGE_BLOCK_COMPRESSION_METHOD*/         {} break;
				case 0x2013: /*UFS_IMAGE_PIXEL_TRANSFORMATION_METHOD*/      {} break;
				case 0x2014: /*UFS_IMAGE_BLOCK_HEADER_TABLE*/               {} break;

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


bool isyntax_parse_xml_header(isyntax_t* isyntax, char* xml_header, i64 header_length) {

	yxml_t* x = NULL;
	bool success = false;

	static bool paranoid_mode = true;

	char* current_element_name = "";
	char* attrbuf = malloc(header_length);
	char* attrbuf_end = attrbuf + header_length;
	char* attrcur = NULL;
	size_t attrlen = 0;
	char* contentbuf = malloc(header_length);
	char* contentbuf_end = contentbuf + header_length;
	char* contentcur = NULL;
	size_t contentlen = 0;

	char current_dicom_attribute_name[256];
	u32 current_dicom_group_tag = 0;
	u32 current_dicom_element_tag = 0;
	i32 attribute_index = 0;
	bool parsing_dicom_tag = false;

	if (0) { failed: cleanup:
		if (x) free(x);
		if (attrbuf) free(attrbuf);
		if (contentbuf) free(contentbuf);
		return success;
	}

	// XML parsing using the yxml library.
	// https://dev.yorhel.nl/yxml/man
	size_t yxml_stack_buffer_size = KILOBYTES(32);
	x = (yxml_t*) malloc(sizeof(yxml_t) + yxml_stack_buffer_size);

	yxml_init(x, x + 1, yxml_stack_buffer_size);

	// parse XML byte for byte


	char* doc = xml_header;
	for (; *doc; doc++) {
		yxml_ret_t r = yxml_parse(x, *doc);
		if (r == YXML_OK) {
			continue; // nothing worthy of note has happened -> continue
		} else if (r < 0) {
			goto failed;
		} else if (r > 0) {
			// token
			switch(r) {
				case YXML_ELEMSTART: {
					// start of an element: '<Tag ..'
					contentcur = contentbuf;
					*contentcur = '\0';
					contentlen = 0;
					attribute_index = 0;
					parsing_dicom_tag = (strcmp(x->elem, "Attribute") == 0);
					current_element_name = x->elem; // We need to remember this pointer, because it may point to something else at the YXML_ELEMEND state

					if (!parsing_dicom_tag) console_print_verbose("element start: %s\n", x->elem);

				} break;
				case YXML_CONTENT: {
					// element content
//				    console_print_verbose("   element content: %s\n", x->elem);
					if (!contentcur) break;
					char* tmp = x->data;
					while (*tmp && contentbuf < contentbuf_end) {
						*(contentcur++) = *(tmp++);
						++contentlen;
					}
					if (contentcur == contentbuf_end) {
						// too long content
						console_print("isyntax_parse_xml_header(): encountered a too long XML element content\n");
						goto failed;
					}
					*contentcur = '\0';
				} break;
				case YXML_ELEMEND: {
					// end of an element: '.. />' or '</Tag>'

					// NOTE: for a leaf node, it is sufficient to wait for an YXML_ELEMEND, and process it here.
					// (The 'content' is just a string -> easy to handle attributes + content at the same time.)
					// But, for an array node, the YXML_ELEMEND won't be triggered until all of its child nodes
					// are also processed. So in that case we need to intervene at an earlier stage, e.g. at
					// the YXML_CONTENT stage, while we know the

					if (parsing_dicom_tag) {
						console_print_verbose("DICOM: %-40s (0x%04x, 0x%04x) = %s\n", current_dicom_attribute_name, current_dicom_group_tag, current_dicom_element_tag, contentbuf);
						isyntax_parse_attribute(isyntax, current_dicom_group_tag, current_dicom_element_tag, contentbuf, contentlen);
					} else {
						console_print_verbose("element end: %s\n", current_element_name);
						if (contentcur) {
							if (contentlen > 0) {
								console_print_verbose("elem content: %s\n", contentbuf);
							}
//
						}
					}

				} break;
				case YXML_ATTRSTART: {
					// attribute: 'Name=..'
//				    console_print_verbose("attr start: %s\n", x->attr);
					attrcur = attrbuf;
					*attrcur = '\0';
					attrlen = 0;
				} break;
				case YXML_ATTRVAL: {
					// attribute value
				    //console_print_verbose("   attr val: %s\n", x->attr);
					if (!attrcur) break;
					char* tmp = x->data;
					while (*tmp && attrbuf < attrbuf_end) {
						*(attrcur++) = *(tmp++);
						++attrlen;
					}
					if (attrcur == attrbuf_end) {
						// too long attribute
						console_print("isyntax_parse_xml_header(): encountered a too long XML attribute\n");
						goto failed;
					}
					*attrcur = '\0';
				} break;
				case YXML_ATTREND: {
					// end of attribute '.."'
					if (attrcur) {
						if (!parsing_dicom_tag) console_print_verbose("attr %s = %s\n", x->attr, attrbuf);
						ASSERT(strlen(attrbuf) == attrlen);
						if (parsing_dicom_tag) {
							if (attribute_index == 0 /* Name="..." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Name");
								size_t copy_size = MIN(attrlen, sizeof(current_dicom_attribute_name));
								memcpy(current_dicom_attribute_name, attrbuf, copy_size);
								i32 one_past_last_char = MIN(attrlen, sizeof(current_dicom_attribute_name)-1);
								current_dicom_attribute_name[one_past_last_char] = '\0';
								DUMMY_STATEMENT;
							} else if (attribute_index == 1 /* Group="0x...." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Group");
								current_dicom_group_tag = strtoul(attrbuf, NULL, 0);
								DUMMY_STATEMENT;
							} else if (attribute_index == 2 /* Element="0x...." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Element");
								current_dicom_element_tag = strtoul(attrbuf, NULL, 0);
								DUMMY_STATEMENT;
							} else if (attribute_index == 3 /* PMSVR="..." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "PMSVR");
							}

						}
						++attribute_index;

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

	goto cleanup;
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

			// TODO: read and parse the header in chunks (maybe we don't need all of it to begin useful work?)
			// We might be able to use the image block header structure (typically located near the top of the file)
			// to quickly start I/O operations on the actual image data, without needing the whole header
			// (the whole XML header can be huge, >32MB in some files)

			size_t read_size = MEGABYTES(32);
			char* read_buffer = malloc(read_size);
			size_t bytes_read = fread(read_buffer, 1, read_size, fp);
			if (bytes_read < 3) goto fail_1;
			bool are_there_bytes_left = (bytes_read == read_size);
			// find EOT candidates, 3 bytes "\r\n\x04"
			bool match = false;
			i64 header_length = 0;
			char* pos = read_buffer;
			i64 offset = 0;
			for (; offset < bytes_read; ++offset, ++pos) {
				char c = *pos;
				if (c == '\x04' && offset > 2) {
					// candidate for EOT marker
					if (pos[-2] == '\r' && pos[-1] == '\n') {
						match = true;
						header_length = offset - 2;
						ASSERT(header_length > 0);
						break;
					}
				}
			}
			if (!match) {
				// TODO: read more
				console_print_error("didn't find the end of the XML header\n");
			}
			if (!(header_length > 0 && header_length < isyntax->filesize)) goto fail_1;

			// Offset of either the Seektable, or the Codeblocks segment of the iSyntax file.
			i64 isyntax_data_offset = header_length + 3;

			// Process the XML header
			char* xml_header = read_buffer;
			xml_header[header_length] = '\0';

			isyntax_parse_xml_header(isyntax, xml_header, header_length);

#if 0 // dump XML header for testing
			FILE* test_out = fopen("isyntax_header.xml", "wb");
			fwrite(xml_header, header_length, 1, test_out);
			fclose(test_out);
#endif

			// TODO: further implement iSyntax support
			success = true;

			fail_1:
			free(read_buffer);
		}
		fclose(fp);
	}
	return success;
}


