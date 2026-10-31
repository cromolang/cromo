#!/bin/sh
# Installs Cromo for the current user, without administrator rights:
#
#   curl -sSf https://cromolang.org/install.sh | sh
#
# It downloads the archive of the latest version for Linux x86-64, checks its
# SHA-256 against the manifest, unpacks it in ~/.cromo/versions/<version>, makes it
# the active version (~/.cromo/current), adds ~/.cromo/current/bin to the PATH in the
# profile of the shell, and installs the extension for Visual Studio Code if the
# command code is there. CROMO_SITE can name another site, for tests, and
# CROMO_NO_MODIFY_PATH=1 leaves the profiles and Visual Studio Code as they are.

set -eu

say() {
    printf '%s\n' "$*"
}

fail() {
    printf 'cromo: %s\n' "$*" >&2
    exit 1
}

need() {
    command -v "$1" >/dev/null 2>&1 || fail "the command $1 is needed to install Cromo"
}

main() {
    site="${CROMO_SITE:-https://cromolang.org}"
    root="$HOME/.cromo"

    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64 | Linux-amd64) ;;
        *) fail "Cromo is available for Linux x86-64 and Windows x64, not for $(uname -s) $(uname -m)" ;;
    esac
    need curl
    need tar
    need sha256sum

    # The manifest of the latest version: each archive is on a line of its own.
    manifest=$(curl -sSfL "$site/latest") || fail "cannot read $site/latest"
    version=$(printf '%s\n' "$manifest" | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' | head -n 1)
    line=$(printf '%s\n' "$manifest" | grep '"linux_x86_64"' || true)
    package=$(printf '%s\n' "$line" | sed -n 's/.*"package": *"\([^"]*\)".*/\1/p')
    sum=$(printf '%s\n' "$line" | sed -n 's/.*"sha256": *"\([^"]*\)".*/\1/p')
    [ -n "$version" ] && [ -n "$package" ] && [ -n "$sum" ] || fail "the manifest of $site/latest has no archive for Linux x86-64"

    target="$root/versions/$version"
    if [ -x "$target/bin/cromo" ]; then
        say "Cromo $version is already installed."
    else
        temp=$(mktemp -d)
        trap 'rm -rf "$temp"' EXIT
        say "Downloading Cromo $version..."
        curl -sSfL "$package" -o "$temp/cromo.tar.gz" || fail "cannot download $package"
        printf '%s  %s\n' "$sum" "$temp/cromo.tar.gz" | sha256sum -c - >/dev/null 2>&1 ||
            fail "the archive is damaged: its SHA-256 is not the one of the manifest"
        mkdir -p "$temp/unpacked"
        tar -xzf "$temp/cromo.tar.gz" -C "$temp/unpacked"
        mkdir -p "$root/versions"
        rm -rf "$target.partial"
        mv "$temp/unpacked" "$target.partial"
        mv "$target.partial" "$target"
    fi

    # The active version is the link current.
    ln -sfn "$target" "$root/current"

    # The bin folder of the active version, on the PATH of the login shells and of bash and zsh.
    entry='export PATH="$HOME/.cromo/current/bin:$PATH"'
    profiles="$HOME/.profile $HOME/.bashrc $HOME/.zshrc"
    if [ -n "${CROMO_NO_MODIFY_PATH:-}" ]; then
        profiles=""
    fi
    for profile in $profiles; do
        case "$profile" in
            "$HOME/.profile") ;;
            *) [ -f "$profile" ] || continue ;;
        esac
        if ! grep -qs '.cromo/current/bin' "$profile"; then
            printf '\n# Cromo\n%s\n' "$entry" >>"$profile"
            say "Added ~/.cromo/current/bin to the PATH in $profile."
        fi
    done

    # The extension for Visual Studio Code.
    if [ -z "${CROMO_NO_MODIFY_PATH:-}" ] && command -v code >/dev/null 2>&1; then
        for vsix in "$target"/extension/*.vsix; do
            [ -f "$vsix" ] || continue
            code --install-extension "$vsix" --force >/dev/null 2>&1 &&
                say "Installed the extension for Visual Studio Code."
            # Up to 0.27 the extension was cromo.cromo-lang: two would both start cromo lsp.
            case "$(basename "$vsix")" in
                cromo-lang-*) ;;
                *) if code --list-extensions 2>/dev/null | grep -qx 'cromo.cromo-lang'; then
                       code --uninstall-extension cromo.cromo-lang >/dev/null 2>&1 || true
                   fi ;;
            esac
            break
        done
    fi

    say ""
    say "Cromo $version is installed in $target."
    say "Open a new terminal, or run: export PATH=\"\$HOME/.cromo/current/bin:\$PATH\""
    say "Then: cromo version"
    say "The Cromo Guide: https://cromolang.org/book/"
}

main "$@"
