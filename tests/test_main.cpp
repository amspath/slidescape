#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// NOTE: guards for extern "C" {} are not needed here, as they are already in the header files
#include "stringutils.h"

TEST_CASE("example stringutils behavior") {
    const char* ext = get_file_extension("test.tiff");
    bool result = strcasecmp(ext, "tiff") == 0;
    CHECK(result);
}

