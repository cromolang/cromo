#!/bin/bash
# The part of the benchmarks that runs in WSL, called by run-bench.ps1: the
# times are taken here, so that the start of wsl.exe is not in them.
#
#   linux.sh toolchains <g++> <clang++> <go> <rust toolchain>
#       one line for each compiler, name|path|version, and the system; an empty
#       path when the compiler is missing. An empty <go> is go in the PATH, or
#       /opt/go/bin/go. rustc is the one of the toolchain, without the proxy of
#       rustup, and clang++ uses the libstdc++ of <g++>.
#   linux.sh prepare <sources> <programs> <to> <names...>
#       copies the folders <sources>/<name> and <programs>/<name> of each
#       benchmark into <to>/<name>; <to> is emptied first
#   linux.sh measure <runs> <output> <folder> [--vary <file>] -- <command...>
#       runs the command in the folder once, with its standard output and error in
#       <output> and <output>.err, then <runs> times more; prints the exit code of
#       the first run and the duration of the others in microseconds, then the
#       standard output of the first run, and its standard error after a line
#       "--- stderr". --vary appends a comment to the file before every run,
#       outside the times, so that a compiler with a cache (go) compiles it again.
set -u
# wsl.exe -e does not start a login shell: the folder of rustup is not in the PATH.
PATH="$PATH:$HOME/.cargo/bin"

case "$1" in
toolchains)
    gxx=${2:-g++} clangxx=${3:-clang++} go=${4:-} rust=${5:-stable}
    path=$(command -v "$gxx" 2>/dev/null)
    echo "g++|$path|$([ -n "$path" ] && "$path" --version | head -1)"
    prefix=$([ -n "$path" ] && dirname "$(dirname "$(readlink -f "$path")")")
    path=$(command -v "$clangxx" 2>/dev/null)
    echo "clang++|$path|$([ -n "$path" ] && "$path" --version | head -1)|$prefix"
    if [ -z "$go" ]; then
        go=$(command -v go 2>/dev/null || echo /opt/go/bin/go)
    fi
    path=$(command -v "$go" 2>/dev/null)
    echo "go|$path|$([ -n "$path" ] && "$path" version)"
    sysroot=$(rustc "+$rust" --print sysroot 2>/dev/null)
    path=$([ -n "$sysroot" ] && [ -x "$sysroot/bin/rustc" ] && echo "$sysroot/bin/rustc")
    echo "rustc|$path|$([ -n "$path" ] && "$path" --version)"
    echo "system|$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //'), $(nproc) threads|$(. /etc/os-release && echo "$PRETTY_NAME"), Linux $(uname -r)"
    ;;
prepare)
    sources=$2 programs=$3 to=$4
    shift 4
    rm -rf "$to"
    for name in "$@"; do
        mkdir -p "$to/$name"
        cp "$sources/$name"/* "$to/$name/"
        if [ -d "$programs/$name" ]; then
            cp "$programs/$name"/* "$to/$name/"
        fi
    done
    ;;
measure)
    runs=$2 output=$3 folder=$4
    shift 4
    vary=''
    if [ "$1" = --vary ]; then
        vary=$2
        shift 2
    fi
    shift  # --
    cd "$folder" || exit 2
    [ -n "$vary" ] && echo "// run 0" >> "$vary"
    "$@" > "$output" 2> "$output.err"
    code=$?
    times=''
    for ((i = 1; i <= runs; i++)); do
        [ -n "$vary" ] && echo "// run $i" >> "$vary"
        start=${EPOCHREALTIME/[.,]/}
        "$@" > /dev/null 2>&1
        end=${EPOCHREALTIME/[.,]/}
        times="$times $((10#$end - 10#$start))"
    done
    echo "$code$times"
    cat "$output"
    if [ -s "$output.err" ]; then
        echo "--- stderr"
        cat "$output.err"
    fi
    ;;
*)
    echo "usage: linux.sh toolchains | prepare | measure" >&2
    exit 2
    ;;
esac
