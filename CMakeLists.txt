cmake_minimum_required(VERSION 2.8)
project(slideviewer)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(GCC_WARN_OPTIONS "-Wall -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-variable -Wno-missing-braces -Wno-format -Wno-strict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2 ${GCC_WARN_OPTIONS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 ${GCC_WARN_OPTIONS} -Wno-class-memaccess")

# In MinSizeRel builds, further reduce file size by removing symbol table and relocation information from the executable
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -s")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -s")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/deps")
include_directories("${CMAKE_SOURCE_DIR}/deps/imgui")
include_directories("${CMAKE_SOURCE_DIR}/deps/jpeg")

# libjpeg-turbo requires these files for JPEG decoding support
set(JPEG_SOURCE_FILES
        jerror.c    jaricom.c   jcapimin.c  jcmarker.c  jcomapi.c
        jdapimin.c  jdapistd.c  jdapistd.c  jdarith.c   jdcoefct.c
        jdcolor.c   jddctmgr.c  jdhuff.c    jdinput.c   jdmainct.c
        jdmarker.c  jdmaster.c  jdmerge.c   jdphuff.c   jdpostct.c
        jdsample.c  jidctflt.c  jidctfst.c  jidctint.c  jidctred.c
        jmemmgr.c   jmemnobs.c  jquant1.c   jquant2.c   jutils.c
)

# extra files needed for JPEG compression support
set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES}
        jcapistd.c  jdatadst.c  jcparam.c   jcinit.c    jcmaster.c
        jccolor.c   jcsample.c  jcprepct.c  jcdctmgr.c  jcarith.c
        jctrans.c   jchuff.c    jccoefct.c  jcmainct.c  jcphuff.c
        jfdctflt.c  jfdctfst.c  jfdctint.c
)

# https://stackoverflow.com/questions/13103018/how-to-prepend-cmake-current-source-dir-to-all-files-in-variable
function(prepend_path SOURCE_FILES PREFIX)
    foreach(SOURCE_FILE ${${SOURCE_FILES}})
        set(MODIFIED ${MODIFIED} ${PREFIX}/${SOURCE_FILE})
    endforeach()
    set(${SOURCE_FILES} ${MODIFIED} PARENT_SCOPE)
endfunction()
prepend_path(JPEG_SOURCE_FILES deps/jpeg)

set(WITH_SIMD 1) # Set to 0 to disable the SIMD code for libjpeg-turbo
add_subdirectory(deps/jpeg/simd)
if (WITH_SIMD EQUAL 1)
    set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES} $<TARGET_OBJECTS:simd> ${SIMD_OBJS})
else()
    set(JPEG_SOURCE_FILES ${JPEG_SOURCE_FILES} deps/jpeg/jsimd_none.c)
endif()

set(VIEWER_SOURCE_FILES
        win32_main.c
        platform.c
        viewer.c
        glad.c
        stringutils.c
        mathutils.c
        shader.c
        tiff.c
        tiff_write.c
        caselist.c
        annotation.cpp
        openslide_api.c
        imgui.cpp
        imgui_demo.cpp
        imgui_draw.cpp
        imgui_widgets.cpp
        imgui_impl_opengl3.cpp
        imgui_impl_win32.cpp
        imgui_freetype.cpp
        freetype_api.c
        gui.cpp
        jpeg_decoder.c
        tlsclient.c
        lz4.c
        parson.c
        yxml.c
)
prepend_path(VIEWER_SOURCE_FILES src)

# client only supported on Windows x64 for now
if (WIN32)
add_executable(slideviewer ${VIEWER_SOURCE_FILES} ${JPEG_SOURCE_FILES})
target_link_libraries(slideviewer user32 gdi32 winmm ws2_32)
endif(WIN32)

set(SERVER_SOURCE_FILES
        platform.c
        server.c
        tiff.c
        jpeg_decoder.c
        lz4.c
        stringutils.c
)
prepend_path(SERVER_SOURCE_FILES src)
add_executable(slideserver ${SERVER_SOURCE_FILES} ${JPEG_SOURCE_FILES})
target_compile_definitions(slideserver PRIVATE IS_SERVER=1)

if (WIN32)
    target_link_libraries(slideserver ws2_32 pthread)
else()
    target_link_libraries(slideserver pthread)
endif()
