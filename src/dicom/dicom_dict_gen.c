/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2022  Pieter Valkema

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

#define STB_SPRINTF_IMPLEMENTATION
#include "common.h"
#include "memrw.h"
#include "platform.h"

#include "yxml.h"

#include "dicom.h"

typedef struct dicom_dict_parser_node_t {
	u32 node_type;
	u16 group;
	u16 element;
} dicom_dict_parser_node_t;

#define DICOM_DICT_MAX_NODE_DEPTH 16

enum dicom_dict_xml_element_enum {
	DICOM_DICT_XML_NONE,
	DICOM_DICT_XML_CHAPTER,
	DICOM_DICT_XML_TBODY,
	DICOM_DICT_XML_TR,
	DICOM_DICT_XML_TD,
	DICOM_DICT_XML_PARA,
	DICOM_DICT_XML_EMPHASIS,
};

typedef struct dicom_dict_parser_t {
	yxml_t* x;
	i32 running_image_index;
	u32 current_image_type;
	char* attrbuf;
	char* attrbuf_end;
	char* attrcur;
	size_t attrlen;
	size_t attrbuf_capacity;
	char* contentbuf;
	char* contentcur;
	size_t contentlen;
	size_t contentbuf_capacity;
	char current_dicom_attribute_name[256];
	char current_cleaned_content[256];
	i32 current_cleaned_content_len;
	u32 current_dicom_group;
	u32 current_dicom_element;
	char current_dicom_name[256];
	char current_dicom_keyword[256];
	u16 current_dicom_vr;
	bool current_dicom_invalid;
	i32 attribute_index;
	u32 current_node_type;
	dicom_dict_parser_node_t node_stack[DICOM_DICT_MAX_NODE_DEPTH];
	i32 node_stack_index;
	dicom_dict_parser_node_t data_object_stack[DICOM_DICT_MAX_NODE_DEPTH];
	i32 data_object_stack_index;
	u32 data_object_flags;
	i32 header_template_index;
	i32 dimension_index;
	i32 td_index;
	bool in_chapter6;
	bool in_chapter6_tbody;
	bool initialized;
} dicom_dict_parser_t;


void dicom_dict_parser_init(dicom_dict_parser_t* parser) {

	parser->initialized = true;

	parser->attrbuf_capacity = KILOBYTES(32);
	parser->contentbuf_capacity = MEGABYTES(8);

	parser->attrbuf = malloc(parser->attrbuf_capacity);
	parser->attrbuf_end = parser->attrbuf + parser->attrbuf_capacity;
	parser->attrcur = NULL;
	parser->attrlen = 0;
	parser->contentbuf = malloc(parser->contentbuf_capacity);
	parser->contentcur = NULL;
	parser->contentlen = 0;

	parser->current_dicom_attribute_name[0] = '\0';
	parser->current_dicom_group = 0;
	parser->current_dicom_element = 0;
	parser->attribute_index = 0;
	parser->current_node_type = 0; // TODO: needed?

	// XML parsing using the yxml library.
	// https://dev.yorhel.nl/yxml/man
	size_t yxml_stack_buffer_size = KILOBYTES(32);
	parser->x = (yxml_t*) malloc(sizeof(yxml_t) + yxml_stack_buffer_size);

	yxml_init(parser->x, parser->x + 1, yxml_stack_buffer_size);
}

static const char* get_spaces(i32 length) {
	ASSERT(length >= 0);
	static const char spaces[] = "                                  ";
	i32 spaces_len = COUNT(spaces) - 1;
	i32 offset_from_end = MIN(spaces_len, length);
	i32 offset = spaces_len - offset_from_end;
	return spaces + offset;
}

#pragma pack(push,1)
typedef struct dicom_dict_entry_t {
	u32 tag;
	u16 vr;
	u32 name_offset;
	u32 keyword_offset;
} dicom_dict_entry_t;
#pragma pack(pop)

