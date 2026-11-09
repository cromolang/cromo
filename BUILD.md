# Building Cromo

This document explains how to build `cromo` from source, run the tests and make the archives of a release. To use Cromo, the archives on [cromolang.org](https://cromolang.org) are enough: building is needed only to work on the compiler, the runtime or the standard library.

## Overview

`cromo` is a C++20 program that contains LLVM and LLD, linked statically: one executable compiles and links Cromo programs, with no other tool. Next to it, `cromo` reads `lib/`: the standard library, written in Cromo, and the runtime, written in C and compiled for each system.

| Build | Where | Result |
|---|---|---|
| Windows | Windows x64, with Visual Studio | `build\cromo.exe` and `build\lib\`, with the runtime for Windows and Linux |
| Linux | WSL 2, or Linux x64 | `build-linux/cromo`, which uses the `lib/` of the Windows build |

The Windows build is the main one: it compiles the runtime of both systems with the clang of LLVM, as a cross-compiler. `cromo` compiles for both systems from either one, so the Windows build alone is enough to develop and to test programs for Linux in WSL.

## Requirements on Windows

- **Windows 10 or 11, x64.**
- **Visual Studio 2022**, Community or another edition, with the workload *Desktop development with C++*. It contains the MSVC compiler, CMake, Ninja and vcpkg.
- **LLVM 23.1.4**, from the archive `clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz` of the [LLVM release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.4). The installer `LLVM-23.1.4-win64.exe` is not enough: it has neither the static libraries nor the CMake files. The default place is `C:\software\llvm-23.1.4`:

  ```powershell
  tar -xf clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz -C C:\software
  Rename-Item C:\software\clang+llvm-23.1.4-x86_64-pc-windows-msvc llvm-23.1.4
  ```

- **Git**, to clone the repository.
- **A connection to the Internet** for the first build: vcpkg downloads and builds zlib, zstd and libxml2, which the LLVM libraries need.
- Optional: **WSL 2** with Ubuntu, to run the tests for Linux and to build `cromo` for Linux.

## Building on Windows

```powershell
git clone https://github.com/cromolang/cromo.git
cd cromo
powershell -ExecutionPolicy Bypass -File build.ps1
build\cromo.exe version
```

The last command prints `cromo 0.69.0 (LLVM 23.1.4)`, with the version of the sources.

`build.ps1` accepts:

| Option | Meaning |
|---|---|
| `-Config Release` | A build without debug information; the default is `RelWithDebInfo` |
| `-LlvmRoot <folder>` | LLVM in another folder than `C:\software\llvm-23.1.4` |
| `-Clean` | Deletes `build\` and starts from scratch |

The script finds Visual Studio with `vswhere`, loads the environment of `vcvars64.bat` and configures CMake with Ninja and the toolchain of vcpkg (triplet `x64-windows-static`). It removes from `PATH` the MinGW toolchains, such as the one of Strawberry Perl, whose zlib CMake would otherwise find. Then it builds:

- `build\cromo.exe`, from `src\`, with the static C runtime (`/MT`) and without RTTI, as the LLVM libraries: `cromo.exe` needs no Visual C++ Redistributable;
- `build\lib\`, with `core.cro` and `std\` copied from `library\`, and the runtime: the C of `runtime\` and `third_party\` compiled by the clang of LLVM, and `std.runtime`, the part written in Cromo, compiled by the `cromo.exe` just built, in `windows\cromo_rt.lib` and `linux\libcromo_rt.a`.

Rebuild after any change to `library\` or `runtime\`: `cromo` reads them from `build\lib`. Debug builds are not supported, since the LLVM libraries are release builds.

## Running the tests

The tests are Cromo programs in `tests\`, grouped by milestone, whose comments say what they must do: the exit code, the output, or the compile error expected. The full list of the directives is at the top of `tests\run-tests.ps1`.

```powershell
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1                            # all the tests, on Windows
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 -Filter m18                # only those whose path contains m18
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 -Flags '--target linux'    # compiled for Linux, run in WSL
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 -Flags '--target linux-arm64'  # for Linux ARM64, run in WSL with qemu
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 -Flags '--target android-x86_64'  # for Android, on an emulator, through adb
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 -Flags '--release'         # optimized
```

- `-Flags` passes options to every compilation; with `--target linux` the programs run in the default distribution of WSL, or in the one given with `-Distro`. With `--target linux-arm64` they run there with `qemu-aarch64-static`, of the package `qemu-user-static` (`sudo apt install qemu-user-static`).
- The APKs of Android (D453) need `lib/android_<architecture>/libSDL3.so` and `lib/android/classes.dex`, which the build makes when it is given the NDK and the SDK of Android, with a JDK in `JAVA_HOME`: `build.ps1 -AndroidNdk C:\software\android-ndk-r27 -AndroidSdk C:\software\android_sdk`, or `ANDROID_NDK_HOME` and `ANDROID_HOME`; CMake remembers them. Without them `cromo build --target android` says what is missing.
- With `--target android-x86_64` or `android-arm64` the programs run on the device or the emulator that adb sees: an emulator of the SDK of Android, started before, as `emulator -avd <name> -no-window`, with an image for x86_64, which runs those for ARM64 too. adb is given with `-Adb <path>`, or found in `ANDROID_HOME`, `ANDROID_SDK_ROOT` or the `PATH`.
- `-Cromo <path>` tests another `cromo`, such as the one for Linux: `-Cromo tools\cromo-wsl.cmd -Flags '--target linux'`.
- The runner builds a few C libraries for the tests of `tests\m8` with the clang of LLVM (`-LlvmRoot` if it is elsewhere).
- Some antivirus programs quarantine the small executables that the tests write in `tests\out\`; if a test fails because its executable cannot be opened, exclude that folder from the scan.

A change to the compiler comes with its tests. A change to the standard library is tested on both systems.

## Building cromo for Linux

In WSL 2, or on a Linux x64 machine with Ubuntu 20.04 or later:

```sh
sudo apt install cmake ninja-build zlib1g-dev libzstd-dev
```

and a `g++` with C++20 support (the releases are built with GCC 14), and LLVM 23.1.4 for Linux, from the archive `LLVM-23.1.4-Linux-X64.tar.xz` of the same release, in `~/llvm-23.1.4`:

```sh
tar -xf LLVM-23.1.4-Linux-X64.tar.xz -C ~
mv ~/LLVM-23.1.4-Linux-X64 ~/llvm-23.1.4
```

Then, from the folder of the repository, after the Windows build:

```sh
sh build-linux.sh            # --clean to start from scratch
build-linux/cromo version
```

`build-linux.sh` builds only the compiler, in `build-linux/cromo`, and links `build-linux/lib` to the `lib` of the Windows build. `LLVM_ROOT` and `CXX` choose another LLVM and another C++ compiler.

`cromo` for Linux links statically the C++ runtime, zlib and zstd, and depends only on the C library of the system: built on Ubuntu 20.04, it runs with glibc 2.29 or later. A build entirely on Linux, runtime included, is not supported yet.

## The pilot of the compiler in Cromo

`compiler/` holds the first part of the compiler written in Cromo (D417, D423): the lexer, the parser, `cromo fmt`, the loader of the modules and the names of the modules. `build.ps1` compiles it with the `cromo` just built into `build\cromo-fmt.exe`, which takes the same options as `cromo fmt`, and `build\cromo-check.exe`, which checks the names of a program as `cromo build --check-names`; they are not in the archives. `tests\run-pilot.ps1` checks that both give the same results as the C++, on every `.cro` file and every program of the repository:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run-pilot.ps1
```

## The extension for Visual Studio Code

```powershell
powershell -ExecutionPolicy Bypass -File tools\vscode-cromo\build-vsix.ps1 -Install
```

It writes `tools\vscode-cromo\dist\cromo-<version>.vsix`, without npm, and with `-Install` installs it with `code --install-extension`, in place of `cromo.cromo-lang`, its name up to 0.9. The extension runs `cromo lsp`: the one of the setting `cromo.path`, else `build\cromo.exe` of the open folder, as while working on Cromo, else the one on the `PATH`. When the grammar changes, raise `version` in `tools\vscode-cromo\package.json`. `-Icon` draws `images\icon.png` again from `images\icon.svg`, with Edge.

The extension is `cromolang.cromo` in the [Visual Studio Marketplace](https://marketplace.visualstudio.com/manage/publishers/cromolang) and in [Open VSX](https://open-vsx.org/namespace/cromolang). A new version goes to the Marketplace by uploading the `.vsix` in the page of the publisher (*Update* on the extension), and to Open VSX with its command line, which needs Node.js and the token of the namespace:

```powershell
npx ovsx publish tools\vscode-cromo\dist\cromo-<version>.vsix -p <token>
```

## Making a release

The version is in `CMakeLists.txt`, in `project(cromo VERSION 0.69.0)`: `0.<milestone>.<fix>` until 1.0. With the two builds of the same version:

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Key <private key>
```

`tools\package.ps1` writes in `dist\`, with the layout of the site:

| Path | Content |
|---|---|
| `package/win_x86_64-<version>.zip` | `bin/cromo.exe`, `lib/`, `extension/` |
| `package/linux_x86_64-<version>.tar.gz` | `bin/cromo`, `lib/`, `extension/` |
| `release/<version>`, `latest` | The manifest: the version, and the address and SHA-256 of each archive |
| `release/<version>.sig`, `latest.sig` | The Ed25519 signature of the manifest |

The signing key is a file kept outside the repository. `tools/sign.cro` makes one and prints its public key, with which `cromo update` will check the versions it downloads:

```powershell
build\cromo.exe run tools\sign.cro -- key <private key>
```

## The guide and the site

The Cromo Guide is written in Markdown in `book/en` and `book/it`, one file for each chapter, listed in the `SUMMARY.md` of each language. `tools/book.py`, which needs Python 3 and nothing else, turns it into the pages of the site:

```powershell
python tools\book.py --check          # compiles and runs every example of the guide, and checks its output
python tools\book.py --pdf            # the pages in dist\site\book, and the PDF of each language
```

`--check` compiles every program of the guide with `build\cromo.exe` and compares what it prints with the `output` block that follows it, in every language: run it after a change to the compiler or to the library, too. The head of `tools/book.py` describes the flags of the code blocks. The PDFs are printed by Microsoft Edge or Chrome, headless.

`tools\site.ps1` assembles the whole site in `dist\site`: the pages of `site\` with the version of the archives, and their sizes and SHA-256 on the download page, the installers `install.ps1` and `install.sh`, the guide with its PDFs, and the archives and the manifests of the version that `tools\package.ps1` made:

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Key <private key>
powershell -ExecutionPolicy Bypass -File tools\site.ps1
```

Publishing is copying `dist\site` to the folder of the server, `/var/www/cromolang.org`, without deleting what is there: the archives and the manifests of the older versions stay, for `cromo update <version>`; `tools/nginx/cromolang.org.conf` is the configuration of nginx, with the types of the manifests and the caching of each kind of file. The installers read `CROMO_SITE` to install from another site, and `CROMO_NO_MODIFY_PATH=1` to leave the `PATH` as it is, for tests.

## Benchmarks

`bench\run-bench.ps1` compiles the programs of `bench\` with `cromo` and with g++, clang++, Go and rustc, which must be installed in WSL, runs them in WSL and writes the results in `bench\results\`. See the head of the script for its options.

## The repository

| Folder | Content |
|---|---|
| `src/` | The compiler, in C++: lexer, parser, semantic analysis, generation of LLVM IR, backend, formatter, language server |
| `runtime/` | The runtime in C, without the C library: memory, threads and tasks, stack traces and TLS |
| `library/` | The standard library, in Cromo |
| `third_party/` | Mbed TLS and Monocypher, with their licenses |
| `tests/` | The tests of the compiler |
| `examples/` | Example programs |
| `bench/` | Benchmarks against C++, Go and Rust |
| `tools/` | The extension for Visual Studio Code, the release scripts and helpers |
| `docs/` | The specification, the design decisions and the architecture of the compiler |
| `site/` | The website |

The architecture of the compiler and of the runtime is described in [docs/compiler.md](docs/compiler.md).
