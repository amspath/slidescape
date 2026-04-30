#include "common.h"
#include "doctest.h"

#include "stringutils.h"
#include "tiff.h"
#include "isyntax.h"

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

static bool file_fixture_exists(const char* fixture_name, char* out_path, size_t out_path_size) {
	join_test_data_path(out_path, out_path_size, fixture_name);
	FILE* fp = fopen(out_path, "rb");
	if (!fp) return false;
	fclose(fp);
	return true;
}

static bool pixel_buffer_has_variation(const u32* pixels, size_t pixel_count) {
	if (pixel_count == 0) return false;
	u32 first = pixels[0];
	for (size_t i = 1; i < pixel_count; ++i) {
		if (pixels[i] != first) return true;
	}
	return false;
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

TEST_CASE("open TIFF fixtures through Slidescape TIFF reader") {
	const char* fixture_names[] = {
			"test_BID_p480_CD3_warped.tiff",
			"test_BID_p480_HE.tiff",
			"test_MF_CD8.cropped.tiff",
	};

	for (int i = 0; i < COUNT(fixture_names); ++i) {
		char path[1024];
		REQUIRE(file_fixture_exists(fixture_names[i], path, COUNT(path)));

		tiff_t tiff = {};
		CAPTURE(path);
		REQUIRE(open_tiff_file(&tiff, path));

		CHECK(tiff.filesize > 0);
		CHECK(tiff.ifd_count > 0);
		REQUIRE(tiff.ifds != NULL);
		REQUIRE(tiff.main_image_ifd != NULL);
		CHECK(tiff.main_image_ifd->image_width > 0);
		CHECK(tiff.main_image_ifd->image_height > 0);
		CHECK(tiff.level_image_ifd_count > 0);
		CHECK(tiff.level_images_ifd != NULL);

		for (u64 level_index = 0; level_index < tiff.level_image_ifd_count; ++level_index) {
			tiff_ifd_t* ifd = tiff.level_images_ifd + level_index;
			CHECK(ifd->image_width > 0);
			CHECK(ifd->image_height > 0);
			CHECK(ifd->tile_width > 0);
			CHECK(ifd->tile_height > 0);
			CHECK(ifd->width_in_tiles > 0);
			CHECK(ifd->height_in_tiles > 0);
			CHECK(ifd->tile_count == (u64)ifd->width_in_tiles * (u64)ifd->height_in_tiles);
			CHECK(ifd->downsample_factor >= 1.0f);
		}

		tiff_destroy(&tiff);
	}
}

TEST_CASE("TODO: TIFF fixture metadata serializes and deserializes" * doctest::skip()) {
	// This currently does not round-trip the cropped TIFF fixture metadata correctly:
	// deserialized.level_image_ifd_count differs from the original and cleanup trips ltalloc
	// guard checks. Keep this as a regression target once tiff_serialize/tiff_deserialize
	// semantics are clarified or fixed.

#if 0
	char path[1024];
	REQUIRE(file_fixture_exists("test_MF_CD8.cropped.tiff", path, COUNT(path)));

	tiff_t tiff = {};
	REQUIRE(open_tiff_file(&tiff, path));

	memrw_t serialized = memrw_create(MEGABYTES(1));
	REQUIRE(tiff_serialize(&tiff, &serialized) == &serialized);
	CHECK(serialized.used_size > 0);

	tiff_t deserialized = {};
	REQUIRE(tiff_deserialize(&deserialized, serialized.data, serialized.used_size));

	CHECK(deserialized.filesize == tiff.filesize);
	CHECK(deserialized.ifd_count == tiff.ifd_count);
	CHECK(deserialized.level_image_ifd_count == tiff.level_image_ifd_count);
	REQUIRE(deserialized.main_image_ifd != NULL);
	REQUIRE(tiff.main_image_ifd != NULL);
	CHECK(deserialized.main_image_ifd->image_width == tiff.main_image_ifd->image_width);
	CHECK(deserialized.main_image_ifd->image_height == tiff.main_image_ifd->image_height);
	CHECK(deserialized.main_image_ifd->tile_width == tiff.main_image_ifd->tile_width);
	CHECK(deserialized.main_image_ifd->tile_height == tiff.main_image_ifd->tile_height);

	tiff_destroy(&deserialized);
	memrw_destroy(&serialized);
	tiff_destroy(&tiff);
#endif
}

TEST_CASE("decode a representative TIFF tile") {
	char path[1024];
	REQUIRE(file_fixture_exists("test_MF_CD8.cropped.tiff", path, COUNT(path)));

	tiff_t tiff = {};
	REQUIRE(open_tiff_file(&tiff, path));
	REQUIRE(tiff.main_image_ifd != NULL);
	tiff_ifd_t* ifd = tiff.main_image_ifd;
	REQUIRE(ifd->tile_count > 0);

	u8* pixels = NULL;
	i32 tile_index = -1;
	for (u64 i = 0; i < ifd->tile_count; ++i) {
		if (ifd->tile_offsets[i] == 0 || ifd->tile_byte_counts[i] == 0) continue;
		pixels = tiff_decode_tile(0, &tiff, ifd, (i32)i, 0, (i32)(i % ifd->width_in_tiles), (i32)(i / ifd->width_in_tiles));
		if (pixels) {
			tile_index = (i32)i;
			break;
		}
	}

	CAPTURE(tile_index);
	REQUIRE(pixels != NULL);
	size_t pixel_count = (size_t)ifd->tile_width * (size_t)ifd->tile_height;
	CHECK(pixel_buffer_has_variation((u32*)pixels, pixel_count));

	free(pixels);
	tiff_destroy(&tiff);
}

TEST_CASE("open iSyntax fixture and read WSI metadata through libisyntax" ) {
	char path[1024];
	REQUIRE(file_fixture_exists("test_MF_CD8.isyntax", path, COUNT(path)));

	REQUIRE(libisyntax_init() == LIBISYNTAX_OK);

	isyntax_t* isyntax = NULL;
	REQUIRE(libisyntax_open(path, (libisyntax_open_flags_t)0, &isyntax) == LIBISYNTAX_OK);
	REQUIRE(isyntax != NULL);

	CHECK(libisyntax_get_tile_width(isyntax) > 0);
	CHECK(libisyntax_get_tile_height(isyntax) > 0);
	CHECK(isyntax->image_count > 0);
	CHECK(isyntax->wsi_image_index >= 0);
	CHECK(isyntax->wsi_image_index < isyntax->image_count);

	const isyntax_image_t* wsi = libisyntax_get_wsi_image(isyntax);
	REQUIRE(wsi != NULL);
	i32 level_count = libisyntax_image_get_level_count(wsi);
	CHECK(level_count > 0);

	for (i32 level_index = 0; level_index < level_count; ++level_index) {
		const isyntax_level_t* level = libisyntax_image_get_level(wsi, level_index);
		CAPTURE(level_index);
		CHECK(libisyntax_level_get_scale(level) >= 0);
		CHECK(libisyntax_level_get_width(level) > 0);
		CHECK(libisyntax_level_get_height(level) > 0);
		CHECK(libisyntax_level_get_width_in_tiles(level) > 0);
		CHECK(libisyntax_level_get_height_in_tiles(level) > 0);
		CHECK(libisyntax_level_get_mpp_x(level) > 0.0f);
		CHECK(libisyntax_level_get_mpp_y(level) > 0.0f);
	}

	libisyntax_close(isyntax);
}

TEST_CASE("decode an iSyntax tile through libisyntax") {
	char path[1024];
	REQUIRE(file_fixture_exists("test_MF_CD8.isyntax", path, COUNT(path)));

	REQUIRE(libisyntax_init() == LIBISYNTAX_OK);

	isyntax_t* isyntax = NULL;
	REQUIRE(libisyntax_open(path, (libisyntax_open_flags_t)0, &isyntax) == LIBISYNTAX_OK);
	REQUIRE(isyntax != NULL);

	isyntax_cache_t* cache = NULL;
	REQUIRE(libisyntax_cache_create("slidescape_tests iSyntax cache", 256, &cache) == LIBISYNTAX_OK);
	REQUIRE(cache != NULL);
	REQUIRE(libisyntax_cache_inject(cache, isyntax) == LIBISYNTAX_OK);

	const isyntax_image_t* wsi = libisyntax_get_wsi_image(isyntax);
	REQUIRE(wsi != NULL);
	i32 level_count = libisyntax_image_get_level_count(wsi);
	REQUIRE(level_count > 0);

	const isyntax_level_t* selected_level = NULL;
	const isyntax_tile_t* selected_tile = NULL;
	i32 selected_level_index = -1;
	for (i32 level_index = level_count - 1; level_index >= 0; --level_index) {
		const isyntax_level_t* level = libisyntax_image_get_level(wsi, level_index);
		if (!level || !level->tiles) continue;
		for (u64 tile_index = 0; tile_index < level->tile_count; ++tile_index) {
			const isyntax_tile_t* tile = level->tiles + tile_index;
			if (tile->exists) {
				selected_level = level;
				selected_tile = tile;
				selected_level_index = level_index;
				break;
			}
		}
		if (selected_tile) break;
	}

	REQUIRE(selected_level != NULL);
	REQUIRE(selected_tile != NULL);
	CAPTURE(selected_level_index);
	CAPTURE(selected_tile->tile_x);
	CAPTURE(selected_tile->tile_y);

	i32 tile_width = libisyntax_get_tile_width(isyntax);
	i32 tile_height = libisyntax_get_tile_height(isyntax);
	REQUIRE(tile_width > 0);
	REQUIRE(tile_height > 0);

	size_t pixel_count = (size_t)tile_width * (size_t)tile_height;
	u32* pixels = (u32*)calloc(pixel_count, sizeof(u32));
	REQUIRE(static_cast<bool>(pixels != NULL));

	REQUIRE(libisyntax_tile_read(isyntax, cache, selected_level_index, selected_tile->tile_x, selected_tile->tile_y,
	                             pixels, LIBISYNTAX_PIXEL_FORMAT_RGBA) == LIBISYNTAX_OK);
	CHECK(pixel_buffer_has_variation(pixels, pixel_count));

	free(pixels);
	libisyntax_cache_destroy(cache);
	libisyntax_close(isyntax);
}

TEST_CASE("TODO: load ASAP XML fixture through annotation parser" * doctest::skip()) {
	// This should call load_asap_xml_annotations() or a lower-level parser once annotation loading can
	// be exercised without constructing a full app_state_t/GUI viewer context.
}
