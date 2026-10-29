# Compiler architecture

> **Status: proposal.** The bootstrap compiler is written in C++ (D44); its details are in D53 of [decisions.md](decisions.md).

## 1. Pipeline

The final product is a single executable, `cromo`, which does everything (D28):

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
- **The library** is `library/core.cro`, embedded in `cromo.exe` like the runtime (`cmake/embed.cmake`) and compiled before every program as a second source file: its locations have `SourceLocation::file == kLibraryFile`, and its messages name the file `cromo:core.cro`. It is the module `std`, which writes `import unsafe` (D127) to use the type `addr` and the built-in functions of D98; its fields are private, as those of any module (D50).
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
- `run [options] [file.cro] [-- arguments]` compiles into `cromo-run-<pid>` in the temporary folder, starts the program with `CreateProcessW`, quoting every argument with the rules of the C runtime of Microsoft, waits for it, deletes the folder and returns its exit code (D156, D159).
- `version` prints `cromo <version> (LLVM <version>)`: the version comes from `project(... VERSION ...)` in `CMakeLists.txt` (D157).
- `fmt [--check] [files and folders | -]` formats the code (D161–D164): see below.
- `lsp` is the language server: see below.
- `help` prints the commands. `update` answers that it comes later (D158).

### Anonymous functions (D183–D186, M13)
- **Syntax:** the parser reads a lambda when an expression starts with `[`, and gives it a `FunctionDecl` with `isLambda` for its body. A function type is a `TypeName` with `isFunction`: after its `)`, a type is the one returned unless a name follows it, as in `func(i32) f`. Inside a brace the lexer forgets the parentheses open outside, so the body of a lambda passed as an argument ends its statements at the newlines.
- **Types:** `TypeKind::Function` points to a `FunctionSig`, one for each signature, so that two types are equal when their pointers are. A function value is `{ ptr, ptr }`: the code and the data.
- **Sema:** the body is checked as a function of its own, with the variables of outside hidden: its parameters first, then the captures, `VariableKind::Captured`. A lambda that captures by address is accepted only as the argument of a read parameter (D184); its captures by address enter the checks of exclusivity of the call. The call of a function value becomes a `CallValueExpr`, checked against a `FunctionDecl` made for the signature, and noted as a call that can throw.
- **Code:** the body is a function whose data arrive as `%env`, where a method has its object; a capture is a field of the data, its value or the address of the variable. The data start with the function that destroys them: `.drop` also frees them from the heap, `.release` leaves them in the frame. As an argument the data are in the frame and registered as a temporary; otherwise they come from `cromo_alloc` and the value owns them. Calling an empty value is a panic.
- **Moves:** a capture with `move` is a move of the variable where the lambda is made; the body is checked as a function of its own.

### The basics of the library (D193–D204, M14)
- **Bitwise operators:** the lexer has `&`, `|`, `^`, `<<` and their assignments; `>>` is two `>` next to each other, so that `list<list<i32>>` still closes its type arguments. The parser has a level for each of `|`, `^`, `&` and the shifts, between the comparisons and `+` (D193). A shift checks its count in debug and masks it in release (D195).
- **Arrays:** `TypeKind::Array` points to an `ArrayInfo`, one for each element type and count, as the signatures of the functions. An array is in memory as a struct: `[4 x i32]`, copied with `memcpy`, passed by address when larger than 16 bytes, destroyed with a loop over its elements, the last first. A large default value is written with `memset`, or with a loop, never as one constant that LLVM would split into a store for each element. `a.len()` becomes a constant.
- **Strings:** `s[i]` reads a byte from the fields `data` and `length`. `format` is a `PrintExpr` on the stream 3 of the runtime, which writes into memory; at the end the string takes that memory (`cromo_format_end`).
- **Numbers from text:** `i32(s)` becomes a call of `parseSigned` of the library, with the largest value of the type and its name for the messages. The library checks the text; for the floats `cromo_parse_float` of the runtime divides the digits as big integers, so the rounding is always exact.
- **map:** written in Cromo in `library/core.cro`, with open addressing. `m[k]` calls `valueOf`, or `place` when it is the target of an assignment, `+=` or `++`, which gives the address of the value. `for (K k, V v in m)` goes over the slots in the code generated. `x.hash()` of a number is computed by the compiler with `cromo_hash_bits`.
- **Modules of the library and files of a system:** the files `library/std/*.cro` are embedded in `cromo.exe` (`cmake/embed_modules.cmake`). The loader adds to a module the file with the suffix of the target, `_windows` or `_linux`, which shares the scope of the module (`ModuleFile::part`); the files of the other system are left out (D202). The Windows files call kernel32 through `extern`; the Linux files use the intrinsic `systemCall` and `environment`. `s.cstring()` becomes the field `data` of a temporary `CString` of the library, destroyed at the end of the statement (D204).

