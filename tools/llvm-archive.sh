#!/bin/sh
# Makes compiler/libcromollvm.a, the C library of the compiler written in Cromo for
# Linux (D291, D576): compiler/shim/shim.cpp, the libraries of LLVM and of LLD that
# cromo needs, as CMake lists them, and the runtime of C++ of the compiler of WSL, in one archive,
# which `extern "cromollvm"` of compiler/llvmc.cro finds next to its module (D143).
# glibc stays outside: the executable is linked with it as any program whose C
# libraries call it (D428). It is called by build-linux.sh, after CMake wrote
# build-linux/build.ninja.
# Usage, in WSL from the folder of the repository: sh tools/llvm-archive.sh
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/build-linux"
llvm="${LLVM_ROOT:-$HOME/llvm-23.1.4}"
cxx="${CXX:-g++}"
archive="$root/compiler/libcromollvm.a"
shim="$root/compiler/shim/shim.cpp"
if [ -f "$archive" ] && [ "$archive" -nt "$shim" ] && [ "$archive" -nt "$0" ]; then exit 0; fi

"$cxx" -c -O2 -std=c++17 -fno-rtti -fno-exceptions -I"$llvm/include" "$shim" -o "$build/shim.o"
"$cxx" -c -O2 -std=c++17 -fno-rtti "$root/compiler/shim/glibc_compat.cpp" -o "$build/glibc_compat.o"

# The libraries that CMake lists for cromo, each once, then those of C++ that the
# compiler adds by itself with -static-libstdc++ and -static-libgcc.
cp "$("$cxx" -print-file-name=libstdc++.a)" "$build/libstdcxx.a"
script="$build/cromollvm.mri"
{
    echo "create $archive"
    echo "addmod $build/shim.o"
    echo "addmod $build/glibc_compat.o"
    grep 'LINK_LIBRARIES = ' "$build/build.ninja" | head -1 | tr ' ' '\n' | grep '\.a$' | awk '!seen[$0]++' | sed 's/^/addlib /'
    echo "addlib $build/libstdcxx.a"  # a copy: a script of ar cannot name a file with a +
    for name in libgcc.a libgcc_eh.a; do
        echo "addlib $("$cxx" -print-file-name=$name)"
    done
    echo save
    echo end
} > "$script"
rm -f "$archive"
ar -M < "$script"
echo "libcromollvm.a: $(grep -c '^addlib' "$script") libraries, $(du -m "$archive" | cut -f1) MB"
