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

#include "common.h"
#include "platform.h"
#include "mathutils.h"
#include "listing.h"

#include "viewer.h"

#include "dicom.h"
#include "dicom_wsi.h"

#include "lz4.h"

#define LISTING_IMPLEMENTATION
#include "listing.h"


static void dicom_switch_data_encoding(dicom_instance_t* instance, dicom_data_element_t *transfer_syntax_uid) {

	// TODO: what if the transfer UID only applies to the interpretation of e.g. pixel data?
	// list of UIDs: https://dicom.nema.org/medical/dicom/current/output/chtml/part06/chapter_A.html

	// The first 18 chars do not discriminate (all have the prefix "1.2.840.10008.1.2")
	const char* suffix = (const char*) &(instance->data + transfer_syntax_uid->data_offset)[17];
	u32 uid_length = transfer_syntax_uid->length;

	if (uid_length == 20 && suffix[0] == '.') {
		if (suffix[1] == '1') {
			instance->encoding = DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_LITTLE_ENDIAN; // 1.2.840.10008.1.2.1
		} else if (suffix[1] == '2') {
			instance->encoding = DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_BIG_ENDIAN_RETIRED; // 1.2.840.10008.1.2.2
		}
	} else if (uid_length == 23 && strncmp(suffix, ".1.99", 5) == 0) {
		instance->encoding = DICOM_TRANSFER_SYNTAX_DEFLATED_EXPLICIT_VR_LITTLE_ENDIAN; // 1.2.840.10008.1.2.1.99
	}
}

static inline bool32 need_alternate_element_layout(u16 vr) {
	int sum = (vr == DICOM_VR_OB) +
	          (vr == DICOM_VR_OD) +
	          (vr == DICOM_VR_OF) +
	          (vr == DICOM_VR_OL) +
	          (vr == DICOM_VR_OV) +
	          (vr == DICOM_VR_OW) +
	          (vr == DICOM_VR_SQ) +
	          (vr == DICOM_VR_UC) +
	          (vr == DICOM_VR_UR) +
	          (vr == DICOM_VR_UT) +
	          (vr == DICOM_VR_UN);
	return (sum != 0);
}

dicom_dict_entry_t* dicom_dict_entries;
const char* dicom_dict_string_pool;

dicom_dict_entry_t* dicom_dict_hash_table;
u32 dicom_dict_hash_table_size;

// Hash function adapted from https://github.com/skeeto/hash-prospector
u32 lowbias32(u32 x) {
	x ^= x >> 16;
	x *= 0x21f0aaad;
	x ^= x >> 15;
	x *= 0x735a2d97;
	x ^= x >> 15;
	return x;
}

// Note: Unfortunately it looks like lookup using a hash table is only marginally faster than linear lookup
static dicom_dict_entry_t* dicom_dict_lookup(u32 tag) {
	u32 hash = lowbias32(tag);
	u32 index = hash & (dicom_dict_hash_table_size-1);
	dicom_dict_entry_t* slot = dicom_dict_hash_table + index;
	i32 j = 0;
	u32 first_checked_index = index;
	while (slot->tag != tag) {
		// linear probing
		++j;
		index = (first_checked_index + j) & (dicom_dict_hash_table_size-1);
		slot = dicom_dict_hash_table + index;
		if (slot->tag == 0) {
			// empty slot -> not found
			return NULL;
		}
	}
	return slot;
}

static u16 get_dicom_tag_vr(u32 tag) {
	dicom_dict_entry_t* entry = dicom_dict_lookup(tag);
	if (entry) {
		return entry->vr;
	} else {
		return DICOM_VR_UN;
	}
}


static const char* get_dicom_tag_name(u32 tag) {
	dicom_dict_entry_t* entry = dicom_dict_lookup(tag);
	if (entry) {
		return dicom_dict_string_pool + entry->name_offset;
	} else {
		return NULL;
	}
}

static const char* get_dicom_tag_keyword(u32 tag) {
	dicom_dict_entry_t* entry = dicom_dict_lookup(tag);
	if (entry) {
		return dicom_dict_string_pool + entry->keyword_offset;
	} else {
		return NULL;
	}
}

#if 0
static u16 get_dicom_tag_vr_linear(u32 tag) {
	u32 tag_count = COUNT(dicom_dict_packed_entries);

	// TODO: hash table
	for (u32 tag_index = 0; tag_index < tag_count; ++tag_index) {
		if (dicom_dict_entries[tag_index].tag == tag) {
			return dicom_dict_entries[tag_index].vr;
		}
	}
	return DICOM_VR_UN;
}

static u16 test_vr;

void lookup_tester() {
	i64 start = get_clock();

	i32 lookup_count = 1000000;

	srand(46458);
	for (i32 i = 0; i < lookup_count; ++i) {
		i32 entry_index = rand() % COUNT(dicom_dict_packed_entries);
		dicom_dict_entry_t* entry_to_lookup = dicom_dict_entries + entry_index;
		test_vr = get_dicom_tag_vr(entry_to_lookup->tag);
	}
	console_print("Lookup using hash table (%dx) took %g seconds\n", lookup_count, get_seconds_elapsed(start, get_clock()));

	start = get_clock();

	srand(46458);
	for (i32 i = 0; i < lookup_count; ++i) {
		i32 entry_index = rand() % COUNT(dicom_dict_packed_entries);
		dicom_dict_entry_t* entry_to_lookup = dicom_dict_entries + entry_index;
		test_vr = get_dicom_tag_vr_linear(entry_to_lookup->tag);
	}
	console_print("Lookup using linear method (%dx) took %g seconds\n", lookup_count, get_seconds_elapsed(start, get_clock()));

}
#endif

