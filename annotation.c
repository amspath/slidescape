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
#include "viewer.h"
#include "annotation.h"
#include "platform.h"
#include "gui.h"
#include "yxml.h"

// XML parsing using the yxml library.
// Note: what is the optimal stack buffer size for yxml?
// https://dev.yorhel.nl/yxml/man
#define YXML_STACK_BUFFER_SIZE KILOBYTES(32)

annotation_t* annotations; // sb
u32 annotation_count;

coordinate_t* coordinates; // sb
u32 coordinate_count;

asap_xml_element_enum current_xml_element_type;
asap_xml_attribute_enum current_xml_attribute_type;

void draw_annotations(v2f camera_min, float screen_um_per_pixel) {
	for (i32 annotation_index = 0; annotation_index < annotation_count; ++annotation_index) {
		annotation_t* annotation = annotations + annotation_index;
//		rgba_t rgba = annotation->color;
		rgba_t rgba = {50, 50, 0, 255 };
		u32 color = TO_RGBA(rgba.r, rgba.g, rgba.b, rgba.a);
		if (annotation->has_coordinates) {
			v2f* points = (v2f*) alloca(sizeof(v2f) * annotation->coordinate_count);
			for (i32 i = 0; i < annotation->coordinate_count; ++i) {
				coordinate_t* coordinate = coordinates + annotation->first_coordinate + i;
				v2f world_pos = {coordinate->x, coordinate->y};
				v2f transformed_pos = world_pos_to_screen_pos(world_pos, camera_min, screen_um_per_pixel);
				points[i] = transformed_pos;
			}
			gui_draw_poly(points, annotation->coordinate_count, color);
		}
	}
}

void annotation_set_attribute(annotation_t* annotation, const char* attr, const char* value) {
	if (strcmp(attr, "Color") == 0) {
		// TODO: parse color hex string #rrggbb
	} else if (strcmp(attr, "Name") == 0) {
		strncpy(annotation->name, value, sizeof(annotation->name));
	} else if (strcmp(attr, "PartOfGroup") == 0) {
		// find group
		// create group if not exists
		annotation->group_id = 0;
	} else if (strcmp(attr, "Type") == 0) {
		annotation->type = ANNOTATION_UNKNOWN_TYPE;
		if (strcmp(value, "Rectangle") == 0) {
			annotation->type = ANNOTATION_RECTANGLE;
		} else if (strcmp(value, "Polygon") == 0) {
			annotation->type = ANNOTATION_POLYGON;
		}
	}
}

void coordinate_set_attribute(coordinate_t* coordinate, const char* attr, const char* value) {
	if (strcmp(attr, "Order") == 0) {
		coordinate->order = atoi(value);
	} else if (strcmp(attr, "X") == 0) {
		coordinate->x = atof(value) * 0.25f; // TODO: address assumption
	} else if (strcmp(attr, "Y") == 0) {
		coordinate->y = atof(value) * 0.25f; // TODO: address assumption
	}
}

void unload_annotations() {
	if (annotations) {
		sb_free(annotations);
		annotations = NULL;
		annotation_count = 0;
	}
	if (coordinates) {
		sb_free(coordinates);
		coordinates = NULL;
		coordinate_count = 0;
	}
}


bool32 load_asap_xml_annotations(app_state_t* app_state, const char* filename) {
	unload_annotations();

	file_mem_t* file = platform_read_entire_file(filename);
	yxml_t* x = NULL;
	bool32 success = false;
	i64 start = get_clock();

	if (0) { failed:
		goto cleanup;
	}

	if (file) {
		// hack: merge memory for yxml_t struct and stack buffer
		x = malloc(sizeof(yxml_t) + YXML_STACK_BUFFER_SIZE);
		yxml_init(x, x + 1, YXML_STACK_BUFFER_SIZE);

		// parse XML byte for byte
		char attrbuf[128];
		char* attrbuf_end = attrbuf + sizeof(attrbuf);
		char* attrcur = NULL;
		char contentbuf[128];
		char* contentbuf_end = contentbuf + sizeof(contentbuf);
		char* contentcur = NULL;

		char* doc = (char*) file->data;
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
//						printf("element start: %s\n", x->elem);
						contentcur = contentbuf;

						current_xml_element_type = ASAP_XML_ELEMENT_NONE;
						if (strcmp(x->elem, "Annotation") == 0) {
							annotation_t new_annotation = (annotation_t){};
							sb_push(annotations, new_annotation);
							++annotation_count;
							current_xml_element_type = ASAP_XML_ELEMENT_ANNOTATION;
						} else if (strcmp(x->elem, "Coordinate") == 0) {
							coordinate_t new_coordinate = (coordinate_t){};
							sb_push(coordinates, new_coordinate);
							current_xml_element_type = ASAP_XML_ELEMENT_COORDINATE;

							annotation_t* current_annotation = &sb_last(annotations);
							if (!current_annotation->has_coordinates) {
								current_annotation->first_coordinate = coordinate_count;
								current_annotation->has_coordinates = true;
							}
							current_annotation->coordinate_count++;
							++coordinate_count;
						}
					} break;
					case YXML_CONTENT: {
						// element content
//						printf("   element content: %s\n", x->elem);
						if (!contentcur) break;
						char* tmp = x->data;
						while (*tmp && contentbuf < contentbuf_end) {
							*(contentcur++) = *(tmp++);
						}
						if (contentcur == contentbuf_end) {
							// too long content
							printf("load_asap_xml_annotations(): encountered a too long XML element content\n");
							goto failed;
						}
						*contentcur = '\0';
					} break;
					case YXML_ELEMEND: {
						// end of an element: '.. />' or '</Tag>'
//						printf("element end: %s\n", x->elem);
						if (contentcur) {
							// NOTE: usually only whitespace (newlines and such)
//							printf("elem content: %s\n", contentbuf);
							if (strcmp(x->elem, "Annotation") == 0) {

							}

						}
					} break;
					case YXML_ATTRSTART: {
						// attribute: 'Name=..'
//						printf("attr start: %s\n", x->attr);
						attrcur = attrbuf;
					} break;
					case YXML_ATTRVAL: {
						// attribute value
//						printf("   attr val: %s\n", x->attr);
						if (!attrcur) break;
						char* tmp = x->data;
						while (*tmp && attrbuf < attrbuf_end) {
							*(attrcur++) = *(tmp++);
						}
						if (attrcur == attrbuf_end) {
							// too long attribute
							printf("load_asap_xml_annotations(): encountered a too long XML attribute\n");
							goto failed;
						}
						*attrcur = '\0';
					} break;
					case YXML_ATTREND: {
						// end of attribute '.."'
						if (attrcur) {
//							printf("attr %s = %s\n", x->attr, attrbuf);
							if (current_xml_element_type == ASAP_XML_ELEMENT_ANNOTATION) {
								annotation_set_attribute(&sb_last(annotations), x->attr, attrbuf);
							} else if (current_xml_element_type == ASAP_XML_ELEMENT_COORDINATE) {
								coordinate_set_attribute(&sb_last(coordinates), x->attr, attrbuf);
							}
						}
					} break;
					case YXML_PISTART:
					case YXML_PICONTENT:
					case YXML_PIEND:
						break; // processing instructions (uninteresting, skip)
					default: {
						printf("yxml_parse(): unrecognized token (%d)\n", r);
						goto failed;
					}
				}
			}
		}
	}

	float seconds_elapsed = get_seconds_elapsed(start, get_clock());
	printf("Loaded annotations in %g seconds.\n", seconds_elapsed);

	cleanup:
	if (x) free(x);
	if (file) free(file);

	return success;
}
