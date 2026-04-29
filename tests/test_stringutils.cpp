#include "common.h"
#include "doctest.h"

#include "stringutils.h"

TEST_CASE("get_file_extension handles paths and missing extensions") {
	CHECK(strcmp(get_file_extension("test.tiff"), "tiff") == 0);
	CHECK(strcmp(get_file_extension("C:\\slides\\case.name\\slide.isyntax"), "isyntax") == 0);
	CHECK(strcmp(get_file_extension("/tmp/archive/file"), "") == 0);
	CHECK(strcmp(get_file_extension("/tmp/archive.with.dots/file"), "") == 0);
}

TEST_CASE("replace_file_extension replaces, appends, and strips extensions") {
	char with_extension[64] = "slide.tiff";
	replace_file_extension(with_extension, COUNT(with_extension), "xml");
	CHECK(strcmp(with_extension, "slide.xml") == 0);

	char without_extension[64] = "slide";
	replace_file_extension(without_extension, COUNT(without_extension), "tiff");
	CHECK(strcmp(without_extension, "slide.tiff") == 0);

	char strip_extension[64] = "slide.isyntax";
	replace_file_extension(strip_extension, COUNT(strip_extension), "");
	CHECK(strcmp(strip_extension, "slide") == 0);
}

TEST_CASE("split_into_lines handles mixed newline conventions") {
	char buffer[] = "alpha\r\nbeta\ngamma\rdelta";
	size_t line_count = 0;
	char** lines = split_into_lines(buffer, &line_count);

	REQUIRE(lines != NULL);
	CHECK(line_count == 4);
	CHECK(strcmp(lines[0], "alpha") == 0);
	CHECK(strcmp(lines[1], "beta") == 0);
	CHECK(strcmp(lines[2], "gamma") == 0);
	CHECK(strcmp(lines[3], "delta") == 0);

	free(lines);
}