static dicom_uid_enum dicom_uid_lookup(const char* uid, i32 len) {
	// TODO: hash table
	if (len > 14 && strncmp(uid, "1.2.840.10008.", 14) == 0) {
		for (i32 i = 1; i < COUNT(dicom_dict_uid_entries); ++i) {
			dicom_dict_uid_entry_t* entry = dicom_dict_uid_entries + i;
			if (strncmp(uid+14, entry->uid_last_part, len - 14) == 0) {
				return i;
			}
		}
		char uid_copy[65] = {};
		strncpy(uid_copy, uid, MIN(len, sizeof(uid_copy)-1));
		console_print("DICOM UID not found: %s\n", uid_copy);
	}
	return 0;
}

static dicom_dict_uid_entry_t* dicom_uid_get_entry(const char* uid, i32 len) {
	dicom_uid_enum uid_index = dicom_uid_lookup(uid, len);
	if (uid_index > 0) {
		return dicom_dict_uid_entries + uid_index;
	} else {
		return NULL;
	}
}

static dicom_data_element_t read_dicom_data_element(u8* data_start, i64 data_offset, dicom_transfer_syntax_enum encoding, i64 bytes_available) {
	dicom_data_element_t result = {};
	u8* pos = data_start + data_offset;
	if (bytes_available >= 8) {
		result.is_valid = true; // may override with false later
		result.tag = *(dicom_tag_t*) pos;

		if (result.tag.group == 0xFFFE &&
				(result.tag.as_u32 == DICOM_Item ||
				result.tag.as_u32 == DICOM_ItemDelimitationItem ||
				result.tag.as_u32 == DICOM_SequenceDelimitationItem))
		{
			// special cases: DICOM_Item, DICOM_ItemDelimitationItem, DICOM_SequenceDelimitationItem
			dicom_implicit_data_element_header_t* element_header = (dicom_implicit_data_element_header_t*) pos;
			result.tag = element_header->tag;
			result.length = element_header->value_length;
//			result.data = element_header->data;
			result.data_offset = data_offset + sizeof(*element_header);
			result.vr = 0; // undefined
		} else if (encoding == DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_LITTLE_ENDIAN || result.tag.group == 2 /*File Meta info*/ ) {
			// Data element is Explicit VR.
			dicom_explicit_data_element_header_t* element_header = (dicom_explicit_data_element_header_t*) pos;
			result.vr = element_header->vr;

			// Some VRs have the value length field stored differently...
			if (need_alternate_element_layout(result.vr)) {
				if (bytes_available >= 12) {
					result.length = *(u32*) &element_header->variable_part[+2];
//					result.data = pos + 12; // Advance to value field
					result.data_offset = data_offset + 12; // Advance to value field
				} else {
					result.is_valid = false;
				}
			} else {
				result.length = *(u16*) &element_header->variable_part[0];
//				result.data = pos + 8;
				result.data_offset = data_offset + 8;
			}
		} else {
			// Data element is Implicit VR.
			dicom_implicit_data_element_header_t* element_header = (dicom_implicit_data_element_header_t*) pos;
			result.tag = element_header->tag;
			result.length = element_header->value_length;
//			result.data = element_header->data;
			result.data_offset = data_offset + sizeof(*element_header);
			result.vr = get_dicom_tag_vr(result.tag.as_u32); // look up the VR from the data dictionary.
		}
	}

	return result;
}

static inline u32 dicom_get_element_length_without_trailing_whitespace(u8* element_data, u32 element_length) {
	u32 length = element_length;
	if (length != 0 && length % 2 == 0 && element_data[length-1] == ' ') {
		--length; // ignore trailing space character
	}
	return length;
}

