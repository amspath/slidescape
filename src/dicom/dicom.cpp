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

#define DICOM_TAGS_IMPLEMENTATION
#include "dicom.h"
#include "dicom_dict.h"

#include "lz4.h"

#define LISTING_IMPLEMENTATION
#include "listing.h"

static void dicom_switch_data_encoding(dicom_t* dicom_state, dicom_data_element_t *transfer_syntax_uid) {

	// TODO: what if the transfer UID only applies to the interpretation of e.g. pixel data?
	// list of UIDs: https://dicom.nema.org/medical/dicom/current/output/chtml/part06/chapter_A.html

	// The first 18 chars do not discriminate (all have the prefix "1.2.840.10008.1.2")
	const char* suffix = (const char*) &transfer_syntax_uid->data[17];
	u32 uid_length = transfer_syntax_uid->length;

	if (uid_length == 20 && suffix[0] == '.') {
		if (suffix[1] == '1') {
			dicom_state->encoding = DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_LITTLE_ENDIAN; // 1.2.840.10008.1.2.1
		} else if (suffix[1] == '2') {
			dicom_state->encoding = DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_BIG_ENDIAN_RETIRED; // 1.2.840.10008.1.2.2
		}
	} else if (uid_length == 23 && strncmp(suffix, ".1.99", 5) == 0) {
		dicom_state->encoding = DICOM_TRANSFER_SYNTAX_DEFLATED_EXPLICIT_VR_LITTLE_ENDIAN; // 1.2.840.10008.1.2.1.99
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

static dicom_data_element_t read_dicom_data_element(u8 *pos, dicom_transfer_syntax_enum encoding) {
	dicom_data_element_t result = {};
	result.tag = *(dicom_tag_t*) pos;

	if (encoding == DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_LITTLE_ENDIAN || result.tag.group == 2 /*File Meta info*/ ) {
		// Data element is Explicit VR.
		dicom_explicit_data_element_header_t* element_header = (dicom_explicit_data_element_header_t*) pos;
		result.vr = element_header->vr;

		// Some VRs have the value length field stored differently...
		if (need_alternate_element_layout(result.vr)) {
			result.length = *(u32*) &element_header->variable_part[+2];
			result.data = pos + 12; // Advance to value field
		} else {
			result.length = *(u16*) &element_header->variable_part[0];
			result.data = pos + 8;
		}
	} else {
		// Data element is Implicit VR.
		dicom_implicit_data_element_header_t* element_header = (dicom_implicit_data_element_header_t*) pos;
		result.tag = element_header->tag;
		result.length = element_header->value_length;
		result.data = element_header->data;
		result.vr = get_dicom_tag_vr(result.tag.as_u32); // look up the VR from the data dictionary.
	}

	return result;
}

static void debug_read_dicom_dataset_recursive(dicom_t* dicom_state, u8* pos, u8* end, dicom_transfer_syntax_enum encoding,
                                               u32 item_number = 0) {
	i32 element_count = 0;
	do {
		// Read an element
		dicom_data_element_t element = read_dicom_data_element(pos, encoding);


		// Switch to a different encoding scheme (Explicit or Implicit VR), if specified in the File Meta info.
		if (element.tag.as_u32 == DICOM_TransferSyntaxUID) {
			dicom_switch_data_encoding(dicom_state, &element);
		}

		// Do actions for specific tags
//		ASSERT(dicom_state->tag_handler_func != NULL);
		dicom_state->current_item_number = item_number;
		if (dicom_state->tag_handler_func != NULL) {
			dicom_state->tag_handler_func(element.tag, element, dicom_state);
		}

		// TODO: handle sequence with unspecified length

		if (element.vr == DICOM_VR_SQ) {
			// Start reading the items
			pos = element.data;
			u8* sequence_end = element.data + element.length;
			u32 sequence_item_number = 0;
			while (pos < sequence_end /*&& sequence_item_number < 50*/) {
				dicom_sequence_item_t* item = (dicom_sequence_item_t*) pos;
				if (item->item_tag.as_u32 == DICOM_SequenceDelimitationItem) {
					break; // End of sequence was reached
				}
				// Read the "Item Value Data Set" recursively!
				++dicom_state->current_nesting_level;
				debug_read_dicom_dataset_recursive(dicom_state, item->data, item->data + item->length, encoding,
				                                   sequence_item_number);
				--dicom_state->current_nesting_level;

				pos = item->data + item->length;
				++sequence_item_number;
			}
		}

		++element_count;
		pos = element.data + element.length;
	} while (pos < end /*&& element_count <= 150*/);
}

static inline u32 dicom_get_element_length_without_trailing_whitespace(dicom_data_element_t* element) {
	u32 length = element->length;
	if (length != 0 && length % 2 == 0 && element->data[length-1] == ' ') {
		--length; // ignore trailing space character
	}
	return length;
}

// Print information about a data element
static void debug_print_dicom_element(dicom_data_element_t element, FILE* out) {
	char vr_text[4] = {}; // convert 2-byte VR to printable form
	*(u16*) vr_text = element.vr;
	const char* keyword = get_dicom_tag_keyword(element.tag.as_u32);
	fprintf(out, "(%04x,%04x) - %s - length: %d - %s",
	        element.tag.group, element.tag.element, vr_text, element.length,
	        keyword);

	// Try to print out the value of the tag.
	if (element.vr == DICOM_VR_UI ||       // These VRs are all simple character strings.
	    element.vr == DICOM_VR_SH ||
	    element.vr == DICOM_VR_LO ||
	    element.vr == DICOM_VR_AE ||
	    element.vr == DICOM_VR_AS ||
	    element.vr == DICOM_VR_CS ||
	    element.vr == DICOM_VR_DS ||
	    element.vr == DICOM_VR_PN ||
	    element.vr == DICOM_VR_IS ||
	    element.vr == DICOM_VR_DA ||
	    element.vr == DICOM_VR_TM) {
		// print identifier
		char identifier[65];
		u32 length = MIN(64, dicom_get_element_length_without_trailing_whitespace(&element));
		strncpy(identifier, (const char*) element.data, length);
		identifier[length] = '\0';
		fprintf(out, " - \"%s\"", identifier);
	} else if (element.vr == DICOM_VR_UL) {
		u32 value = *(u32*) element.data;
		fprintf(out, " - %u", value);
	} else if (element.vr == DICOM_VR_US) {
		u32 value = *(u16*) element.data;
		fprintf(out, " - %u", value);
	} else if (element.vr == DICOM_VR_FL) {
		float value = *(float*) element.data;
		fprintf(out, " - %g", value);
	}
	putc('\n', out);
}

// Print information about a nested data element from a sequence.
static inline void
debug_print_dicom_element(dicom_data_element_t element, FILE* out, i32 nesting_level, u32 item_number) {
	if (nesting_level > 0) {
		for (i32 i = 1; i < nesting_level; ++i) {
			fprintf(out, "  "); // extra indentation
		}
		fprintf(out, "  %u: ", item_number);
	}
	debug_print_dicom_element(element, out);
}

static void
handle_dicom_tag_for_tag_dumping(dicom_tag_t tag, dicom_data_element_t element, dicom_t* dicom_parser) {
	debug_print_dicom_element(element, dicom_parser->debug_output_file, dicom_parser->current_nesting_level, dicom_parser->current_item_number);
}

void dicom_debug_load_file(dicom_t* dicom, const char* filename) {
	file_stream_t fp = file_stream_open_for_reading(filename);
	if (fp) {
		i64 filesize = file_stream_get_filesize(fp);
		if (filesize > sizeof(dicom_header_t)) {
			memrw_t buffer = memrw_create(filesize);
			buffer.used_size = file_stream_read(buffer.data, (size_t)filesize, fp);


			dicom_header_t* dicom_header = (dicom_header_t*) buffer.data;
			u32 prefix = *(u32*) dicom_header->prefix;

			if (prefix == LE_4CHARS('D','I','C','M')) {
				console_print("Hooray! '%s' looks like a valid DICOM file!\n", filename);
				buffer.cursor = sizeof(dicom_header_t);
				if (dicom->debug_output_file) {
					fprintf(dicom->debug_output_file, "\nFile: %s\n\n", filename);
				}
				debug_read_dicom_dataset_recursive(dicom, buffer.data + buffer.cursor, buffer.data + (buffer.used_size - buffer.cursor), DICOM_TRANSFER_SYNTAX_EXPLICIT_VR_LITTLE_ENDIAN);

			} else {
				//printf("Oh no! '%s' does not seem to be a DICOM file!\n", filename);
			}
			memrw_destroy(&buffer);
		}
	}
	file_stream_close(fp);


	// TODO: create ingestion routine




}


void dicom_open(dicom_t* dicom, const char* path) {
	if (is_directory(path)) {
		directory_listing_t* directory_listing = create_directory_listing_and_find_first_file(path, NULL);
		if (directory_listing != NULL) {

#if DO_DEBUG
			dicom->debug_output_file = fopen("dicom_dump.txt", "wb");
			dicom->tag_handler_func = handle_dicom_tag_for_tag_dumping;
#endif
			i64 start = get_clock();

			do {
				char* current_filename = get_current_filename_from_directory_listing(directory_listing);
				char full_filename[512];
				snprintf(full_filename, sizeof(full_filename), "%s" PATH_SEP "%s", path, current_filename);
				console_print("File: %s\n", full_filename);

#if DO_DEBUG
				if (strcmp(current_filename, "10_0") == 0) {
					dicom_debug_load_file(dicom, full_filename);
				}
#endif

//				FILE* fp = fopen(full_filename, "rb");
			} while (find_next_file(directory_listing));
			close_directory_listing(directory_listing);

//			console_print("DICOM parsing took %g seconds\n", get_seconds_elapsed(start, get_clock()));

			if (dicom->debug_output_file) {
				fclose(dicom->debug_output_file);
			}

		}
	}
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
	u32 running_offset = 0;
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
