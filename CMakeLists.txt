cmake_minimum_required(VERSION 2.8)
project(slideviewer)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(GCC_WARN_OPTIONS "-Wall -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-variable -Wno-missing-braces -Wno-format -Wno-strict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2 ${GCC_WARN_OPTIONS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 ${GCC_WARN_OPTIONS}")

# In MinSizeRel builds, further reduce file size by removing symbol table and relocation information from the executable
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -s")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -s")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/deps")
include_directories("${CMAKE_SOURCE_DIR}/deps/imgui")
include_directories("${CMAKE_SOURCE_DIR}/deps/jpeg")

set(JPEG_SOURCE_FILES deps/jpeg/jerror.c
        deps/jpeg/jcapimin.c
        deps/jpeg/jcomapi.c
        deps/jpeg/jdapimin.c
        deps/jpeg/jdapistd.c
        deps/jpeg/jmemmgr.c
        deps/jpeg/jmemnobs.c
        deps/jpeg/jcmarker.c
        deps/jpeg/jdmarker.c
        deps/jpeg/jdinput.c
        deps/jpeg/jdapistd.c
        deps/jpeg/jdmaster.c
        deps/jpeg/jutils.c
        deps/jpeg/jquant1.c
        deps/jpeg/jquant2.c
        deps/jpeg/jdmerge.c
        deps/jpeg/jdcolor.c
        deps/jpeg/jdsample.c
        deps/jpeg/jdpostct.c
        deps/jpeg/jddctmgr.c
        deps/jpeg/jdarith.c
        deps/jpeg/jdhuff.c
        deps/jpeg/jdcoefct.c
        deps/jpeg/jdmainct.c
        deps/jpeg/jidctflt.c
        deps/jpeg/jidctfst.c
        deps/jpeg/jidctred.c
        deps/jpeg/jidctint.c
        deps/jpeg/jaricom.c
        deps/jpeg/jdphuff.c
)

set(WITH_SIMD 1) # Set to 0 to disable the SIMD code for libjpeg-turbo
add_subdirectory(deps/jpeg/simd)
if (WITH_SIMD EQUAL 1)
    set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES} $<TARGET_OBJECTS:simd> ${SIMD_OBJS})
else()
    set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES} deps/jpeg/jsimd_none.c)
endif()

# client only supported on Windows x64 for now
if (WIN32)
add_executable(slideviewer
        src/win32_main.c
        src/platform.c
        src/viewer.c
        src/glad.c
        src/stringutils.c
        src/mathutils.c
        src/shader.c
        src/tiff.c
        src/caselist.c
        src/annotation.cpp
        src/openslide.c
        src/imgui.cpp
        src/imgui_demo.cpp
        src/imgui_draw.cpp
        src/imgui_widgets.cpp
        src/imgui_impl_opengl3.cpp
        src/imgui_impl_win32.cpp
        src/gui.cpp
        src/jpeg_decoder.c
        src/tlsclient.c
        ${JPEG_SOURCE_FILES}
        src/lz4.c
        src/parson.c
        src/yxml.c
)
target_link_libraries(slideviewer user32 gdi32 winmm ws2_32)
endif(WIN32)

add_executable(slideserver
        src/platform.c
        src/server.c
        src/tiff.c
        src/jpeg_decoder.c
        ${JPEG_SOURCE_FILES}
        src/lz4.c
        src/stringutils.c
)
target_compile_definitions(slideserver PRIVATE IS_SERVER=1)

if (WIN32)
    target_link_libraries(slideserver ws2_32 pthread)
else()
    target_link_libraries(slideserver pthread)
endif()
