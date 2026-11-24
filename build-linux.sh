#!/bin/sh
# Builds cromo for Linux in build-linux/ (D292), which is written in Cromo (D291, D576),
# with the cromo of the version before; the C++ compiler of the system compiles only
# compiler/shim/shim.cpp, against the official archive of LLVM 23.1.4 for Linux,
# extracted in ~/llvm-23.1.4 unless LLVM_ROOT says otherwise. Only the compiler: lib/
# comes from the build of Windows, which makes the runtime of both systems (build.ps1).
# Usage, in WSL from the folder of the repository: sh build-linux.sh [--clean]
# The cromo of the version before is downloaded once from cromolang.org into
# bootstrap/, and its SHA-256 checked; BOOTSTRAP names another one.
set -e
root=$(cd "$(dirname "$0")" && pwd)
build="$root/build-linux"
llvm="${LLVM_ROOT:-$HOME/llvm-23.1.4}"
if [ "$1" = "--clean" ]; then rm -rf "$build"; fi

# The version that builds this one, and the SHA-256 of its archive for Linux, as in
# its manifest, https://cromolang.org/release/<version> (D285).
version=0.98.0
sum=76e68b0d3f76abf9e1c7cb0c4c2e4b084d7a9a557d4bf700cb1fdf1c577f9853
bootstrap="${BOOTSTRAP:-$root/bootstrap/linux_x86_64-$version/bin/cromo}"
if [ -z "$BOOTSTRAP" ] && [ ! -x "$bootstrap" ]; then
    archive="$root/bootstrap/linux_x86_64-$version.tar.gz"
    mkdir -p "$root/bootstrap/linux_x86_64-$version"
    echo "Downloading cromo $version, which builds this one"
    curl -fsSL "https://cromolang.org/package/linux_x86_64-$version.tar.gz" -o "$archive"
    echo "$sum  $archive" | sha256sum -c - > /dev/null || { echo "the archive of cromo $version has another SHA-256"; exit 1; }
    tar -xzf "$archive" -C "$root/bootstrap/linux_x86_64-$version"
    rm "$archive"
fi
[ -x "$bootstrap" ] || { echo "$bootstrap is missing: the cromo that builds this one"; exit 1; }

# CMake only finds the libraries of LLVM and of LLD, for tools/llvm-archive.sh, which
# puts them with the shim in compiler/libcromollvm.a.
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="${CXX:-g++}" \
    -DCROMO_LLVM_ROOT="$llvm" \
    -DCROMO_BUILD_LIBRARY=OFF
sh "$root/tools/llvm-archive.sh"
# The library of the build of Windows, as lib/ inside the folder of cromo (D237).
ln -sfn ../build/lib "$build/lib"

# stage1: the compiler of these sources, built by the cromo of the version before with
# its own library; then cromo: these sources compiled by themselves, with the library
# of these sources. tests/run-stage.sh checks that the two build the same programs.
mkdir -p "$build/stage1"
"$bootstrap" build --release "$root/compiler/cromo.cro" -o "$build/stage1/cromo"
"$build/stage1/cromo" build --release "$root/compiler/cromo.cro" -o "$build/cromo"