// Print information about a data element
static void debug_print_dicom_element(dicom_instance_t* instance, dicom_data_element_t element, FILE* out, i32 nesting_level, u32 item_number) {
	memrw_t string_builder = memrw_create(512);
	if (nesting_level > 0) {
		for (i32 i = 1; i < nesting_level; ++i) {
			memrw_write_literal("  ", &string_builder); // extra indentation
		}
		memrw_printf(&string_builder, "  %u: ", item_number);
	}

	char vr_text[4] = {}; // convert 2-byte VR to printable form
	*(u16*) vr_text = element.vr;
	const char* keyword = get_dicom_tag_keyword(element.tag.as_u32);
	memrw_printf(&string_builder, "(%04x,%04x) - %s - length: %d - %s",
	             element.tag.group, element.tag.element, vr_text, element.length,
	             keyword);

	u8* element_data = instance->data + element.data_offset;

	// Try to print out the value of the tag.
	switch(element.vr) {
		default: break;
		case DICOM_VR_UI:
		case DICOM_VR_SH:
		case DICOM_VR_LO:
		case DICOM_VR_AE:
		case DICOM_VR_AS:
		case DICOM_VR_CS:
		case DICOM_VR_DS:
		case DICOM_VR_PN:
		case DICOM_VR_IS:
		case DICOM_VR_DA:
		case DICOM_VR_LT:
		case DICOM_VR_UT:
		case DICOM_VR_TM: {
			// print identifier
			char identifier[65];
			u32 length = MIN(64, dicom_get_element_length_without_trailing_whitespace(element_data, element.length));
			strncpy(identifier, (const char*) element_data, length);
			identifier[length] = '\0';
			if (length > 0) {
				memrw_printf(&string_builder, " - \"%s\"", identifier);
				if (element.vr == DICOM_VR_UI) {
					dicom_dict_uid_entry_t* uid_entry = dicom_uid_get_entry(identifier, length);
					if (uid_entry) {
						const char* keyword = dicom_dict_string_pool + uid_entry->keyword_offset;
						memrw_printf(&string_builder, " - %s", keyword);
					}
				}
			}
		} break;
		case DICOM_VR_UL: {
			u32 value = *(u32*) element_data;
			memrw_printf(&string_builder, " - %u", value);
		} break;
		case DICOM_VR_SL: {
			i32 value = *(i32*) element_data;
			memrw_printf(&string_builder, " - %d", value);
		} break;
		case DICOM_VR_US: {
			u32 value = *(u16*) element_data;
			memrw_printf(&string_builder, " - %u", value);
		} break;
		case DICOM_VR_SS: {
			i32 value = *(i16*) element_data;
			memrw_printf(&string_builder, " - %d", value);
		} break;
		case DICOM_VR_FL: {
			float value = *(float*) element_data;
			memrw_printf(&string_builder, " - %g", value);
		} break;
	}
	memrw_putc('\n', &string_builder);

	memrw_putc('\0', &string_builder);
	console_print_verbose("%s", string_builder.data);
	if (out) {
		fprintf(out, "%s", string_builder.data);
	}
	memrw_destroy(&string_builder);
}

static void handle_dicom_tag_for_tag_dumping(dicom_series_t* series, dicom_instance_t* instance, dicom_data_element_t element) {
	u32 current_item_number = instance->pos_stack[instance->nesting_level].item_number;
	debug_print_dicom_element(instance, element, series->debug_output_file, instance->nesting_level,
	                          current_item_number);
}

static i64 dicom_parse_integer_string(const char* s, size_t len) {
	// Notes:
	// * the string will generally NOT be zero-terminated
	// * only base 10
	// * leading and trailing spaces are not significant
	i64 result = 0;
	bool positive = true;
	bool in_leading_section = true;
	for (i32 i = 0; i < len; ++i) {
		char c = s[i];
		if (c == '\0') break;
		if (in_leading_section) {
			if (c == ' ') continue;
			else if (c == '-') {
				positive = false;
				in_leading_section = false;
				continue;
			} else if (c == '+') {
				in_leading_section = false;
				continue;
			}
		}
		in_leading_section = false;
		if (c >= '0' && c <= '9') {
			result = (result * 10) + (c - '0');
		} else {
			break; // any other character other than a decimal terminates the number
		}
	}
	if (!positive) {
		result = -result;
	}
	return result;
}


dicom_cs_t dicom_parse_code_string(const char* s, size_t len, const char** next) {
	dicom_cs_t result = {};
	i32 bytes_written = 0;
	bool in_leading_section = true;
	if (next) *next = NULL; // set to position after \ separator if there is another value
	for (i32 i = 0; i < len; ++i) {
		char c = s[i];
		if (c == '\0') break;
		if (in_leading_section && c == ' ') continue;
		in_leading_section = false;
		if (c == '\\') {
			if (next) *next = s + i + 1;
			break;
		}
		result.value[bytes_written++] = c;
		if (bytes_written >= COUNT(result.value)-1) break;
	}
	return result;
}

