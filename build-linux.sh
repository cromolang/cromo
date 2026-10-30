#!/bin/sh
# Builds cromo for Linux in build-linux/, with the official archive of LLVM 23.1.4
# for Linux (D292), extracted in ~/llvm-23.1.4 unless LLVM_ROOT says otherwise.
# Only the compiler: lib/ comes from the build of Windows, which makes the
# runtime of both systems (build.ps1).
# Usage, in WSL from the folder of the repository: sh build-linux.sh [--clean]
set -e
root=$(cd "$(dirname "$0")" && pwd)
build="$root/build-linux"
llvm="${LLVM_ROOT:-$HOME/llvm-23.1.4}"
if [ "$1" = "--clean" ]; then rm -rf "$build"; fi
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="${CXX:-g++}" \
    -DCROMO_LLVM_ROOT="$llvm" \
    -DCROMO_BUILD_LIBRARY=OFF
cmake --build "$build"
# The library of the build of Windows, as lib/ inside the folder of cromo (D237).
ln -sfn ../build/lib "$build/lib"
