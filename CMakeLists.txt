cmake_minimum_required(VERSION 2.8)
project(slideviewer)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/deps")
include_directories("${CMAKE_SOURCE_DIR}/deps/imgui")
include_directories("${CMAKE_SOURCE_DIR}/deps/jpeg")
include_directories("${CMAKE_SOURCE_DIR}/deps/tomcrypt")

add_executable(slideviewer
        win32_main.c
        viewer.c
        glad.c
        stringutils.c
        shader.c
        tiff.c
        imgui.cpp
        imgui_demo.cpp
        imgui_draw.cpp
        imgui_widgets.cpp
        imgui_impl_opengl3.cpp
        imgui_impl_win32.cpp
        gui.cpp
        jpeg_decoder.c
        tlsclient.c
        # files from the jpeg library:
        deps/jpeg/jerror.c
        deps/jpeg/jcapimin.c
        deps/jpeg/jcomapi.c
        deps/jpeg/jdapimin.c
        deps/jpeg/jdapistd.c
        deps/jpeg/jmemmgr.c
        deps/jpeg/jcmarker.c
        deps/jpeg/jdmarker.c
        deps/jpeg/jdinput.c
        deps/jpeg/jdapistd.c
        deps/jpeg/jdmaster.c
        deps/jpeg/jmemansi.c
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
        deps/jpeg/jidctint.c
        deps/jpeg/jaricom.c
)
target_link_libraries(slideviewer user32 gdi32 winmm opengl32 ws2_32)