static void dicom_interpret_top_level_data_element(dicom_instance_t* instance, dicom_data_element_t element) {
	u8* data = instance->data + element.data_offset;
	char* data_str = (char*)data;

	switch(element.tag.group) {
		default: break;
		case 0x0002: {
			if (element.vr == DICOM_VR_UI) {
				if (element.tag.as_enum == DICOM_MediaStorageSOPClassUID) {
					dicom_uid_enum uid = dicom_uid_lookup(data_str, element.length);
					instance->media_storage_sop_class_uid = uid;
					if (uid == DICOM_VLWholeSlideMicroscopyImageStorage) {

					}
				}
			}
		} break;
		case 0x0008: {
			switch(element.tag.as_u32) {
				default: break;
				case DICOM_ImageType: {
					// TODO: handle by SOP Class
					// Parse multi-valued code string, e.g. "ORIGINAL\PRIMARY\VOLUME\NONE"
					// for WSI: https://dicom.nema.org/medical/dicom/current/output/chtml/part03/sect_C.8.12.4.html#sect_C.8.12.4.1.1
					temp_memory_t temp = begin_temp_memory_on_local_thread();
					char* data_copy = arena_push_size(temp.arena, element.length + 1);
					memcpy(data_copy, data_str, element.length);
					data_copy[element.length] = '\0';
					char** values = arena_push_array(temp.arena, element.length, char*); // worst case: every character is a separator \ character
					values[0] = data_copy;
					i32 value_count = 1;
					for (i32 i = 0; i < element.length; ++i) {
						char c = data_str[i];
						if (c == '\0') break;
						if (c == '\\') {
							values[value_count] = data_copy + (i+1);
							++value_count;
							data_copy[i] = '\0'; // stomp out the separator characters so that 0-terminated strings remain
						}
						// TODO: handle trailing/leading spaces, which the standard says are not significant
					}

					for (i32 i = 0; i < value_count; ++i) {
						char* value = values[i];

					}

					end_temp_memory(&temp);
				} break;
				case DICOM_SOPClassUID: {
					// TODO
				} break;
			}
		} break;
		case 0x0020: {
			switch(element.tag.as_u32) {
				default: break;
				case DICOM_StudyInstanceUID: {
					// TODO

				} break;
				case DICOM_SeriesInstanceUID: {
					// TODO
				} break;
				case DICOM_StudyID: {
					// TODO
				} break;
				case DICOM_SeriesNumber: {
					// TODO
				} break;
				case DICOM_InstanceNumber: {
					instance->instance_number = dicom_parse_integer_string(data_str, element.length);
				} break;
				case DICOM_PatientOrientation: {
					// TODO
				} break;
				case DICOM_FrameOfReferenceUID: {
					// TODO
				} break;
				case DICOM_PositionReferenceIndicator: {
					// TODO
				} break;

			}
		} break;
		case 0x0028: {
			switch(element.tag.as_u32) {
				default: break;
				case DICOM_SamplesPerPixel: {
					instance->samples_per_pixel = *(u16*)data;
				} break;
				case DICOM_PhotometricInterpretation: {
					dicom_photometric_interpretation_enum photometric_interpretation = DICOM_PHOTOMETRIC_INTERPRETATION_UNKNOWN;
					static const char* possible_values[] = {"MONOCHROME1", "MONOCHROME2", "PALETTE COLOR", "RGB", "HSV",
															"ARGB", "CMYK", "YBR_FULL", "YBR_FULL_422", "YBR_PARTIAL_422",
															"YBR_PARTIAL_420", "YBR_ICT", "YBR_RCT"};
					ASSERT(COUNT(possible_values) == DICOM_PHOTOMETRIC_INTERPRETATION_YBR_RCT); // enum value equals index+1
					size_t length = dicom_get_element_length_without_trailing_whitespace(data, element.length);
					for (i32 i = 0; i < COUNT(possible_values); ++i) {
						if (strncmp(data_str, possible_values[i], length) == 0) {
							photometric_interpretation = i + 1;
							break;
						}
					}
					if (photometric_interpretation == DICOM_PHOTOMETRIC_INTERPRETATION_UNKNOWN) {
						// TODO: decide error checking here or at the end?
						console_print("DICOM: unknown Photometric Interpretation '%s'\n", data_str);
						instance->is_image_invalid = true;
					}
					instance->photometric_interpretation = photometric_interpretation;
				} break;
				case DICOM_PlanarConfiguration: { instance->planar_configuration = *(u16*)data; } break;
				case DICOM_NumberOfFrames: {
					instance->number_of_frames = dicom_parse_integer_string(data_str, element.length);
				} break;
				case DICOM_Rows:                instance->rows = *(u16*)data; break;
				case DICOM_Columns:             instance->columns = *(u16*)data; break;
				case DICOM_BitsAllocated:       instance->bits_allocated = *(u16*)data; break;
				case DICOM_BitsStored:          instance->bits_stored = *(u16*)data; break;
				case DICOM_HighBit:             instance->high_bit = *(u16*)data; break;
				case DICOM_PixelRepresentation: instance->high_bit = *(u16*)data; break;
				case DICOM_BurnedInAnnotation: {

				} break;
				case DICOM_LossyImageCompression: {

				} break;
				case DICOM_LossyImageCompressionRatio: {

				} break;
				case DICOM_LossyImageCompressionMethod: {
					dicom_lossy_image_compression_method_enum method = DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_UNKNOWN;
					static const char* possible_values[] = {"ISO_10918_1", "ISO_14495_1", "ISO_15444_1", "ISO_13818_2", "ISO_14496_10", "ISO_23008_2"};
					ASSERT(COUNT(possible_values) == DICOM_LOSSY_IMAGE_COMPRESSION_METHOD_ISO_23008_2); // enum value equals index+1
					size_t length = dicom_get_element_length_without_trailing_whitespace(data, element.length);
					for (i32 i = 0; i < COUNT(possible_values); ++i) {
						if (strncmp(data_str, possible_values[i], length) == 0) {
							method = i + 1;
							break;
						}
					}
					instance->lossy_image_compression_method = method;
				} break;
			}
		} break;
	}

	if (instance->media_storage_sop_class_uid == DICOM_VLWholeSlideMicroscopyImageStorage) {
		// TODO: handle microscopy stuff
		dicom_wsi_interpret_top_level_data_element(instance, element);
	}
}


