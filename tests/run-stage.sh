#!/bin/sh
# Checks the two compilers of a build on Linux (D291, D576), as tests\run-stage.ps1 does
# on Windows: build-linux/stage1/cromo must build every program of tests/, examples/ and
# bench/ as build-linux/cromo does: the same exit code and the same errors for those that
# are refused, and for the others an executable with the same bytes at the same path.
# Usage, in WSL from the folder of the repository: sh tests/run-stage.sh [filter] [options of build]
root=$(cd "$(dirname "$0")/.." && pwd)
cromo="$root/build-linux/cromo"
stage="${STAGE:-$root/build-linux/stage1/cromo}"
filter="$1"
[ $# -gt 0 ] && shift
for needed in "$cromo" "$stage"; do
    [ -x "$needed" ] || { echo "$needed is missing: run build-linux.sh"; exit 2; }
done
work=$(mktemp -d)
same=0
refused=0
problems=""
cd "$root" || exit 2
for file in $(find tests examples bench -name '*.cro' | sort); do
    case "$file" in
        */out2/*|*/modules/*|*/formatting/*|*/lsp/*|*/project/*|*_test.cro) continue ;;
    esac
    [ -n "$filter" ] && case "$file" in *"$filter"*) ;; *) continue ;; esac
    rm -f "$work/program"
    "$cromo" build -q "$@" "$file" -o "$work/program" > "$work/a.out" 2>&1
    a=$?
    [ -f "$work/program" ] && mv "$work/program" "$work/a.bin" || rm -f "$work/a.bin"
    "$stage" build -q "$@" "$file" -o "$work/program" > "$work/b.out" 2>&1
    b=$?
    if [ ! -f "$work/a.bin" ] || [ ! -f "$work/program" ]; then
        # A link that fails writes its errors in the order of the threads of the linker.
        if [ $a -eq $b ] && { cmp -s "$work/a.out" "$work/b.out" || { grep -q 'linking failed' "$work/a.out" && grep -q 'linking failed' "$work/b.out"; }; }; then
            refused=$((refused + 1))
        else
            problems="$problems
the errors differ: $file"
        fi
    elif cmp -s "$work/a.bin" "$work/program"; then
        same=$((same + 1))
    else
        problems="$problems
the executable differs: $file"
    fi
done
rm -rf "$work"
echo "$same programs with the same executable, $refused refused with the same errors"
if [ -n "$problems" ]; then
    echo "$problems" | head -21
    echo "stage: FAILED"
    exit 1
fi
echo "stage: the same results"
