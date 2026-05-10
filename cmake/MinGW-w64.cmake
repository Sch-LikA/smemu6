# cmake/MinGW-w64.cmake — Cross-compile for Windows x86-64 using MinGW-w64
#
# Prerequisites (Ubuntu/Debian/Mint):
#   sudo apt install mingw-w64
#
# SDL2 Windows dev package (MinGW flavour):
#   wget https://github.com/libsdl-org/SDL/releases/download/release-2.30.3/SDL2-devel-2.30.3-mingw.tar.gz
#   tar xf SDL2-devel-2.30.3-mingw.tar.gz
#   # Place (or symlink) the extracted folder at: deps/SDL2-mingw/
#   # Expected layout: deps/SDL2-mingw/x86_64-w64-mingw32/{include,lib,bin}/
#
# Configure + build:
#   cmake --preset win64
#   cmake --build build-win -j$(nproc)
#
# The output is build-win/smemu6.exe.  Bundle it together with SDL2.dll:
#   mkdir -p dist/smaky6-win
#   cp build-win/smemu6.exe dist/smaky6-win/
#   cp deps/SDL2-mingw/x86_64-w64-mingw32/bin/SDL2.dll dist/smaky6-win/
#   cp roms/samos_sys17.rom dist/smaky6-win/roms/
#   zip -r dist/smaky6-win.zip dist/smaky6-win/

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_VERSION 10)

set(CMAKE_C_COMPILER    x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER  x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER   x86_64-w64-mingw32-windres)

# Sysroot for headers/libs provided by the MinGW package
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

# Never search the host system for programs, only for libraries and headers
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