bool dicom_read_chunk(dicom_instance_t* instance) {

	for (;;) {
		dicom_parser_pos_t* current_position = instance->pos_stack + instance->nesting_level;

		// Check if we reached the end of a sequence or item
		if (instance->nesting_level > 0) {
			dicom_parser_pos_t* parent_position = instance->pos_stack + instance->nesting_level - 1;
			dicom_data_element_t* parent_element = &parent_position->element;
			if (parent_element->length != DICOM_UNDEFINED_LENGTH) {
				if (current_position->offset >= parent_position->offset + parent_element->length) {
					// TODO: finalize sequence?
					// Pop
					--instance->nesting_level;
					--current_position;
					// Advance position
					current_position->offset = parent_element->data_offset + parent_element->length;
					if (parent_element->tag.as_u32 == DICOM_Item) {
						++current_position->item_number;
					}
					continue;
				}
			}
		}

		i64 bytes_left = instance->total_bytes_in_stream - current_position->offset;
		dicom_data_element_t element = read_dicom_data_element(instance->data, current_position->offset, instance->encoding, bytes_left);

		// Perform checks to prevent going out of bounds
		if (!element.is_valid) {
			return false; // not enough bytes left for the tag/header of this element
		}

		i64 data_bytes_left = instance->bytes_read_from_file - element.data_offset;
		bool known_enough_bytes_left = false;
		if (element.length != DICOM_UNDEFINED_LENGTH) {

			// Special case: start of unencapsulated Pixel Data
			if (element.tag.as_u32 == DICOM_PixelData) {
				instance->found_pixel_data = true;
				instance->is_pixel_data_encapsulated = false;
				instance->pixel_data = element;
			}

			if (data_bytes_left >= element.length) {
				known_enough_bytes_left = true;
			} else {
				return false; // not enough bytes left to read the data of this element
			}
		}

		current_position->element = element;
		// Switch to a different encoding scheme (Explicit or Implicit VR), if specified in the File Meta info.
		if (element.tag.as_u32 == DICOM_TransferSyntaxUID && known_enough_bytes_left) {
			dicom_switch_data_encoding(instance, &element);
		}

		// Do actions for specific tags
//		ASSERT(dicom_state->tag_handler_func != NULL);
		dicom_series_t* series = instance->series;
		if (series) {
			if (series->tag_handler_func != NULL) {
				// Dump all tags (except Item and ItemDelimitationItem to reduce spam)
				bool want_dump = true;
				if (element.tag.as_u32 == DICOM_ItemDelimitationItem) {
					want_dump = false;
				} else if (element.tag.as_u32 == DICOM_Item || element.tag.as_u32 == DICOM_SequenceDelimitationItem) {
					if (instance->nesting_level > 0) {
						dicom_parser_pos_t* parent_position = instance->pos_stack + instance->nesting_level - 1;
						dicom_data_element_t* parent_element = &parent_position->element;
						if (parent_element->vr == DICOM_VR_SQ) {
							want_dump = false;
						}
					}
				}

				if (want_dump) {
					series->tag_handler_func(series, instance, element);
				}
			}
		}

		if (known_enough_bytes_left && element.vr != DICOM_VR_SQ) {
			dicom_interpret_top_level_data_element(instance, element);
		}


		bool need_pop = false;
		bool need_push = false;
		bool need_increment_item_number = false; // for items that are not part of a SQ, e.g. PixelData items

		if (element.tag.as_u32 == DICOM_ItemDelimitationItem || element.tag.as_u32 == DICOM_SequenceDelimitationItem) {
			if (element.tag.as_u32 == DICOM_ItemDelimitationItem) {
				need_increment_item_number = true;
			}
			need_pop = true;
		}

		if (need_pop) {
			//return element.data - start;
			if (instance->nesting_level > 0) {
				--instance->nesting_level;
				--current_position;
				dicom_parser_pos_t* parent_position = instance->pos_stack + instance->nesting_level;
				dicom_data_element_t* parent_element = &parent_position->element;

				// End of sequence was reached
				if (parent_element->length == DICOM_UNDEFINED_LENGTH) {
					// Hack: advance correctly to the next element by retroactively telling what the length was.
					parent_element->length = element.data_offset - parent_element->data_offset;
				}
			} else {
				// TODO: handle error condition
				panic();
			}
		} else {

			if (element.vr == DICOM_VR_SQ) {
				need_push = true;
			} else if (element.tag.as_u32 == DICOM_Item) {
				if (instance->nesting_level > 0) {
					dicom_parser_pos_t* parent_position = instance->pos_stack + instance->nesting_level - 1;
					dicom_data_element_t* parent_element = &parent_position->element;
					if (parent_element->vr == DICOM_VR_SQ) {
						// This is an item in a sequence -> data contains new data elements -> push is needed
						need_push = true;
					} else {
						// Maybe this is encapsulated pixel data -> no pushing needed
						need_increment_item_number = true;
						if (parent_element->tag.as_u32 == DICOM_PixelData) {
							if (current_position->item_number == 0) {
								console_print_verbose("Found Basic Offset Table at offset=%d\n", element.data_offset);
								if (known_enough_bytes_left && element.length % 4 == 0) {
									instance->found_pixel_data = true;
									// TODO: is this always true?
									// https://dicom.nema.org/dicom/2013/output/chtml/part05/sect_A.4.html
									u32 offset_count = element.length / 4;
									if (offset_count > 0) {
										instance->has_basic_offset_table = true;
										instance->pixel_data_offset_count = offset_count;
										arrsetlen(instance->pixel_data_offsets, offset_count);
										ASSERT(instance->pixel_data_offsets != NULL);
										ASSERT(offset_count * sizeof(u32) == element.length);
										memcpy(instance->pixel_data_offsets, instance->data + element.data_offset, offset_count * sizeof(u32));
									}

								} else {
									// TODO: handle error condition (malformed DICOM file?)
								}
							} else {
								// pixel data
							}
						}
					}
				} else {
					// unknown, does this ever happen?
				}
			} else if (element.tag.as_u32 == DICOM_PixelData) {
				if (element.length == DICOM_UNDEFINED_LENGTH) {
					need_push = true;
					instance->is_pixel_data_encapsulated = true;
				}
			}

			if (need_push) {

				dicom_parser_pos_t new_position = {};
				new_position.element_index = 0;
				new_position.offset = element.data_offset;
				if (element.tag.as_u32 == DICOM_Item) {
					new_position.item_number = current_position->item_number;
				} else {
					new_position.item_number = 0;
				}
				new_position.bytes_left_in_sequence_or_item = data_bytes_left;

				instance->pos_stack[instance->nesting_level] = *current_position;
				++instance->nesting_level; // TODO: bounds check
				++current_position;
				*current_position = new_position;

				continue;
			}
		}

		element = current_position->element;
		if (element.length == DICOM_UNDEFINED_LENGTH) {
			current_position->offset = element.data_offset;
		} else {
			current_position->offset = element.data_offset + element.length;
		}

		++current_position->element_index;
		if (need_increment_item_number) {
			++current_position->item_number;
		}

		bytes_left = instance->total_bytes_in_stream - current_position->offset;
		if (bytes_left < 8) {
			DUMMY_STATEMENT;
			break; // reached end of file
		}
	}
	return true;

}

