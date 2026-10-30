#!/bin/sh
# cromo for Linux (build-linux/cromo) with the arguments of Windows, whose paths,
# as C:\x, become those of WSL; tools/cromo-wsl.cmd calls it (D292).
cromo="$(dirname "$0")/../build-linux/cromo"
first=1
for arg in "$@"; do
    if [ $first = 1 ]; then
        set --
        first=0
    fi
    case "$arg" in
        [A-Za-z]:*) set -- "$@" "$(wslpath -u "$arg")" ;;
        *) set -- "$@" "$arg" ;;
    esac
done
exec "$cromo" "$@"