### Threads (D214–D217, M15)
- **The runtime is safe with threads:** a lock is an SRWLOCK on Windows and a futex on Linux, free at zero. One lock lets a single `print` write at a time, taken by `cromo_print_begin` after the arguments are computed; the allocator for Linux has its own lock, the heap of Windows already has one. A `format` writes directly into its string, whose address the compiler passes in place of the number of the stream.
- **Threads of the system:** `CreateThread` on Windows; on Linux `clone` with a stack from `mmap` and a guard page, in a few lines of assembly, and `join` waits on the futex that the kernel clears when the thread ends. The runtime calls the code of the lambda with its data and a slot for its exception, as any call that can throw (D36).
- **`std.thread`** is written in Cromo (`library/std/thread.cro`) with the built-in functions `threadStart`, `threadWait`, `threadRethrow`, `threadUncaught`, `threadFree`, `mutexLock`, `mutexUnlock` and `selfAddress`. `threadRethrow` moves the exception of the thread into the slot of the caller of `join()`, which can therefore throw anything.
- **Threads of a block:** sema lets the lambda written in `thread.Thread(...)` capture by address when the construction is the value of a declaration; the data of the lambda stay in the frame and the `Thread` owns them, destroying them with `.release`. From `start()` to `join()`, or to the end of the block, the captures by address are lent as the elements of a `for` (D101): `loopBorrows_` refuses the modifications, and `threadBorrows_` also the reads of what the thread has with `ref`.
- **Mutex:** `lock` is a `const` method that writes the mutex through `selfAddress()`. The struct is marked `addressed`, and so is every struct that holds one: it is passed by address and without `noalias readonly`, so that LLVM does not assume that a call leaves it unchanged.
- **LLVM without the C library:** the analysis of the library functions allows only `memcpy`, `memmove`, `memset`, `memcmp` and, on Linux, `bcmp`, which `runtime/mem_x64.S` defines; otherwise LLVM turned a loop of `std.fs` into a call to `strlen`.

### Network (D223–D226, M16)
- **`std.net`** is written in Cromo (`library/std/net.cro`): `Listener`, `Connection`, `Udp`, the timeouts with `poll`, and the addresses of IPv4 and IPv6 with their text. The calls of the system are in `net_windows.cro`, with ws2_32, and `net_linux.cro`, with the system calls; `connect` and `listen` of ws2_32 are declared in `std.winsock`, a module that only the library can import, because `std.net` has functions with those names.
- **The resolver for Linux** reads `/etc/hosts` and `/etc/resolv.conf`, builds the questions of DNS, sends them with UDP and reads the answers, following the pointers of the names and the aliases. On Windows `GetAddrInfoW` does it.
- **Threads:** the lambda given to `thread.Thread(...)` can modify what it captures with `move`, since it runs once.

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
- **The extension for VS Code** (`tools/vscode-cromo/extension.js`) is a client of the protocol written without packages from npm: it starts `cromo lsp`, sends the open documents with their whole text at every change, shows the diagnostics, and asks for hover, definition, symbols, completion and formatting. On Windows it runs `cromo lsp` from a copy of `cromo.exe` in the temporary folder, so that `build.ps1` can write `cromo.exe` again while VS Code is open; a new `cromo.exe` restarts the server.

## 4. Toolchain (D28)