dicom_instance_t dicom_load_file(dicom_series_t* dicom_series, file_info_t* file) {

	dicom_instance_t instance = {};
	instance.series = dicom_series;

	// TODO: pipe all debug output to private memrw_t for dumping it onto the console later (enable concurrent loading?)

	file_stream_t fp = file_stream_open_for_reading(file->filename);
	if (fp) {
		size_t chunk_size = KILOBYTES(64);
		size_t bytes_to_read = MIN(chunk_size, file->filesize);

		if (file->filesize > sizeof(dicom_header_t)) {
			memrw_t buffer = memrw_create(bytes_to_read);
			size_t bytes_read = file_stream_read(buffer.data, (size_t)bytes_to_read, fp);
			buffer.used_size = bytes_read;

			if (bytes_read == bytes_to_read) {
				if (is_file_a_dicom_file(buffer.data, bytes_read)) {
					console_print_verbose("Found DICOM file: '%s'\n", file->filename);
					i64 payload_offset = sizeof(dicom_header_t);
					if (dicom_series->debug_output_file) {
						fprintf(dicom_series->debug_output_file, "\nFile: %s\n\n", file->filename);
					}
					i64 payload_bytes = (buffer.used_size - payload_offset);
					ASSERT(payload_bytes > 0);
					i64 total_bytes_in_stream = file->filesize - payload_offset;
					ASSERT(total_bytes_in_stream >= payload_bytes);

					instance.encoding = DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_LITTLE_ENDIAN;
					instance.data = buffer.data + payload_offset;
					instance.bytes_read_from_file = bytes_read - payload_offset;
					instance.total_bytes_in_stream = file->filesize - payload_offset;

					// Read the DICOM file in chunks, until we hit the PixelData tag (which should hopefully have a
					// basic offset table as well for seeking/random access).
					// This way we don't need to read the whole file initially.
					for(;;) {
						bool finished = dicom_read_chunk(&instance);
						if (finished || instance.found_pixel_data) {
							instance.is_valid = !instance.is_image_invalid; // TODO: what to do here?
							break;
						} else {
							i64 bytes_left_in_file = file->filesize - instance.bytes_read_from_file;
							if (bytes_left_in_file <= 0) {
								// TODO: handle error condition
							}
							ASSERT(bytes_left_in_file > 0 && bytes_left_in_file < instance.total_bytes_in_stream);
							bytes_to_read = MIN(chunk_size, bytes_left_in_file);
							memrw_maybe_grow(&buffer, buffer.used_size + bytes_to_read);
							bytes_read = file_stream_read(buffer.data + buffer.used_size, (size_t)bytes_to_read, fp);
							buffer.used_size += bytes_read;
							buffer.cursor += bytes_read;
							instance.data = buffer.data + payload_offset; // need to update the pointer, maybe it moved due to resizing
							instance.bytes_read_from_file += bytes_read;

							bytes_left_in_file = file->filesize - instance.bytes_read_from_file;
							if (bytes_left_in_file <= 0) {
								DUMMY_STATEMENT;
							}
						}
					}

				}
			}

			memrw_destroy(&buffer);
		}
	}
	file_stream_close(fp);
	return instance;
}

typedef struct indexed_value_t {
	i64 value;
	i32 index;
} indexed_value_t;

