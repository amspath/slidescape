# Slidescape

Slidescape is a whole-slide image viewer for digital pathology.

## Download
The latest binaries can be downloaded on GitHub from the [releases](https://github.com/amspath/slidescape/releases) section
for Windows, Linux and macOS.


## Features

### Supported image formats

The viewer has built-in support for:
* Tiled TIFF and BigTIFF (including generic and Philips TIFF variants).
* Philips iSyntax.
* Simple images (JPEG, PNG).

Slidescape can also detect and load the [OpenSlide](https://github.com/openslide/openslide) library at runtime.
If OpenSlide is present, the Aperio, Hamamatsu, Leica, MIRAX, Sakura, Trestle, and Ventana formats can additionally be loaded.

To enable OpenSlide support on Windows, download (or compile) the [64-bit binaries](https://openslide.org/download/)
and put all of the DLL files together in an `openslide/` folder, and put that folder in the same location as `slidescape.exe`.

To enable OpenSlide support on Linux, install the `openslide` library, either using a package manager or
by building and installing it manually. The program will try to locate `libopenslide.so`, either in the
default system library paths or in `/usr/local/lib/`.

To enable OpenSlide support on macOS, install the `openslide` library using Homebrew or MacPorts.
The program will try to locate `libopenslide.dylib` in the default install path: either `/usr/local/opt/openslide/lib/` for
Homebrew, or `/opt/local/lib/` for MacPorts.

### Viewing options

To navigate an image, you can pan and zoom using either the mouse or the keyboard.
The mouse and keyboard sensitivity can be adjusted in the general options,
`Edit` > `General options...` under the tab `Controls`.

Basic image filters are available under `View` > `Image options`. 
These allow adjusting the black and white level and filtering out a background color.

There is experimental support for loading a second image as an overlay (e.g. a mask image).
To load an image as an overlay, press `F6` before loading the second image.


### Annotations
Slidescape can create and edit annotations in XML format, 
compatible with [ASAP](https://github.com/computationalpathologygroup/ASAP).

Annotations can be manipulated in a variety of ways:
* New annotations can be created (points `Q`, lines `M`, rectangles `R`, freeforms `F`).
* Individual coordinates can be moved, inserted or deleted (to toggle editing of coordinates, press `E`).
* Annotations can be assigned a (color-coded) group.
* Annotations can be split into parts.

Changes to annotations are autosaved by default (a backup of the original unchanged XML file will be preserved with file extension `.orig`).

### Image cropping

Images in TIFF format can be cropped to a smaller size. This may be useful to reduce the file size, or to restrict the image to a specific region of interest.

To crop an image, select a region for cropping (`Edit` > `Select region`), then use `File` > `Export` > `Export region...` to export the file.


## How to build

### Windows
You can build the program using CMake and the MinGW-w64 toolchain.

All library code is included with the source code distribution (there are no other external dependencies).

You may wish to install `nasm` so that the SIMD-optimized assembly code in libjpeg-turbo can be compiled.

### Linux / macOS

Make sure the build tools `cmake` and (optionally) `nasm` are installed.

The following libraries are required to be installed: SDL2 (on Linux and macOS), GLEW (Linux only).
```
mkdir build && cd build
cmake ..
cd ..
cmake --build build --target slidescape -- -j
./slidescape
```


## Credits

Author: Pieter Valkema.

Slidescape embeds code from the following projects, under their respective licenses:
* [Dear ImGui](https://github.com/ocornut/imgui) (graphical interface library)
* [FreeType](https://www.freetype.org/index.html) (font renderer)
* [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) (JPEG image codec)
* [Mbed TLS](https://github.com/ARMmbed/mbedtls) (SSL/TLS and cryptography library)
* [Yxml](https://dev.yorhel.nl/yxml) (XML parser)
* [json.h](https://github.com/sheredom/json.h) (JSON parser)
* [linmath.h](https://github.com/datenwolf/linmath.h) (linear math library)
* [stb_image.h](https://github.com/nothings/stb) (image loader)
* [OpenJPEG](https://github.com/uclouvain/openjpeg) (discrete wavelet transform)
* [LZ4](https://github.com/lz4/lz4) (compression algorithm)
* [base64.c](http://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c) (base64 decoder)
* [cityhash](https://github.com/google/cityhash/blob/8af9b8c2b889d80c22d6bc26ba0df1afb79a30db/src/city.cc#L50) (byte swap operations)
* [stb_ds.h](https://github.com/nothings/stb/blob/master/stb_ds.h) (typesafe dynamic array and hash tables for C)
* [ltalloc](https://github.com/r-lyeh-archived/ltalloc) (fast memory allocator)
* [glad](https://github.com/Dav1dde/glad) (OpenGL loader)
* [Keycode](https://github.com/depp/keycode) (library for platform-independent keyboard input handling)
* [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog) (file dialog for dear ImGui)

Uses [SDL2](https://www.libsdl.org/download-2.0.php) and [GLEW](http://glew.sourceforge.net/) on Linux/macOS.

The application icon was made by [Freepik](https://www.flaticon.com/authors/freepik) from [Flaticon](https://www.flaticon.com/).

## License

Copyright (C) 2019-2023 Pieter Valkema. 

This program is free software: you can redistribute it and/or modify 
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

See [LICENSE.txt](https://github.com/amspath/slidescape/blob/master/LICENSE.txt) for more information.
