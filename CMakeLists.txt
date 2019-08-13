cmake_minimum_required(VERSION 2.8)
project(slideviewer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

if (WIN32)
    # Use the -mwindows compiler flag when compiling with MinGW to hide the console window
    # Only do this when not in debug mode
    if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mwindows")
    endif (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))

    # If the location of SDL2_DIR is not specified, we will try to guess it.
    if (NOT(DEFINED SDL2_DIR))
        cmake_policy(SET CMP0015 NEW) # suppress warning about relative paths
        set(SDL2_DIR "../../SDL2-2.0.10")
    endif()

    # Automatically detect whether we need the x86 or x64 version of the SDL2 library.
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SDL2_ARCH "x86_64-w64-mingw32")
    else()
        set(SDL2_ARCH "i686-w64-mingw32")
    endif()

    include_directories(${SDL2_DIR}/${SDL2_ARCH}/include)
    link_directories(${SDL2_DIR}/${SDL2_ARCH}/lib)
else()
    if (DEFINED SDL2_DIR)
        include_directories(${SDL2_DIR}/include)
        link_directories(${SDL2_DIR}/lib)
    else()
        # not supported yet
    endif()
endif()


include_directories("${CMAKE_SOURCE_DIR}/deps")
link_directories("${CMAKE_SOURCE_DIR}/deps-lib")

add_executable(slideviewer
        win32_main.c
        platform.c
        win32_multithreading.c
        openslide.c
        wsi.c
        font_test.c
        shader.c
        viewer.c
        glad.c
)
target_link_libraries(slideviewer user32 gdi32 winmm opengl32 freetype)

add_executable(slideviewer-sdl2
        sdl2_main.c
        platform.c
        win32_multithreading.c
        shader.c
        openslide.c
        wsi.c
#        viewer.c
        glad.c
)

if(WIN32)
    target_link_libraries(slideviewer-sdl2 mingw32 SDL2main SDL2 SDL2.dll winmm)
else()
    target_link_libraries(slideviewer-sdl2 SDL2main SDL2 m)
endif()

set_target_properties(slideviewer slideviewer-sdl2 PROPERTIES OUTPUT_NAME slideviewer)