bool output_dicom_dict_to_generated_c_code(dicom_dict_entry_t* dict_entries, memrw_t* name_buffer) {
	ASSERT(dict_entries);
	if (!dict_entries) return false;
	ASSERT(name_buffer);
	if (!name_buffer) return false;

	console_print("Dictionary size: %u\n", arrlen(dict_entries)*sizeof(dicom_dict_entry_t));
	console_print("String pool size: %u\n", name_buffer->used_size);


	memrw_t code_buffer = memrw_create(MEGABYTES(1));

	// First generate the header file: dicom_dict.h
	memrw_write_literal("// This file is generated by dicom_dict_parser.c\n"
						"\n"
	                    "#pragma once\n"
						"#include \"dicom.h\"\n"
						"\n"
						"enum dicom_tag_enum {\n", &code_buffer);

	// enum containing all dicom codes
	for (i32 i = 0; i < arrlen(dict_entries); ++i) {
		dicom_dict_entry_t entry = dict_entries[i];
		dicom_tag_t tag = *(dicom_tag_t*)&entry.tag;
		const char* keyword = (const char*)name_buffer->data + entry.keyword_offset;
		memrw_printf(&code_buffer, "\tDICOM_%s = DICOM_TAG(0x%04x,0x%04x),\n", keyword, tag.group, tag.element);
	}
	memrw_write_literal("};\n", &code_buffer);

	memrw_printf(&code_buffer, "\nextern dicom_dict_entry_t dicom_dict[%d];\n", arrlen(dict_entries));
	memrw_write_literal("\nextern const char* dicom_dict_string_pool;\n", &code_buffer);

	FILE* fp = fopen("dicom_dict.h", "w");
	fwrite(code_buffer.data, code_buffer.used_size, 1, fp);
	fclose(fp);

	// Now generate implementation: dicom_dict.c
	memrw_rewind(&code_buffer);

	memrw_write_literal("// This file is generated by dicom_dict_parser.c\n"
	                    "\n"
						"#include \"dicom_dict.t\"\n", &code_buffer);

	// dictionary entries
	memrw_printf(&code_buffer, "\ndicom_dict_entry_t dicom_dict[%d] = {\n", arrlen(dict_entries));
	for (i32 i = 0; i < arrlen(dict_entries); ++i) {
		dicom_dict_entry_t entry = dict_entries[i];
		memrw_printf(&code_buffer, "\t{0x%x,%u,%u,%u},\n", entry.tag, entry.vr, entry.name_offset, entry.keyword_offset);
	}
	memrw_write_literal("};\n", &code_buffer);

	// string pool
	memrw_write_literal("\nconst char* dicom_dict_string_pool =\n", &code_buffer);
	for (i32 i = 0; i < arrlen(dict_entries); ++i) {
		dicom_dict_entry_t entry = dict_entries[i];
		const char* name = (const char*)name_buffer->data + entry.name_offset;
		const char* keyword = (const char*)name_buffer->data + entry.keyword_offset;
		memrw_printf(&code_buffer, "\t\"%s\\0%s\\0\"\n", name, keyword);
	}
	memrw_write_literal(";\n", &code_buffer);

	fp = fopen("dicom_dict.c", "w");
	fwrite(code_buffer.data, code_buffer.used_size, 1, fp);
	fclose(fp);

	return true;
}