// for qsort
static int compare_indexed_value (const void* a, const void* b) {
	return ( ((indexed_value_t*)b)->value - ((indexed_value_t*)a)->value );
}

bool dicom_open_from_directory(dicom_series_t* dicom, directory_info_t* directory) {
	i64 start = get_clock();

	#if DO_DEBUG
	dicom->debug_output_file = fopen("dicom_dump.txt", "wb");
	#endif
	dicom->tag_handler_func = handle_dicom_tag_for_tag_dumping;

	// TODO: load child directories as well.

	i32 file_count = arrlen(directory->dicom_files);
	for (i32 i = 0; i < file_count; ++i) {
		file_info_t* file = directory->dicom_files + i;
		dicom_instance_t instance = dicom_load_file(dicom, file);
		if (instance.is_valid) {
			arrput(dicom->instances, instance);
		}
	}

	if (dicom->debug_output_file) {
		fclose(dicom->debug_output_file);
	}

	console_print("DICOM: series has %d instances\n", arrlen(dicom->instances));

	// TODO: move much of this to dicom_wsi.c

	for (i32 i = 0; i < arrlen(dicom->instances); ++i) {
		dicom_instance_t* instance = dicom->instances + i;

		// TODO: handle concatenations
		// https://dicom.nema.org/medical/dicom/current/output/chtml/part03/sect_C.7.6.16.html#sect_C.7.6.16.1.3
		// Strategy: fold concatenated instances back into a single 'parent' instance, with links to the children?

		console_print_verbose("%d: #=%d flavor=%s w=%u h=%u\n", i, instance->instance_number, instance->image_flavor_cs.value,
		              instance->total_pixel_matrix_columns, instance->total_pixel_matrix_rows);
	}

	// Sort levels (volumes) by descending image width to get the
	indexed_value_t volume_image_widths[16] = {};
	i32 running_volume_index = 0;
	for (i32 i = 0; i < arrlen(dicom->instances); ++i) {
		dicom_instance_t* instance = dicom->instances + i;
		if (instance->image_flavor == DICOM_IMAGE_FLAVOR_VOLUME) {
			i64 pixel_count = instance->total_pixel_matrix_columns;
			volume_image_widths[running_volume_index++] = (indexed_value_t){pixel_count, i};
		}
	}
	i32 volume_count = running_volume_index;
	qsort(volume_image_widths, volume_count, sizeof(indexed_value_t), compare_indexed_value);

	// Verify that all the widths are different (we are not supporting concatenations or z-levels just yet)
	i64 previous_width = 0;
	bool ok = true;
	for (i32 i = 0; i < volume_count; ++i) {
		i64 width = volume_image_widths[i].value;
		if (width == previous_width) {
			ok = false;
			break;
		}
		previous_width = width;
	}
	if (!ok) {
		// TODO: handle concatenations
		console_print("DICOM: multiple instances with same image width - can't determine levels\n");
	} else {
		dicom->wsi.level_count = volume_count;
		for (i32 i = 0; i < volume_count; ++i) {
			i32 instance_index = volume_image_widths[i].index;
			dicom_instance_t* instance = dicom->instances + instance_index;
			dicom->wsi.level_instances[i] = instance;
			console_print("level %d: #=%d w=%u h=%u\n", i, instance_index, instance->total_pixel_matrix_columns, instance->total_pixel_matrix_rows);
		}
	}

	console_print("DICOM parsing took %g seconds\n", get_seconds_elapsed(start, get_clock()));
	return true;
}

bool dicom_open_from_file(dicom_series_t* dicom, file_info_t* file) {
	i64 start = get_clock();

	#if DO_DEBUG
	dicom->debug_output_file = fopen("dicom_dump.txt", "wb");
	#endif
	dicom->tag_handler_func = handle_dicom_tag_for_tag_dumping;

	dicom_load_file(dicom, file);

	if (dicom->debug_output_file) {
		fclose(dicom->debug_output_file);
	}

	console_print("DICOM parsing took %g seconds\n", get_seconds_elapsed(start, get_clock()));
	return true;
}

bool is_file_a_dicom_file(u8* file_header_data, size_t file_header_data_len) {
	if (file_header_data_len > sizeof(dicom_header_t)) {
		dicom_header_t* dicom_header = (dicom_header_t*) file_header_data;
		u32 prefix = *(u32*) dicom_header->prefix;
		if (prefix == LE_4CHARS('D','I','C','M')) {
			return true;
		}
	}
	return false;
}


static u16 dicom_vr_tbl[] = {
	0, // undefined
	DICOM_VR_AE,
	DICOM_VR_AS,
	DICOM_VR_AT,
	DICOM_VR_CS,
	DICOM_VR_DA,
	DICOM_VR_DS,
	DICOM_VR_DT,
	DICOM_VR_FD,
	DICOM_VR_FL,
	DICOM_VR_IS,
	DICOM_VR_LO,
	DICOM_VR_LT,
	DICOM_VR_OB,
	DICOM_VR_OD,
	DICOM_VR_OF,
	DICOM_VR_OL,
	DICOM_VR_OV,
	DICOM_VR_OW,
	DICOM_VR_PN,
	DICOM_VR_SH,
	DICOM_VR_SL,
	DICOM_VR_SQ,
	DICOM_VR_SS,
	DICOM_VR_ST,
	DICOM_VR_SV,
	DICOM_VR_TM,
	DICOM_VR_UC,
	DICOM_VR_UI,
	DICOM_VR_UL,
	DICOM_VR_UN,
	DICOM_VR_UR,
	DICOM_VR_US,
	DICOM_VR_UT,
	DICOM_VR_UV,
};

