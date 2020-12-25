# Slideviewer

Slideviewer is a viewer for whole-slide images. It is currently in early development.

## How to build

### Windows
You can build the program using CMake, with the MinGW-w64 toolchain.

Optionally, you may wish to install `nasm` so that the SIMD-optimized assembly code in libjpeg-turbo can be compiled.

All library code is included as part of the source code distribution (there are no other external dependencies).

### Linux / macOS

Make sure the build tools `cmake` and (optionally) `nasm` are installed.

On Linux, the following libraries are required: SDL2, GLEW.
```
mkdir build && cd build
cmake ..
cd ..
cmake --build build --target slideviewer -- -j
./slideviewer
```

## Credits

Slideviewer embeds code from the following libraries, under their respective licenses:
* [Dear ImGui](https://github.com/ocornut/imgui) (graphical interface library)
* [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) (JPEG image codec)
* [LZ4](https://github.com/lz4/lz4) (compression algorithm)
* [Mbed TLS](https://github.com/ARMmbed/mbedtls) (SSL/TLS and cryptography library)
* [FreeType](https://www.freetype.org/index.html) (font renderer)
* [Yxml](https://dev.yorhel.nl/yxml) (XML parser)
* [Parson](https://github.com/kgabis/parson) (JSON parser)
* [linmath.h](https://github.com/datenwolf/linmath.h) (linear math library)
* [stb_image.h](https://github.com/nothings/stb) (image loader)
* [stretchy_buffer.h](https://github.com/nothings/stb) (typesafe dynamic array for C)
* [glad](https://github.com/Dav1dde/glad) (OpenGL loader)
* [Keycode](https://github.com/depp/keycode) (library for platform-independent keyboard input handling)
* [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog) (file dialog for dear ImGui)

Uses [SDL2](https://www.libsdl.org/download-2.0.php) and [GLEW](http://glew.sourceforge.net/) on Linux.

The application icon was made by [Freepik](https://www.flaticon.com/authors/freepik) from [Flaticon](https://www.flaticon.com/).

## License

This program is free software: you can redistribute it and/or modify 
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

See [LICENSE.txt](https://github.com/Falcury/slideviewer/blob/master/LICENSE.txt) for more information.