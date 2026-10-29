# Cromo

> *Rust rusts. Cromo doesn't.*

Cromo is a **general-purpose**, compiled programming language with a syntax close to **C/C++**, **no pointers** and **no garbage collector**. Resources are managed with the **RAII** paradigm of C++: every resource belongs to an object and is released deterministically when the object goes out of scope.

The compiler generates **LLVM IR**: optimization and machine-code generation are done by LLVM, as for Rust and Clang. It will be a single executable, `cromo`, that compiles and links with no other dependencies. Cromo programs do not depend on the C library: the basic functions are written in Cromo and in assembly. The goal is a language **simpler than Rust** that stays safe and fast.

The first compiler is written in C++; the goal is a compiler written in Cromo that compiles itself.

The name comes from chromium (*cromo* in Italian), the element that makes steel stainless.

## Status

**Milestones M0 to M9 completed.** The bootstrap compiler in C++ (`src/`) compiles programs with numbers, `bool`, variables, `if`/`for`, functions, `println`, structs with methods, constructors and destructors (RAII), ownership with `ref`, `move` and `copy()`, generics and interfaces, `list<T>` and `string`, enums and exceptions, modules and calls to C functions into executables with no C runtime, for Windows without the Windows SDK and, from Windows, for Linux. The standard library is written in Cromo, in `library/core.cro`. The next milestone is the compiler written in Cromo (see [docs/compiler.md](docs/compiler.md)). The design choices are in the documents under `docs/`; of the examples under `examples/`, `01_functions.cro`, `02_loops.cro`, `04_lists.cro` and `05_c_interop.cro` can be compiled.

## Building and trying it

Requirements, on Windows x64:
- Visual Studio 2022 with the C++ tools (it already contains CMake, Ninja and vcpkg);
- LLVM 23.1.4 in `C:\software\llvm-23.1.4`, from the official archive `clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz`.

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1               # builds build\cromo.exe
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1     # runs the tests
build\cromo.exe examples\02_loops.cro -o loops.exe               # compiles a program (debug, with checks)
build\cromo.exe examples\02_loops.cro --release -o loops.exe     # release: integers wrap around
build\cromo.exe examples\02_loops.cro --emit-ir                  # shows the LLVM IR
build\cromo.exe examples\02_loops.cro --target linux -o loops    # an executable for Linux x64
```

On the first build vcpkg downloads and builds zlib, zstd and libxml2, which the static LLVM libraries need. The runtime in `runtime/` is compiled with the clang of the LLVM archive and embedded in `cromo.exe`. To compile programs, `cromo.exe` needs neither Visual Studio nor the Windows SDK. The tests also build a few C libraries with the clang of the archive. With `-Flags '--target linux'` they run in WSL.

## Documents

| File | Content |
|---|---|
| [docs/decisions.md](docs/decisions.md) | Log of the decisions taken and of what is left to define |
| [docs/language.md](docs/language.md) | Draft of the language specification |
| [docs/compiler.md](docs/compiler.md) | Compiler architecture, runtime and development milestones |
| [examples/](examples/) | Example programs, as drafts, to discuss the syntax |
| [tools/vscode-cromo/](tools/vscode-cromo/) | VS Code extension for syntax highlighting of `.cro` files |
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
