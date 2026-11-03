#!/bin/sh
# libSDL3.a for Linux x86-64 (D429), in WSL on Ubuntu 20.04: compiled with glibc
# 2.31, the oldest that the stubs of glibc give (D428), with X11, Wayland, the
# graphics and the audio of the system loaded by dlopen when the program starts,
# so that it needs only their headers here. The build of cromo runs it; the
# objects stay in the cache of WSL, for the next build.
# Usage: linux.sh <third_party/SDL> <libSDL3.a to write>
set -e
source=$1
out=$2
work=${XDG_CACHE_HOME:-$HOME/.cache}/cromo/sdl-linux
mkdir -p "$work"
compiler=$(command -v clang-18 || command -v clang || command -v cc)
cmake -S "$source" -B "$work" -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_C_COMPILER="$compiler" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF \
    -DSDL_DEPS_SHARED=ON -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_KMSDRM=OFF -DSDL_X11_XTEST=OFF > "$work.log" 2>&1 ||
    { tail -30 "$work.log"; exit 1; }
cmake --build "$work" >> "$work.log" 2>&1 || { tail -30 "$work.log"; exit 1; }
cp "$work/libSDL3.a" "$out"