void dicom_dict_init_hash_table() {
	ASSERT(dicom_dict_entries);
	dicom_dict_hash_table_size = next_pow2(COUNT(dicom_dict_packed_entries) * 4);
	dicom_dict_hash_table = (dicom_dict_entry_t*)calloc(1, dicom_dict_hash_table_size * sizeof(dicom_dict_entry_t));
	i32 collision_count = 0;
	i32 extra_lookup_count = 0;
	for (i32 i = 0; i < COUNT(dicom_dict_packed_entries); ++i) {
		dicom_dict_entry_t entry = dicom_dict_entries[i];
		u32 hash = lowbias32(entry.tag);
		u32 index = hash % dicom_dict_hash_table_size;
		dicom_dict_entry_t* slot = dicom_dict_hash_table + index;
		if (slot->tag == 0) {
			// empty slot!
			*slot = entry;
		} else {
			++collision_count;
			i32 cluster_size = 1;
			bool resolved = false;
			for (i32 j = 1; j < dicom_dict_hash_table_size; ++j) {
				// Use linear probing for collision resolution
				i32 new_index = (index + (j)) % dicom_dict_hash_table_size;
				slot = dicom_dict_hash_table + new_index;
				if (slot->tag == 0) {
					*slot = entry;
					resolved = true;
					break; // resolved
				} else {
					++cluster_size;
//					console_print("Hash table collision: extra lookups %d\n", cluster_size);
				}
			}
			if (!resolved) {
				panic();
			}
			extra_lookup_count += cluster_size;
		}
	}
	console_print_verbose("Hash table size: %d entries: %d (load factor %.2f) collisions: %d extra lookups: %d", dicom_dict_hash_table_size,
				  COUNT(dicom_dict_packed_entries),
				  (float)COUNT(dicom_dict_packed_entries)/(float)dicom_dict_hash_table_size,
				  collision_count, extra_lookup_count);
}

bool dicom_unpack_and_decompress_dictionary() {
	// Unpack dictionary entries, these have been packed to take up ~0.5x space
	// - convert 1-byte name/keyword lengths back into 4-byte offsets into the string pool
	// - convert 1-byte VR lookup indices back into 2-byte VR codes as used in DICOM streams
	// (this packed data is not much further compressible using LZ4, and is therefore left uncompressed)
	dicom_dict_entry_t* unpacked_entries = (dicom_dict_entry_t*) malloc(COUNT(dicom_dict_packed_entries) * sizeof(dicom_dict_entry_t));
	u32 running_offset = 1; // the first byte is a '\0' character (for the null / empty string), so start at offset=1
	for (i32 i = 0; i < COUNT(dicom_dict_packed_entries); ++i) {
		dicom_dict_packed_entry_t packed_entry = dicom_dict_packed_entries[i];
		dicom_dict_entry_t entry = {};
		entry.tag = packed_entry.tag;
		entry.name_offset = running_offset;
		running_offset += packed_entry.name_len + 1; // one extra byte for the '\0'
		entry.keyword_offset = running_offset;
		running_offset += packed_entry.keyword_len + 1;
		ASSERT(running_offset <= DICOM_DICT_STRING_POOL_UNCOMPRESSED_SIZE);
		entry.vr = dicom_vr_tbl[packed_entry.vr_index];
		unpacked_entries[i] = entry;
	}
	dicom_dict_entries = unpacked_entries;

	// LZ4-decompress string pool, which contains the DICOM tag names and keywords
	i32 decompressed_size = DICOM_DICT_STRING_POOL_UNCOMPRESSED_SIZE;
	i32 compressed_size = DICOM_DICT_STRING_POOL_COMPRESSED_SIZE;
	u8* decompressed = (u8*) malloc(decompressed_size);
	i32 bytes_decompressed = LZ4_decompress_safe((char*)dicom_dict_string_pool_lz4_compressed, (char*)decompressed, compressed_size, decompressed_size);
	if (bytes_decompressed <= 0) {
		console_print_error("LZ4_decompress_safe() failed (return value %d)\n", bytes_decompressed);
		free(decompressed);
		return false;
	} else {
		if (bytes_decompressed != decompressed_size) {
			console_print_error("LZ4_decompress_safe() decompressed %d bytes, however the expected size was %d\n", bytes_decompressed, decompressed_size);
			free(decompressed);
			return false;
		} else {
			dicom_dict_string_pool = (const char*)decompressed;
			return true;
		}
	}
}

bool dicom_init() {
	i64 start = get_clock();
	bool success = dicom_unpack_and_decompress_dictionary();
	if (success) {
		dicom_dict_init_hash_table();
		console_print_verbose("Initialized DICOM dictionary in %g seconds.\n", get_seconds_elapsed(start, get_clock()));
	}
	return success;
}
