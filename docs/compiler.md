# Compiler architecture

> **Status: proposal.** The bootstrap compiler is written in C++ (D44); its details are in D53 of [decisions.md](decisions.md).

## 1. Pipeline

The final product is a single executable, `cromo`, which does everything (D28):

```
.cro source
  → lexer                      tokens
  → parser                     AST
  → name resolution
  → type checking              generics, interfaces, conversions
  → ownership analysis         default values, moves, exclusivity, destructor insertion
  → LLVM IR emission           text (D53)
  → LLVM, inside cromo         reading the IR, optimization and machine code
  → LLD, inside cromo          linking with the runtime and with the generated import libraries
  → executable
```

- **Parser** written by hand, recursive descent. Parser generators are not used: the error messages are better and the grammar stays under control.
- **Cromo intermediate IR.** The compiler goes directly from the AST to LLVM IR. A dedicated control-flow-graph IR (like Rust's MIR) will be introduced when a feature needs it: since the control flow of Cromo is structured, the analysis of moves is done on the AST (D77).
- **Ownership analysis** (`src/ownership.cpp`, D77). It runs after type checking and follows the state of every variable (alive, moved, maybe moved) through branches and loops, as the definite assignment of Java and C# does. A loop is visited until the state at its start no longer changes, then once more to report the errors. Exclusivity (D22, D79) is checked by sema, call by call.
## 2. Bootstrap: from C++ to Cromo (D44)

The first compiler is written in C++ and uses LLVM and LLD directly, statically linked into `cromo.exe`. The goal is a compiler written in Cromo that compiles itself. The classic path has three stages:

1. **Stage 0:** the C++ compiler (`cromo0`) compiles the compiler written in Cromo and produces `cromo1`.
2. **Stage 1:** `cromo1` compiles the same source again and produces `cromo2`.
3. **Verification:** `cromo1` and `cromo2` must generate the same code for the same sources. From then on the C++ compiler is no longer needed, except to rebuild everything from scratch.

For this reason the C++ compiler must support all of the language used by the compiler in Cromo, but it does not need to be elegant or fast: it needs to be correct. Generating the IR as text (D53) makes the code generator easy to translate into Cromo.

## 3. LLVM IR generation

### Local variables and default values
Every local variable becomes an `alloca` in the entry block of the function, with `load` and `store`. SSA form is not built by hand: LLVM's `mem2reg` pass promotes the variables to registers. This is the technique used by Clang and by Rust.

A variable declared without a value is initialized to its default (D31): zero for numbers and `bool`, empty string and empty list, which are also represented with all bytes set to zero. When the value is immediately overwritten, LLVM removes the useless store.

### RAII
The compiler keeps a stack of the live objects for each scope. At every exit from the scope (end of the block, `ret`, `break`, `continue`, exception) it emits the destructor calls in reverse order. Indicative example:

```llvm
define i32 @main() {
entry:
  %f = alloca %struct.File
  call void @File..new(ptr %f, ptr @.str.0)
  ; ... use of f ...
  call void @File..drop(ptr %f)
  ret i32 0
}
```

Pointers exist in the IR (`ptr`), even though the language has none: the compiler manages them.

How M2 implements the rules of D68:
- **Constructor.** `T..new(ptr self, args)` first writes the default value of the struct (D48), then runs the body.
- **Destructor.** `T..drop(ptr self)` runs the body, then destroys the fields in reverse order; a `ret` inside the body jumps to the block that destroys the fields. A struct without a destructor whose fields need one gets a `T..drop` that only destroys the fields.
- **Temporaries.** An object created inside an expression (a constructor, or a call that returns a struct) gets a slot in the entry block and is destroyed at the end of the statement. In the condition of an `if` or a `for` it is destroyed before the branch; in the right side of `&&` and `||` at the end of that side, because it exists only on that path.
- **`ret` of a local variable** moves it (D23): the variable is copied to the return value and is not destroyed.
- **Assignment of a new object** to a variable that needs destruction (D75): the new value is built in a temporary, then the old value is destroyed, then the new one is copied into place.

Container destructors first destroy the elements and then free their own memory. This way, when a variable goes out of scope, everything it contains is freed, at any depth (D11).

- **Conditional moves (D23).** When an object is moved only in one branch, the compiler uses a local boolean flag (*drop flag*) to decide whether to call the destructor. In simple cases the optimizer removes it. In M3 every variable that is moved somewhere gets a flag `%vN.name.alive`: true after the initialization and after an assignment, false after the `move`; the destructor is called only when it is true.
- **Moving a field or a `ref` parameter (D78)** copies the value and then writes the default value of the type in its place.

### Exceptions (D36)
Hidden error return, as in Swift. Every function that can throw returns, in addition to the value, an error indicator. After every call the compiler emits a check that jumps to the cleanup block (destructors, `finally`) or to the `catch`.
- For LLVM these are normal branches, which it knows how to optimize, and all functions remain `nounwind`.
- The code is the same on every platform, and no runtime is needed.
- LLVM already has support for this technique: the `swifterror` attribute keeps the indicator in a dedicated register. Whether to use it remains to be evaluated.

### Information for the optimizer
This is where the rules of the language become performance:

| Cromo rule | LLVM attribute |
|---|---|
| Exclusivity of `ref` parameters (D22) | `noalias` on the parameter |
| `const` methods and read-only parameters (D22, D24) | `readonly` on the object parameter |
| No null (D4, D47) | `nonnull`, `dereferenceable(n)` |
| Exceptions as error return (D36) | `nounwind` on all functions |
| Overflow (D30) | `llvm.sadd.with.overflow.*` intrinsics in debug, plain `add` in release |
| `panic` | `@cromo_panic` function marked `noreturn` and `cold` |

### Bounds checking (D30)
A comparison, then a branch to a block that calls `@cromo_panic`. LLVM removes the checks it can prove unnecessary.

### Read parameters (D22)
- **Up to 16 bytes:** by copy, in registers.
- **Beyond 16 bytes:** by address, marked `readonly`, `noalias` and `dereferenceable(n)`. `noalias` is correct because during the call nobody can modify the object: there are no pointers, and an object passed for reading cannot also be passed with `ref` in the same call. When it would overlap with something the call modifies, a simple type is passed as a copy (D79).
- **`extern` functions to C:** they follow the platform ABI, not this rule.

**Return values.** A struct of up to 16 bytes is returned by value; a larger one is written through a hidden `sret` pointer, which points to the variable being initialized or to a temporary.

**Watch out for the `x = f(x)` case.** The result of `f` would be constructed directly in `x` while `f` is still reading `x` by address. The compiler must treat the destination of the assignment as a modification of `x`. In M2 every assignment of a struct builds the new value in a temporary and copies it into `x` at the end; LLVM removes the copy when it can.

**`ref` and `move` parameters.** A `ref` parameter is always an address marked `noalias`. A `move` parameter up to 16 bytes is passed by value; beyond, by address: when the argument is a whole variable (`f(move big)`) the caller hands over the memory of the variable, otherwise it builds the value in a slot of its own frame. In both cases the callee owns the object and destroys it. The object of a method is `noalias` too, and `readonly` for `const` methods. `dereferenceable(n)` is not emitted yet.

Zig had a similar rule, with the compiler free to pass "by value" parameters by reference, and removed it in version 0.10 precisely because of aliasing problems. There, however, the main cause was pointers, which do not exist in Cromo.

### Generics and interfaces (D39, D40)
- **Generics:** the compiler generates a copy of the function or of the struct for each combination of types used (monomorphization). Each copy has its own name, which contains the types.
- **Interfaces, static call:** with a constrained generic the type is known, and the call is direct.
- **Interfaces, dynamic call:** a value of interface type is a pair (address of the object, method table). In a `list<Shape>` each element is allocated on the heap with its table (D49).

How M4 implements them:
- **Instances.** Sema keeps the AST of a generic as it was parsed, and checks a copy of it (`clone()` in `src/ast.cpp`) for each instance, with the type parameters bound to the type arguments. The instances are stored in `Module::structInstances` and `Module::functionInstances`; codegen emits only the ones whose types are all real.
- **Checked once (D52).** The first instance of each generic has its own type parameters as types (`TypeKind::Param`): it is the check of the definition, and it is never emitted. On a `T` only the methods of its constraints can be called, and `T` is treated as a type that may need destruction, so the analysis of moves of the definition holds for every instance. The instances with real types are checked again only for the copies of `T` (D85); their errors name the instance, for example `(in max<File>, used at line 23)`. When the definition has errors, its instances are not checked.
- **Deduction (D86).** The arguments of a call are checked on a copy, with the diagnostics muted, to learn their types; then the types of the parameters, as written, are matched with them.
- **Nesting.** An instance whose type arguments are nested more than 16 levels deep, such as a generic that calls itself with `Pair<T>`, is an error.
- **Parameters of interface type** are a pair `{ ptr, ptr }` passed by value, for read and `ref` parameters alike: the address of the object and its method table, a constant array `@vtable.Circle.Shape` with the methods in the order of the interface. It is emitted the first time a `Circle` is passed as a `Shape`. A dynamic call loads the address of the method from the table and calls it with the same convention as a direct call.

### `print` and `println` (D38)
The compiler parses the format string at compile time and translates it into a sequence of runtime calls. For example `println("average: {:.2f}", m)` becomes:
1. write the text `"average: "`;
2. write `m` as a floating-point number with 2 decimal places;
3. write the newline.

No string is parsed at runtime.

### Symbol names
A naming scheme is needed for functions, methods and instances of generics, for example `@module.Type.method`. It must be defined taking modules (D50) and generics (D39) into account.

Provisional names since M2: the type of struct `T` is `%struct.T`, its methods are `@T.method`, the constructor `@T..new` and the destructor `@T..drop`. The `copy()` generated by the compiler (D35) is `@T.copy`, like one written by hand. The instances of generics (M4) have the type arguments in their name, which is then quoted: `@"max<Money>"`, `%"struct.Pair<i32, f64>"`, `@"Pair<i32, f64>.first"`. The double dot cannot clash with the name of a method. A method receives the object as its first parameter, `ptr %self` (`readonly` for `const` methods), after the `sret` pointer if there is one.

## 4. Toolchain (D28)

- **LLVM and LLD inside `cromo`:** a stable version of LLVM is pinned, because the textual IR format and the API can change between major versions. The tests always run with that version.
- **Target:** the compiler writes the `target triple`, for example `x86_64-pc-windows-msvc`.
- **Windows (D27):** Microsoft x64 ABI. Linking with `kernel32.dll` does not require the Windows SDK: `cromo` generates the import libraries by itself from the list of the functions used, as Zig does. LLVM already contains the code to write them.
- **Without libc (D19):** linking uses a custom entry point and no default libraries. On Windows with `/ENTRY` and `/NODEFAULTLIB`, on Linux with `-nostdlib` and `-static`.
- **Linux (D27):** at a later stage, with the System V ABI.
- **Build of the C++ compiler (D53):** C++20 with MSVC, CMake and Ninja, through `build.ps1`. LLVM and LLD come from the official 23.1.4 archive, installed in `C:\software\llvm-23.1.4`.
  - **Static runtime:** the LLVM libraries are Release, with a static C runtime (`/MT`) and without RTTI, so `cromo` uses the same options. This way `cromo.exe` does not require the Visual C++ Redistributable.
  - **zlib, zstd and libxml2:** LLVM was built with these libraries, but the archive does not include them. vcpkg provides them as static libraries (`vcpkg.json`, triplet `x64-windows-static`).
  - **Size:** `cromo.exe` is about 100 MB, because it contains LLVM and LLD.
- **Provisional symbol names:** until the naming scheme exists, Cromo functions keep their own name in the IR (`@main`). The entry point is called `cromo_start`, and the names `cromo_start` and `ExitProcess` are reserved. Until the import libraries are generated by `cromo` (M8), the one from the Windows SDK is used for `kernel32`.

## 5. Runtime (D19)

Neither the C library nor the C++ runtime is used. The runtime and the standard library are written in Cromo, in the `unsafe` dialect (D37), and where needed in assembly or in hand-written LLVM IR. The program talks directly to the operating system:
- **Windows:** it has no stable system calls, so the functions of `kernel32.dll` are used (`ExitProcess`, `GetStdHandle`, `WriteFile`, `HeapAlloc`/`VirtualAlloc`, `CreateFileW`, …). They are the system API, not libc.
- **Linux:** direct system calls (`write`, `mmap`, `exit`, …).

What the runtime must provide:
- its own **entry point**, which replaces the C runtime startup: it calls `main` and terminates the process with its return value;
- `cromo_panic(message)`: prints the message and terminates the program;
- **allocator** on top of the system primitives: at first the Windows heap (`HeapAlloc`) is enough, and a simple allocator on top of `mmap` on Linux;
- **minimal I/O:** writing to stdout and stderr;
- **number formatting** without libc (D38):
  - integers, in base 10, 16, 2 and 8, are simple;
  - floating-point numbers (`f32`, `f64`) are the most delicate part. They need a dedicated algorithm (for example Ryū) to print the shortest number that reads back identical, and correct rounding when a precision is requested;
- **symbols that LLVM expects anyway:** `memcpy`, `memmove`, `memset` and `memcmp`, because the optimizer can generate calls to these functions even if the code does not use them. On Windows `__chkstk` is also needed, the stack check for frames larger than 4 KB, and `_fltused`, required when floating-point numbers are used.

Exceptions require nothing from the runtime (D36).

### The temporary runtime of M1–M4 (D58)
Until the `unsafe` dialect exists (M5), the runtime is the freestanding C code in `runtime/`:
- `cromo_rt.c`: buffered output on stdout and stderr (UTF-8, converted to UTF-16 when writing to a console), the formatting functions called by `print`/`println`, `cromo_exit` and `cromo_panic`;
- `mem_x64.S`: `memcpy`, `memset`, `memmove`, `memcmp` and `__chkstk`.

CMake compiles them with the clang of the LLVM archive (`-ffreestanding -fno-builtin -nostdlib`), archives them with `llvm-lib` and embeds the library in `cromo.exe` as a byte array (`cmake/embed.cmake`). At link time `cromo` writes it next to the object file and passes it to LLD.

Floating-point numbers are formatted with big integers, so the digits are always exact: the shortest form uses the algorithm of Steele & White and Burger & Dybvig, and the forms with a precision round the exact value half to even. It was checked against Python on 500 values (random bit patterns, subnormals, extremes, exact ties) in five formats, with no differences.

The generated code calls these functions (all `nounwind`):

| Function | Use |
|---|---|
| `cromo_write(stream, ptr, len)` | literal text of a format string |
| `cromo_fmt_int(stream, value, signed, spec)` | integers, extended to 64 bits |
| `cromo_fmt_f64` / `cromo_fmt_f32(stream, value, spec)` | floating-point numbers |
| `cromo_fmt_bool(stream, value, spec)` | `true` / `false` |
| `cromo_fmt_str(stream, ptr, len, spec)` | string literals (D65) |
| `cromo_print_end(stream, newline)` | end of a print: newline and flush |
| `cromo_exit(code)` | end of the program, called by `cromo_start` |
| `cromo_panic(message, len)` | panic (D62), exit code 101 |

`spec` points to a constant `FmtSpec` built by the compiler from the format specification, or is null for `{}`.

### `memcpy` and `memset` in assembly (D19)
Implementation for x86-64:
- **Instructions:** `rep movsb` for `memcpy` and `rep stosb` for `memset`. They are short and, on modern processors (ERMSB), as fast as hand-optimized versions for most sizes.
- **Calling conventions:** they change between systems, so two variants or some macros are needed. On Windows the arguments arrive in `rcx`, `rdx`, `r8`, on Linux in `rdi`, `rsi`, `rdx`.
- **Assembler:** the one integrated in LLVM, with Intel syntax (`.intel_syntax noprefix`). NASM or MASM are not needed.

## 6. Tests

- **Behavior tests:** in `tests/`, `.cro` files with the expected result written in the comments:
  ```cromo
  // cromo-flags: --release
  // expect-exit: 42
  // expect-output: hello
  // expect-stderr: panic: integer overflow
  // expect-error: 's' has been moved
  // expect-warning: unreachable code
  ```
  `tests/run-tests.ps1` compiles and runs each file and compares the result with the expected one; `-Flags -O0` passes extra options to every compilation. With `-Flags --release` the three tests that check the panics of debug mode fail, as expected.
- **IR tests:** a few checks that verify the attributes that matter for the optimizer, for example that a `ref` parameter is really `noalias`.
- **Later:** differential tests with generated programs.

## 7. Development milestones

Each milestone lists the decisions still open that are needed **before** writing its code.

**Status:**
- **M0** completed on 2026-10-28: `cromo.exe` compiles `i32 main() { ret 42 }` into a 2 KB executable that imports only `ExitProcess` from `kernel32.dll`. Tests in `tests/m0/`.
- **M1** completed on 2026-10-28: integer, floating-point and `bool` types, `char` literals, arithmetic with the overflow and division checks, conversions, variables with default values and `const`, `if`/`else`, the three forms of `for` with `break`/`continue`, functions with parameters and recursion, `print`/`println`/`eprint`/`eprintln` with formatting, debug and `--release` modes, and the temporary runtime (D58). Tests in `tests/m1/`; the choices made for M1 are decisions D54–D67 in [decisions.md](decisions.md).
- **M2** completed on 2026-10-28: structs with initial values of the fields, nested structs, methods and `const` methods, one constructor, destructors, destruction at the end of the scope and on `ret`/`break`/`continue`, temporaries destroyed at the end of the statement, structs passed and returned by value or by address depending on the size. Tests in `tests/m2/`; the choices made for M2 are decisions D68–D76 in [decisions.md](decisions.md).
- **M3** completed on 2026-10-28: `ref` and `move` parameters, `move` in initializations, assignments and `ret`, the error on the use of a moved variable along every path, drop flags, moving fields and `ref` parameters with the default value left in place, `copy()` written by hand or generated, exclusivity with the copy of simple read arguments. Tests in `tests/m3/`; the choices made for M3 are decisions D77–D83 in [decisions.md](decisions.md).
- **M4** completed on 2026-10-28: generic functions and structs with constraints, deduction of the type arguments, interfaces with `Self`, structs that implement them, static calls through generics and dynamic calls through parameters of interface type. Tests in `tests/m4/`; the choices made for M4 are decisions D84–D92 in [decisions.md](decisions.md).

| Milestone | Content | Open decisions |
|---|---|---|
| **M0** | `i32 main() { ret 42 }` becomes an executable that returns 42 as its exit code. Lexer, parser, IR emission, `cromo.exe` in C++ with LLVM and LLD inside, test runner, entry point without the C runtime | none |
| **M1** | Integers, floating-point numbers and booleans, `char` and character literals, arithmetic, variables with default values, `if`/`else`, `for` in the forms without lists, functions and recursion with the `ret` check, `print`/`println` with formatting | none |
| **M2** | Structs with default values, `const` methods, constructors, destructors and cleanup on all exits from the scope (RAII) | none |
| **M3** | Ownership: `move`, `copy()`, `ref`/`move` parameters, exclusivity, analysis of moves on the AST (D77) | none |
| **M4** | Generics and interfaces | none |
| **M5** | Containers and minimal library written in `unsafe` Cromo: `list<T>`, `string`, `for` over the elements, `panic`, bounds checking | none |
| **M6** | Exceptions (`try`/`catch`/`finally`) and enums | enum syntax |
| **M7** | Modules and interoperability with C | `extern` syntax |
| **M8** | No Windows SDK: import libraries generated by `cromo` | none |
| **M9** | Linux | none |
| **M10** | Compiler written in Cromo that compiles itself (§2) | none |

Generics come before containers because `list<T>` is written in Cromo as a generic struct (D37, D39).

## 8. Repository structure (proposal)

```
cromo/
  docs/                 project documents
  src/                  bootstrap compiler in C++ (lexer, parser, semantics, IR emission, driver)
  compiler/             compiler written in Cromo (from milestone M10)
  runtime/              entry point and low-level primitives (LLVM IR, assembly)
  std/                  standard library in Cromo
  tests/                behavior and IR tests
  examples/             example programs
  tools/
    vscode-cromo/       VS Code extension for syntax highlighting
```
