# Slideviewer

Slideviewer is a viewer for whole-slide images. It is currently in early development.

## How to build

### Windows
You can build the program using CMake, with the MinGW-w64 toolchain.

Building against MSVC may work, but is currently untested.

As an optional dependency, you may wish to install `nasm` so that the assembly code included in libjpeg-turbo can be compiled.   

All library code is included in the source code distribution, and will be compiled together with the program (you do not need to hunt down dependencies manually).

### Linux / macOS

Make sure the build tools `cmake` and (optionally) `nasm` are installed.

On Linux only, the following libraries are required: SDL2, GLEW.
```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cd ..
cmake --build build --target slideviewer -- -j
./slideviewer
```

## Credits

The application icon was made by [Freepik](https://www.flaticon.com/authors/freepik) from [Flaticon](https://www.flaticon.com/).

## License

This program is free software: you can redistribute it and/or modify 
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

See [LICENSE.txt](https://github.com/Falcury/slideviewer/blob/master/LICENSE.txt) for more information.