# Compiler architecture

> **Status: proposal.** The bootstrap compiler is written in C++ (D44); its details are in D53 of [decisions.md](decisions.md).

## 1. Pipeline

The final product is an archive for each system (D237): `cromo`, which does everything (D28), and next to it the library and the runtime of each target. `cromo` reads them from `lib/`, in the archive and in the build of development (§4). The pipeline of `cromo`:

```
.cro source
  → lexer                      tokens
  → parser                     AST, one per file
  → loading of the modules     the files imported, at any depth (D50)
  → name resolution            per module (D126)
  → type checking              generics, interfaces, conversions
  → ownership analysis         default values, moves, exclusivity, destructor insertion
  → LLVM IR emission           text (D53)
  → LLVM, inside cromo         reading the IR, optimization and machine code
  → LLD, inside cromo          linking with the runtime: COFF for Windows, ELF for Linux (D142)
  → executable
```

- **Parser** written by hand, recursive descent. Parser generators are not used: the error messages are better and the grammar stays under control.
- **Cromo intermediate IR.** The compiler goes directly from the AST to LLVM IR. A dedicated control-flow-graph IR (like Rust's MIR) will be introduced when a feature needs it: since the control flow of Cromo is structured, the analysis of moves is done on the AST (D77).
- **Ownership analysis** (`src/ownership.cpp`, D77). It runs after type checking and follows the state of every variable (alive, moved, maybe moved) through branches and loops, as the definite assignment of Java and C# does. A loop is visited until the state at its start no longer changes, then once more to report the errors. Exclusivity (D22, D79) is checked by sema, call by call.
## 2. Bootstrap: from C++ to Cromo (D44, D147)

The first compiler is written in C++ and uses LLVM and LLD directly, statically linked into `cromo.exe`. The goal is a compiler written in Cromo that compiles itself, as the last milestone: until then the language and its library grow in the C++ compiler (D147). The classic path has three stages:

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

How M6 implements it:
- **Which functions can throw.** Sema records, for every function, the `throw` statements and the calls that no enclosing `try` with `catch (Exception e)` protects, and computes a fixpoint over the program: a function can throw if one of those calls goes to a function that can. A method of an interface can throw if one of its implementations can, and then all of them get the hidden parameter, because they share the method table. The functions that cannot throw are compiled as before, at no cost.
- **The slot.** A function that can throw has a last hidden parameter `ptr %raised`, pointing to `{ ptr object, ptr descriptor }` in the caller; the others use a slot of their own. `throw x` allocates the object on the heap, writes it with its descriptor into the slot and leaves; after every call that can throw, a non-null object in the slot means an exception.
- **Leaving.** The code that handles an exception at a point destroys the temporaries and the objects of the scopes it leaves, runs the `finally` blocks on the way, and jumps to the dispatch of the nearest `try` body, or returns from the function with the exception still in the slot. A constructor first destroys the fields of its object (D123).
- **The descriptor** of a thrown struct, `@"exception.T" = { drop, methods of Exception }`, tells the `catch` clauses its type, frees it, and gives `catch (Exception e)` its method table. A `catch` moves the exception out of the slot and frees it at its end, unless `throw` alone gives it back.
- **`finally`** is generated again at every exit of its `try` (end of the body or of a `catch`, `ret`, `break`, `continue`, exception), as the C# compilers do. It can always be copied, because it can neither jump out nor let an exception out (D113, D120).
- **`main`** that can throw is called by `cromo_start` with a slot: an exception left there is reported with its `message()` (D121). When `main` takes the arguments (D154), `cromo_start` first calls `programArguments()` of the library, which builds the `list<string>`.

### Enums (D104)
An enum is `{ [0 x i64], i32 tag, [n x i8] padding, data of the first case, [m x i8] padding }`: the field of size zero gives it the alignment of its most aligned case, the data of every case start at the same offset after the tag, and the size is that of the largest case. Each case also has a type of its own, `%"case.Shape.Rect" = type { double, double }`, used to read its data at that offset. The default value is a constant: the first case with the defaults of its data (D111). `switch` on an enum is a `switch` of LLVM on the tag; destroying and copying an enum look at the tag too.

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

### The standard library and `unsafe` (D37, M5)
- **The library** is `library/core.cro`, read from `lib/core.cro` like the runtime (D237) and compiled before every program as a second source file: its locations have `SourceLocation::file == kLibraryFile`, and the messages of the program name the file `cromo:core.cro`. It is the module `std`, which writes `import unsafe` (D127) to use the type `addr` and the built-in functions of D98; its fields are private, as those of any module (D50).
- **`list<T>` and `string`** are structs of the library, `{ addr data, i64 length, i64 capacity }`. The compiler knows the names of `data` and `length`: it uses them for `v[i]`, for `for` over the elements, for string literals and for printing strings.
- **String literals** are `string` values `{ ptr @.str.N, i64 len, i64 0 }`: capacity 0 means that the bytes belong to the program, so the destructor does not free them and the first change copies them to the heap (D100). The operators of D95 become calls to `equals`, `compare`, `concat` and `append`.
- **`v[i]`** loads the length, compares the index with it as an unsigned number, which also catches the negative ones, and calls `cromo_panic_index` when it is out of bounds (D30). Then it computes the address of the element in `data`.
- **`for` over the elements** reads the length and the buffer once, because the list cannot change during the loop (D101), and the variable of the loop is the address of the current element, without a copy.
- **Exclusivity with elements (D35).** `v[0]` and `v[1]` never overlap, `v[0]` and `v[0]` always do; when an index is not a constant, sema records the pair of arguments and the call compares the address ranges of the two places at runtime before the call.
- **Panics of the library (D102).** Every function of the library, except the destructors, has a hidden last parameter `ptr %caller`: the location of the call, a constant `{ ptr, i64 }` with the text `file:line:column`. Inside the library, the panics and the bounds checks report that location, so the user sees the line of their own code.
- **Methods of instances (D103).** For an instance of a generic struct with real types, only the destructor is checked at once; a method is checked, and emitted, the first time it is used, or when the struct is passed as an interface that has it.

### `print` and `println` (D38)
The compiler parses the format string at compile time and translates it into a sequence of runtime calls. For example `println("average: {:.2f}", m)` becomes:
1. write the text `"average: "`;
2. write `m` as a floating-point number with 2 decimal places;
3. write the newline.

No string is parsed at runtime.

### Modules (D50, D126, M7)
- **Loading.** The driver (`src/main.cpp`) parses the standard library and the main file, then the files that they import, and the ones these import, once each: a file is known by its full path. Every file has its index in `SourceLocation::file` and its `ModuleFile` in the `Module`: the name of the module, its imports, with the files each one brings, and whether it writes `import unsafe`. All the declarations end up in one `Module`, compiled together, so two modules can import each other.
- **Names.** Sema keeps the names of each file apart. A name is looked for in the file itself, then among the public names of the files it imports, then in the standard library (D131); two imports that give it are an error at the point of use, which then writes the module in front. `geometry.area(c)` reaches the parser as a method call on a name: sema recognizes the name of an import and turns it into a call with its module. In a type the parser reads `geometry.Circle` directly.
- **Visibility.** Fields, methods and constructors are checked where they are used: the module of the struct, its source file, can use them all, the others only the public ones (D132). The instances of a generic have the locations of the generic, so they see what the generic sees.

### Symbol names (D134)
Every symbol has its module in front, set by sema in `symbol`: the type of struct `T` of the module `geometry` is `%"struct.geometry.T"`, its methods are `@"geometry.T.method"`, the constructor `@"geometry.T..new"` and the destructor `@"geometry.T..drop"`. The `copy()` generated by the compiler (D35) is `@"geometry.T.copy"`, like one written by hand. The instances of generics (M4) have the type arguments in their name, with their module: `@"std.list<geometry.T>.add"`. The double dot cannot clash with the name of a method. The main file is the module named after it, the library is `std`, and the C functions keep their own name. A name that starts with a digit, as the module `04_lists`, is quoted. A method receives the object as its first parameter, `ptr %self` (`readonly` for `const` methods), after the `sret` pointer if there is one.

### C functions (D128, D129, M7)
- An `extern` block gives `FunctionDecl`s with `isExtern`, without a body, which become `declare` lines in the IR, once per name. They have no hidden parameters: they cannot throw, and they do not receive the location of the call.
- A `ref` parameter is a pointer. A `bool` is a byte, as the `bool` of C: the call extends the argument with `zext` and compares the result with zero.
- The driver looks for the library of each block (`src/libraries.cpp`): a `name.lib` next to the modules that declare it goes to LLD as it is; otherwise `name.dll`, next to those modules or in `System32`, is read with the COFF reader of LLVM, and every C function must be among its exports (D136, D137, D139, D140).
- For each DLL the backend writes the import library with `writeImportLibrary` of LLVM, the function that `lld-link /def` uses, in a temporary file next to the executable. The one of `kernel32` also has the functions of the runtime: the `__imp_` symbols that the runtime archive leaves undefined.
- After linking, the DLLs found next to the modules are copied next to the executable, if it is in another folder (D138, D141).

### The values of the cases (D135)
The tag of an enum is the value of its case, which is its position unless it is written. `i32(color)` reads the tag; `Color(n)` is a `switch` on `n` with a value for each case, and the `default` is a panic.

### The commands of `cromo` (D151, M10)
`cromo` reads its command line as UTF-16 (`wmain`): the program arguments of `cromo run` reach the program exactly as they were written.
- `build [options] [file.cro]` compiles; without a file, `main.cro` of the current folder, and the executable is named after the folder (D155).
- `run [options] [file.cro] [-- arguments]` compiles into `cromo-run-<pid>` in the temporary folder, starts the program with `CreateProcessW`, quoting every argument with the rules of the C runtime of Microsoft, or on Linux with `fork` and `execv`, waits for it, deletes the folder and returns its exit code (D156, D159).
- `version` prints `cromo <version> (LLVM <version>)`: the version comes from `project(... VERSION ...)` in `CMakeLists.txt` (D157).
- `fmt [--check] [files and folders | -]` formats the code (D161–D164): see below.
- `lsp` is the language server: see below.
- `update [version]` compiles and runs `lib/tools/update.cro` (D403): see below.
- `help` prints the commands.

### Linux ARM64 (D405–D408, M29)
- **The target:** `Target::LinuxArm64` (`src/target.h`), `aarch64-unknown-linux-gnu` with the CPU `generic`, ARMv8.0, and the feature `+outline-atomics`: an atomic operation calls `__aarch64_cas4_acq` and the others, as with GCC and Rust (D407). LLD writes the static ELF as for x86-64, and the backend registers both targets of LLVM, whatever the system `cromo` runs on; `src/llvm_trims.cpp` no longer leaves AArch64 out: `cromo.exe` went from 73 MB to 84 MB, `cromo` for Linux from 54 MB to 63 MB.
- **`systemCall`** becomes `svc #0`, with the number in `x8` and the arguments in `x0` to `x5`.
- **The runtime in C** has the numbers of the system calls of each architecture, and on both the calls that ARM64 has: `epoll_pwait` in place of `epoll_wait`, `openat` in place of `open` (P23). The parts in assembly have their ARM64 version: `clone` with the order of its arguments on ARM64, the register of the thread `tpidr_el0` in place of `fs`, set by `_start` with `msr` and by `clone` with `CLONE_SETTLS`; `struct epoll_event` not packed; the registers of the ucontext for the stack overflow.
- **`runtime/mem_arm64.S`:** `memcpy` and the others, `_start`, `cromo_switch`, which keeps x19 to x30, d8 to d15 and `fpcr` on the stack of the task, `cromo_task_entry`, `cromo_sigreturn`, and the 96 functions of the atomics, written by four macros: each one reads `cromo_lse_atomics`, set at the start from `HWCAP_ATOMICS` of `AT_HWCAP`, and uses one instruction of ARMv8.1 (`casal`, `ldaddal`, …) or a loop of `ldaxr` and `stlxr`.
- **The stack traces:** the interpreter of the rules of DWARF (D375) works with the numbers of the registers of each architecture; on ARM64 the return address stays in x30 until a function saves it, so the first frame, of `traceCapture` or of the handler of a stack overflow, can take it from there, and the CFA can be the stack pointer itself.
- **The page:** the runtime reads `AT_PAGESZ`, since a kernel for ARM64 can have pages of 16 or 64 KB, as the one of the Raspberry Pi 5, and the guard pages of the stacks of the threads and of the tasks must be pages.
- **The library:** each module that makes system calls on Linux has `name_linux_x86_64.cro` and `name_linux_arm64.cro` with an `enum Call` of their numbers, and the place of `st_mode` and the value of `O_DIRECTORY` in `std.fs`; the loader takes the files with the suffix of the architecture as those of the system (D408).
- **`sret` at the calls:** a result passed by address goes in x8 on ARM64, not in the first register of the arguments as on x86-64, so a call through a pointer, to a method of an interface or to a function value, must say `sret` itself: LLVM cannot read it from a function it does not know. On x86-64 the two are the same register, and the call without it worked.
- **The tests** run in WSL with qemu: all those for Linux pass with `-Flags '--target linux-arm64'`.

### `cromo update` and the archives (D403, D404, M28)
- **A program in Cromo:** `lib/tools/` holds the programs of `cromo` written in Cromo; the build copies it from `library/tools/`, and `main.cpp` compiles `update.cro` with `run()`, as `cromo run`, giving it the version asked. Compiling takes about a second: updating to 0.27.1 from cromolang.org, 27 MB, took 4.6 s on Windows, download included.
- **The checks:** the manifest and its `.sig` are read with `http.Client`, the signature is checked with `crypto.ed25519Verify` and the public key written in the program (D286, D300), then the SHA-256 of the archive, which is downloaded with `Accept-Encoding: identity`, so that its bytes are the ones of the manifest, and a `bodyLimit` of 1 GB (P19).
- **The archives** are opened by `std.zip`, which reads the central directory and inflates every file with `gzip.inflate`, checking its CRC-32, and by `std.tar` after `gzip.decompress`, all in memory, into `<version>.partial`, renamed when complete.
- **The link:** `fs.createLink` makes a junction on Windows, writing the reparse point of an empty folder with `FSCTL_SET_REPARSE_POINT`, and a symbolic link on Linux. The new link is made as `current.next` and renamed over `current`, in one step on Linux and after removing the old junction on Windows. The `cromo` that runs from `current` can change the version it comes from: its file stays where it is.
- **The tests** cannot sign a manifest with the key of Cromo: `tests/m28/update_refused.cro` plays the site with a server of its own and checks what is refused, with `{cromo}` and `{port}` of the runner. Installing a real version, and changing it while the `cromo` of `current` runs, were tried against cromolang.org on Windows and on Linux.

### Anonymous functions (D183–D186, M13)
- **Syntax:** the parser reads a lambda when an expression starts with `[`, and gives it a `FunctionDecl` with `isLambda` for its body. A function type is a `TypeName` with `isFunction`: after its `)`, a type is the one returned unless a name follows it, as in `func(i32) f`. Inside a brace the lexer forgets the parentheses open outside, so the body of a lambda passed as an argument ends its statements at the newlines.
- **Types:** `TypeKind::Function` points to a `FunctionSig`, one for each signature, so that two types are equal when their pointers are. A function value is `{ ptr, ptr }`: the code and the data.
- **Sema:** the body is checked as a function of its own, with the variables of outside hidden: its parameters first, then the captures, `VariableKind::Captured`. A lambda that captures by address is accepted only as the argument of a read parameter (D184); its captures by address enter the checks of exclusivity of the call. The call of a function value becomes a `CallValueExpr`, checked against a `FunctionDecl` made for the signature, and noted as a call that can throw.
- **Literals of a list (D308):** a `ListLiteralExpr`, whose sema finds the type of the elements, instantiates `list<T>` and checks every element as the argument of `add(move T)`; codegen builds the list in its destination with `reserve()` and an `add()` for each element, through `emitCall`, and keeps it among the temporaries until the end, so that an exception of an element destroys what was added.
- **Code:** the body is a function whose data arrive as `%env`, where a method has its object; a capture is a field of the data, its value or the address of the variable. The data start with the function that destroys them: `.drop` also frees them from the heap, `.release` leaves them in the frame. As an argument the data are in the frame and registered as a temporary; otherwise they come from `cromo_alloc` and the value owns them. Calling an empty value is a panic.
- **Moves:** a capture with `move` is a move of the variable where the lambda is made; the body is checked as a function of its own.

### The basics of the library (D193–D204, M14)
- **Bitwise operators:** the lexer has `&`, `|`, `^`, `<<` and their assignments; `>>` is two `>` next to each other, so that `list<list<i32>>` still closes its type arguments. The parser has a level for each of `|`, `^`, `&` and the shifts, between the comparisons and `+` (D193). A shift checks its count in debug and masks it in release (D195).
- **Arrays:** `TypeKind::Array` points to an `ArrayInfo`, one for each element type and count, as the signatures of the functions. An array is in memory as a struct: `[4 x i32]`, copied with `memcpy`, passed by address when larger than 16 bytes, destroyed with a loop over its elements, the last first. A large default value is written with `memset`, or with a loop, never as one constant that LLVM would split into a store for each element. `a.len()` becomes a constant.
- **Strings:** `s[i]` reads a byte from the fields `data` and `length`. `format` is a `PrintExpr` on the stream 3 of the runtime, which writes into memory; at the end the string takes that memory (`cromo_format_end`).
- **Numbers from text:** `i32(s)` becomes a call of `parseSigned` of the library, with the largest value of the type and its name for the messages. The library checks the text; for the floats `cromo_parse_float` of the runtime divides the digits as big integers, so the rounding is always exact.
- **map:** written in Cromo in `library/core.cro`, with open addressing and linear probing. A slot keeps together the hash, the key at 8 bytes and the value after it, so that a lookup reads one place of memory; the hash, with its highest bit set, also says that the slot is full, and a key is compared only where the hashes are equal, in place, with the built-in function `equalAt`. `m[k]` calls `valueOf`, or `place` when it is the target of an assignment, `+=` or `++`, which gives the address of the value. `for (K k, V v in m)` goes over the slots in the code generated, which knows their layout. `x.hash()` of a number is computed by the compiler with `cromo_hash_bits`; a string is hashed eight bytes at a time.
- **Modules of the library and files of a system:** the files `library/std/*.cro` are read from `lib/std/` (D237). The loader adds to a module the file with the suffix of the target, `_windows` or `_linux`, which shares the scope of the module (`ModuleFile::part`); the files of the other system are left out (D202). The Windows files call kernel32 through `extern`; the Linux files use the intrinsic `systemCall` and `environment`. `s.cstring()` becomes the field `data` of a temporary `CString` of the library, destroyed at the end of the statement (D204).

### Threads (D214–D217, M15)
- **The runtime is safe with threads:** a lock is an SRWLOCK on Windows and a futex on Linux, free at zero. One lock lets a single `print` write at a time, taken by `cromo_print_begin` after the arguments are computed; the allocator for Linux has its own lock, the heap of Windows already has one. Until the first thread starts, these locks are skipped, as glibc does. A `format` writes directly into its string, whose address the compiler passes in place of the number of the stream.
- **Threads of the system:** `CreateThread` on Windows; on Linux `clone` with a stack from `mmap` and a guard page, in a few lines of assembly, and `join` waits on the futex that the kernel clears when the thread ends. The runtime calls the code of the lambda with its data and a slot for its exception, as any call that can throw (D36).
- **`std.thread`** is written in Cromo (`library/std/thread.cro`) with the built-in functions `threadStart`, `threadWait`, `threadRethrow`, `threadUncaught`, `threadFree`, `mutexLock`, `mutexUnlock` and `selfAddress`. `threadRethrow` moves the exception of the thread into the slot of the caller of `join()`, which can therefore throw anything.
- **Threads of a block:** sema lets the lambda written in `thread.Thread(...)` capture by address when the construction is the value of a declaration; the data of the lambda stay in the frame and the `Thread` owns them, destroying them with `.release`. From `start()` to `join()`, or to the end of the block, the captures by address are lent as the elements of a `for` (D101): `loopBorrows_` refuses the modifications, and `threadBorrows_` also the reads of what the thread has with `ref`.
- **Mutex:** `lock` is a `const` method that writes the mutex through `selfAddress()`. The struct is marked `addressed`, and so is every struct that holds one: it is passed by address and without `noalias readonly`, so that LLVM does not assume that a call leaves it unchanged.
- **LLVM without the C library:** the analysis of the library functions allows only `memcpy`, `memmove`, `memset`, `memcmp` and, on Linux, `bcmp`, which `runtime/mem_x64.S` defines; otherwise LLVM turned a loop of `std.fs` into a call to `strlen`.
- **`const func` (D253):** `checkConstFunction` refuses, in the argument of a `const func` parameter, a lambda that captures with `ref` or captures a parameter `func` that is not `const`; `mayModifyCaptures` follows the captures of nested lambdas back to the parameter. The lambda of a thread or of a task cannot capture such a parameter either.

### Tasks (D269–D284, M19)
- **In the runtime in C** (`runtime/cromo_rt.c`): a task is a record with the same head as a thread, `{ code, data, exception, kind }`, so that `threadWait`, `threadRethrow`, `threadUncaught` and `threadFree` take threads, tasks and groups; `taskStart` and `groupStart` are the new built-in functions of the `unsafe` dialect.
- **Stacks:** on Windows a fiber with 1 MB reserved; on Linux a slot of 1 MB in regions of 4 GB of addresses reserved without memory, opened with one `mprotect` that leaves the guard page at its bottom, and `cromo_switch` of `runtime/mem_x64.S`, which saves the registers that a call preserves and the control words of SSE and x87. The stack of a task that ends is kept for the next one, up to 1024; the others give their memory back. On Linux every stack is two areas of the kernel, the guard page and the stack: `vm.max_map_count`, often 65530, allows about 32 000 tasks at once.
- **Workers:** one thread of the system for each processor takes the tasks from one queue. A worker knows itself from TLS on Windows and from the register fs on Linux, which `_start` and `clone` (`CLONE_SETTLS`) point to a block of every thread. A task that waits switches to its worker; whoever wakes it puts it in the queue once its worker has left its stack (`running`).
- **Waits:** a mutex, `join()` and the end of a group use lists keyed by an address, as the futexes of Linux: a task parks, a thread of the system sleeps on a futex or on a condition variable. The mutex is 0 free, 1 taken, 2 with others waiting.
- **Workers that spin:** a worker that finds the queue empty looks at it for some tens of microseconds before it sleeps, at most half of the workers at once, as in Go: a task woken by another, as through a channel, is taken without waking a thread, which costs some microseconds and much more in WSL. `callWorker` counts on a worker that spins (`claimed`) before it wakes one that sleeps. A ping-pong of 200 000 values between two tasks went from 11.3 s to about 0.35 s on Linux.
- **The poller** is a thread with the heap of the timers (`time.sleep`, and the timeouts of the network) and the waits for the sockets: epoll edge-triggered on Linux, with a record for each socket; on Windows the polls of `\Device\Afd`, which end on an IOCP, as in libuv and in mio. The sockets do not block: a call that would wait returns, and `std.net` waits for its socket, with the poller in a task and with `poll` in a thread.
- **The monitor** (D271), while tasks wait and no worker is idle, looks at the workers every millisecond: one that has run the same task for the whole tick, blocked in a call or busy with the processor, gives its place to another worker, up to 256 more than the processors.
- **Channels and atomics (D326–D337, M23):** a `thread.Channel<T>` holds the address of a channel of the runtime, made by its first use with a compare-and-swap, so that its default value needs no constructor. The channel is a ring of `capacity` values, at least one, behind a lock; who finds it full waits on the counter `space`, who finds it empty on `values`, with `parkOn` and `unpark` as the mutex; with capacity 0 the sender also waits on `taken` until a receiver has its value, unless a receiver was already waiting, which takes it for sure. A value goes through a buffer of the heap, written with `put<T>` and read with `take<T>`. `Channel` and `Atomic` are `addressed`, as the mutex; the instructions of an `Atomic<bool>` work on its byte.
- **Sema:** `thread.Task` follows the rules of `thread.Thread` (`isThreadStruct`). The lambda written in `g.start(...)` of a `thread.Group` gets `group`: its data are on the heap and the group owns them; it cannot capture with `ref`; what it reads by address must be declared before the group, is lent to the group until the end of its block (`loopBorrows_`, `threadBorrows_`), and must not have been modified after the declaration of the group, which `activeGroups_` records.

### Debug information and stack traces (D338–D348, M24)
- **The lines, always:** every instruction that codegen writes has `!dbg` with the line and the column of its statement, or of its call; every function has a `DISubprogram` named as in the source (`debugName`), and `cromo_start` one of its own, so that `main` inlined there keeps its lines. With `--release` the compile unit has only the lines (`LineTablesOnly`), with `debugInfoForProfiling`, without which LLVM would leave out the functions that inline nothing.
- **For a debugger** (without `--release`): a `DILexicalBlock` for each block, `#dbg_declare` for the parameters, the variables, the captures, the elements of a `for`, the data of a case and the exception of a `catch`, `#dbg_value` of `this` for the object of a method, and the types: numbers and bools as in C, a struct with its fields at the offsets of its layout, an enum with its case and the fields of each case after the tag. On Windows the module has both CodeView, for the PDB that `lld-link /debug` writes, and DWARF; `/opt:ref` keeps the code that nothing calls out of the executable, as without `/debug`. The DWARF stays in the executable, about as large as the code: without it Bitdefender took the small executables for malware and quarantined them (D348).
- **The table of the stack traces** (`backend.cpp`): a label `cromo_code`, written with `module asm` at the start of the section of the code, gives the offsets a base; after the optimizations every function gets the section `.text`, otherwise LLVM moves those it finds cold to `.text.unlikely`. The backend reads the line table of the DWARF in the object, asks `DWARFContext::getInliningInfoForAddress` the frames of each address where a row starts, and keeps a point where the frames change; the points, their chains of frames and the strings go into `cromo_trace_table`, in an object of its own compiled from IR. With `--release` the linker drops the DWARF (`--strip-debug` on Linux).
- **The walk of the stack** (D341, D375): with the unwind tables, which every function has with `uwtable`, and the runtime with `-funwind-tables` on Windows and `-fasynchronous-unwind-tables` on Linux. On Windows `RtlCaptureStackBackTrace` reads them. On Linux the runtime finds `.eh_frame_hdr`, which `ld.lld --eh-frame-hdr` writes, in the program headers shown by `__ehdr_start`, looks up the FDE of the address, runs the instructions of its CIE and its own up to the address, and gets the CFA and where the return address and `rbp` were saved; the stack ends at an address without rules, as `_start` and `cromo_task_entry`, or at `cromo_thread_bottom`, the label after the first call of a thread made by `clone`. The frame pointers cost about 20% on fib, so they are not kept. A return address is looked up at the byte before it, inside its call.
- **Exceptions:** `throw` makes its object with `cromo_exception_new`, which puts the return addresses in front of it; `cromo_exception_free` frees both. `cromo_uncaught` and `cromo_thread_uncaught` print them.
- **Stack overflow:** on Windows a vectored handler, with 64 KB that `SetThreadStackGuarantee` keeps on every thread and fiber; on Linux a handler of SIGSEGV with `SA_ONSTACK`, on a stack of 64 KB for each thread, between two guard pages at the bottom of its stack, and `cromo_sigreturn` of `mem_x64.S` as `SA_RESTORER`. A fault far from the stack pointer, of C code, ends the program as usual.

### Values of interfaces and computed defaults (D357–D364, M26)
- **The value of an interface** is the pair `{ object, table }` of a parameter, so that it is passed to a read parameter as it is; owned, the object is on the heap. `coerce` turns a struct into it with a `ConversionExpr` to the interface, unless it is the argument of a read parameter (`borrow`); codegen allocates the object, writes the struct into it and takes the table of the struct for the interface, which ends with `Struct..release`, the destructor of the struct and `cromo_free`. The drop of the pair calls the release of a non-empty object; a method call on an empty pair is a panic. `needsDestruction` holds for an interface, so the values follow the rules of the moves and of the drop flags as those of a function (D184).
- **Computed defaults:** the initial values of the fields that are not literals become `void Struct..default(ref Struct value)`, with `value.field = init` in the order of the fields, checked as any function, without the object of a method, so that the other fields are not visible. `storeDefault` writes the constant, where those fields have the default of their type, then `fillDefault` fills the structs inside, of the fields, of the elements of an array and of the first case of an enum, and calls the default function.

### Tests (D349–D356, M25)
- **The tests:** `[[test]]` is an attribute of the parser, as `[[json]]`; sema checks that the function takes nothing and returns nothing and sets `isTest`. The loader, with `tests`, adds `name_test.cro` next to `name.cro` to its module as a part, as the files of a system (D202), and leaves the `_test.cro` files out of the modules of a folder otherwise.
- **The executable of the tests:** with `CodegenOptions::tests` the entry point is not `main`: `cromo_test_index` of the runtime reads the number in the first argument, and a `switch` calls that test, with the check of an exception that leaves it as for `main`. `cromo test` (`main.cpp`) builds it in a temporary folder, starts it once for each test with the output in a file (`CreateProcessW` with inherited handles on Windows, `fork` and `execv` on Linux), as many at a time as the processors, and writes the results in the order of the tests.
- **`assert`:** the parser keeps the bytes between the parentheses of a call to `assert`, and those of the operator of each comparison; sema cuts the texts of the condition and of its sides from the source. For a comparison of numbers or bools, codegen computes the two sides once, compares them with `compare`, and on the false branch writes the message into a string with the calls of a format, then calls `cromo_panic`.

### JSON and HTTP (D251–D268, M18)
- **The conversion of a type** is written in `library/std/json.cro` with two built-in functions of the `unsafe` dialect, `jsonWrite<T>(ref writer, value)` and `jsonRead<T>(value)`. In the check of a generic they stand for any `T`; in an instance sema turns them into the call of a function of the library for that type: `writeI32`, `readString`, an instance of `writeList` or `readMap`, or a function that it generates for a struct or an enum.
- **Generated in Cromo:** for a struct sema writes the text of a function, `void write(ref Writer w, T value)` with `w.key("name")` and `jsonWrite<F0>(ref w, value.name)` for each field, or `T read(Value v)` with a `switch` on the names of the members; it parses the text with the tokens placed in `json.cro`, so that the names of the module are visible, and checks it as an instance whose bindings give `T` and `F0, F1, ...` their types. `convertsJson` lets it reach the private fields, as `copy()`. The names come from the attributes `[[json(...)]]`, which the parser keeps on the fields and sema reads into `jsonName` (D256).
- **`json.Data`** is an empty interface: `satisfies` accepts every type whose values can be converted, and `jsonProblem` writes the path of the field that cannot, as `Config.log.flush has type func()`, at the call of the program.
- **The server** starts its threads with a recursion of threads of a block, one for each level, which all read the listener and the handler: each accepts its connections (`accept` is `const`) and serves the requests of one connection after the other, with keep-alive. The handler is a `const func`, so the threads can call it at once.

### Network (D223–D226, M16)
- **`std.net`** is written in Cromo (`library/std/net.cro`): `Listener`, `Connection`, `Udp`, the timeouts with `poll`, and the addresses of IPv4 and IPv6 with their text. The calls of the system are in `net_windows.cro`, with ws2_32, and `net_linux.cro`, with the system calls; `connect` and `listen` of ws2_32 are declared in `std.winsock`, a module that only the library can import, because `std.net` has functions with those names.
- **The resolver for Linux** reads `/etc/hosts` and `/etc/resolv.conf`, builds the questions of DNS, sends them with UDP and reads the answers, following the pointers of the names and the aliases. On Windows `GetAddrInfoW` does it.
- **Threads:** the lambda given to `thread.Thread(...)` can modify what it captures with `move`, since it runs once.

### TLS and cryptography (D233–D250, M17)
- **Third-party C:** mbedTLS 3.6 and Monocypher 4 are in `third_party/`, with their versions and checksums in its README. CMake compiles them with the clang of LLVM like the runtime, without the C library and without the Windows SDK (`-nostdlibinc`): `runtime/libc` has the few headers they include, `runtime/tls/libc.c` the functions they call (strings, `snprintf`, memory, time, random bytes, the mutexes of the runtime), and `runtime/tls/mbedtls_user_config.h` leaves out files, sockets, DTLS, renegotiation and the old ciphers. Their objects go into `cromo_rt.lib` and `libcromo_rt.a`, so the linker takes only those a program uses.
- **Without sockets:** mbedTLS reads and writes its records in two buffers of the session (`runtime/tls/cromo_tls.c`), and `std.net` moves them between the buffers and the socket, with the same timeouts as a plain connection. The client verifies the certificate and the name of the server; on Windows, a chain that mbedTLS does not accept is checked again by `CertGetCertificateChain`, because Windows downloads the roots it does not have when a chain needs them. `bcrypt.dll` and `crypt32.dll` are loaded with `LoadLibraryExW` from System32, so the runtime imports only kernel32 (`runtimeImports` gives every `__imp_` symbol of the runtime to kernel32).
- **`extern "cromo"`:** the library declares the C functions of the runtime as those of a DLL, in a block whose library is `"cromo"`; only the files of the standard library can write it, and `libraries.cpp` leaves it out, since the runtime is always linked.
- **Only what is used:** every function of a program has internal linkage, and LLVM removes those that nothing calls, also at `-O0` (`GlobalDCEPass`). A `Connection` reaches mbedTLS only through the table of functions of its session (`runtime/tls/calls.c`), filled by `connectTls` and `listenTls`: a program that never calls them has no TLS, and one that does is about 500 KB.
- **`std.crypto`** (`library/std/crypto.cro`) calls the hashes, HMAC, AES-GCM and ChaCha20-Poly1305 of mbedTLS and Ed25519 and X25519 of Monocypher (`runtime/tls/crypto_*.c`), copying the bytes of the lists into memory for C; hex and base64 are written in Cromo.
- **`std.jwt`** (`library/std/jwt.cro`, D399–D402) is written in Cromo on `std.crypto` and `std.json`. The keys of RSA and of P-256 and P-384 are read once by mbedTLS (`runtime/tls/crypto_key.c`), which keeps them in a `jwt.Key` until its destructor; the glue turns the signatures of ECDSA from r and s side by side, as JWS has them, to DER and back. The tasks of a server verify with the same key at once: a verification of RSA copies n and e, since `mbedtls_rsa_public` holds the mutex of the key for all its work and four threads went as one; the multiples of the generator of a curve, which mbedTLS computes at their first use and keeps in the group, are computed when the key is read, before it is shared, so that two first verifications do not both write them. A key from a JWK Set becomes the DER of a SubjectPublicKeyInfo, built in Cromo from `n` and `e` or from `x` and `y`, and then the same key of mbedTLS. mbedTLS starts once, with its mutexes and PSA, in `cromo_tls_setup` (`runtime/tls/libc.c`), for TLS and for the keys.
- **mbedTLS for Windows** is compiled without the compatibility with MSVC (`-fno-ms-compatibility`): clang then defines `__GNUC__`, and mbedTLS multiplies its big integers with the assembly of x64 of `bn_mul.h`, as on Linux, instead of halves of 32 bits; a verification of RS256 went from 190 to 87 µs, as on Linux.

### The formatter (D161–D164, M11)
`src/format.cpp` works on the tokens, not on the AST: it only changes spaces and line breaks, so literals, comments and the parentheses of the author stay as they are.
- The lexer, with `keepComments`, also gives the comments as tokens, and every token has the byte offsets of its text. The parser marks the `<` and `>` of type arguments and parameters, which are written without spaces.
- The tokens are split into the lines of the source; `{`, and `else`, `catch` and `finally` after a `}`, go to the line before (D162). Every line is written again: indentation from the open blocks and parentheses and from the statements that continue (D32), one space or none between two tokens (D166), the comments at the end of consecutive lines in a column (D168).
- D169: the result is lexed and parsed again. Its tokens must be the ones of the source, apart from the removed line breaks, as must its comments; it must have no syntax errors; and formatting it again must give the same text. Otherwise the file is not changed.
- `cromo fmt -` reads the standard input and writes the standard output in binary mode, so the line endings stay as they are: the extension for VS Code (`tools/vscode-cromo/extension.js`) uses it to format a document.

### The language server (D170–D173, M12)
`cromo lsp` speaks LSP 3.17 on the standard input and output (`src/lsp.cpp`, with the small JSON reader and writer of `src/json.cpp`). A thread reads the messages, so the main loop can wait for the next one with a time limit: 300 ms after the last change it analyzes the documents that changed (D173); opening a document, and every request, analyze at once.
- **The program** (D172): the document itself if it has `main`, else the nearest `main.cro` above it, up to the folder of the workspace, else the document alone, checked with `requireMain` false. The loader (`src/loader.cpp`) takes the texts of the open documents in place of the files on the disk.
- **Errors:** `Diagnostics` collects the messages instead of printing them, and the server publishes them for every file of the program, the standard library excepted.
- **Hover and definition:** after sema, `src/index.cpp` walks the checked program and records every name with what it shows and where it is declared: variables, functions, methods, fields, types, cases. Sema keeps in every `TypeName` the type it resolved. Only names really written at their position are recorded, so the nodes that sema makes, as the `append` of `s += t`, have no hover. While a program has errors before sema, the last index is used.
- **Symbols** come from parsing the document alone; **formatting** is `cromo fmt` (D161).
- **Completion after a dot** (D179): the text with the dot cannot be parsed, so the server analyzes a copy where the member being written becomes a call to `cromo_complete()`, a method that does not exist. Sema reports it, but it still resolves the name before the dot: the index gives its type, whose visible fields and methods are the proposals. After the name of an enum, its cases.
- **The extension for VS Code** (`tools/vscode-cromo/extension.js`) is a client of the protocol written without packages from npm: it starts `cromo lsp`, sends the open documents with their whole text at every change, shows the diagnostics, and asks for hover, definition, symbols, completion and formatting. On Windows it runs `cromo lsp` from a copy of `cromo.exe` in a folder of the temporary folder, next to a junction `lib` to the library of the original (D237), so that `build.ps1` can write `cromo.exe` again while VS Code is open; a new `cromo.exe` restarts the server.

## 4. Toolchain (D28)

- **LLVM and LLD inside `cromo`:** a stable version of LLVM is pinned, because the textual IR format and the API can change between major versions. The tests always run with that version.
- **Target:** `--target windows`, `--target linux` or `--target linux-arm64` (D142, D144, D406); the default is the system that `cromo` runs on. The compiler writes the `target triple`, `x86_64-pc-windows-msvc`, `x86_64-unknown-linux-gnu` or `aarch64-unknown-linux-gnu`, and `src/target.h` keeps the choice.
- **Windows (D27):** Microsoft x64 ABI. Linking does not require the Windows SDK: `cromo` writes the import libraries by itself from the list of the functions used, as Zig does, with the code that LLVM already contains (D136).
- **Without libc (D19):** linking uses a custom entry point and no default libraries. On Windows with `/ENTRY` and `/NODEFAULTLIB`, on Linux with `-nostdlib` and `-static`.
- **Linux (D27, D142):** System V ABI, by cross-compilation from Windows, on x86-64 and on ARM64 (D405). The ELF driver of LLD, which is in `cromo` with the COFF one, writes a static executable at a fixed address (`-static`, entry point `_start`), without the dynamic loader: it runs on any Linux x64, with or without glibc (D145). The static libraries of the `extern` blocks, `libname.a`, come before the runtime on the command line of the linker, because their C code can call `memcpy` (D143).
- **Build of the C++ compiler (D53):** C++20 with MSVC, CMake and Ninja, through `build.ps1`. LLVM and LLD come from the official 23.1.4 archive, installed in `C:\software\llvm-23.1.4`.
  - **Static runtime:** the LLVM libraries are Release, with a static C runtime (`/MT`) and without RTTI, so `cromo` uses the same options. This way `cromo.exe` does not require the Visual C++ Redistributable.
  - **zlib, zstd and libxml2:** LLVM was built with these libraries, but the archive does not include them. vcpkg provides them as static libraries (`vcpkg.json`, triplet `x64-windows-static`).
  - **Size:** `cromo.exe` is about 100 MB, because it contains LLVM and LLD.
- **`cromo` for Linux (D292):** `build-linux.sh` in WSL, with g++ and the official archive of LLVM 23.1.4 for Linux in `~/llvm-23.1.4`, writes `build-linux/cromo`; only the compiler, since `lib/` comes from the build of Windows. The WSL of the author is Ubuntu 20.04, with glibc 2.31 and CMake 3.16, and `cromo` depends only on glibc, from 2.29:
  - the C++ runtime is linked statically (`-static-libstdc++ -static-libgcc`), and so are the libraries that LLVM needs: zlib and zstd, as the targets defined before LLVM looks for them. libxml2, which LLD uses only to merge the manifests of Windows with `/manifest`, would bring 28 MB of ICU: `src/llvm_trims.cpp` gives the 16 functions that LLVM calls, which fail, and the targets `LibXml2::LibXml2` link only zlib;
  - `src/llvm_trims.cpp` also gives empty initializers for the targets other than x86-64 and AArch64, which LLD calls for its LTO: the linker finds them first, and the code of the other targets stays in their archives, on Windows too (RISC-V there). With `--gc-sections` and the symbols stripped, `cromo` for Linux went from 180 MB to 54 MB (23 MB compressed), `cromo.exe` from 104 MB to 73 MB; with AArch64 too (D405) they are 63 MB and 84 MB;
  - LLVM is compiled for a newer glibc: `src/glibc_compat.cpp` gives `__libc_single_threaded` (glibc 2.32) and `mallinfo2` (2.33);
  - on Linux there is no folder of the system to check the C functions of the DLLs of Windows (D137): for `--target windows` they are taken as declared.
- **The installation (D237):** `cromo` reads the library and the runtime from `lib/`: next to its folder `bin/` in an archive, inside its folder in a build, where CMake places them (`build/lib`): `core.cro`, `std/`, `windows/cromo_rt.lib`, `linux/libcromo_rt.a` and `linux_arm64/libcromo_rt.a` (P24). A file of the library is one under that folder (`isLibraryFile`); in the messages of a program it is named `cromo:std/fs.cro`, without the folder where `cromo` is installed.
- **The archives (D285–D287):** `tools/package.ps1` makes them in `dist/`, with the layout of the site: `package/win_x86_64-<version>.zip` and `package/linux_x86_64-<version>.tar.gz`, each with `bin/`, `lib/` and `extension/` at its root; `release/<version>` and `latest`, the manifest with the SHA-256 of the archives; and, with `-Key`, their signatures `.sig`, made by `tools/sign.cro`, a program in Cromo that makes the keys and signs with Ed25519. The `.tar.gz` is made in WSL, for the permissions of `bin/cromo`, whose symbols are stripped.
- **Tests with `cromo` for Linux:** `tools/cromo-wsl.cmd` runs it in WSL with the paths of Windows converted, so that `tests\run-tests.ps1 -Cromo tools\cromo-wsl.cmd -Flags '--target linux'` runs the tests with it.
- **Symbol names:** D134. The entry point is called `cromo_start`, and the C names that start with `cromo_` are reserved for the runtime.

## 5. Runtime (D19)

Neither the C library nor the C++ runtime is used. The runtime and the standard library are written in Cromo, in the `unsafe` dialect (D37), and where needed in assembly or in hand-written LLVM IR. The program talks directly to the operating system:
- **Windows:** it has no stable system calls, so the functions of `kernel32.dll` are used (`ExitProcess`, `GetStdHandle`, `WriteFile`, `HeapAlloc`/`VirtualAlloc`, `CreateFileW`, …). They are the system API, not libc.
- **Linux:** direct system calls (`write`, `mmap`, `mremap`, `munmap`, `exit_group`), with the `syscall` instruction, `svc #0` on ARM64.

What the runtime must provide:
- its own **entry point**, which replaces the C runtime startup: it calls `main` and terminates the process with its return value;
- `cromo_panic(message, where)`: prints the message and terminates the program;
- **allocator** on top of the system primitives: the Windows heap (`HeapAlloc`), and on Linux a simple allocator of its own on top of `mmap`;
- **minimal I/O:** writing to stdout and stderr;
- **number formatting** without libc (D38):
  - integers, in base 10, 16, 2 and 8, are simple;
  - floating-point numbers (`f32`, `f64`) are the most delicate part. They need a dedicated algorithm (Ryū for the `f64`) to print the shortest number that reads back identical, and correct rounding when a precision is requested;
- **symbols that LLVM expects anyway:** `memcpy`, `memmove`, `memset` and `memcmp`, because the optimizer can generate calls to these functions even if the code does not use them. On Windows `__chkstk` is also needed, the stack check for frames larger than 4 KB, and `_fltused`, required when floating-point numbers are used. On Linux LLVM also calls `bcmp`, which is `memcmp`.

Exceptions require nothing from the runtime (D36).

### The runtime in Cromo (D393–D398, M28)
- **`std.runtime`** (`library/std/runtime.cro`, with a file for each system when it needs one) is a module of the library that the programs cannot import, as `std.winsock`. Its functions are exported with the names of the runtime in C that they replace, so the generated code calls them as before.
- **Compiled once.** The build of cromo runs `cromo build --runtime --target <system> -o <object>`, an option only for the build: it compiles `import std.runtime` from a file of the library that exists only in memory, without `main`, as in `--release`, and writes only the object, without the label `cromo_code` of the stack traces. CMake puts it in `cromo_rt.lib` and `libcromo_rt.a` next to the objects of the C, so a program links it as the C and compiles none of it: compiled with every program, the 390 lines of the first pieces had made the build of a hello world go from 158 to 241 ms. The order of the build is the sources of the library in `lib/`, `cromo.exe`, the objects of the runtime in Cromo, the archives, and the archives in `lib/`.
- **`[[export("name")]]`** (D395): sema checks the types of C and gives the function that name as its symbol; codegen defines it without `internal`, without the hidden parameter of the caller of the library (`tracksCaller`), with `zeroext` on a `bool`, and leaves out the `declare` of the runtime with the same name. A function that can throw cannot be exported: computeThrows reports it.
- **`wrapMul(a, b)`** (D396) becomes the `BinaryExpr` of the operation with `wrap` set, which unify types as the other operations and codegen writes as in `--release`, without `llvm.*.with.overflow`.
- **`x.bits()` and `f64.fromBits(b)`** (D397) are the intrinsics `FloatBits` and `FloatFromBits`, a `bitcast`.
- **Module variables** (D394): the parser takes a type and a name followed by `=` or by the end of the line as a `GlobalDecl` of the module. Sema gives each one a `Variable` of kind `Module`, which the name finds after the local variables and the fields, only in the files of its module; the analysis of the moves leaves it out, as its types are simple. Codegen writes an `internal global` with its literal or `zeroinitializer`, and its name is the address of the global; `variableAddress(x)` gives that address, also of a field or of an element.
- **`[[cold]]`** (D398): sema takes it out of the attributes of the function, so that it goes with `[[export]]` and `[[test]]`, and sets `isCold`; codegen adds `cold` to the attributes of the function, and LLVM does not put it inline in its callers.
- **`addr(n)`, `i64(p)` and `u64(p)`** (P13) are a `ConversionExpr` between `addr` and an integer of 64 bits: `inttoptr` and `ptrtoint`.
- **The functions of the systems.** In the object of the runtime for Windows, codegen declares the functions of the `extern` blocks of DLLs with `dllimport`, as the C does, so that the runtime leaves undefined their `__imp_` symbols, which the linker of a program reads from the archive and puts in the import library of kernel32. On Linux codegen writes `systemCall` as the instruction `syscall`, inline, as the C library does: the library and the runtime in Cromo call no function for it, and a function with a system call stays small enough to be put inline. `cromo_pause`, in `mem_x64.S`, is the instruction `pause` of the loops that wait for another thread.
- **Moved so far:**
  - the hashes of the keys (`cromo_hash_bits`, `cromo_hash_bytes`) and the reading of the decimal numbers (`cromo_parse_float`, with its big integers);
  - the heap (`cromo_alloc`, `cromo_realloc`, `cromo_free`): on Windows the heap of the process, on Linux the allocator on `mmap` with a list of free blocks for each size, in module variables, whose path that asks the system for memory is `[[cold]]`, and its lock with a futex, which it takes once the runtime in C has said, with `cromo_heap_threaded`, that the first thread starts. `cromo_out_of_memory` stays in the C, with the output.
- **Next:** the output and the formatting of the numbers.

### The runtime in C (D58, D93)
Until the bitwise operators and fixed-size arrays exist, the runtime is the freestanding C code in `runtime/`; `extern` (M7) is already there:
- `cromo_rt.c`: buffered output on stdout and stderr (UTF-8, converted to UTF-16 when writing to a Windows console), the formatting functions called by `print`/`println`, `cromo_exit` and the panics; the heap was here until it moved to `std.runtime` (D393);
- `mem_x64.S`: `memcpy`, `memset`, `memmove` and `memcmp`; on Windows `__chkstk`, on Linux `bcmp` and the entry point `_start`, which keeps the address of the stack in `cromo_initial_stack` (the arguments of the program are there), aligns the stack to 16 bytes and calls `cromo_start`.

The two files have a part for each system, chosen with `#ifdef _WIN32` (D142). CMake compiles them twice with the clang of the LLVM archive (`-ffreestanding -fno-builtin -nostdlib`): for Windows it archives them with `llvm-lib` into `cromo_rt.lib`, for Linux with `llvm-ar` into `libcromo_rt.a`. CMake places both in `lib/windows` and `lib/linux` (D237), and at link time `cromo` passes the one of the target to LLD.

The heap on Linux is a simple allocator under the lock of the heap. Every block starts with 16 bytes that hold its capacity. The small blocks, up to 128 KB with those 16 bytes, have a size that is a power of two from 32 bytes: they are cut from areas of 4 MB, whose pages the system gives when they are touched, and the freed ones go to a list for each size, to be used again. The larger blocks have a mapping of their own, which grows with `mremap`. The limit is the one of `malloc` of glibc: with 4 KB, a string that grew past 4 KB and was emptied, again and again, made two system calls each time, and they made `bench/strings` spend a fifth of its time in the kernel.

Floating-point numbers are formatted with big integers, so the digits are always exact: the shortest form uses the algorithm of Steele & White and Burger & Dybvig, and the forms with a precision round the exact value half to even. For an `f64` the shortest form is made with Ryū (`runtime/ryu.h`), with the powers of 5 in 125 bits that `tools/ryu_tables.py` writes in `runtime/ryu_tables.h`, and products of 64 by 128 bits made of halves of 32 bits, since the shifts of `__int128` would call compiler-rt on Windows; it gives the same digits as the big integers on more than two million values, random bit patterns, every power of two, the edges of the subnormals and short decimals, and makes `format("{}", x)` of an `f64` about 4.5 times faster. It was checked against Python on 500 values (random bit patterns, subnormals, extremes, exact ties) in five formats, with no differences. `{:.Nf}` takes a shorter way when it can (`fastFixed`): the value is m * 2^e, so m * 10^N in 128 bits, shifted right by -e bits and rounded half to even on what the shift drops, gives the same digits, when N is at most 19 and the result fits in 64 bits; it was checked against Python on 208 636 values, with no differences, and makes `{:.3f}` about four times faster. The buffers of the digits are on the stack: `format` does not take the lock of printing (D221), and several threads write numbers at once.

The generated code calls these functions (all `nounwind`):

| Function | Use |
|---|---|
| `cromo_write(stream, ptr, len)` | literal text of a format string |
| `cromo_fmt_int(stream, value, signed, spec)` | integers, extended to 64 bits |
| `cromo_fmt_f64` / `cromo_fmt_f32(stream, value, spec)` | floating-point numbers |
| `cromo_fmt_bool(stream, value, spec)` | `true` / `false` |
| `cromo_fmt_str(stream, ptr, len, spec)` | string literals and strings |
| `cromo_print_end(stream, newline)` | end of a print: newline and flush |
| `cromo_exit(code)` | end of the program, called by `cromo_start` |
| `cromo_panic(message, len, where)` | panic (D62), exit code 101; `where` points to `{ text, length }` |
| `cromo_panic_index(index, length, where)` | an index out of bounds (D30) |
| `cromo_alloc(size)`, `cromo_realloc(p, size)`, `cromo_free(p)` | the heap of the process (`HeapAlloc` on Windows); out of memory is a panic |
| `cromo_uncaught(message, len)` | an exception that leaves `main` (D121), exit code 1 |
| `cromo_arg_count()`, `cromo_arg_length(i)`, `cromo_arg_data(i)` | the arguments of the program, in UTF-8 (D154): on Windows `GetCommandLineW` split as the C runtime of Microsoft does, on Linux the stack found by `_start` |

`spec` points to a constant `FmtSpec` built by the compiler from the format specification, or is null for `{}`.

### `memcpy` and `memset` in assembly (D19)
Implementation for x86-64:
- **Instructions:** `rep movsb` for `memcpy` and `rep stosb` for `memset`. They are short and, on modern processors (ERMSB), as fast as hand-optimized versions for most sizes.
- **Calling conventions:** they change between systems, so the file has two variants. On Windows the arguments arrive in `rcx`, `rdx`, `r8`, and `rdi` and `rsi` must be preserved; on Linux they arrive in `rdi`, `rsi`, `rdx`.
- **Assembler:** the one integrated in LLVM, with Intel syntax (`.intel_syntax noprefix`). NASM or MASM are not needed.
- **ARM64:** `runtime/mem_arm64.S` copies and fills 16 bytes at a time with `ldp` and `stp`, which ARMv8 allows on addresses that are not aligned, and compares 8 bytes at a time, as `memcmp` of x86-64.

## 6. Tests

- **Behavior tests:** in `tests/`, `.cro` files with the expected result written in the comments:
  ```cromo
  // cromo-flags: --release
  // expect-exit: 42
  // expect-output: hello
  // expect-stderr: panic: integer overflow
  // expect-error: 's' has been moved
  // expect-warning: unreachable code
  // target: windows
  // program-arg: two words
  // cromo-command: run
  ```
  The files in a folder named `lsp` are opened in `cromo lsp`, and checked with `lsp-hover`, `lsp-definition`, `lsp-error`, `lsp-symbols` and `lsp-completion` (positions as line:column); no error is allowed besides the expected ones. The files in a folder named `formatting` are cases of `cromo fmt`: `x.cro` must become `x.formatted.cro`, which must not change when formatted again; without `x.formatted.cro`, `cromo fmt` must refuse `x.cro`. `tests/run-tests.ps1` compiles each file with `cromo build`, runs it with the arguments of `program-arg` and compares the result with the expected one; with `cromo-command: run` it uses `cromo run` instead (D156); `-Flags -O0` passes extra options to every compilation. With `-Flags --release` the three tests that check the panics of debug mode fail, as expected. The files in a folder named `modules` are imported by the tests, and are not tests themselves. Before the tests the runner builds the C libraries of `tests/m8/native` with the clang of LLVM, next to the modules of `tests/m8`: a DLL, a static library and a DLL for 32 bits. It also sets the error mode of Windows, so that a program that cannot find a DLL ends with an error code instead of opening a window.
- **Linux:** `-Flags '--target linux'` compiles the tests for Linux and runs them in WSL, in the default distribution or in the one given with `-Distro`; the expected exit code keeps only its lowest byte (D145). `// target: windows` marks the tests that use the DLLs of Windows; the runner also builds `libscale.a` for the static library of `tests/m8`, for the architecture of the tests.
- **Linux ARM64:** `-Flags '--target linux-arm64'` does the same, and runs the programs in WSL with `qemu-aarch64-static`, of the package `qemu-user-static`, called directly: the registration of qemu in the kernel (binfmt_misc), which Docker Desktop also makes, does not last after WSL starts again. `// target: linux` holds for both architectures, `// target: linux-arm64` only for ARM64 (D406). A program that ends with SIGSEGV under qemu makes WSL write a crash dump of qemu in `%TEMP%\wsl-crashes`, which can be tens of GB.
- **IR tests:** a few checks that verify the attributes that matter for the optimizer, for example that a `ref` parameter is really `noalias`.
- **Benchmarks:** in `bench/`, apart from the tests. Each folder has the same program in Cromo, C++, Go and Rust, which prints a checksum of its work: integer arithmetic, recursion, floating point, allocation, strings and `format`, `map`, sorting, bitwise operators, an enum with an interface and a generic, threads. `bench/run-bench.ps1` compiles each program with one or more `cromo.exe`, on Windows with `--release --target linux`, and with g++, clang++, Go and rustc in WSL; it runs all the programs in WSL, where `bench/linux.sh` takes the times, checks that they print the same, and writes the compile and run times, the median of several runs, with the version of each compiler, in `bench/results` as Markdown and CSV. Two versions of cromo are compared by giving both to `-Cromo`.
- **Later:** differential tests with generated programs.

## 7. Development milestones

Each milestone lists the decisions still open that are needed **before** writing its code.

**Status:**
- **M0** completed on 2026-10-28: `cromo.exe` compiles `i32 main() { ret 42 }` into a 2 KB executable that imports only `ExitProcess` from `kernel32.dll`. Tests in `tests/m0/`.
- **M1** completed on 2026-10-28: integer, floating-point and `bool` types, `char` literals, arithmetic with the overflow and division checks, conversions, variables with default values and `const`, `if`/`else`, the three forms of `for` with `break`/`continue`, functions with parameters and recursion, `print`/`println`/`eprint`/`eprintln` with formatting, debug and `--release` modes, and the temporary runtime (D58). Tests in `tests/m1/`; the choices made for M1 are decisions D54–D67 in [decisions.md](decisions.md).
- **M2** completed on 2026-10-28: structs with initial values of the fields, nested structs, methods and `const` methods, one constructor, destructors, destruction at the end of the scope and on `ret`/`break`/`continue`, temporaries destroyed at the end of the statement, structs passed and returned by value or by address depending on the size. Tests in `tests/m2/`; the choices made for M2 are decisions D68–D76 in [decisions.md](decisions.md).
- **M3** completed on 2026-10-28: `ref` and `move` parameters, `move` in initializations, assignments and `ret`, the error on the use of a moved variable along every path, drop flags, moving fields and `ref` parameters with the default value left in place, `copy()` written by hand or generated, exclusivity with the copy of simple read arguments. Tests in `tests/m3/`; the choices made for M3 are decisions D77–D83 in [decisions.md](decisions.md).
- **M4** completed on 2026-10-28: generic functions and structs with constraints, deduction of the type arguments, interfaces with `Self`, structs that implement them, static calls through generics and dynamic calls through parameters of interface type. Tests in `tests/m4/`; the choices made for M4 are decisions D84–D92 in [decisions.md](decisions.md).
- **M5** completed on 2026-10-28: the standard library `library/core.cro` with `list<T>` and `string` written in the `unsafe` dialect, indexing with bounds checking, `for` over the elements, the operators of `string`, `panic`, and the runtime checks of exclusivity between elements. The runtime in C stays (D93). Tests in `tests/m5/`, among them the program of `examples/04_lists.cro`; the choices made for M5 are decisions D93–D103 in [decisions.md](decisions.md).
- **M6** completed on 2026-10-28: enums with data, methods and type parameters, `switch` on enums, integers and strings, and exceptions with `throw`, `try`, `catch` and `finally`, the interface `Exception` and the struct `Error` of the library. Tests in `tests/m6/`; the choices made for M6 are decisions D104–D125 in [decisions.md](decisions.md).
- **0.27.1** on 2026-10-30: performance, measured with `bench/` and its new benchmarks `tasks` and `http`, against Go. The shortest form of an `f64` with Ryū, 4.5 times faster; the workers that spin before they sleep and the channels without a buffer that give a value to a receiver already waiting, so that a ping-pong between two tasks on Linux went from 11.3 s to 0.35 s; the stack of Linux walked with the unwind tables instead of the frame pointers, which cost about 20% on `fib` (D375); `cromo` for Linux from 180 MB to 54 MB, without libxml2, ICU and the other targets of LLVM. The geometric mean of the benchmarks went from 0.96 to 0.90 of g++; `http` takes 1.5 s against 2.2 s of Go.
- **M28** in progress since 2026-10-31: `std.gzip` in Cromo, with the same bytes as the C and as fast in `--release` (D392); `[[export("name")]]`, `wrapAdd`, `wrapSub`, `wrapMul`, `x.bits()` and `f64.fromBits(b)`; the hashes of the keys and the reading of the decimal numbers moved to `std.runtime`, which the build of cromo compiles once into the archive of the runtime; the module variables and `variableAddress`; `addr(n)`, `i64(p)` and `u64(p)`; `[[cold]]`; the heap moved to `std.runtime`, and `systemCall` written as the instruction `syscall`; `std.jwt` (D399–D402), with SHA-384 in `std.crypto` and `time.unix()`; `cromo update`, written in Cromo (D403), with `std.zip` and `std.tar` (D404). Next: the output and the formatting of the numbers. Tests in `tests/m28/`.
- **M29** completed on 2026-11-02: Linux ARM64, as a target of `cromo` on Windows and on Linux x64 (D405–D408): `--target linux-arm64`, the runtime with `mem_arm64.S` and the atomics chosen at the start, the system calls of the library in files for each architecture, and the tests in WSL with qemu. Version 0.29.0.
- **M27** completed on 2026-10-30: JSON of the enums with data, generated in Cromo as for the structs, of the fixed arrays and of the `u64` above the `i64`; `http.Client`, which keeps the connections in a `thread.Mutex`; gzip in `std.gzip`, first in C in the runtime and then in Cromo (D392), and the client that asks for it; WebSocket in `std.websocket`, which the server gives with `req.webSocket(work)` on the connection lent to the request while the handler runs. Version 0.27.0. Tests in `tests/m27/`; the choices made for M27 are decisions D365–D374 in [decisions.md](decisions.md).
- **M26** completed on 2026-10-30: the values of an interface everywhere, a pair of the object on the heap and its method table, which ends with the release of the object; the initial values of the fields that are not literals, which sema moves into a default function of the struct that codegen calls after the constant of the default value. Version 0.26.0. Tests in `tests/m26/`; the choices made for M26 are decisions D357–D364 in [decisions.md](decisions.md).
- **M25** completed on 2026-10-30: `cromo test`. The parser keeps the attributes before a function and the bytes of the operator of a comparison; sema accepts `[[test]] void name()` and checks `assert`, whose condition it reads from the source; the loader adds `name_test.cro` to the module of `name.cro`, only for the tests; codegen writes an entry point that runs the test given by its number, and `assert` as a comparison of the two sides computed once, with a format that becomes the message of the panic; `cromo test` runs the executable once for each test, in parallel, with its output in a file. Version 0.25.0. Tests in `tests/m25/`, with the directives `cromo-command: test` and `expect-output-contains` of the runner; the choices made for M25 are decisions D349–D356 in [decisions.md](decisions.md).
- **M24** completed on 2026-10-30: the debug information and the stack traces. Codegen gives every instruction its line and every function its subprogram, and without `--release` the variables, the blocks and the types; the backend reads the DWARF of the object, which LLVM writes on both systems, also next to the CodeView of Windows, and makes from it the table of the stack traces, linked as an object of its own; the runtime walks the stack with the unwind tables (with the frame pointers on Linux until D375), and prints the frames of a panic, of the `throw` of an uncaught exception and of a stack overflow. Version 0.24.0. Tests in `tests/m24/`; the choices made for M24 are decisions D338–D348 in [decisions.md](decisions.md).
- **M23** completed on 2026-10-30: `thread.Channel<T>` with a fixed capacity, a ring of values in the runtime behind a lock, whose waits park a task as those of the mutex, and `for (T v in channel)`, which sema turns into an endless `for` with `T v` and `if (!channel.next(ref v)) { break }`; `thread.Atomic<T>` for integers and bools, with the built-in functions `atomicLoad`, `atomicStore`, `atomicAdd`, `atomicExchange` and `atomicCompareExchange`, which become the atomic instructions of LLVM, all `seq_cst`. Version 0.23.0. Tests in `tests/m23/`; the choices made for M23 are decisions D326–D337 in [decisions.md](decisions.md).
- **M22** completed on 2026-10-30: the types printable with `text()`, which sema calls for a struct printed by `{}`; the form of debug `{:?}`, which becomes `debugText<T>(x)` of the core, whose built-in function `debugWrite<T>` sema turns into `debugString`, `debugList`, `debugMap` or a writer that it generates in Cromo, as for JSON; the methods with type parameters of their own, checked once with their parameters as types and instantiated with the bindings of their struct and their own; `map` and `filter` of a list. Version 0.22.0. Tests in `tests/m22/`; the choices made for M22 are decisions D318–D325 in [decisions.md](decisions.md).
- **M21** completed on 2026-10-30: the literals of a list, `[a, b, c]`, which the parser tells from a lambda by the `(` after its `]`, and sema checks as calls of `add()`; the functions of text of `string` and `join`; `std.fs` with sizes, dates, links, copying, renaming, the folders above and the parts of a path; `std.process`, with `fork` and `execve` on Linux and `CreateProcessW` with a list of the handles to inherit on Windows. Version 0.21.0. Tests in `tests/m21/`; the choices made for M21 are decisions D304–D317 in [decisions.md](decisions.md).
- **M20** completed on 2026-10-30: the archives of a version made from the repository. `cromo` reads the library and the runtime from `lib/` next to its folder, no more from copies inside it; `cromo` for Linux, built in WSL with the official LLVM for Linux, depends only on glibc and compiles for both systems; `tools/package.ps1` makes the `.zip` for Windows, the `.tar.gz` for Linux and the manifest, which `tools/sign.cro` signs with Ed25519. Version 0.20.0. The choices made for M20 are decisions D285–D292 in [decisions.md](decisions.md).
- **M19** completed on 2026-10-30: lightweight tasks with `thread.Task`, which has the interface of `thread.Thread`, and `thread.Group` for tasks started in a loop; a runtime with one worker for each processor, stacks of 1 MB taken page by page, a poller with epoll on Linux and AFD on Windows for the network and `time.sleep`, and a monitor for the calls that block; a mutex that parks a task; sockets that do not block; the server of HTTP with a task for each connection. Version 0.19.0. Tests in `tests/m19/`, and the example `examples/14_tasks.cro`; the choices made for M19 are decisions D269–D284 in [decisions.md](decisions.md).
- **M18** completed on 2026-10-30: the module `std.json` with `json.Value` and the text of RFC 8259, the conversion of the structs to JSON and back generated by the compiler, with the attributes `[[json(...)]]` of the fields; `const func` for the functions that several threads call at once; the module `std.http` with a server that gives every request to one handler on a pool of threads, keep-alive, chunked bodies and patterns for the routes, and a client with redirects, over TCP or TLS. Version 0.18.0. Tests in `tests/m18/`, and the examples `examples/12_json.cro` and `examples/13_web_server.cro`; the choices made for M18 are decisions D251–D268 in [decisions.md](decisions.md).
- **M17** completed on 2026-10-29: TLS 1.2 and 1.3 for clients and servers with mbedTLS, the root certificates of the system, and `std.crypto` with hashes, HMAC, random bytes, hex and base64, AES-GCM, ChaCha20-Poly1305, Ed25519 and X25519. Version 0.17.0. Tests in `tests/m17/`, and the example `examples/11_https.cro`; the choices made for M17 are decisions D233–D236 and D240–D250 in [decisions.md](decisions.md).
- **0.16.1** on 2026-10-29: performance, measured with `bench/`. A `map` keeps hash, key and value of a slot together and compares the keys in place: the benchmark `wordcount` takes a third of the time. The locks of the runtime are skipped until the first thread starts; `memcmp` and the hash of the strings work eight bytes at a time.
- **M16** completed on 2026-10-29: TCP listeners and connections, UDP sockets, timeouts, the names of the hosts with a resolver written in Cromo on Linux and the one of the system on Windows, IPv4 and IPv6. Version 0.16.0. Tests in `tests/m16/`, and the example `examples/10_network.cro`; the choices made for M16 are decisions D223–D232 in [decisions.md](decisions.md).
- **M15** completed on 2026-10-29: threads with `start()` and `join()`, threads of a block that use its variables with the checks of the compiler, free threads, `Mutex`, the exceptions of a thread thrown again by `join()`, and a runtime safe with threads on Windows and Linux. Version 0.15.0. Tests in `tests/m15/`, and the example `examples/09_threads.cro`; the choices made for M15 are decisions D214–D222 in [decisions.md](decisions.md).
- **M14** completed on 2026-10-29: bitwise operators, fixed-size arrays, `map` with the interface `Key`, the bytes of a string, `slice`, `format`, numbers from text, the modules `std.fs`, `std.time` and `std.os` with their files for Windows and Linux, the files of a system in the programs, and `s.cstring()` for C. Version 0.14.0. Tests in `tests/m14/`, and the examples `examples/07_collections.cro` and `examples/08_system.cro`; the choices made for M14 are decisions D193–D212 in [decisions.md](decisions.md).
- **M13** completed on 2026-10-29: anonymous functions with their captures, function types, function values kept in variables, fields and lists, calls through them, lambdas with generics and exceptions; `cromo fmt`, the language server and the grammar of VS Code know them. Version 0.13.0. Tests in `tests/m13/`, and the example `examples/06_lambdas.cro`; the choices made for M13 are decisions D183–D192 in [decisions.md](decisions.md).
- **M12** completed on 2026-10-29: `cromo lsp`, the language server, with errors while writing, hover, definition, the symbols of a file, the members after a dot (D179) and formatting, for the program that a file belongs to; the extension for VS Code is its client. Version 0.12.0. Tests in `tests/m12/lsp/`; the choices made for M12 are decisions D170–D178, and D179–D182 for the completion after a dot, in [decisions.md](decisions.md).
- **M11** completed on 2026-10-29: `cromo fmt` with a single style: spaces, indentation, braces on the same line, comments in a column, checked against the tokens and the syntax of the result; `--check` and the standard input for editors; the extension for VS Code formats the files, also when they are saved. The library and the examples are formatted. Version 0.11.0. Tests in `tests/m11/formatting/`; the choices made for M11 are decisions D161–D169 in [decisions.md](decisions.md).
- **M10** completed on 2026-10-29: the commands `cromo build`, `run`, `version` and `help`; `build` and `run` without a file use `main.cro` of the current folder; `main(list<string> args)` with the arguments of the program, on Windows and on Linux. Version 0.10.0. Tests in `tests/m10/`; the choices made for M10 are decisions D154–D160 in [decisions.md](decisions.md).
- **M9** completed on 2026-10-29: executables for Linux x64 by cross-compilation, with `--target linux`. The runtime in C has a version for Linux, with system calls and an allocator on top of `mmap`, and LLD writes static ELF executables. In `extern` blocks, the static libraries `libname.a`. All the tests that do not use the DLLs of Windows also pass in WSL, at every optimization level. Tests in `tests/m9/`; the choices made for M9 are decisions D142–D146 in [decisions.md](decisions.md).
- **M8** completed on 2026-10-29: linking without the Windows SDK. `cromo` writes the import libraries of the DLLs, checks the C functions in their export tables, uses a `.lib` next to the module when there is one, and copies the DLLs of the modules next to the executable. Tests in `tests/m8/`; the choices made for M8 are decisions D136–D141 in [decisions.md](decisions.md).
- **M7** completed on 2026-10-28: modules with `import` of files and folders, `public`, names written with their module when they clash, symbols with the module in front, `import unsafe`, C functions declared in `extern` blocks with their library, `ref` as a C pointer, and the values of the cases of enums. Tests in `tests/m7/`, and the example `examples/05_c_interop.cro`; the choices made for M7 are decisions D126–D135 in [decisions.md](decisions.md).

| Milestone | Content | Open decisions |
|---|---|---|
| **M0** | `i32 main() { ret 42 }` becomes an executable that returns 42 as its exit code. Lexer, parser, IR emission, `cromo.exe` in C++ with LLVM and LLD inside, test runner, entry point without the C runtime | none |
| **M1** | Integers, floating-point numbers and booleans, `char` and character literals, arithmetic, variables with default values, `if`/`else`, `for` in the forms without lists, functions and recursion with the `ret` check, `print`/`println` with formatting | none |
| **M2** | Structs with default values, `const` methods, constructors, destructors and cleanup on all exits from the scope (RAII) | none |
| **M3** | Ownership: `move`, `copy()`, `ref`/`move` parameters, exclusivity, analysis of moves on the AST (D77) | none |
| **M4** | Generics and interfaces | none |
| **M5** | Containers and minimal library written in `unsafe` Cromo: `list<T>`, `string`, `for` over the elements, `panic`, bounds checking. The runtime in C stays (D93) | none |
| **M6** | Exceptions (`try`/`catch`/`finally`) and enums | none: the syntax of enums is D104–D107 |
| **M7** | Modules and interoperability with C | none: D126–D129 |
| **M8** | No Windows SDK: import libraries generated by `cromo` | none: D136–D138 |
| **M9** | Linux | none: D142–D143 |
| **M10** | The commands `cromo build`, `run` and `version` (D151) | none: D154–D157 |
| **M11** | `cromo fmt`: the formatter, also for VS Code; the lexer keeps the comments | none: D161–D164 |
| **M12** | Language server for VS Code: errors while writing, types, definitions, then completion | none: D170–D173 |
| **M13** | Anonymous functions and the type of a function (D148) | none: D183–D186 |
| **M14** | Basics of the library: bitwise operators, fixed-size arrays, `map`, bytes, files, time, code for each system, strings to C | none: D193–D204 |
| **M15** | Threads and synchronization, with a runtime safe with threads (D149) | none: D214–D217 |
| **M16** | Networking: TCP and UDP sockets, DNS without libc (D150) | none: D223–D226 |
| **M17** | TLS, cryptography and the HTTPS client (D150) | none: D233–D236 |
| **M18** | HTTP server and client, JSON (D150) | none: D251–D268 |
| **M19** | Lightweight tasks under the same API as threads (D223) | none: D269–D284 |
| **M20** | The archives for each system made from the repository: `cromo` reads `lib/` from its folder, `cromo` for Linux, the manifest and its signature (D237, D285–D287, D290, D292) | none: D285–D303 |
| **M21** | Lists, text, files and programs: literals of a list, the functions of text, `std.fs`, `std.process` | none: D304–D317 |
| **M22** | Printable types (D38) and generic methods | none: D318–D325 |
| **M23** | Channels and atomic values for the tasks | none: D326–D337 |
| **M24** | Debug information and stack traces | none: D338–D348 |
| **M25** | `cromo test` | none: D349–D356 |
| **M26** | Owned values of an interface, initial values of the fields that are not literals | none: D357–D364 |
| **M27** | The rest of JSON and HTTP: arrays and enums with data, `u64`, `http.Client`, gzip, WebSocket | none: D365–D374 |
| **M28** | The runtime in Cromo, where it can be: gzip, `[[export]]`, `wrapMul` and the others, the bits of a float, then the module variables, the heap, `std.jwt`, `cromo update` with `std.zip` and `std.tar`, the output and the formatting | none: D392–D404 |
| **M29** | Linux ARM64: the target, the runtime in C and in assembly, the system calls of the library for each architecture | none: D405–D408 |
| **Later** | Packages: `cromo.mod`, `cromo.sum`, `cromo get`, the archives of the tags over HTTPS, a reader of tar (D376–D391) | none: D376–D391 |
| **Last** | Compiler written in Cromo that compiles itself and makes the same archives, when the language is mature (§2, D147, D290, D291) | |

Generics come before containers because `list<T>` is written in Cromo as a generic struct (D37, D39). After M9 the order is D152: the tools come first, because they help to write the library. `cromo update` (D153) downloads the versions from the manifests and the archives of cromolang.org (D285–D289): it came in M28, once the archives were published and the library could download and open them (D403).

## 8. Repository structure (proposal)

```
cromo/
  docs/                 project documents
  src/                  bootstrap compiler in C++ (lexer, parser, semantics, IR emission, driver)
  compiler/             compiler written in Cromo (the last milestone, D147)
  runtime/              entry point and low-level primitives (LLVM IR, assembly)
  library/              standard library in Cromo: core.cro and the modules of std/ (D201)
  tests/                behavior and IR tests
  bench/                benchmarks in Cromo, C++, Go and Rust, and their runner
  examples/             example programs
  tools/
    vscode-cromo/       VS Code extension for syntax highlighting
  site/                 the site cromolang.org and the logo (D213)
```