bool parse_dicom_part06_xml(const char* xml, i64 length) {
	yxml_t* x = NULL;
	bool success = false;

	static bool paranoid_mode = true;

	dicom_dict_parser_t parser = {};

	if (!parser.initialized) {
		dicom_dict_parser_init(&parser);
	}
	x = parser.x;

	memrw_t name_buffer = memrw_create(MEGABYTES(1));
	dicom_dict_entry_t* dict_entries = NULL; // array

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
	const char* doc = xml;
	for (i64 remaining_length = length; remaining_length > 0; --remaining_length, ++doc) {
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
					dicom_dict_parser_node_t* parent_node = parser.node_stack + parser.node_stack_index;
					++parser.node_stack_index;
					dicom_dict_parser_node_t* node = parser.node_stack + parser.node_stack_index;
					memset(node, 0, sizeof(dicom_dict_parser_node_t));
					// Inherit group and element of parent node
					node->group = parent_node->group;
					node->element = parent_node->element;

					parser.contentcur = parser.contentbuf;
					*parser.contentcur = '\0';
					parser.contentlen = 0;
					parser.attribute_index = 0;
					if (strcmp(x->elem, "chapter") == 0) {
						node->node_type = DICOM_DICT_XML_CHAPTER;
//						console_print_verbose("%schapter\n", get_spaces(parser.node_stack_index));
					} else if (strcmp(x->elem, "tbody") == 0) {
						node->node_type = DICOM_DICT_XML_TBODY;
						if (parser.in_chapter6) {
							parser.in_chapter6_tbody = true;
						}
//						console_print_verbose("%stbody\n", get_spaces(parser.node_stack_index));
					} else if (strcmp(x->elem, "tr") == 0) {
						node->node_type = DICOM_DICT_XML_TR;
						parser.td_index = 0; // reset counter of <td> elements
						parser.current_dicom_group = 0;
						parser.current_dicom_element = 0;
						parser.current_dicom_name[0] = '\0';
						parser.current_dicom_keyword[0] = '\0';
						parser.current_dicom_vr = 0;
						parser.current_dicom_invalid = false;
//						console_print_verbose("%str\n", get_spaces(parser.node_stack_index));
					} else if (strcmp(x->elem, "td") == 0) {
						node->node_type = DICOM_DICT_XML_TD;
						parser.current_cleaned_content[0] = '\0';
						parser.current_cleaned_content_len = 0;
//						console_print_verbose("%std\n", get_spaces(parser.node_stack_index));
					} else if (strcmp(x->elem, "para") == 0) {
						node->node_type = DICOM_DICT_XML_PARA;
//						console_print_verbose("%spara\n", get_spaces(parser.node_stack_index));
					}else if (strcmp(x->elem, "emphasis") == 0) {
						node->node_type = DICOM_DICT_XML_EMPHASIS;
//						console_print_verbose("%semphasis\n", get_spaces(parser.node_stack_index));
					}

					parser.current_node_type = node->node_type;

				} break;

				case YXML_CONTENT: {
					// element content
					if (!parser.contentcur) break;


					char* tmp = x->data;
					while (*tmp && parser.contentlen < parser.contentbuf_capacity) {
						*(parser.contentcur++) = *(tmp++);
						++parser.contentlen;
						// too long content -> resize buffer
						if (parser.contentlen == parser.contentbuf_capacity) {
							size_t new_capacity = parser.contentbuf_capacity * 2;
							char* new_ptr = (char*)realloc(parser.contentbuf, new_capacity);
							if (!new_ptr) panic();
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
					if (parser.current_node_type == DICOM_DICT_XML_EMPHASIS || parser.current_node_type == DICOM_DICT_XML_PARA) {
						if (parser.in_chapter6_tbody && parser.contentlen > 0) {
							// strip whitespace and non ascii UTF-8 characters (may contain zero width spaces - U+200B)
							u8 cleaned[256] = "";
							i32 pos = 0;
							i32 len = MIN(parser.contentlen, sizeof(cleaned)-1);
							bool only_whitespace = true;
							for (i32 i = 0; i < len; ++i) {
								u8 c = parser.contentbuf[i];
								if (c >= 128) {
									// in_utf8_run = true;
								} else {
									bool is_whitespace = (c == ' ' || c == '\n' || c == '\r' || c == '\t');
									if (!is_whitespace) {
										only_whitespace = false;
									} else if (only_whitespace) {
										continue; // content should not only exist of spaces/newlines, etc.
									}
									cleaned[pos++] = c;
								}
							}
							cleaned[pos] = '\0';
							i32 cleaned_len = pos;
							if (cleaned_len > 0) {
								strncpy(parser.current_cleaned_content, (char*)cleaned, MIN(sizeof(parser.current_cleaned_content), sizeof(cleaned)));
								parser.current_cleaned_content_len = cleaned_len;
							}
						}
					} else if (parser.current_node_type == DICOM_DICT_XML_TD) {
						if (parser.in_chapter6_tbody) {
							char* content = parser.current_cleaned_content;
							i32 content_len = parser.current_cleaned_content_len;
							if (parser.td_index == 0) {
								// DICOM Tag: group / element: e.g. (300A,00A2)
								if (content_len == 11 && content[0] == '(' && content[10] == ')' && content[5] == ',') {
									// parse group
									char* start_ptr = content+1;
									char* end_ptr = NULL;
									u32 parsed = strtoul(start_ptr, &end_ptr, 16);
									if (end_ptr - start_ptr != 4) {
										parser.current_dicom_invalid = true;
										console_print_verbose("DICOM group: invalid hex value %s\n", start_ptr);
									}
									parser.current_dicom_group = parsed;

									// parse element
									start_ptr = content+6;
									end_ptr = NULL;
									parsed = strtoul(start_ptr, &end_ptr, 16);
									if (end_ptr - start_ptr != 4) {
										parser.current_dicom_invalid = true;
										console_print_verbose("DICOM element: invalid hex value %s\n", start_ptr);
									}
									parser.current_dicom_element = parsed;
								} else {
									console_print_verbose("unexpected DICOM content layout: %s\n", content);
								}
							} else if (parser.td_index == 1) {
								// Name
								strncpy(parser.current_dicom_name, content, sizeof(parser.current_dicom_name));
							} else if (parser.td_index == 2) {
								// Keyword
								strncpy(parser.current_dicom_keyword, content, sizeof(parser.current_dicom_name));
							} else if (parser.td_index == 3) {
								// VR
								bool ok = false;
								if (content_len == 2) {
									u16 vr = LE_2CHARS(content[0], content[1]);
									switch(vr) {
										default: break;
										case DICOM_VR_AE: case DICOM_VR_AS: case DICOM_VR_AT: case DICOM_VR_CS:
										case DICOM_VR_DA: case DICOM_VR_DS: case DICOM_VR_DT: case DICOM_VR_FD:
										case DICOM_VR_FL: case DICOM_VR_IS: case DICOM_VR_LO: case DICOM_VR_LT:
										case DICOM_VR_OB: case DICOM_VR_OD: case DICOM_VR_OF: case DICOM_VR_OL:
										case DICOM_VR_OV: case DICOM_VR_OW: case DICOM_VR_PN: case DICOM_VR_SH:
										case DICOM_VR_SL: case DICOM_VR_SQ: case DICOM_VR_SS: case DICOM_VR_ST:
										case DICOM_VR_SV: case DICOM_VR_TM: case DICOM_VR_UC: case DICOM_VR_UI:
										case DICOM_VR_UL: case DICOM_VR_UN: case DICOM_VR_UR: case DICOM_VR_US:
										case DICOM_VR_UT: case DICOM_VR_UV:
											parser.current_dicom_vr = vr;
											ok = true;
											break;
									}
								}
								if (!ok) {
									// Handle some weird exceptions
									if (strcmp(content, "OB or OW") == 0) {
										// There is always an even number of bytes in the data, so might as well go for a 16-bit number?
										parser.current_dicom_vr = DICOM_VR_OW;
										ok = true;
									} else if (strcmp(content, "US or SS") == 0) {
										// We just guess if it's signed or unsigned?? This is weird.
										parser.current_dicom_vr = DICOM_VR_SS;
										ok = true;
									} else if (strcmp(content, "US or OW") == 0) {
										// This is the case for (0028,3006) LUT Data
										parser.current_dicom_vr = DICOM_VR_US; // stupid guess
										ok = true;
									} else if (strcmp(content, "US or SS or OW") == 0) {
										// This is the case for (0028,1200) Gray Lookup Table Data (RET)
										parser.current_dicom_vr = DICOM_VR_US; // stupid guess
										ok = true;
									} else if (parser.current_dicom_group == 0xfffe &&
									           (parser.current_dicom_element == 0xe000 ||
									            parser.current_dicom_element == 0xe00d ||
									            parser.current_dicom_element == 0xe0dd)
													) {
										// Special cases without a defined VR
										parser.current_dicom_vr = 0;
										ok = true;
									}
								}

								if (!ok) {
									parser.current_dicom_invalid = true;
									if (content_len > 0) {
										console_print_verbose("unexpected DICOM VR layout: %s\n", content);
									}
								}
							} else if (parser.td_index == 4) {
								// VM
							} else if (parser.td_index == 5) {
								// RET/notes
							}
							++parser.td_index;
//							console_print_verbose("%s content: %s\n", get_spaces(parser.node_stack_index), parser.current_cleaned_content);
						}
					} else if (parser.current_node_type == DICOM_DICT_XML_TR) {
						if (parser.in_chapter6_tbody) {

							if (!parser.current_dicom_invalid) {
								// add new dictionary item
								u32 name_offset = name_buffer.cursor;
								memrw_write(parser.current_dicom_name, &name_buffer, strlen(parser.current_dicom_name)+1);
								u32 keyword_offset = name_buffer.cursor;
								memrw_write(parser.current_dicom_keyword, &name_buffer, strlen(parser.current_dicom_keyword)+1);

								dicom_dict_entry_t new_entry = {
									.tag = DICOM_TAG(parser.current_dicom_group, parser.current_dicom_element),
									.vr = parser.current_dicom_vr,
									.name_offset = name_offset,
									.keyword_offset = keyword_offset,
								};
								arrput(dict_entries, new_entry);
							}

							if (is_verbose_mode) {
								char vr_text[4] = {}; // convert 2-byte VR to printable form
								*(u16*) vr_text = parser.current_dicom_vr;
								if (!parser.current_dicom_invalid) {
									console_print_verbose("(%04x,%04x) | %s | %s | %s\n", parser.current_dicom_group,
									                      parser.current_dicom_element, vr_text, parser.current_dicom_name, parser.current_dicom_keyword);
								} else {
									console_print_verbose("(%04x,%04x) | invalid\n", parser.current_dicom_group, parser.current_dicom_element);
								}
							}
						}
					} else if (parser.current_node_type == DICOM_DICT_XML_CHAPTER) {
						parser.in_chapter6 = false;
					} else if (parser.current_node_type == DICOM_DICT_XML_TBODY) {
						parser.in_chapter6_tbody = false;
					}


					parser.contentcur = parser.contentbuf;
					*parser.contentcur = '\0';
					parser.contentlen = 0;

					// 'Pop' context back to parent node
					if (parser.node_stack_index > 0) {
						--parser.node_stack_index;
						parser.current_node_type = parser.node_stack[parser.node_stack_index].node_type;
					} else {
						//TODO: handle error condition
						console_print_error("Dicom dict XML error: closing element without matching start\n");
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
							if (!new_ptr) panic();
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

						if (parser.current_node_type == DICOM_DICT_XML_CHAPTER) {
							if (strcmp(x->attr, "label") == 0 && strcmp(parser.attrbuf, "6") == 0) {
								parser.in_chapter6 = true;
							}
						} else {
//							console_print_verbose("%sattr %s = %s\n", get_spaces(parser.node_stack_index), x->attr, parser.attrbuf);
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

	output_dicom_dict_to_generated_c_code(dict_entries, &name_buffer);

	success = true;
	goto cleanup;
}

int main(int argc, const char** argv) {

	mem_t* file = platform_read_entire_file("resources/dicom/part06.xml");

	is_verbose_mode = true;
	parse_dicom_part06_xml((const char*)file->data, file->len);


	return 0;
}