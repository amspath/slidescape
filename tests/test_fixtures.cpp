#include "common.h"
#include "doctest.h"

#include "stringutils.h"

#include <stdint.h>

#ifndef SLIDESCAPE_TEST_DATA_DIR
#define SLIDESCAPE_TEST_DATA_DIR "data_for_testing"
#endif

static void join_test_data_path(char* dest, size_t dest_size, const char* filename) {
	snprintf(dest, dest_size, "%s/%s", SLIDESCAPE_TEST_DATA_DIR, filename);
}

static bool read_file_prefix(const char* filename, u8* dest, size_t bytes_to_read) {
	FILE* fp = fopen(filename, "rb");
	if (!fp) return false;
	size_t bytes_read = fread(dest, 1, bytes_to_read, fp);
	fclose(fp);
	return bytes_read == bytes_to_read;
}

TEST_CASE("private WSI fixtures are present with expected extensions") {
	const char* fixture_names[] = {
			"test_BID_p480_CD3_warped.tiff",
			"test_BID_p480_HE.tiff",
			"test_MF_CD8.cropped.tiff",
			"test_MF_CD8.isyntax",
			"test_MF_CD8.xml",
	};

	for (int i = 0; i < COUNT(fixture_names); ++i) {
		char path[1024];
		join_test_data_path(path, COUNT(path), fixture_names[i]);

		FILE* fp = fopen(path, "rb");
		CAPTURE(path);
		REQUIRE(fp != NULL);
		fseek(fp, 0, SEEK_END);
		CHECK(ftell(fp) > 0);
		fclose(fp);
	}

	CHECK(strcmp(get_file_extension(fixture_names[0]), "tiff") == 0);
	CHECK(strcmp(get_file_extension(fixture_names[3]), "isyntax") == 0);
	CHECK(strcmp(get_file_extension(fixture_names[4]), "xml") == 0);
}

TEST_CASE("TIFF fixtures have valid TIFF or BigTIFF headers") {
	const char* fixture_names[] = {
			"test_BID_p480_CD3_warped.tiff",
			"test_BID_p480_HE.tiff",
			"test_MF_CD8.cropped.tiff",
	};

	for (int i = 0; i < COUNT(fixture_names); ++i) {
		char path[1024];
		u8 header[4] = {};
		join_test_data_path(path, COUNT(path), fixture_names[i]);

		CAPTURE(path);
		REQUIRE(read_file_prefix(path, header, sizeof(header)));

		bool little_endian_tiff = header[0] == 'I' && header[1] == 'I' && header[2] == 42 && header[3] == 0;
		bool little_endian_bigtiff = header[0] == 'I' && header[1] == 'I' && header[2] == 43 && header[3] == 0;
		bool big_endian_tiff = header[0] == 'M' && header[1] == 'M' && header[2] == 0 && header[3] == 42;
		bool big_endian_bigtiff = header[0] == 'M' && header[1] == 'M' && header[2] == 0 && header[3] == 43;
		CHECK((little_endian_tiff || little_endian_bigtiff || big_endian_tiff || big_endian_bigtiff));
	}
}

TEST_CASE("iSyntax fixture starts with XML metadata") {
	char path[1024];
	u8 header[256] = {};
	join_test_data_path(path, COUNT(path), "test_MF_CD8.isyntax");

	REQUIRE(read_file_prefix(path, header, sizeof(header)));
	CHECK(memcmp(header, "<?xml", 5) == 0);
	CHECK(static_cast<bool>(strstr((char*)header, "DPUfsImport") != NULL));
	CHECK(static_cast<bool>(strstr((char*)header, "DICOM_MANUFACTURER") != NULL));
}

TEST_CASE("ASAP XML fixture exposes at least one annotation node") {
	char path[1024];
	join_test_data_path(path, COUNT(path), "test_MF_CD8.xml");

	FILE* fp = fopen(path, "rb");
	REQUIRE(fp != NULL);
	fseek(fp, 0, SEEK_END);
	long file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char* xml = (char*)calloc((size_t)file_size + 1, 1);
	REQUIRE(static_cast<bool>(xml != NULL));
	REQUIRE(fread(xml, 1, (size_t)file_size, fp) == (size_t)file_size);
	fclose(fp);

	CHECK(static_cast<bool>(strstr(xml, "<Annotations") != NULL));
	CHECK(static_cast<bool>(strstr(xml, "<Annotation") != NULL));
	CHECK(static_cast<bool>(strstr(xml, "<Coordinate") != NULL));

	free(xml);
}

TEST_CASE("TODO: open TIFF fixtures through Slidescape TIFF reader" * doctest::skip()) {
	// This should call open_tiff_file(), assert pyramid metadata, serialize/deserialize metadata,
	// and decode at least one tile. It currently needs a reusable non-GUI test library target so
	// tests do not have to duplicate the full application source list.
}

TEST_CASE("TODO: open iSyntax fixture and read metadata/tile through libisyntax" * doctest::skip()) {
	// This should call libisyntax_open(), assert WSI level count/dimensions/mpp, read label or macro
	// image data, and decode a small representative region.
}

TEST_CASE("TODO: load ASAP XML fixture through annotation parser" * doctest::skip()) {
	// This should call load_asap_xml_annotations() or a lower-level parser once annotation loading can
	// be exercised without constructing a full app_state_t/GUI viewer context.
}
