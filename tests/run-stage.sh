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
# The programs in several processes, JOBS of them, by default half the processors and
# at most 8: each has those of its number, in a folder of its own, and the counts are
# added.
if [ -z "$STAGE_PART" ]; then
    jobs="${JOBS:-$(( $(nproc) / 2 ))}"
    [ "$jobs" -gt 8 ] && jobs=8
    [ "$jobs" -lt 1 ] && jobs=1
    if [ "$jobs" -gt 1 ]; then
        logs=$(mktemp -d)
        i=0
        while [ $i -lt "$jobs" ]; do
            STAGE_PART=$i STAGE_PARTS=$jobs sh "$0" "$filter" "$@" > "$logs/$i.out" 2>&1 &
            i=$((i + 1))
        done
        wait
        same=$(cat "$logs"/*.out | sed -n 's/^\([0-9]*\) programs with the same executable.*/\1/p' | awk '{ s += $1 } END { print s + 0 }')
        refused=$(cat "$logs"/*.out | sed -n 's/.* \([0-9]*\) refused with the same errors$/\1/p' | awk '{ s += $1 } END { print s + 0 }')
        ended=$(cat "$logs"/*.out | grep -c 'programs with the same executable')
        problems=$(cat "$logs"/*.out | grep '^the \(errors\|executable\) differ')
        if [ "$ended" -ne "$jobs" ]; then
            cat "$logs"/*.out | tail -20
            rm -rf "$logs"
            echo "stage: a process of the comparison did not end"
            exit 1
        fi
        rm -rf "$logs"
        echo "$same programs with the same executable, $refused refused with the same errors"
        if [ -n "$problems" ]; then
            echo "$problems" | head -20
            echo "stage: FAILED"
            exit 1
        fi
        echo "stage: the same results"
        exit 0
    fi
fi
work=$(mktemp -d)
index=-1
same=0
refused=0
problems=""
cd "$root" || exit 2
for file in $(find tests examples bench -name '*.cro' | sort); do
    case "$file" in
        */out2/*|*/modules/*|*/formatting/*|*/lsp/*|*/project/*|*_test.cro) continue ;;
    esac
    [ -n "$filter" ] && case "$file" in *"$filter"*) ;; *) continue ;; esac
    index=$((index + 1))
    [ -n "$STAGE_PART" ] && [ $((index % STAGE_PARTS)) -ne "$STAGE_PART" ] && continue
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
