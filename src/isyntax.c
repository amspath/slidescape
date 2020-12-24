/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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




bool isyntax_parse_xml_header(isyntax_t* isyntax, char* xml_header, i64 header_length) {

	yxml_t* x = NULL;
	bool success = false;

	char* attrbuf = malloc(header_length);
	char* attrbuf_end = attrbuf + header_length;
	char* attrcur = NULL;
	char* contentbuf = malloc(header_length);
	char* contentbuf_end = contentbuf + header_length;
	char* contentcur = NULL;

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
//						    console_print("element start: %s\n", x->elem);
					contentcur = contentbuf;
					*contentcur = '\0';

					/*current_element_type = ASAP_XML_ELEMENT_NONE;
					if (pass == ASAP_XML_PARSE_ANNOTATIONS && strcmp(x->elem, "Annotation") == 0) {
						annotation_t new_annotation = (annotation_t){};
								sb_push(annotation_set->stored_annotations, new_annotation);
						++annotation_set->stored_annotation_count;
						current_element_type = ASAP_XML_ELEMENT_ANNOTATION;
					} else if (pass == ASAP_XML_PARSE_ANNOTATIONS && strcmp(x->elem, "Coordinate") == 0) {
						coordinate_t new_coordinate = (coordinate_t){};
								sb_push(annotation_set->coordinates, new_coordinate);
						current_element_type = ASAP_XML_ELEMENT_COORDINATE;

						annotation_t* current_annotation = &sb_last(annotation_set->stored_annotations);
						if (!current_annotation->has_coordinates) {
							current_annotation->first_coordinate = annotation_set->coordinate_count;
							current_annotation->has_coordinates = true;
						}
						current_annotation->coordinate_count++;
						current_annotation->coordinate_capacity++; // used for delete/insert operations
						++annotation_set->coordinate_count;
					} else if (pass == ASAP_XML_PARSE_GROUPS && strcmp(x->elem, "Group") == 0) {
						current_element_type = ASAP_XML_ELEMENT_GROUP;
						// reset the state (start parsing a new group)
						current_group = (annotation_group_t){};
						current_group.is_explicitly_defined = true; // (because this group has an XML tag)
					}*/
				} break;
				case YXML_CONTENT: {
					// element content
//						    console_print("   element content: %s\n", x->elem);
					if (!contentcur) break;
					char* tmp = x->data;
					while (*tmp && contentbuf < contentbuf_end) {
						*(contentcur++) = *(tmp++);
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
//						    console_print("element end: %s\n", x->elem);
					if (contentcur) {
						// NOTE: usually only whitespace (newlines and such)
//							    console_print("elem content: %s\n", contentbuf);
					}
					/*if (pass == ASAP_XML_PARSE_GROUPS && strcmp(x->elem, "Group") == 0) {
						annotation_group_t* parsed_group = &current_group;
						// Check if a group already exists with this name, if not create it
						i32 group_index = find_annotation_group(annotation_set, parsed_group->name);
						if (group_index < 0) {
							group_index = add_annotation_group(annotation_set, parsed_group->name);
						}
						annotation_group_t* destination_group = annotation_set->groups + group_index;
						// 'Commit' the group with all its attributes
						memcpy(destination_group, parsed_group, sizeof(*destination_group));
					}*/
				} break;
				case YXML_ATTRSTART: {
					// attribute: 'Name=..'
//						    console_print("attr start: %s\n", x->attr);
					attrcur = attrbuf;
					*attrcur = '\0';
				} break;
				case YXML_ATTRVAL: {
					// attribute value
//						    console_print("   attr val: %s\n", x->attr);
					if (!attrcur) break;
					char* tmp = x->data;
					while (*tmp && attrbuf < attrbuf_end) {
						*(attrcur++) = *(tmp++);
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
//							    console_print("attr %s = %s\n", x->attr, attrbuf);
						/*if (pass == ASAP_XML_PARSE_ANNOTATIONS && current_element_type == ASAP_XML_ELEMENT_ANNOTATION) {
							annotation_set_attribute(annotation_set, &sb_last(annotation_set->stored_annotations), x->attr, attrbuf);
						} else if (pass == ASAP_XML_PARSE_ANNOTATIONS && current_element_type == ASAP_XML_ELEMENT_COORDINATE) {
							coordinate_set_attribute(&sb_last(annotation_set->coordinates), x->attr, attrbuf);
						} else if (pass == ASAP_XML_PARSE_GROUPS && current_element_type == ASAP_XML_ELEMENT_GROUP) {
							group_set_attribute(&current_group, x->attr, attrbuf);
						}*/
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


