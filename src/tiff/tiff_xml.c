/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2024  Pieter Valkema

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

// Routines to read and write XML data in the ImageDescription tag
// for the Philips TIFF variant.

// The code here is similar to the equivalent in isyntax.c

#include "common.h"
#include "tiff.h"
#include "isyntax.h"

#include "yxml.h"

#if 0

static const char* get_spaces(i32 length) {
	ASSERT(length >= 0);
	static const char spaces[] = "                                  ";
	i32 spaces_len = COUNT(spaces) - 1;
	i32 offset_from_end = MIN(spaces_len, length);
	i32 offset = spaces_len - offset_from_end;
	return spaces + offset;
}

static void push_to_buffer_maybe_grow(u8** restrict dest, size_t* restrict dest_len, size_t* restrict dest_capacity, void* restrict src, size_t src_len) {
	ASSERT(dest && dest_len && dest_capacity && src);
	size_t old_len = *dest_len;
	size_t new_len = old_len + src_len;
	size_t capacity = *dest_capacity;
	if (new_len > capacity) {
		capacity = next_pow2(new_len);
		u8* new_ptr = (u8*)realloc(*dest, capacity);
		if (!new_ptr) fatal_error();
		*dest = new_ptr;
		*dest_capacity = capacity;
	}
	memcpy(*dest + old_len, src, src_len);
	*dest_len = new_len;
}
static bool validate_dicom_attr(const char* expected, const char* observed) {
	bool ok = (strcmp(expected, observed) == 0);
	if (!ok) {
		console_print("TIFF XML header validation error: while reading DICOM metadata, expected '%s' but found '%s'\n", expected, observed);
	}
	return ok;
}


