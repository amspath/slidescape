cmake_minimum_required(VERSION 2.8)
project(slideviewer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(slideviewer
        win32_main.c
        viewer.c
)
target_link_libraries(slideviewer user32 gdi32 winmm opengl32)
