cmake_minimum_required(VERSION 2.8)
project(slideviewer)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -msse2")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/deps")
include_directories("${CMAKE_SOURCE_DIR}/deps/imgui")

add_executable(slideviewer
        win32_main.c
        viewer.c
        glad.c
        stringutils.c
        shader.c
        imgui.cpp
        imgui_demo.cpp
        imgui_draw.cpp
        imgui_widgets.cpp
        imgui_impl_opengl3.cpp
        imgui_impl_win32.cpp
        gui.cpp
)
target_link_libraries(slideviewer user32 gdi32 winmm opengl32)