- **LLVM and LLD inside `cromo`:** a stable version of LLVM is pinned, because the textual IR format and the API can change between major versions. The tests always run with that version.
- **Target:** `--target windows`, the default, or `--target linux` (D142, D144). The compiler writes the `target triple`, `x86_64-pc-windows-msvc` or `x86_64-unknown-linux-gnu`, and `src/target.h` keeps the choice.
- **Windows (D27):** Microsoft x64 ABI. Linking does not require the Windows SDK: `cromo` writes the import libraries by itself from the list of the functions used, as Zig does, with the code that LLVM already contains (D136).
- **Without libc (D19):** linking uses a custom entry point and no default libraries. On Windows with `/ENTRY` and `/NODEFAULTLIB`, on Linux with `-nostdlib` and `-static`.
- **Linux (D27, D142):** System V ABI, by cross-compilation from Windows. The ELF driver of LLD, which is in `cromo` with the COFF one, writes a static executable at a fixed address (`-static`, entry point `_start`), without the dynamic loader: it runs on any Linux x64, with or without glibc (D145). The static libraries of the `extern` blocks, `libname.a`, come before the runtime on the command line of the linker, because their C code can call `memcpy` (D143).
- **Build of the C++ compiler (D53):** C++20 with MSVC, CMake and Ninja, through `build.ps1`. LLVM and LLD come from the official 23.1.4 archive, installed in `C:\software\llvm-23.1.4`.
  - **Static runtime:** the LLVM libraries are Release, with a static C runtime (`/MT`) and without RTTI, so `cromo` uses the same options. This way `cromo.exe` does not require the Visual C++ Redistributable.
  - **zlib, zstd and libxml2:** LLVM was built with these libraries, but the archive does not include them. vcpkg provides them as static libraries (`vcpkg.json`, triplet `x64-windows-static`).
  - **Size:** `cromo.exe` is about 100 MB, because it contains LLVM and LLD.
- **Symbol names:** D134. The entry point is called `cromo_start`, and the C names that start with `cromo_` are reserved for the runtime.

## 5. Runtime (D19)

Neither the C library nor the C++ runtime is used. The runtime and the standard library are written in Cromo, in the `unsafe` dialect (D37), and where needed in assembly or in hand-written LLVM IR. The program talks directly to the operating system:
- **Windows:** it has no stable system calls, so the functions of `kernel32.dll` are used (`ExitProcess`, `GetStdHandle`, `WriteFile`, `HeapAlloc`/`VirtualAlloc`, `CreateFileW`, …). They are the system API, not libc.
- **Linux:** direct system calls (`write`, `mmap`, `mremap`, `munmap`, `exit_group`), with the `syscall` instruction.

What the runtime must provide:
- its own **entry point**, which replaces the C runtime startup: it calls `main` and terminates the process with its return value;
- `cromo_panic(message, where)`: prints the message and terminates the program;
- **allocator** on top of the system primitives: the Windows heap (`HeapAlloc`), and on Linux a simple allocator of its own on top of `mmap`;
- **minimal I/O:** writing to stdout and stderr;
- **number formatting** without libc (D38):
  - integers, in base 10, 16, 2 and 8, are simple;
  - floating-point numbers (`f32`, `f64`) are the most delicate part. They need a dedicated algorithm (for example Ryū) to print the shortest number that reads back identical, and correct rounding when a precision is requested;
- **symbols that LLVM expects anyway:** `memcpy`, `memmove`, `memset` and `memcmp`, because the optimizer can generate calls to these functions even if the code does not use them. On Windows `__chkstk` is also needed, the stack check for frames larger than 4 KB, and `_fltused`, required when floating-point numbers are used. On Linux LLVM also calls `bcmp`, which is `memcmp`.

Exceptions require nothing from the runtime (D36).

### The runtime in C (D58, D93)
Until the bitwise operators and fixed-size arrays exist, the runtime is the freestanding C code in `runtime/`; `extern` (M7) is already there:
- `cromo_rt.c`: buffered output on stdout and stderr (UTF-8, converted to UTF-16 when writing to a Windows console), the formatting functions called by `print`/`println`, the heap for `list` and `string`, `cromo_exit` and the panics;
- `mem_x64.S`: `memcpy`, `memset`, `memmove` and `memcmp`; on Windows `__chkstk`, on Linux `bcmp` and the entry point `_start`, which keeps the address of the stack in `cromo_initial_stack` (the arguments of the program are there), aligns the stack to 16 bytes and calls `cromo_start`.

