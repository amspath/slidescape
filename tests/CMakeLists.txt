set(TEST_JPEG_SOURCE_FILES)
foreach(SOURCE_FILE ${JPEG_SOURCE_FILES})
        list(APPEND TEST_JPEG_SOURCE_FILES ${CMAKE_SOURCE_DIR}/${SOURCE_FILE})
endforeach()

add_executable(slidescape_tests
        test_main.cpp
        test_fixtures.cpp
        test_mathutils.cpp
        test_memrw.cpp
        test_stb_sprintf.cpp
        test_support.cpp
        test_stringutils.cpp
        ${TEST_JPEG_SOURCE_FILES}
        ../src/utils/mathutils.c
        ../src/utils/memrw.c
        ../src/utils/stringutils.c
        ../src/third_party/ltalloc.cc
)

target_include_directories(slidescape_tests PRIVATE
        ../src
        ../src/core
        ../src/platform
        ../src/utils
        ../src/third_party
)

target_compile_definitions(slidescape_tests PRIVATE
        IS_SERVER=0
        SLIDESCAPE_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data_for_testing"
        ISYNTAX_JPEG_DECODER_USE_LIBJPEG=1
)
target_link_libraries(slidescape_tests PRIVATE slidescape_nongui)
if (WIN32)
        target_link_libraries(slidescape_tests PRIVATE winmm)
endif()

add_test(NAME slidescape_tests COMMAND slidescape_tests)
