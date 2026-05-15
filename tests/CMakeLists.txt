
add_executable(slidescape_tests
        test_main.cpp
        test_fixtures.cpp
        test_mathutils.cpp
        test_memrw.cpp
        test_stb_sprintf.cpp
        test_support.cpp
        test_stringutils.cpp
        test_work_queue.cpp
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
        SLIDESCAPE_TEST_FIXTURE_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/manifest.txt"
        SLIDESCAPE_TEST_LOCAL_FIXTURE_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/local_manifest.txt"
        ISYNTAX_JPEG_DECODER_USE_LIBJPEG=1
)
target_link_libraries(slidescape_tests PRIVATE slidescape_nongui slidescape_jpeg)
if (WIN32)
        target_link_libraries(slidescape_tests PRIVATE winmm)
endif()

add_test(NAME slidescape_tests COMMAND slidescape_tests)