The two files have a part for each system, chosen with `#ifdef _WIN32` (D142). CMake compiles them twice with the clang of the LLVM archive (`-ffreestanding -fno-builtin -nostdlib`): for Windows it archives them with `llvm-lib` into `cromo_rt.lib`, for Linux with `llvm-ar` into `libcromo_rt.a`. Both libraries are embedded in `cromo.exe` as byte arrays (`cmake/embed.cmake`). At link time `cromo` writes the one of the target next to the object file and passes it to LLD.

The heap on Linux is an allocator without threads (D43): it will change with M15 (D149). Every block starts with 16 bytes that hold its capacity. The small blocks, up to 4 KB with those 16 bytes, have a size that is a power of two from 32 bytes: they are cut from areas of 1 MB, and the freed ones go to a list for each size, to be used again. The larger blocks have a mapping of their own, which grows with `mremap`.

Floating-point numbers are formatted with big integers, so the digits are always exact: the shortest form uses the algorithm of Steele & White and Burger & Dybvig, and the forms with a precision round the exact value half to even. It was checked against Python on 500 values (random bit patterns, subnormals, extremes, exact ties) in five formats, with no differences.

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
- **Linux:** `-Flags '--target linux'` compiles the tests for Linux and runs them in WSL, in the default distribution or in the one given with `-Distro`; the expected exit code keeps only its lowest byte (D145). `// target: windows` marks the tests that use the DLLs of Windows; the runner also builds `libscale.a` for the static library of `tests/m8`.
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
- **M5** completed on 2026-10-28: the standard library `library/core.cro` with `list<T>` and `string` written in the `unsafe` dialect, indexing with bounds checking, `for` over the elements, the operators of `string`, `panic`, and the runtime checks of exclusivity between elements. The runtime in C stays (D93). Tests in `tests/m5/`, among them the program of `examples/04_lists.cro`; the choices made for M5 are decisions D93–D103 in [decisions.md](decisions.md).
- **M6** completed on 2026-10-28: enums with data, methods and type parameters, `switch` on enums, integers and strings, and exceptions with `throw`, `try`, `catch` and `finally`, the interface `Exception` and the struct `Error` of the library. Tests in `tests/m6/`; the choices made for M6 are decisions D104–D125 in [decisions.md](decisions.md).
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
| **M17** | TLS, cryptography and the HTTPS client (D150) | the TLS library |
| **M18** | HTTP server and client, JSON (D150) | |
| **Later** | Lightweight tasks under the same API as threads | |
| **Last** | Compiler written in Cromo that compiles itself (§2, D147) | |

Generics come before containers because `list<T>` is written in Cromo as a generic struct (D37, D39). After M9 the order is D152: the tools come first, because they help to write the library. `cromo update` (D153) comes when the resource on the network from which it downloads the new versions exists.

For JSON, networking and the utilities of the library (M14–M18) the reference is fcl, the C++ library of the author, in WSL at `/home/andrea/source/private/fcl`:
- its HTTP server makes blocking calls on a pool of threads and keeps the idle connections in epoll, and handles about 330k requests per second on loopback: the model recommended for Cromo works (D150);
- the parsers of JSON, HTTP/1.1, URI and WebSocket, the HTTP client, and the utilities (scheduler with cron, rate limiter, processes, atomic writes, file watcher, logs with rotation, command line, dates, UUID) can be translated;
- TLS, certificates, keys, AES-GCM, ChaCha20-Poly1305, HMAC, HKDF and the random numbers come from OpenSSL, which Cromo replaces (D150). SHA-1/256/512, MD5, Argon2id, Base64 and Ed25519/X25519 are written by hand and can be translated; its AES is not constant-time and is not translated;
- its interfaces return references (`json&`, iterators) and store callbacks with `std::function` and `shared_ptr`: in Cromo the references become methods that take an anonymous function, or copies (D22), and the callbacks anonymous functions that capture with `move` (D148).

## 8. Repository structure (proposal)

```
cromo/
  docs/                 project documents
  src/                  bootstrap compiler in C++ (lexer, parser, semantics, IR emission, driver)
  compiler/             compiler written in Cromo (the last milestone, D147)
  runtime/              entry point and low-level primitives (LLVM IR, assembly)
  library/              standard library in Cromo: core.cro and the modules of std/ (D201)
  tests/                behavior and IR tests
  examples/             example programs
  tools/
    vscode-cromo/       VS Code extension for syntax highlighting
  site/                 the site cromolang.org and the logo (D213)
```
