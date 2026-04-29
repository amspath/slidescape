add_executable(slidescape_tests
        test_main.cpp
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

target_compile_definitions(slidescape_tests PRIVATE IS_SERVER=0)

add_test(NAME slidescape_tests COMMAND slidescape_tests)
