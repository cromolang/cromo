#!/bin/sh
# Writes runtime/glibc/symbols.txt, the functions and the variables of glibc that
# the stubs of lib/linux/glibc give to the linker (D428). It runs once, in WSL on
# Ubuntu 20.04, whose glibc 2.31 is the oldest that the archives support: the
# stubs give only the default version of each name, so an executable needs no
# version newer than those of 2.31, and in practice 2.29 (D299).
#   wsl -d Ubuntu-20.04 -e sh tools/glibc-symbols.sh
set -e
cd "$(dirname "$0")/.."
out=runtime/glibc/symbols.txt
{
    echo "# The names of glibc 2.31 for x86-64, from Ubuntu 20.04, written by tools/glibc-symbols.sh (D428)."
    echo "# library version kind size name: kind is func, object or weak-func, weak-object; size for the objects."
    for library in libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 ld-linux-x86-64.so.2; do
        path=/lib/x86_64-linux-gnu/$library
        [ "$library" = ld-linux-x86-64.so.2 ] && path=/lib64/$library
        readelf --dyn-syms -W "$path" | awk -v library=$library '
            $7 != "UND" && $7 != "ABS" && index($8, "@@") > 0 {
                split($8, parts, "@@")
                version = parts[2]
                if (version == "GLIBC_PRIVATE") next
                kind = ""
                if ($4 == "FUNC" || $4 == "IFUNC") kind = "func"
                if ($4 == "OBJECT") kind = "object"
                if (kind == "") next
                if ($5 == "WEAK") kind = "weak-" kind
                size = $3
                print library, version, kind, size, parts[1]
            }' | sort -k5
    done
} > $out
wc -l $out
