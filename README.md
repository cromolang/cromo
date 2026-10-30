# Cromo

> *Rust rusts. Cromo doesn't.*

Cromo is a **general-purpose**, compiled programming language with a syntax close to **C/C++**, **no pointers** and **no garbage collector**. Resources are managed with the **RAII** paradigm of C++: every resource belongs to an object and is released deterministically when the object goes out of scope.

The compiler generates **LLVM IR**: optimization and machine-code generation are done by LLVM, as for Rust and Clang. It will be distributed as an archive for each system, installed with one line: `cromo`, which compiles and links with no other dependencies, and next to it the library and the runtime (D237, D238). Cromo programs do not depend on the C library: the basic functions are written in Cromo and in assembly. The goal is a language **simpler than Rust** that stays safe and fast.

The first compiler is written in C++; the goal is a compiler written in Cromo that compiles itself.

The name comes from chromium (*cromo* in Italian), the element that makes steel stainless.

## Status

**Milestones M0 to M23 completed.** The bootstrap compiler in C++ (`src/`) compiles programs with numbers, `bool`, variables, `if`/`for`, functions, `println`, structs with methods, constructors and destructors (RAII), ownership with `ref`, `move` and `copy()`, generics and interfaces, `list<T>`, `map<K, V>`, fixed-size arrays and `string`, enums and exceptions, modules, anonymous functions, threads with mutexes, lightweight tasks with channels and atomics, TCP and UDP networking, TLS and cryptography, HTTP servers and clients, JSON, and calls to C functions into executables with no C runtime, for Windows without the Windows SDK and for Linux, from either system. The standard library is written in Cromo: its core in `library/core.cro`, and the modules `std.fs`, `std.time`, `std.os`, `std.process`, `std.thread`, `std.net`, `std.crypto`, `std.json` and `std.http` in `library/std/`, with a file for each system; TLS and the algorithms of `std.crypto` come from mbedTLS and Monocypher (`third_party/`), compiled into the runtime. `cromo` has the commands `build`, `run`, `fmt`, `lsp` and `version`; the extension for VS Code uses the language server `cromo lsp` for errors, hover, definitions, the members after a dot and formatting. Next come the archive and the installation in one line; the compiler written in Cromo comes last (see [docs/compiler.md](docs/compiler.md)). The design choices are in the documents under `docs/`; of the examples under `examples/`, `01_functions.cro`, `02_loops.cro`, `04_lists.cro`, `05_c_interop.cro`, `06_lambdas.cro`, `07_collections.cro`, `08_system.cro`, `09_threads.cro`, `10_network.cro`, `11_https.cro`, `12_json.cro`, `13_web_server.cro` and `14_tasks.cro` can be compiled.

## Building and trying it

Requirements, on Windows x64:
- Visual Studio 2022 with the C++ tools (it already contains CMake, Ninja and vcpkg);
- LLVM 23.1.4 in `C:\software\llvm-23.1.4`, from the official archive `clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz`.

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1               # builds build\cromo.exe
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1     # runs the tests
build\cromo.exe build examples\02_loops.cro -o loops.exe              # compiles a program (debug, with checks)
build\cromo.exe build examples\02_loops.cro --release -o loops.exe    # release: integers wrap around
build\cromo.exe build examples\02_loops.cro --emit-ir                 # shows the LLVM IR
build\cromo.exe build examples\02_loops.cro --target linux -o loops   # an executable for Linux x64
build\cromo.exe run examples\02_loops.cro                             # compiles and runs; arguments after --
build\cromo.exe build                                                 # main.cro of this folder
build\cromo.exe fmt library examples                                   # formats the code; --check only lists it
build\cromo.exe version
```

On the first build vcpkg downloads and builds zlib, zstd and libxml2, which the static LLVM libraries need. The runtime in `runtime/` is compiled with the clang of the LLVM archive and placed, with the library, in `build\lib`, which `cromo` reads (D237). `build-linux.sh`, in WSL, builds `cromo` for Linux, and `tools\package.ps1` makes the archives of a version in `dist\` (D285–D287). To compile programs, `cromo.exe` needs neither Visual Studio nor the Windows SDK. The tests also build a few C libraries with the clang of the archive. With `-Flags '--target linux'` they run in WSL.

## Benchmarks

`bench/` has small programs, each written in Cromo, C++, Go and Rust, that do the same work and print the same checksum: integer arithmetic (`collatz`), recursion (`fib`), floating point (`mandelbrot`, `spectralnorm`), allocation (`binarytrees`), strings and `format` (`strings`), a `map` (`wordcount`), sorting (`sort`), bitwise operators (`bits`), an enum with an interface and a generic (`shapes`), and threads (`threads`).

```powershell
powershell -ExecutionPolicy Bypass -File bench\run-bench.ps1                       # build\cromo.exe against g++, clang++, Go and rustc
powershell -ExecutionPolicy Bypass -File bench\run-bench.ps1 -Cromo 'old=C:\old\cromo.exe,build\cromo.exe'  # two builds of cromo side by side
powershell -ExecutionPolicy Bypass -File bench\run-bench.ps1 -Filter fib -Runs 3  # one benchmark, three runs
```

The programs run on Linux, in WSL, where the reference compilers are: `cromo` compiles them on Windows with `--release --target linux`, and g++ and clang++ (`-O2`), Go (`go build`) and rustc (`-C opt-level=3`, as `cargo --release`) compile them in WSL. The script checks that every version prints the same, and writes to `bench/results` a table in Markdown and in CSV with the version of each compiler, the compile time and the run time, the median of several runs. A new benchmark is a folder `bench/name` with `name.cro`, `name.cpp`, `name.go` and `name.rs`, whose first line says what it measures.

## Documents

| File | Content |
|---|---|
| [docs/decisions.md](docs/decisions.md) | Log of the decisions taken and of what is left to define |
| [docs/language.md](docs/language.md) | Draft of the language specification |
| [docs/compiler.md](docs/compiler.md) | Compiler architecture, runtime and development milestones |
| [examples/](examples/) | Example programs, as drafts, to discuss the syntax |
| [bench/](bench/) | Benchmarks against C++, Go and Rust, and between versions of `cromo` |
| [tools/vscode-cromo/](tools/vscode-cromo/) | VS Code extension for syntax highlighting of `.cro` files |
| [site/](site/) | Mockup of the site [cromolang.org](https://cromolang.org), with the logo (D213) |
| [CLAUDE.md](CLAUDE.md) | Instructions for the AI assistants working on the project |

## A taste

**Indicative** syntax, not fully decided yet (see [docs/decisions.md](docs/decisions.md)):

```cromo
struct File {
    i32 fd = -1                // default: no file open

    File(string path) {        // constructor: acquires the resource
        fd = os.open(path)
    }

    ~File() {                  // destructor: releases the resource
        if (fd >= 0) {
            os.close(fd)
        }
    }
}

i32 main() {
    File f = File("data.txt")
    // ... use the file ...
    ret 0
}                              // f goes out of scope: the file is closed
```