bool tiff_parse_xml_header(tiff_t* tiff, tiff_ifd_t* ifd, char* xml_header, i64 chunk_length, bool is_last_chunk) {

	yxml_t* x = NULL;
	bool success = false;

	static bool paranoid_mode = true;

	isyntax_xml_parser_t parser = {0};
	isyntax_xml_parser_init(&parser);

	x = parser.x;

	if (0) { failed: cleanup:
		if (parser.x) {
			free(parser.x);
			parser.x = NULL;
		}
		if (parser.attrbuf) {
			free(parser.attrbuf);
			parser.attrbuf = NULL;
		}
		if (parser.contentbuf) {
			free(parser.contentbuf);
			parser.contentbuf = NULL;
		}
		return success;
	}

	// parse XML byte for byte
	char* doc = xml_header;
	for (i64 remaining_length = chunk_length; remaining_length > 0; --remaining_length, ++doc) {
		int c = *doc;
		if (c == '\0') {
			// This should never trigger; iSyntax file is corrupt!
			goto failed;
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
					isyntax_parser_node_t* parent_node = parser.node_stack + parser.node_stack_index;
					++parser.node_stack_index;
					isyntax_parser_node_t* node = parser.node_stack + parser.node_stack_index;
					memset(node, 0, sizeof(isyntax_parser_node_t));
					// Inherit group and element of parent node
					node->group = parent_node->group;
					node->element = parent_node->element;

					parser.contentcur = parser.contentbuf;
					*parser.contentcur = '\0';
					parser.contentlen = 0;
					parser.attribute_index = 0;
					if (strcmp(x->elem, "Attribute") == 0) {
						node->node_type = ISYNTAX_NODE_LEAF;
					} else if (strcmp(x->elem, "DataObject") == 0) {
						node->node_type = ISYNTAX_NODE_BRANCH;
						// push into the data object stack, to keep track of which type of DataObject we are parsing
						// (we need this information to restore state when the XML element ends)
						++parser.data_object_stack_index;
						parser.data_object_stack[parser.data_object_stack_index] = *parent_node;						// set relevant flag for which data object type we are now parsing
						// set relevant flag for which data object type we are now parsing
						// NOTE: the data objects can have different DICOM groups, but currently there are no element
						// ID collisions so we can switch on only the element ID. This may change in the future.
						u32 flags = parser.data_object_flags;
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
							case PIIM_PIXEL_DATA_REPRESENTATION_SEQUENCE: flags |= ISYNTAX_OBJECT_PixelDataRepresentation; break;
						}

						parser.data_object_flags = flags;
					} else if (strcmp(x->elem, "Array") == 0) {
						node->node_type = ISYNTAX_NODE_ARRAY;
						console_print_verbose("%sArray\n", get_spaces(parser.node_stack_index));
					} else {
						node->node_type = ISYNTAX_NODE_NONE;
						console_print_verbose("%selement start: %s\n", get_spaces(parser.node_stack_index), x->elem);
					}
					parser.current_node_type = node->node_type;
					parser.current_node_has_children = false;
					parser.current_element_name = x->elem; // We need to remember this pointer, because it may point to something else at the YXML_ELEMEND state

				} break;

				case YXML_CONTENT: {
					// element content
					if (!parser.contentcur) break;

					// Load iSyntax block header table (and other large XML tags) greedily and bypass yxml parsing overhead
					if (parser.current_node_type == ISYNTAX_NODE_LEAF) {
						u32 group = parser.current_dicom_group_tag;
						u32 element = parser.current_dicom_element_tag;
						isyntax_parser_node_t* node = parser.node_stack + parser.node_stack_index;
						node->group = group;
						node->element = element;
						bool need_skip = (group == 0x301D && element == 0x2014) || // UFS_IMAGE_BLOCK_HEADER_TABLE
						                 (group == 0x301D && element == 0x1005) || // PIM_DP_IMAGE_DATA
						                 (group == 0x0028 && element == 0x2000);   // DICOM_ICCPROFILE

						if (need_skip) {
							parser.node_stack[parser.node_stack_index].has_base64_content = true;
							char* content_start = doc;
							char* pos = (char*)memchr(content_start, '<', remaining_length);
							if (pos) {
								i64 size = pos - content_start;
								push_to_buffer_maybe_grow((u8**)&parser.contentbuf, &parser.contentlen, &parser.contentbuf_capacity, content_start, size);
								parser.contentcur = parser.contentbuf + parser.contentlen;
//								console_print("iSyntax: skipped tag (0x%04x, 0x%04x) content length = %d\n", group, element, size);
								doc += (size-1); // skip to the next tag
								remaining_length -= (size-1);
								break;
							} else {
//								console_print("iSyntax: skipped tag (0x%04x, 0x%04x) content length = %d\n", group, element, remaining_length);
								push_to_buffer_maybe_grow((u8**)&parser.contentbuf, &parser.contentlen, &parser.contentbuf_capacity, content_start, remaining_length);
								parser.contentcur = parser.contentbuf + parser.contentlen;
								remaining_length = 0; // skip to the next chunk
								break;
							}
						}
					}

					char* tmp = x->data;
					while (*tmp && parser.contentlen < parser.contentbuf_capacity) {
						*(parser.contentcur++) = *(tmp++);
						++parser.contentlen;
						// too long content -> resize buffer
						if (parser.contentlen == parser.contentbuf_capacity) {
							size_t new_capacity = parser.contentbuf_capacity * 2;
							char* new_ptr = (char*)realloc(parser.contentbuf, new_capacity);
							if (!new_ptr) fatal_error();
							parser.contentbuf = new_ptr;
							parser.contentcur = parser.contentbuf + parser.contentlen;
							parser.contentbuf_capacity = new_capacity;
//							console_print("isyntax_parse_xml_header(): XML content buffer overflow (resized buffer to %u)\n", new_capacity);
						}
					}

					*parser.contentcur = '\0';
				} break;

				case YXML_ELEMEND: {
					// end of an element: '.. />' or '</Tag>'

					if (parser.current_node_type == ISYNTAX_NODE_LEAF && !parser.current_node_has_children) {
						// Leaf node WITHOUT children.
						// In this case we didn't already parse the attributes at the YXML_ATTREND stage.
						// Now at the YXML_ELEMEND stage we can parse the complete tag at once (attributes + content).
						console_print_verbose("%sDICOM: %-40s (0x%04x, 0x%04x), size:%-8u = %s\n", get_spaces(parser.node_stack_index),
						                      parser.current_dicom_attribute_name,
						                      parser.current_dicom_group_tag, parser.current_dicom_element_tag,
						                      parser.contentlen, parser.contentbuf);

//						if (parser.node_stack[parser.node_stack_index].group == 0) {
//							DUMMY_STATEMENT; // probably the group is 0 because this is a top level node.
//						}

						if (parser.node_stack_index == 2) {
							isyntax_parse_ufsimport_child_node(isyntax, parser.current_dicom_group_tag,
							                                   parser.current_dicom_element_tag,
							                                   parser.contentbuf, parser.contentlen);
						} else {
							isyntax_parse_scannedimage_child_node(isyntax, parser.current_dicom_group_tag,
							                                      parser.current_dicom_element_tag,
							                                      parser.contentbuf, parser.contentlen);
						}
					} else {
						// We have reached the end of a branch or array node, or a leaf node WITH children.
						// In this case, the attributes have already been parsed at the YXML_ATTREND stage.
						// Because their content is not text but child nodes, we do not need to touch the content buffer.
						const char* elem_name = NULL;
						if (parser.current_node_type == ISYNTAX_NODE_LEAF) {
							// End of a leaf node WITH children.
							elem_name = "Attribute";
						} else if (parser.current_node_type == ISYNTAX_NODE_BRANCH) {
							elem_name = "DataObject";
							// pop data object stack
							isyntax_parser_node_t data_object = parser.data_object_stack[parser.data_object_stack_index];
							--parser.data_object_stack_index;
							// reset relevant data for the data object type we are now no longer parsing
							u32 flags = parser.data_object_flags;
							switch(data_object.element) {
								default: break;
								case 0:                                       flags &= ~ISYNTAX_OBJECT_DPUfsImport; break;
								case PIM_DP_SCANNED_IMAGES:                   flags &= ~ISYNTAX_OBJECT_DPScannedImage; break;
								case UFS_IMAGE_GENERAL_HEADERS: {
									flags &= ~ISYNTAX_OBJECT_UFSImageGeneralHeader;
									parser.dimension_index = 0;
								} break;
								case UFS_IMAGE_BLOCK_HEADER_TEMPLATES: {
									flags &= ~ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate;
									++parser.header_template_index;
									parser.dimension_index = 0;
								} break;
								case UFS_IMAGE_DIMENSIONS: {
									flags &= ~ISYNTAX_OBJECT_UFSImageDimension;
									++parser.dimension_index;
								} break;
								case UFS_IMAGE_DIMENSION_RANGES: {
									flags &= ~ISYNTAX_OBJECT_UFSImageDimensionRange;
									++parser.dimension_index;
								} break;
								case DP_COLOR_MANAGEMENT:                     flags &= ~ISYNTAX_OBJECT_DPColorManagement; break;
								case DP_IMAGE_POST_PROCESSING:                flags &= ~ISYNTAX_OBJECT_DPImagePostProcessing; break;
								case DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR: flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor; break;
								case DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL: flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel; break;
								case PIIM_PIXEL_DATA_REPRESENTATION_SEQUENCE: flags &= ~ISYNTAX_OBJECT_PixelDataRepresentation; break;
							}
							parser.data_object_flags = flags;
						} else if (parser.current_node_type == ISYNTAX_NODE_ARRAY) {
							parser.dimension_index = 0;
							elem_name = "Array";
						}

						console_print_verbose("%selement end: %s\n", get_spaces(parser.node_stack_index), elem_name);
					}

					// 'Pop' context back to parent node
					if (parser.node_stack_index > 0) {
						--parser.node_stack_index;
						parser.current_node_type = parser.node_stack[parser.node_stack_index].node_type;
						parser.current_node_has_children = parser.node_stack[parser.node_stack_index].has_children;
					} else {
						//TODO: handle error condition
						console_print_error("iSyntax XML error: closing element without matching start\n");
					}

				} break;

				case YXML_ATTRSTART: {
					// attribute: 'Name=..'
//				    console_print_verbose("attr start: %s\n", x->attr);
					parser.attrcur = parser.attrbuf;
					*parser.attrcur = '\0';
					parser.attrlen = 0;
				} break;

				case YXML_ATTRVAL: {
					// attribute value
					//console_print_verbose("   attr val: %s\n", x->attr);
					if (!parser.attrcur) break;
					char* tmp = x->data;
					while (*tmp && parser.attrbuf < parser.attrbuf_end) {
						*(parser.attrcur++) = *(tmp++);
						++parser.attrlen;
						// too long content -> resize buffer
						if (parser.attrlen == parser.attrbuf_capacity) {
							size_t new_capacity = parser.attrbuf_capacity * 2;
							char* new_ptr = (char*)realloc(parser.attrbuf, new_capacity);
							if (!new_ptr) fatal_error();
							parser.attrbuf = new_ptr;
							parser.attrcur = parser.attrbuf + parser.attrlen;
							parser.attrbuf_capacity = new_capacity;
//							console_print("isyntax_parse_xml_header(): XML attribute buffer overflow (resized buffer to %u)\n", new_capacity);
						}
					}
					*parser.attrcur = '\0';
				} break;

				case YXML_ATTREND: {
					// end of attribute '.."'
					if (parser.attrcur) {
						ASSERT(strlen(parser.attrbuf) == parser.attrlen);

						if (parser.current_node_type == ISYNTAX_NODE_LEAF) {
							if (parser.attribute_index == 0 /* Name="..." */) {
								if (paranoid_mode) validate_dicom_attr(x->attr, "Name");
								size_t copy_size = MIN(parser.attrlen, sizeof(parser.current_dicom_attribute_name));
								memcpy(parser.current_dicom_attribute_name, parser.attrbuf, copy_size);
								i32 one_past_last_char = MIN(parser.attrlen, sizeof(parser.current_dicom_attribute_name)-1);
								parser.current_dicom_attribute_name[one_past_last_char] = '\0';
							} else if (parser.attribute_index == 1 /* Group="0x...." */) {
								if (paranoid_mode) validate_dicom_attr(x->attr, "Group");
								parser.current_dicom_group_tag = strtoul(parser.attrbuf, NULL, 0);
							} else if (parser.attribute_index == 2 /* Element="0x...." */) {
								if (paranoid_mode) validate_dicom_attr(x->attr, "Element");
								parser.current_dicom_element_tag = strtoul(parser.attrbuf, NULL, 0);
							} else if (parser.attribute_index == 3 /* PMSVR="..." */) {
								if (paranoid_mode) validate_dicom_attr(x->attr, "PMSVR");
								if (strcmp(parser.attrbuf, "IDataObjectArray") == 0) {
									// Leaf node WITH children.
									// Don't wait until YXML_ELEMEND to parse the attributes (this is the only opportunity we have!)
									parser.current_node_has_children = true;
									parser.node_stack[parser.node_stack_index].has_children = true;
									console_print_verbose("%sDICOM: %-40s (0x%04x, 0x%04x), array\n", get_spaces(parser.node_stack_index),
									                      parser.current_dicom_attribute_name,
									                      parser.current_dicom_group_tag, parser.current_dicom_element_tag);
									if (parser.node_stack_index == 2) { // At level of UfsImport
										isyntax_parse_ufsimport_child_node(isyntax, parser.current_dicom_group_tag,
										                                   parser.current_dicom_element_tag,
										                                   parser.contentbuf, parser.contentlen);
									} else {
										bool parse_ok = isyntax_parse_scannedimage_child_node(isyntax,
										                                                      parser.current_dicom_group_tag,
										                                                      parser.current_dicom_element_tag,
										                                                      parser.contentbuf, parser.contentlen);
									}

								}
							}
						} else if (parser.current_node_type == ISYNTAX_NODE_BRANCH) {
							// A DataObject node is supposed to have one attribute "ObjectType"
							ASSERT(parser.attribute_index == 0);
							ASSERT(strcmp(x->attr, "ObjectType") == 0);
							console_print_verbose("%sDataObject %s = %s\n", get_spaces(parser.node_stack_index), x->attr, parser.attrbuf);
							if (strcmp(parser.attrbuf, "DPScannedImage") == 0) {
								// We started parsing a new image (which will be either a WSI, LABELIMAGE or MACROIMAGE).
								parser.current_image = isyntax->images + isyntax->image_count;
								parser.running_image_index = isyntax->image_count++;
							}
						} else {
							console_print_verbose("%sattr %s = %s\n", get_spaces(parser.node_stack_index), x->attr, parser.attrbuf);
						}
						++parser.attribute_index;

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

#endif
