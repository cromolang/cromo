# Decision log

This file is the **source of truth** for Cromo's design choices. Each entry has a status:

- **Decided**: agreed with the author. It is not reopened without a new reason.
- **Proposed**: suggested and justified, awaiting confirmation.
- **Open**: to be discussed. The options are listed and, where there is one, the recommended one is indicated.

Open entries marked **[M0]** are already needed for the first milestone of the compiler (see [compiler.md](compiler.md)).

---

## Decided

### D1. Name: Cromo
*2026-10-28.* Chromium (*cromo* in Italian) is the element that makes steel stainless: a language "that does not rust", in dialogue with Rust (whose name literally means rust). It starts with C, like the C/C++/C# family. We checked that no known language has this name.

### D2. General-purpose language
*2026-10-28.* It is not designed for a specific domain.

### D3. Syntax close to C/C++
*2026-10-28.* Braces, parentheses around conditions, constructors and destructors recognizable to anyone coming from C/C++. However, the syntax does not inherit the ambiguous parts of C (see D20).

### D4. No pointers
*2026-10-28.* The language exposes neither pointers nor pointer arithmetic. Passing without copying and recursive data structures are achieved in other ways (see D11, D22).

### D5. No garbage collector
*2026-10-28.* Memory is freed deterministically, without a collection runtime.

### D6. RAII resource management, as in C++
*2026-10-28.* Every resource is owned by an object. The constructor acquires it and the destructor releases it when the object goes out of scope. Destruction happens in reverse order of construction.

### D7. Backend: LLVM IR
*2026-10-28.* The compiler emits LLVM IR. Optimization and machine code generation are done by LLVM, as for Rust; linking is done by LLD (D28).

### D8. Goal: simpler than Rust
*2026-10-28.* It is the guiding criterion for all other choices. The aim is memory safety **without** explicit lifetimes and without a general borrow checker.

### D9. Primitive types
*2026-10-28.* As in Rust but **without `isize` and `usize`**:
- integers `i8 i16 i32 i64` and `u8 u16 u32 u64`;
- floating-point `f32 f64`;
- `bool`.

Without "platform" types, every type has the same size everywhere. For indices and lengths see D12, for characters and 128-bit integers see D14.

### D10. UTF-8 strings
*2026-10-28.* The `string` type contains UTF-8 text and owns its own memory, which is freed when the string goes out of scope (D11).

### D11. Composite types and dynamic memory
*2026-10-28.* Composite types (structs) are made of:
- primitive values;
- other composite types;
- dynamic containers (lists/vectors) that allocate their elements on the heap.

Dynamic memory is obtained **only** through these containers and `string`. There is no garbage collector: when a variable goes out of scope, its destructor automatically also frees the memory of the containers it contains, at any depth. User-defined constructors and destructors follow D6.

Consequences:
- recursive structures go through a container (a tree node contains a list of children);
- a struct cannot directly contain itself;
- graphs are modeled with indices into a list;
- `Box` and `Shared` are not planned.

The dynamic container is `list<T>` (D13).

### D12. Indices and lengths: `i64`
*2026-10-28.* Lengths of lists and strings, and indices, are `i64`, signed. If `i` is 0, `i - 1` gives -1 instead of overflowing, and an access with a negative index is caught by bounds checking (D30). 64 bits are enough for any container. It is Go's choice.

### D13. A single dynamic container: `list<T>`
*2026-10-28.* In the first version there is a single dynamic container, `list<T>`, with a lowercase initial like `string`. It is a dynamic array with its elements contiguous on the heap, like C#'s `List<T>` or C++'s `std::vector`. There is no linked list. Maps and sets may come later in the standard library.

### D14. 8-bit `char`; no 128-bit integers for now
*2026-10-28, revised the same day by D46.*
- **`char` is a synonym for `u8`**, as in C: one byte of a UTF-8 string. Unicode code points are `u32`. Details in D46.
- **`i128`/`u128` postponed** until after the first version: 128-bit division and printing require support functions (compiler-rt) that are still to be verified on Windows.

### D15. Control flow: `if` and a single `for` loop
*2026-10-28.*
- `if`, `else` and `else if`, with the condition in parentheses as in C.
- A single loop construct, `for`, in three forms:

```cromo
for { ... }                              // infinite loop
for (active) { ... }                     // condition only, instead of while
for (i64 i = 0; i < n; i++) { ... }      // classic for
```

- There is neither `while` nor `do … while`.

It is Go's approach, with C's parentheses. The form over elements is in D25, the other details in D32.

### D16. Errors: exceptions with `try`, `catch` and `finally`
*2026-10-28.* Errors are handled with exceptions, as in C++, C# and Java: `throw` throws an exception, `try`/`catch` catches it, `finally` runs code on every exit from the block.

Consequences:
- **Constructors:** a constructor can fail by throwing an exception. The fields already constructed are destroyed, as in C++.
- **RAII:** while an exception propagates, the destructors of all live objects in the abandoned scopes are called. `finally` remains useful for cleanup that is not tied to an object.

What is thrown and how they are implemented: D36.

### D17. No semicolons: a statement ends at the newline
*2026-10-28.* As in Go and Python, `;` is not needed to end statements: every statement ends at the end of the line. The precise rules are in D32.

### D18. `ret` instead of `return`
*2026-10-28.* A function returns control, and possibly a value, with the keyword `ret`.

```cromo
i32 sum(i32 a, i32 b) {
    ret a + b
}
```

### D19. No libc
*2026-10-28.* Cromo programs use neither the C library nor the C++ runtime. The basic functions (memory, I/O, strings, …) are implemented step by step in assembly or in Cromo, and the program talks directly to the operating system.

Consequences:
- **Windows** has no stable system calls: the functions of `kernel32.dll` are used (`ExitProcess`, `WriteFile`, `HeapAlloc`, …). They are the system API, not libc. **Linux** uses direct system calls.
- **Startup:** a custom entry point is needed, instead of the C runtime startup.
- **Symbols expected by LLVM:** `memcpy`, `memmove`, `memset` and `memcmp` must be provided anyway, because the optimizer can generate calls to these functions. On Windows `__chkstk` and `_fltused` are also needed.
- **`memcpy` and `memset` are written in assembly** (2026-10-28). This also avoids a known problem: if they were written in Cromo or in LLVM IR, the optimizer could recognize the loop and turn it into a call to themselves.
- **Exceptions:** they do not rely on the C++ runtime (D36).
- **Low-level code:** it is written in the `unsafe` dialect of the standard library (D37).

### D20. Syntax confirmed by the examples
*2026-10-28.* The examples in `examples/` confirmed:
- **Declarations with the type before the name**, as in C/C++/C#: `i64 a = 0`. (`var`, which inferred the type, was removed by D54.)
  - Without pointers, C's complicated declarators do not exist.
  - The ambiguity of `<` in generics (`a < b > c`) is resolved as in C#.
  - There is no C++ *most vexing parse*: `File f("x")` is not valid, you write `File f = File("x")`.
- **Structs as in C++:** fields, constructor `Name(...)`, destructor `~Name()` and methods inside the struct.
- **Exceptions:** `throw` to throw and `catch (Type name)` to catch.
- **`break` and `continue`**, without labels.
- **`i++` and `i--`** as a standalone statement.
- **The `;` appears only in the header of the classic `for`:** `for (i64 i = 0; i < n; i++)`.
- **Indexing** with `v[i]`, and the `add` and `len` methods of `list`.
- **Explicit conversion** written as a call: `f64(n)`.
- **`void`** for functions that return nothing.

### D21. `print` and `println` with C++-style formatting
*2026-10-28.* You print with `print`, without a newline, and `println`, which ends with a newline. The first argument is a format string with the syntax of C++'s `std::format` / `{fmt}`:
- `{}` takes the next argument;
- `{0}` takes an argument by position;
- after the colon you specify width, alignment, precision and base.

```cromo
println("Hello {}, you are {} years old", name, age)
println("average: {:.2f}", average)
```

The details are in D38.

### D22. Parameter passing: read, `ref` and `move`
*2026-10-28.* A parameter is passed in one of three modes, and **the word that appears in the signature is repeated in the call**:

| Mode | In the signature | In the call | Meaning |
|---|---|---|---|
| Read (default) | `Student s` | `f(anna)` | the function only reads, the caller remains the owner |
| Modify | `ref Student s` | `f(ref anna)` | the function modifies the caller's object |
| Ownership transfer | `move string n` | `f(move name)` | the function becomes the owner, and the variable is no longer usable |

Rules:
- **`ref` exists only for parameters:** no `ref` local variables, fields or return values. This way a reference never outlives the object, and safety is achieved without a borrow checker. It is Hylo's approach, with C#'s `ref` keyword.
- **Exclusivity:** an argument passed with `ref` cannot refer to the same object as another argument of the same call. Thanks to this the compiler can declare the parameters `noalias` to LLVM.
- **Temporaries:** a temporary value passed to a `move` parameter does not require `move`, as in `students.add(Student("Marco"))`.
- **Read and the 16-byte threshold:** a read parameter up to 16 bytes is passed by copy, a larger one **always** by reference. This way the cost is predictable without having to write it. 16 bytes are two registers on x86-64, in line with rule F.16 of the C++ Core Guidelines ("two or three words").
- **`move const`:** a `move` parameter can also be `const`, with the mode first: `move const string n`. The function becomes its owner but cannot modify it; it can still move it, like any `const` variable (D26). On the other parameters `const` makes no sense: a read parameter is already immutable, and `ref` exists precisely to modify.

Elements and fields as references: D35.

### D23. Moving and copying
*2026-10-28.*
- **Simple types** (numbers, `bool`, structs made only of simple types and without their own destructor): they are always copied, implicitly.
- **Resource-owning types** (`string`, `list`, structs that contain them or that have their own destructor): no implicit copy. Every time a variable gives up ownership you write `move`, both in calls and in assignments: `string t = move s`. To copy you write `s.copy()`.
- **After a `move`**, using the variable is a compile error.
- **`ret`** of a local variable moves it without needing `move`, because it is its last use.
- **Temporaries** are moved without `move`.

How `copy()` is obtained for structs: D35.

### D24. `const` methods
*2026-10-28.* As in C++, a method can modify the object's fields, unless it is marked `const` after the parameter list.

```cromo
struct Student {
    string name
    list<f64> grades

    f64 average() const { ... }             // cannot modify name or grades
    void addGrade(f64 v) { grades.add(v) }  // can modify
}
```

Rules:
- **Independent of parameter passing:** `const` concerns the object on which the method is called, not its parameters.
- **What can be called:** on a read-only object (a parameter passed without `ref`, the element of a `for (T x in items)`, a `const` variable) only `const` methods can be called. On one's own variable or on a `ref` parameter, all methods.
- **The call does not change:** calling a method that modifies does not require `ref`, as in `students.add(x)`.
- **`const` is deep:** without pointers, everything the object contains belongs to it. A `const` method cannot modify even the strings and lists in the fields. In C++, by contrast, constness does not propagate through pointers.

### D25. `for` loop over elements
*2026-10-28.* Besides the three forms of D15, `for` can iterate over the elements of a list:

```cromo
for (Student s in students) { ... }        // s is read-only
for (ref Student s in students) { ... }    // s is modifiable: the changes remain in the list
```

- While the loop is running the list cannot change length (exclusivity, D22).
- On the read-only element only `const` methods can be called (D24).

### D26. `const` variables
*2026-10-28.* Variables are modifiable by default, as in C/C++. `const` makes them immutable: the value is given at initialization and can no longer change.

```cromo
i32 count = 0                // modifiable
const f64 PI = 3.14159       // immutable
```

- **It is not necessarily a compile-time constant:** the initial value can come from any expression, as with Java's `final`.
- **The type is always written:** there is no `const` form with an inferred type.
- **A `const` variable can be moved** with `move`: the value does not change, it only passes to another owner. After the move the variable is no longer usable, like any moved variable.
- **`const` is deep**, as for methods (D24): on a `const` `string` or `list` only `const` methods can be called, and it cannot be passed with `ref`.
- **`const` fields**, which do not change after the constructor: postponed until after the first version.

### D27. Platforms: first Windows x64, then Linux x64
*2026-10-28.* The first platform is Windows x64, with the Microsoft x64 ABI. Linux x64 comes right after, with the System V ABI.

### D28. A single `cromo` executable
*2026-10-28.* The final product is a single executable, `cromo`, with as few dependencies as possible. It compiles the `.cro` sources, optimizes, generates the machine code and links, producing executables directly.

Consequences:
- LLVM (optimization and machine code) and LLD (linking) are **inside** `cromo`, as Zig does. Clang, Visual Studio or the Windows SDK do not need to be installed.
- To link against `kernel32.dll` without the Windows SDK, `cromo` generates the import libraries itself from the list of functions used, as Zig does.
- The compiler is written in C++ and uses LLVM and LLD directly (D44).

Replaces an earlier proposal: textual IR with Clang as the driver.

### D29. Source file extension: `.cro`
*2026-10-28.* Files containing Cromo code have the `.cro` extension. *2026-10-29:* `.cr`, the symbol of chromium, was considered and left out: it is the extension of Crystal, which GitHub and the editors already give to it. The symbol stays in the logo (D213).

### D30. Overflow and bounds checking
*2026-10-28.*
- **Integer overflow:** in debug it stops the program with an error; in release the value restarts from the other end (*wrap*), as in Rust.
- **Bounds checking:** always on. An out-of-bounds access terminates the program (`panic`). LLVM eliminates most of the redundant checks.

### D31. Initialization with default values
*2026-10-28.* If there is no value in the declaration, the variable takes a default value:
- numbers: `0`;
- `bool`: `false`;
- `string`: empty string;
- `list<T>`: empty list;
- struct: each field with its own default value (D48).

```cromo
i32 n            // 0
string name      // ""
list<f64> grades // empty list
```

So there are no uninitialized values. The same applies to the fields that the constructor does not assign.

### D32. Conditions, loops and newlines
*2026-10-28.*
- **Mandatory braces** after `if`, `else` and `for`.
- **`bool`-only conditions:** `if (n)` is an error, you write `if (n != 0)`.
- **`++` and `--`** only as a standalone statement, never inside expressions: `v[i++]` is not valid.
- **Multi-line statements:** a statement continues on the next line if the line ends with a binary operator, a comma or an open parenthesis, or if there are parentheses or square brackets still open.
- **Free brace style:** `{` on the same line or on a new line. The parser skips newlines where it expects a block.
- **One statement per line.**

```cromo
i64 total = price * quantity +
            shipping
```

### D33. Conversions between numeric types
*2026-10-28.* They are implicit only when no information is lost (for example `i32` → `i64`, `i32` → `f64`), as in C#. In the other cases they are written as a call: `i32(x)`, `f64(n)`.

### D34. Enums with data and `switch`
*2026-10-28.* Enums can contain data (*tagged union*). A `switch` on an enum must cover all cases, or have a `default` branch, which is executed for the cases not listed. The enum syntax is still to be defined.

### D35. Elements, fields and `copy()`
*2026-10-28.*
- **Elements and fields:** `v[i]` and `s.field` denote the element or the field directly, so they can be assigned and passed with `ref`: `addGrade(ref students[0], 9.0)`. Indexed access on `list` is an operation of the language.
- **Exclusivity:** `swap(ref v[0], ref v[1])` is fine, `swap(ref v[0], ref v[0])` is not. With variable indices (`swap(ref v[i], ref v[j])`) the check is done at runtime.
- **`copy()` for structs:** the compiler generates it if all the fields can be copied and the struct does not have its own destructor. A struct with a destructor, like `File`, can be copied only if it defines `copy()` itself.

### D36. Exceptions: what is thrown and how they are implemented
*2026-10-28.*
- **What is thrown:** types that implement an `Exception` interface (D40). Until interfaces exist, there is a predefined `Exception` type with a message and a code.
- **They are not declared in the signature**, as in C++ and C#.
- **Programming errors** (index out of bounds, overflow, division by zero) are not exceptions: they terminate the program (`panic`).
- **A destructor that throws** while an exception is already propagating terminates the program.
- **Implementation: hidden error return**, as in Swift. Every function that can throw also returns an error indicator, and after every call the compiler checks it and jumps to the cleanup (destructors, `finally`) or to the `catch`. No runtime is needed and the generated code is the same on every platform.

### D37. Low-level library: `unsafe` dialect
*2026-10-28.* The parts of the standard library that touch raw memory or the operating system (`list`, `string`, allocator, I/O) are written in an `unsafe` dialect of Cromo, with an address type and intrinsic functions (`load`, `store`, system calls). User code cannot use it (amended by D127: modules that write `import unsafe` can). Underneath there are a few primitives in LLVM IR and in assembly, such as `memcpy` and `memset` (D19).

### D38. Formatting details
*2026-10-28.*
- **Format checked by the compiler:** the format string is a literal, and the compiler checks the number and types of the arguments, like `std::format` since C++20. A string held in a variable is printed with `println("{}", s)`.
- **`print` and `println` are special functions:** the compiler translates them into a sequence of runtime calls. No string is parsed at runtime.
- **Specifiers:** fill and alignment (`<`, `>`, `^`), sign, `#`, `0`, width, `.precision` and type (`d`, `x`, `X`, `b`, `o`, `e`, `f`, `g`, `s`).
- **`format(...)`** returns a `string` instead of printing.
- **`eprint` and `eprintln`** write to stderr.
- How to make user-defined types printable is decided together with the standard library, with a dedicated interface (D49).

### D39. Generics from the first version
*2026-10-28.* User-defined generic functions and structs are there from the start, not only the library containers. The compiler generates a version of the code for each type used (monomorphization), like C++ and Rust, so generics cost nothing at runtime. Constraints on types are expressed with interfaces (D52).

### D40. Interfaces yes, inheritance no
*2026-10-28.* There is no inheritance between structs. Polymorphism goes through **interfaces**: a struct declares that it implements an interface and provides its methods. Syntax and dynamic calls: D49.

### D41. Modules with `import`
*2026-10-28.* Code is organized in modules, which are imported with `import`. There is no preprocessor and there are no header files. Visibility and mapping between files and modules: D50.

### D42. Interoperability with C
*2026-10-28.* C functions are declared with `extern` and called from the `unsafe` dialect (D37). Normal code uses safe wrappers, without seeing the pointers of the C APIs. C resources (handles, descriptors) are encapsulated in structs with a destructor (RAII). Syntax: D128 and D129.

### D43. Concurrency postponed
*2026-10-28.* Threads, async and the like come after the first version. Amended by D149: threads and synchronization come before.

### D44. Compiler in C++, then in Cromo
*2026-10-28.* The first compiler, the bootstrap one, is written in **C++**. The goal is to reach a Cromo compiler written in Cromo, which compiles itself.
- **Access to LLVM and LLD:** they are written in C++, so the compiler uses them directly and `cromo.exe` contains them from the start (D28), as Zig does.
- **Coverage:** the bootstrap compiler must support all of the language needed to write the compiler in Cromo.
- **Details** (C++ standard, build system, LLVM version, how to generate the IR): D53.
- Amended by D147: the compiler in Cromo is the last milestone.

### D45. No undefined behavior in normal code
*2026-10-28.* Overflow, out-of-bounds accesses and use of moved objects do not produce undefined behavior: either they are compile errors or they terminate the program in a controlled way (D30, D23). Any exceptions to this rule are confined to the `unsafe` dialect of the standard library (D37) and to interoperability with C (D42).

### D46. Character literals: one byte
*2026-10-28.*
- **`'a'` is a byte:** it is a `u8` and is allowed only for ASCII characters. `'à'` is a compile error, with a message that suggests the string `"à"` or the code point `0xE0`.
- **Escape sequences:** `'\n'`, `'\t'`, `'\\'`, `'\''`, `'\x41'`.
- **`char` is a synonym for `u8`** (revises D14): `char c = 'a'`.
- **`s[i]` on a `string`** gives the `i`-th byte, of type `char`. To iterate over Unicode characters a library function that returns `u32` code points will be needed.

### D47. No optionals; every function returns the declared type
*2026-10-28.*
- **No optionals:** there are no `T?` types and no `none`.
- **Mandatory return:** the compiler checks that every path of a non-`void` function ends with a `ret` of a value of the declared type. If a path can reach the end without a `ret`, it is a compile error.
- **`void` functions:** the `ret` at the end is optional.

To say "not found" you use the tools that already exist: a conventional value (for example `-1` as an index), an exception, or a `bool` return value with the result in a `ref` parameter, like C#'s `TryParse`.

### D48. Default value of structs
*2026-10-28.* The default value of a struct is made of the default values of all its fields. A declaration without a value (`Student s`) does not call any constructor: to call one you write `Student s = Student("Anna")`.

Consequence: as in Go, a type must be designed so that its default value is also sensible, and the destructor must be able to handle it. For example, the destructor of `File` must not close a descriptor that was never opened: this is why a field can declare its initial value (D51).

### D49. Interface syntax and dynamic calls
*2026-10-28.*

```cromo
interface Shape {
    f64 area() const
}

struct Circle : Shape {
    f64 r
    f64 area() const { ret 3.14159 * r * r }
}
```

- **Static calls, with generics** (`T : Shape`): the type is known and there is no cost.
- **Interface-typed parameters** (`void draw(Shape f)`): always allowed, because they are passed by reference without allocating anything.
- **Objects of different types together:** only inside a `list` of interfaces (`list<Shape>`), where each element is allocated on the heap with its method table. The allocation is already provided for by D11.
- **Interface-typed variables and fields:** not allowed in the first version, because they would require a hidden allocation. A generic is used instead. *Changed by D357: they are allowed, with the allocation.*

### D50. Modules: files and visibility
*2026-10-28.*
- **Files and modules:** a file is a module; a folder is a module that contains those of its files. `import std.io` corresponds to `std/io.cro`.
- **Visibility:** everything is private to the module, except what is marked `public`, as in C#.

### D51. Default value details
*2026-10-28.*
- **Initial value in fields:** a field can declare its initial value, as in C++ and C#. The struct's default uses that value instead of the type's default.

  ```cromo
  struct File {
      i32 fd = -1        // the default of File is "no open file"

      ~File() {
          if (fd >= 0) {
              os.close(fd)
          }
      }
  }
  ```
- **`const` without a value** is not allowed: for `const` the value is mandatory.
- **Enums:** the default is the first declared case.

### D52. Generic constraints with interfaces
*2026-10-28.* The type parameters of generics are constrained with interfaces (D40), like Rust's traits or C++20's concepts. The compiler checks the generic only once, when you define it, and the error messages are clear.

```cromo
T max<T : Comparable>(T a, T b) {
    if (a.compare(b) >= 0) {
        ret a
    }
    ret b
}
```

### D53. Bootstrap compiler details
*2026-10-28.*
- **C++20**, compiled with **MSVC** (clang-cl is also fine). LLVM requires at least C++17.
- **CMake** as the build system, the same as LLVM.
- **LLVM and LLD** with static libraries and headers: from the official prebuilt archive for Windows if it contains them for the chosen version, otherwise built from source. The version is the latest stable one at the time of starting, and then it stays pinned.
- **The IR is generated as text**, which LLVM then parses in memory. When the compiler is written in Cromo, the generator will be translated almost line by line, and at that point a few functions of the LLVM C API will be enough (D42). The time to parse the text is negligible, and the IR can always be inspected.

### D54. No type inference: the type is always written
*2026-10-28.* `var` is removed (revises D20). Every declaration writes its type, including the variable of a `for` over elements:

```cromo
i32 n = 5
Student anna = Student("Anna")
for (Student s in students) { ... }
for (ref Student s in students) { ... }
```

### D55. Literals take the type from their context
*2026-10-28.* A literal has no type of its own: it takes the type required by its context (the declared type, the parameter, the other operand). An integer literal must fit in that type; an integer literal can also be used where a floating-point type is required.

```cromo
i64 big = 5          // 5 is an i64
f32 x = 1.5          // 1.5 is an f32
if (n > 0) { ... }   // 0 takes the type of n
```

### D56. Mixing signed and unsigned integers is an error
*2026-10-28.* An operation between a signed and an unsigned integer (`i32 + u32`) is a compile error: one of the two operands must be converted explicitly, for example `i64(a) + i64(b)`. Neither type contains the other without loss (D33).

### D57. Debug by default, `--release` on request
*2026-10-28.* `cromo prog.cro` compiles in debug mode, with the overflow checks of D30. `--release` removes them, and integers wrap around. The optimization level (`-O0` … `-O3`) is independent of the mode.

### D58. Temporary runtime in C until M5
*2026-10-28.* Printing and panics need a runtime, but the `unsafe` dialect of D37 only arrives in M5. Until then the runtime is written in **freestanding C**, without the C library, and compiled with the clang of the LLVM archive. `memcpy`, `memset`, `memmove`, `memcmp` and `__chkstk` are written in assembly (D19). The runtime is embedded inside `cromo.exe`, which stays a single file (D28). In M5 it is rewritten in Cromo `unsafe`.

### D59. Operators of M1
*2026-10-28.*
- Arithmetic: `+ - * / %`; comparisons: `== != < <= > >=`; logic: `&& || !`, evaluated left to right with short-circuit; unary minus.
- Assignment `=` and compound assignments `+= -= *= /= %=`.
- **Assignment is a statement, not an expression**, as in Go: `if (x = 5)` and `a = b = c` are not valid.
- `-x` on an unsigned integer is a compile error.
- `%` only works on integers: on floating-point numbers it would need `fmod` from the C library.
- Bitwise operators (`& | ^ ~ << >>`) are still to be decided.

### D60. Integer division
*2026-10-28.*
It truncates toward zero, and `%` has the sign of the dividend, as in C. Dividing by zero terminates the program (D36). `MIN / -1` is an overflow (D30): a panic in debug, `MIN` in release.

### D61. Floating-point numbers and explicit conversions
*2026-10-28.*
- Floating-point numbers follow IEEE 754: dividing by zero gives `inf` or `nan`, without a panic.
- **Explicit conversions between integers** (`i8(x)`) keep the low bits when the value does not fit, as in C, Go and Rust: the programmer asked for it explicitly.
- **From floating-point to integer** (`i32(x)`) the value is truncated toward zero. If it does not fit, or it is `nan`: a panic in debug, the nearest value in release.
- `bool` cannot be converted to or from numbers.

### D62. Panic
*2026-10-28.*
A panic writes `panic: <reason> at <file>:<line>:<column>` on stderr and terminates the program with exit code 101, as in Rust.

### D63. Printing numbers
*2026-10-28.*
- `{}` on an integer prints it in decimal. Since `char` is a synonym of `u8` (D46), `{}` on a `char` prints the number, and `{:c}` prints the character.
- `{}` on a floating-point number prints the shortest representation that reads back to the same value, as `std::format` does (`0.1`, `100`, `1e+20`).
- `{}` on a `bool` prints `true` or `false`.

### D64. Literals without a context
*2026-10-28.*
A literal whose type cannot be deduced from the context, for example `println("{}", 5)`, is a compile error (D54, D55): you write `println("{}", i32(5))`.

### D65. Strings in M1
*2026-10-28.*
Strings arrive in M5. In M1 a string literal can only be used as an argument of `print`, `println`, `eprint` and `eprintln`.

### D66. No shadowing of local variables
*2026-10-28.*
A local variable cannot have the same name as another local variable or parameter that is still visible, as in C#.

### D67. Terminating statements (D47)
*2026-10-28.*
A non-`void` function must end with a terminating statement: a `ret`, an `if` with an `else` whose branches all terminate, or an infinite `for` without a `break`, as in Go. Code after `ret`, `break` or `continue` gets a warning, because it is unreachable.

### D68. Object lifetime
*2026-10-28.* The rules of [language.md](language.md) §6, modeled on C++:
- an object is destroyed at the end of the scope where it is declared; the objects of the same scope are destroyed in reverse order of declaration;
- temporary objects are destroyed at the end of the statement that contains them;
- after the body of a destructor, the fields are destroyed in reverse order of declaration;
- `ret`, `break` and `continue` destroy everything that is alive in the scopes they leave;
- a destructor is never called by hand.

### D69. One constructor per struct
*2026-10-28.* A struct has at most one constructor. The default value (D48) already covers the case without arguments. Overloading will be decided later, together with the overloading of functions.

### D70. Fields by name inside methods
*2026-10-28.* Inside a method, constructor or destructor, the fields of the object are used by their name, as in the examples: `fd = os.open(path)`. There is no `this`. A local variable or a parameter cannot have the same name as a field (extends D66).

### D71. `T()` without a constructor is the default value
*2026-10-28.* For a struct without a constructor, `Point()` gives its default value (D48). To set the fields, assign them afterwards or write a constructor: there is no positional initialization like `Point(1.0, 2.0)`.

### D72. Calls inside methods
*2026-10-28.* Inside a method, `f()` refers to a method of the same struct if there is one, otherwise to a free function. The name closest to the code wins, as with the fields (D70) and as in C++.

### D73. Initial values of fields are literals
*2026-10-28.* For now the initial value of a field (D51) must be a literal: a number, `true`, `false` or a character. The default value of a struct is then a constant known by the compiler. Expressions and constructor calls will come later. *Replaced by D360.*

### D74. Memory layout of structs
*2026-10-28.* The fields stay in the order in which they are declared, with the natural alignment of C. This keeps the layout predictable and compatible with C (D42).

### D75. Structs that need destruction are not copied
*2026-10-28.* A struct that needs destruction (it has a destructor, or a field that has one) cannot be copied implicitly (D23). Until `move` arrives in M3 it can only be:
- created directly in a variable: `File f = File("a.txt")`;
- returned from a function: `ret f` of a local variable moves it (D23);
- used as a temporary, which is destroyed at the end of the statement (D68);
- replaced with a new temporary: `f = File("b.txt")` builds the new value, destroys the old one and puts the new one in its place.

A parameter or a field of such a struct cannot be returned, because it would be a copy.

Since M3 the other cases are written with `move x` (D23, D78) or `x.copy()` (D35).

### D76. Structs are not compared or printed yet
*2026-10-28.* `==` and `{}` in `println` do not work on structs: they need a way for the type to say how, which will come with interfaces (D38, D40).

### D77. Analysis of moves on the AST; the intermediate IR is postponed
*2026-10-28.* The control flow of Cromo is structured (`if`, `for`, `break`, `continue`, `ret`, no `goto`), so the analysis of moves is done on the AST, like the definite assignment of Java and C#. Where an object may or may not have been moved, the generated code uses a drop flag, and LLVM removes the ones it can decide. The control-flow-graph IR planned in [compiler.md](compiler.md) will be introduced when a feature needs it.

### D78. Moving a field or a `ref` parameter leaves the default value
*2026-10-28.* `move` on something that is not a whole local variable, such as a field (`move s.log`) or a `ref` parameter, takes the value and leaves the default value of the type in its place (D31, D48), like `mem::take` in Rust. The object stays usable, and destructors already handle the default value. For whole local variables D23 stays: after the `move` they can no longer be used.

### D79. Exclusivity and read arguments of simple types
*2026-10-28.* For exclusivity (D22) the object of a method that is not `const` counts as a `ref` argument. A read argument of a simple type that overlaps an argument that is modified is passed as a copy: `c.add(c.step)` is allowed, and the method sees the value of `c.step` at the time of the call. If the read argument is not a simple type, the overlap is an error.

```cromo
c.add(c.step)            // ok: c.step is copied
h.use(h.file)            // ERROR: a File is not copied implicitly
swap(ref a, ref a)       // ERROR (D22)
```

### D80. `move` of a simple type is a copy
*2026-10-28.* A simple type (D23) passed with `move`, as in `f(move n)` with `n` an `i32`, is copied, and the variable stays usable, as for the `Copy` types of Rust. This way a generic `move T x` (M4) behaves the same for `i32` and for `string`.

### D81. Variables after a move
*2026-10-28.* As in Rust:
- using a variable that has been moved, even only on some paths, is an error;
- assigning a new value to a moved variable makes it usable again;
- a variable declared outside a loop and moved inside it must receive a new value before the next iteration.

### D82. Where `ref` and `move` are written
*2026-10-28.*
- `ref x` only as the argument of a `ref` parameter, on a variable or a field that can be modified, with exactly the type of the parameter.
- `move x` only on a variable or a field, in an initialization, an assignment, the argument of a `move` parameter and after `ret`. Before a new object, such as `move File("a")`, it is an error: a temporary is moved anyway (D23).
- A read parameter that needs destruction cannot be moved: it belongs to the caller.

### D83. `copy()` written by hand
*2026-10-28.* A struct that needs destruction can define `copy()` with exactly the signature `T copy() const`. A struct that is copied implicitly cannot define it: its `copy()` is generated by the compiler (D35), and a hand-written one could behave differently from the implicit copy.

### D84. `Self` in interfaces
*2026-10-28.* Inside an interface, `Self` is the type that implements it, as in Rust and Swift. This keeps the constraint of D52 as `T : Comparable`:

```cromo
interface Comparable {
    i32 compare(Self other) const
}

struct Money : Comparable {
    i64 cents
    i32 compare(Money other) const { ... }
}
```

An interface that uses `Self` can only be a constraint, not the type of a parameter: in a dynamic call the type that `Self` stands for is not known.

### D85. Copies of `T` are checked when the type is chosen
*2026-10-28.* Inside a generic, a value of type `T` can be copied (`T b = a`, `ret a` of a read parameter). If the generic is then used with a type that is not copied implicitly (D23), the error is reported there, and it names the line of the generic that copies. This way the example of D52 works as it is. It is the only check of a generic that is not done once, at the definition.

### D86. Type arguments are deduced from the arguments
*2026-10-28.* Calling a generic function, the type parameters are deduced from the types of the arguments, as in C++, Rust and C#: `max(a, b)`. They are written, `max<Money>(a, b)`, when they cannot be deduced, for example when the arguments are only literals (D55) or there are none. The types of the variables are still always written (D54).

### D87. Built-in types do not implement interfaces yet
*2026-10-28.* In M4 the numbers and `bool` implement no interface, so for example `max<i32>` cannot be used. The built-in interfaces for comparison, equality, printing and arithmetic will be designed together with the standard library; they will also make `==` and `{}` work on structs (D76).

### D88. Syntax of generics and interfaces
*2026-10-28.*
- Generic function: `T max<T : Comparable>(T a, T b)`. Generic struct: `struct Pair<A, B> { ... }`, used as `Pair<i32, f64>`.
- Several constraints are joined with `+`: `T : Comparable + Named`, as in Rust.
- A struct that implements several interfaces lists them with commas: `struct Circle : Shape, Named`.

### D89. What a generic can do with a value of type `T`
*2026-10-28.*
Checked once, at the definition (D52): declare it (with its default value, D31), copy it (D85), move it, pass it to parameters of type `T`, and call the methods of the constraints. Operators, comparisons, printing and conversions are not available, because no interface provides them yet (D87).

`x.copy()` works on every type that can be copied, numbers and `bool` included, so that a generic can also copy a `T` explicitly.

### D90. Interfaces contain only methods
*2026-10-28.*
An interface lists method signatures: no fields, no method bodies, no constructors, and no interface that extends another one. A struct that declares an interface must have each of its methods with the same signature: the same parameter modes and types, the same return type, and `const` where the interface has it, with `Self` replaced by the struct.

### D91. Parameters of interface type
*2026-10-28.*
- They are read parameters, on which only `const` methods can be called (D24), or `ref` parameters; `move` is not allowed, because the size of the object is not known.
- A struct is converted implicitly to an interface it declares.
- An interface cannot be the type of a variable, a field or a return value (D49), nor a type argument, until `list<Shape>` (M5). *Changed by D357: it can, and a `move` parameter can be of an interface.*

### D92. Generic structs
*2026-10-28.*
- They are built with explicit type arguments, `Pair<i32, f64>()`: deduction (D86) applies only to functions.
- Their methods use the type parameters of the struct; methods with type parameters of their own will come later.

### D93. The runtime in C stays until it is needed
*2026-10-28.* It amends D58. In M5 the `unsafe` dialect arrives, with `list<T>` and `string` written in Cromo. The runtime in C stays for the calls to the operating system, the output and the formatting of numbers, until `extern` (M7), the bitwise operators and fixed-size arrays exist: without them, the exact formatting of floating-point numbers is very awkward to write.

### D94. `move` can be omitted for simple types
*2026-10-28.* It amends D22 and D82. Passing a simple type to a `move` parameter, `move` can be omitted, because nobody gives up anything: the value is copied (D80). For the other types it stays mandatory.

```cromo
grades.add(grade)          // ok: f64
grades.add(move grade)     // ok as well
students.add(anna)         // ERROR: Student
students.add(move anna)    // ok
```

### D95. Operators on `string`
*2026-10-28.* `==`, `!=`, `<`, `<=`, `>`, `>=`, `+` and `+=` are operations of the language on `string`, as for the numbers. The order is the order of the bytes, which in UTF-8 is the order of the code points. The interfaces that will give operators to user-defined types (D87) come later, in a step of their own.

### D96. Lists of interfaces after M5
*2026-10-28.* In M5 `list<T>` takes only concrete types, and D91 stays. `list<Shape>` (D49), where every element is allocated on the heap with its method table, comes in a later step.

### D97. The standard library
*2026-10-28.*
- It is the file `library/core.cro`, embedded in `cromo` and compiled with every program. In M5 it contains `list<T>` and `string`.
- It is the only code that can use the `unsafe` dialect (D37). There is no syntax to mark it yet: it will come with modules (M7). Amended by D127: it writes `import unsafe`, as the modules of the user can.

### D98. The `unsafe` dialect in M5
*2026-10-28.*
- The type `addr`: an address without a type. Its default value is null, and it can be compared with `==` and `!=`.
- Built-in functions: `alloc(bytes)`, `resize(p, bytes)` and `free(p)` for the heap; `sizeOf<T>()`; `offset(p, bytes)`; `take<T>(p)`, which moves the value out of `p`; `put<T>(p, move value)`, which moves it in without destroying what was there; `destroy<T>(p)`; `copyAt<T>(p)`, which returns a copy (D35); `copyMemory(dest, source, bytes)` and `compareMemory(a, b, bytes)`; `checkIndex(index, length)`, the bounds check of D30.

### D99. `list<T>`
*2026-10-28.*
- `list<T>()` is the empty list, which is also the default value.
- Its methods: `add(move T)`, `insert(i64 index, move T)`, `remove(i64 index)`, which returns the element, `len()`, `clear()`, `reserve(i64 count)` and `copy()`.
- `v[i]` is the element itself, with bounds checking (D30, D35): it can be read, assigned, passed with `ref`, and moved with `move v[i]`, which leaves the default value (D78).

### D100. `string`
*2026-10-28.*
- A string literal is a `string` wherever a `string` is expected (D55). It does not allocate memory until it is modified.
- `len()` is the length in bytes (D12). The other methods are `append`, `reserve` and `copy`, besides the operators of D95.
- Indexing, slicing and `format()` (D38) come later.

### D101. `for` over the elements
*2026-10-28.*
- `for (T x in v)` gives each element read-only; `for (ref T x in v)` lets the body modify it (D25). The type of `x` is written, and it must be exactly the type of the elements.
- In the body the list can be read, but not modified, except its elements through `x`: no method that changes it, no assignment and no `ref` or `move` of the list or of its other elements.

### D102. `panic`
*2026-10-28.*
- `panic("reason")` stops the program as in D62. Like `ret`, it ends the paths of a function (D67).
- A panic inside the standard library, for example an index out of bounds in `remove`, reports the line of the user's code that called it, as `#[track_caller]` does in Rust.

### D103. The methods of generic structs are checked when they are used
*2026-10-28.*
For an instance with real types, a method is checked (and emitted) only if it is used. This extends D85: `list<File>` can be used even if `File` cannot be copied, as long as `copy()` of the list is not called. The definition of the generic is still checked whole, once (D52).

### D104. Declaration of enums
*2026-10-28.* One case per line, as the members of a struct (D17). The data of a case are written like parameters, type before name (D20), and read like the constructor of the case.

```cromo
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
    Empty
}

enum Color {
    Red
    Green
    Blue
}
```

### D105. Values are created with the name of the enum
*2026-10-28.* `Shape.Circle(1.0)`, `Color.Red`, as in C#, Java and Swift. Two enums can have cases with the same name, and a case is never confused with a struct.

### D106. `case X { ... }`
*2026-10-28.* In a `switch` a case is written like `if` and `for`: braces are mandatory (D32) and there is no colon. Inside the `switch` the type is known, so the cases are written without the name of the enum; `default` is written the same way.

```cromo
switch (shape) {
    case Circle(f64 r) {
        ret 3.14 * r * r
    }
    case Rect(f64 w, f64 h) {
        ret w * h
    }
    default {
        ret 0.0
    }
}
```

### D107. The data of a case in a `switch`
*2026-10-28.* They are given a name, with the type written (D54), as the element of a `for` (D101): read-only and without a copy; with `ref` they can be modified, when the value of the `switch` can. Without the parentheses, the data are ignored.

```cromo
case Circle(f64 r) {            // r is read-only
    println("{}", r)
}
case Rect(ref f64 w, ref f64 h) {
    w *= 2.0                    // modifies the data of the case
}
case Empty {
}
```

### D108. No fall-through, several cases in one branch
*2026-10-28.*
- At the end of a branch the `switch` ends: there is no fall-through and no `break` to write. There is no `goto`.
- Several cases without data can share a branch: `case Red, Green { ... }`.

### D109. `switch` on numbers and strings
*2026-10-28.*
- Besides enums, `switch` works on integers (`char` included) and on `string`, with literals as cases: `case 1, 2 { ... }`, `case "yes" { ... }`. The same value cannot appear twice.
- Only on enums the cases must all be covered (D34): on numbers and strings `default` is optional, and without it a value that matches no case does nothing, as in C.
- A `switch` ends the paths of a function (D67) when it covers all the cases, or has `default`, and all its branches end them.

### D110. Enums like structs
*2026-10-28.*
- After the cases an enum can have methods, also `const`, and it can implement interfaces (D40). It has no constructor or destructor written by hand.
- An enum can be generic, with the rules of the generic structs (D92): `enum Result<T, E> { Ok(T value) Err(E error) }` and `Result<i32, string>.Ok(5)`.

### D111. Ownership and copies of enums
*2026-10-28.*
- An enum needs destruction when the data of one of its cases do (D23): it is not copied implicitly, and destroying it destroys the data of its current case. Its `copy()` is generated as for structs (D35).
- The default value is the first case (D51), with the default values of its data.

### D112. Comparing and printing enums
*2026-10-28.*
- `==` and `!=` work on enums whose cases have no data, as `color == Color.Red`. With data, the case is told by a `switch` (D76).
- Explicit numeric values of the cases (`Red = 1`) and conversions to numbers come with `extern` (M7, D42, D135); printing with `{}` comes with the interface for printing (D38).

### D113. Destructors and `finally` do not let exceptions out
*2026-10-28.* It amends D36. If the body of a destructor or of a `finally` block can throw without catching, it is a compile error, as for the `noexcept` destructors of C++11 and `drop` in Rust. This way a second exception can never arise while one is propagating, and the cleanup has no special cases.

### D114. The variable of a `catch` and rethrowing
*2026-10-28.* The exception of a `catch` is read-only, like a read parameter (only `const` methods, D24), both in `catch (IoError e)` and in `catch (Exception e)`. `throw` alone rethrows it, as `throw;` in C++.

```cromo
try {
    load()
} catch (IoError e) {
    println("{}", e.message())
    throw                       // the same exception again
} catch (Exception e) {
    println("{}", e.message())
}
```

### D115. The interface `Exception`
*2026-10-28.* The standard library declares `interface Exception { string message() const }`, and a ready-made struct `Error : Exception` that holds the message, for the simple cases: `throw Error("bad input")`.

### D116. `break` and `continue` in a `switch` are errors
*2026-10-28.* Since a branch of a `switch` ends by itself (D108), `break` and `continue` written directly in a branch are errors, with a message that explains it. This avoids the trap of the C habit, where a `break` written to close the case would leave the loop instead.

### D117. `switch` without a subject in the methods of an enum
*2026-10-28.* Inside a method of an enum, `switch { ... }` without parentheses looks at the value of the method. It agrees with D70 (no `this`), as the fields of a struct are used by name. The whole value cannot be named, for example to pass it to a function.

```cromo
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)

    f64 area() const {
        switch {
            case Circle(f64 r) {
                ret 3.14 * r * r
            }
            case Rect(f64 w, f64 h) {
                ret w * h
            }
        }
    }
}
```

### D118. `try` and `catch`
*2026-10-28.*
- `try { ... } catch (T e) { ... } finally { ... }`: `catch` and `finally` may start on the next line, like `else` (D32). A `try` has at least one `catch` or a `finally`.
- `T` is a struct that implements `Exception`, or `Exception` itself, which catches everything. The first `catch` that matches wins; a `catch` after one that catches everything, or a repeated one, is an error, because it would never be reached.

### D119. `throw`
*2026-10-28.*
- What is thrown is a struct that implements `Exception`: a new object, or `move x` (D23). `throw` ends the path, like `ret` (D67).
- `throw` alone (D114) is allowed only inside a `catch`, where there is an exception to rethrow.

### D120. Leaving `finally`
*2026-10-28.* `ret`, `break` and `continue` cannot leave a `finally` block, as in C#: they would silently discard an exception or a value being returned. With D113, `finally` always ends normally.

### D121. Uncaught exceptions
*2026-10-28.* An exception that leaves `main` stops the program with `uncaught exception: <message>` on stderr and exit code 1. A panic stays different: exit code 101 (D62).

### D122. `copy()` and `message()` do not let exceptions out either
*2026-10-28.* Like destructors (D113), a `copy()` and a `message()` written by hand cannot throw without catching: the compiler calls them in places where nobody could catch, such as the copies it generates (D35) and the report of an uncaught exception (D121).

### D123. A constructor that throws
*2026-10-28.* The fields of the object, which start from their default values (D48), are destroyed, but the destructor does not run, because the object was never complete (D16). The variable being initialized is not created.

### D124. The data of a case are given like `move` parameters
*2026-10-28.* `Message.Text(move name)`, or a new value: the enum becomes their owner. For simple types `move` can be omitted (D94).

### D125. The value of a `switch` while a branch uses its data
*2026-10-28.* While a branch has names for the data of the case, the value of the `switch` cannot be modified, except through the names given with `ref`, as the list in a `for` (D101).

### D126. The names of an imported module are used directly
*2026-10-28.* After `import geometry` the public names of the module are used as they are, `area(c)` and `Circle c`, as with `#include` or the `using` of C#. The name of the module in front, `geometry.area(c)`, is needed only when two imported modules give the same name. The names of the module itself come first.

### D127. `import unsafe`
*2026-10-28.* It amends D37 and D97. A module that writes `import unsafe`, as in Go, can declare C functions with `extern` (D42), use the type `addr` and the built-in functions of D98. The other modules use only its public interface, which must be safe. The standard library is a module of this kind. It is easy to find with a search.

### D128. An `extern` block names its library
*2026-10-28.* C functions are declared in a block that names the library they come from. Inside, the prototypes are written as in Cromo:

```cromo
extern "kernel32" {
    u32 GetLastError()
    addr GetStdHandle(u32 id)
}
```

`cromo` links that library with no options on the command line, and in M8 it will use the name to generate the import library, as Zig and the `DllImport` of C# do.

### D129. The types at the boundary with C
*2026-10-28.* The parameters and the result of a C function can be numbers, `bool` and `addr`. A `ref T` parameter, where `T` is a number or a struct of only numbers, is a C pointer to that value: structs already have the layout of C. The `bool` of C is one byte; the `BOOL` of Windows is an `i32`. Structs passed by value, callbacks and variadic functions such as `printf` come later.

### D130. Where the modules are
*2026-10-29.*
- `import a.b` is the file `a/b.cro`, or the folder `a/b`, of which every `.cro` file is imported, without the subfolders. The paths start from the folder of the main file, as in Go and Java: a module is always written with the same name, wherever it is imported. A file and a folder with the same name are an error.
- `std` is the standard library. In M7 it has only its core (`list`, `string`, `Exception`, `Error`), which every module sees without importing it; `import std.x` is an error until the library has modules.

### D131. The rules of `import`
*2026-10-29.*
- The imports come at the top of the file, before the declarations, and they are not public: each module writes its own. They are not transitive: a module sees only what it imports itself.
- Two modules can import each other: the program is compiled all together.
- The name in front is the last name of the path: after `import a.shapes` it is `shapes.area(c)`. Two imports with the same last name in a file are an error, and so is a variable with the name of an imported module.
- A name is looked for in the module itself, then in the imported modules, then in the standard library. So a name added to the library never breaks a program, as the prelude of Rust.

### D132. `public` on members
*2026-10-29.*
- Fields, methods and the constructor are private to the module unless they are marked `public`, as in C#: `public struct Point { public f64 x }`. The destructor has no `public`: it runs wherever the object ends.
- The cases of an enum and the methods of an interface are public with their type. A method that implements an interface needs no `public`, because it can be called through the interface anyway.
- What a public declaration shows must be public too: a public function cannot return a private struct.

### D133. The details of `extern`
*2026-10-29.*
- The C functions are private to their module, which offers safe functions instead (D42), and they keep their C name. An `extern` block is not public.
- The library is a name without extension: `"kernel32"` is `kernel32.lib`, looked for in the folders of the modules that use it, then in the Windows SDK, until M8 (amended by D136). The names that start with `cromo_` are reserved for the runtime, whose functions the library declares with `extern "cromo"` (D240).
- The same C function can be declared by several modules, with the same signature.

### D134. The names of the symbols (D39, D50)
*2026-10-29.* Every symbol has its module in front: `@"geometry.area"`, `@"geometry.Circle.area"`, `%"struct.geometry.Circle"`, and the instances of generics have the types with their module, `@"std.list<geometry.Circle>.add"`. The main file is the module named after it, the standard library is `std`, and the C functions keep their own name. The names are readable in the IR and in a debugger, and two modules can have private names that are equal.

### D135. The values of the cases
*2026-10-29.* It closes the point left open by D112.
- In an enum without data, every case can have a value, or none: `Red = 1`. The values are integer literals that fit in an `i32`, all different; without them a case has its position, from 0.
- `i32(color)` gives the value of the case, as an explicit conversion to any integer type (D61). `Color(n)` gives the case whose value is `n`, and is a panic if there is none (D45). The default value stays the first case (D111).

### D136. A C library is a DLL, or a `.lib` next to the module
*2026-10-29.* It amends D133: the Windows SDK is no longer used. `extern "name"` is `name.dll`, and `cromo` writes its import library from the functions declared in the blocks, as Zig and the `raw-dylib` of Rust do. If a `name.lib` is next to the module, that one is used instead: it can be a static C library, or the import library of a DLL with another name.

### D137. The names are checked in the DLL
*2026-10-29.* While compiling, `cromo` reads the list of the functions that `name.dll` exports, looking for the DLL next to the module and then in the system folder of Windows. A C function that the DLL does not have, such as `GetStdHandel`, is a compile error, and so is a DLL that cannot be found. Without the check, the mistake would appear only when the program starts, in a window of Windows.

### D138. `cromo` copies the DLLs next to the executable
*2026-10-29.* A DLL found next to a module is needed next to the executable when the program starts. If the executable is written to another folder, `cromo` copies the DLL there, as Visual Studio does with the references.

### D139. Where the libraries are looked for
*2026-10-29.*
- `name.lib` and `name.dll` are looked for in the folders of the modules that declare the library, in the order in which the modules are loaded, and `name.dll` then in the system folder of Windows (`System32`). The first one found is used.
- A DLL that is not for x64 is an error.

### D140. What is checked
*2026-10-29.*
- A C function must be exported by name by the DLL, also when the DLL forwards it to another one, as `kernel32` does with `HeapAlloc`.
- With a `.lib` `cromo` checks nothing: a missing function is reported by the linker.

### D141. The copy of the DLLs
*2026-10-29.*
- The DLLs found next to the modules are copied after linking, also when a `.lib` with the same name is used with them. The DLLs of the system folder are not copied.
- A DLL is copied only if the executable is in another folder and the copy there is missing or different, in size or date. A copy that fails is an error. `--emit-ir` copies nothing.

### D142. Linux by cross-compilation
*2026-10-29.* `cromo` on Windows also produces executables for Linux x64, with `--target linux`, as Zig does: LLD for ELF is already in `cromo`, and the runtime for Linux is embedded next to the one for Windows. The tests also run in WSL. A `cromo` that runs on Linux comes later.

### D143. On Linux `extern` uses static libraries
*2026-10-29.* When compiling for Linux, `extern "name"` is the static library `libname.a` next to the module, linked as a `.lib` on Windows (D136). Shared libraries (`.so`) and the C library come later, with a decision of their own; a module that uses the DLLs of Windows stays only for Windows.

### D144. The option `--target`
*2026-10-29.*
- `--target windows` or `--target linux`; without it, the program is for Windows, the system on which `cromo` runs.
- For Linux the executable is named after the source, without extension: `cromo hello.cro --target linux` writes `hello`. With D151 the command comes first: `cromo build hello.cro --target linux`.

### D145. The executable for Linux
*2026-10-29.*
- It is static, at a fixed address, and does not use the dynamic loader: it runs on any Linux x64, because it talks to the kernel only with system calls (`write`, `mmap`, `exit_group`).
- The exit code is the lowest byte of the value of `main`, as in C: `ret -1` gives 255.

### D146. Where `libname.a` is looked for
*2026-10-29.* In the folders of the modules that declare the library, in the order in which the modules are loaded, as on Windows (D139). `cromo` does not check the names: a missing function is reported by the linker (D140).

### D147. The compiler in Cromo comes last
*2026-10-29.* It amends D44. The compiler stays in C++ until the language is mature: first the language and its library grow, then the compiler is written in Cromo, as the last milestone. Writing it earlier would mean changing two compilers at every change of the language. The goals stay those of D19 and D28: executables without dependencies, and fast code.

### D148. Anonymous functions
*2026-10-29.* Cromo will have anonymous functions, as the lambdas of C++. Sorting, threads and the callbacks of the library need them. The syntax, how the variables are captured and the type of a function are still open.

### D149. Threads and synchronization
*2026-10-29.* It amends D43: threads and the means to synchronize them come before the first version, after the anonymous functions. The model is still open.

### D150. A library ready for backends
*2026-10-29.* The standard library, included in `cromo`, will have what is needed to write efficient backends: networking, TLS and cryptography, HTTP, JSON. The executables stay without dependencies (D19, D28), so a C library included for TLS or anything else must work without libc. Which TLS library is still open.

### D151. The commands of `cromo`
*2026-10-29.* The first argument of `cromo` is a command: `build`, `run`, `update`, `version`, `fmt`, which formats the code and is also used by the extension of VS Code, and the language server that VS Code uses for errors, types and definitions (LSP). The name of the last command is still open; `update`: D153.

### D152. The milestones after M9
*2026-10-29.* M10 the commands `build`, `run` and `version`; M11 `fmt`; M12 the language server; M13 anonymous functions; M14 the basics of the library (bitwise operators, arrays, `map`, bytes, files, time, code for each system); M15 threads and synchronization; M16 networking; M17 TLS, cryptography and the HTTPS client; M18 HTTP server and client, JSON. Then lightweight tasks, and last the compiler in Cromo (D147). The tools come first because they help to write the library.

### D153. `cromo update` updates the compiler
*2026-10-29.* `cromo update` downloads the new version of `cromo` from a resource on the network, which does not exist yet, and replaces itself. It is not a manager of the libraries of a project. Where the resource is, and how a download is checked before replacing `cromo`, are still open.

### D154. The arguments of the program
*2026-10-29.* `main` can receive the arguments of the program: `i32 main(list<string> args)`. As the `argv` of C, `args[0]` is the name of the program and the arguments follow. `main()` without parameters stays valid.

### D155. `cromo build` without a file
*2026-10-29.* Without a file, `cromo build` compiles `main.cro` of the current folder, and the executable is named after the folder, as in Go. With a file, it is named after the file. There is no project file for now.

### D156. `cromo run`
*2026-10-29.* `cromo run main.cro -- a b` compiles the program in a temporary folder, runs it with the arguments that follow `--`, deletes the folder and ends with the exit code of the program, as `cargo run` and `dotnet run`. Without a file it runs `main.cro` of the current folder (D155).

### D157. The version of `cromo`
*2026-10-29.* The version is `0.milestone.fix`: `0.10.0` at the end of M10, `0.10.1` for a fix of it; `1.0` comes with the first stable version. `cromo version` also prints the version of LLVM: `cromo 0.10.0 (LLVM 23.1.4)`.

### D158. The command comes first
*2026-10-29.*
- `cromo hello.cro` is an error that shows the right form, `cromo build hello.cro`: there is a single way to write it.
- `cromo help`, `-h` and `--help` print the commands; `cromo` alone prints them as an error. `update`, `fmt`, `lsp` and `rls` answer with the milestone in which they come (`fmt` exists from M11, `lsp` from M12, and `rls` points to `cromo lsp`).

### D159. The options of `cromo run`
*2026-10-29.*
- `-o`, `--target` and `--emit-ir` are options of `cromo build` only: `cromo run` makes a program for the system on which `cromo` runs, and its executable is deleted.
- The temporary folder is `cromo-run-<number of the process>`, in the temporary folder of the system. The executable has the name of the source, and the DLLs of the modules are copied next to it (D138).

### D160. How the arguments reach `main`
*2026-10-29.*
- The parameter is a read parameter of type `list<string>`, with any name: `ref`, `move` and other types are an error.
- On Windows the runtime splits the command line with the rules of the C runtime of Microsoft (quotes, backslashes before a quote, `""` inside quotes) and converts it from UTF-16 to UTF-8. On Linux the arguments are the bytes that the kernel gives.
- The standard library builds the list with three built-in functions of the `unsafe` dialect: `argumentCount()`, `argumentLength(i)` and `argumentData(i)`.

### D161. One style, with 4 spaces
*2026-10-29.* `cromo fmt` has a single style, without options, as `gofmt`: all the code in Cromo looks the same, and nobody discusses the style. It indents with 4 spaces, as the examples, the tests and the library already do.

### D162. Braces on the same line
*2026-10-29.* `cromo fmt` puts `{` at the end of the line, and `else`, `catch` and `finally` on the line of the `}` that comes before: `} else {`. The parser still accepts both styles (D32).

### D163. Long lines are not broken
*2026-10-29.* As `gofmt`, `cromo fmt` keeps the line breaks of the author and only fixes spaces and indentation, also of the lines that continue a statement. It never changes where a statement ends (D17), and the result is predictable: a change to one line does not move the lines around it.

### D164. VS Code formats with `cromo fmt`
*2026-10-29.* From M11 the extension for VS Code formats a file, also when it is saved, by passing its text to `cromo fmt` and reading the result. With M12 the language server takes its place.

### D165. The command
*2026-10-29.*
- `cromo fmt` formats every `.cro` file of the current folder and of its subfolders, except the folders whose name starts with a dot; `cromo fmt a.cro lib` formats those files and folders. The files are rewritten in place, and their names are printed when they change.
- `cromo fmt --check` changes nothing: it prints the files that are not formatted, and ends with 1 if there are some, for a check before a commit.
- `cromo fmt -` reads the text from the standard input and writes the result on the standard output: this is what editors use.
- A file with syntax errors is not changed: `cromo fmt` reports the errors and ends with 1.

### D166. Spaces
*2026-10-29.*
- One space around binary operators, assignments and `:`, after commas and semicolons, and after keywords: `if (x > 0) {`.
- No space inside parentheses and square brackets, before the `(` of a call, around `.`, inside the angle brackets of generics (`list<string>`, `max<Money>(a, b)`), after `-`, `!` and `~` used as unary operators, and before `++` and `--`.
- One space before `{` and after it when the block goes on in the same line, and one before `}` in that case.

### D167. Lines
*2026-10-29.*
- Each open block indents by 4 spaces. A line that continues a statement (D32) gets one more level, and one more for each parenthesis or square bracket still open where it starts; a line that starts by closing them gets one less.
- At most one blank line in a row, and none at the start or at the end of a block or of the file. No spaces at the end of the lines, and the file ends with one newline.
- The file keeps its line endings, LF or CRLF.

### D168. Comments
*2026-10-29.*
- The text of the comments does not change. A comment alone on its line is indented like the code, unless it is right under the comment at the end of the line before, in the same column: then it goes on in the column of that comment.
- The comments at the end of consecutive lines are aligned in a column, two spaces after the longest line of code of the group, as in the examples. A blank line, or a line without such a comment, ends the group.
- When `{` or `else` move to the line before, they go before the comment at the end of that line. If both lines have a comment at the end, or there is a line with only a comment between them, they stay where they are.

### D169. A formatting that cannot change the program
*2026-10-29.* The formatter only changes spaces and line breaks. Before writing, it checks that the tokens of the file are the same, apart from the line breaks before `{`, `else`, `catch` and `finally` that it removes, that the result has no syntax errors, and that formatting it again changes nothing. If one of these checks fails, the file is not changed and `cromo fmt` reports an internal error.

### D170. `cromo lsp`
*2026-10-29.* The language server is the command `cromo lsp`, after the name of the protocol (Language Server Protocol), as `gopls` and `zls`. The editor starts it and talks to it on the standard input and output. It resolves the open point of D151.

### D171. What the language server does in M12
*2026-10-29.* The errors while writing, the type and the signature under the mouse, going to the definition, the symbols of the file and the formatting. Completion comes later, because it needs to analyze code that is not complete. Amended by D179: the completion after a dot comes at once.

### D172. Which program the language server analyzes
*2026-10-29.* The file itself if it has `main`; otherwise the nearest `main.cro` in its folder or in the folders above it, as `cromo build` (D155), so that the imports are resolved as when compiling; without one, the file alone, without asking for `main`.

### D173. The analysis while writing
*2026-10-29.* After a short pause, about 300 ms, the language server analyzes the text of the editor, also when it is not saved. It only checks the program, without generating code, so it is fast.

### D174. The protocol
*2026-10-29.*
- LSP 3.17 on the standard input and output, with the whole text of a document at every change. The server offers diagnostics, hover, definition, the symbols of a document and formatting.
- References, renaming and completion come later.

### D175. The program and its files
*2026-10-29.*
- Every file of the analyzed program gets its errors, also the ones that are not open. The open documents are read from the editor, the others from the disk.
- The search for `main.cro` (D172) stops at the folder of the workspace. A module that its `main.cro` does not import is analyzed alone.
- The symbols of a file come from parsing it alone, so they are there also when the program has errors.

### D176. When it analyzes
*2026-10-29.* The analysis starts 300 ms after the last change; opening a document, and every request, analyze at once what has changed.

### D177. Hover and definition
*2026-10-29.*
- A variable shows its type and its name, a function or a method its signature with `ref` and `move`, a field its type and its struct, a type whether it is a struct, an enum or an interface, a case its enum.
- The standard library is inside `cromo`: its names have a hover, but no definition to go to.
- In a generic, the types are those of its first instance; a generic that is never used has no hover. While a file has syntax errors, hover and definition use the last analysis without them.

### D178. The extension for VS Code
*2026-10-29.*
- The extension speaks the protocol by itself, in `extension.js`, without packages from npm, so it is built without Node.js. It starts `cromo lsp` with the path of `cromo.path`; when that is empty, `build\cromo.exe` in a folder of the workspace, as in the folder of cromo itself, or else `cromo` from PATH. If cromo cannot start, a message says so once, with a button to the setting.
- The formatting goes through the language server, as D164 says; `cromo fmt -` stays for the other editors.

### D179. Completion after a dot
*2026-10-29.* Asked by the author after M12: when `.` is written after a name, the language server proposes the fields and the methods of its type, as `len`, `add` and `insert` after a `list`. The completion of the other names, while writing them, comes later (D171).

### D180. Completion: how the type is found
*2026-10-29.*
- The name right before the dot gives the type: a variable, a parameter or a field, also at the end of a chain as `a.b.`. After a call or an index, as `f().` or `v[i].`, there is no completion yet.
- The code with the dot is not complete, so the server analyzes the text with a call to a method that does not exist in place of the member being written, and reads the type of the name from that analysis.

### D181. Completion: what is proposed
*2026-10-29.*
- The fields and the methods that the module can use: the public ones, or all of them in the module of the type (D132), with their type or signature. The constructor and the destructor are not there.
- After the name of an enum, as `Color.`, its cases.
- A method is inserted by name, without parentheses.

### D182. Completion: when
*2026-10-29.* VS Code asks when `.` is written, and with Ctrl+Space right after a dot or inside the name after it. Elsewhere the server proposes nothing yet.

### D183. How an anonymous function is written
*2026-10-29.* As the lambdas of C++, with the captures written: `[limit, ref total](i32 x) bool { ret x > limit }`. The captures are between square brackets, with the modes of Cromo: a name alone is read, `ref` can modify, `move` takes the ownership (D22). The parameters are written as in a function, and the type returned follows the parentheses; without it the function returns nothing. Whoever reads sees at once what the lambda uses and how. It resolves the open point of D148.

### D184. Where an anonymous function can go
*2026-10-29.* It depends on its captures. A lambda that captures with `ref`, or reads an object without copying it, can only be passed as an argument, as a `ref` parameter (D22): no borrow checker is needed. A lambda that captures only with `move`, or values of simple types, is a value that owns its captures: it can be kept in variables, fields and lists, returned, and moved with `move`. Keeping one that has captures allocates them on the heap, as a `string` does.

### D185. The type of a function
*2026-10-29.* `func(i32, i32) bool`: the keyword `func`, the types of the parameters with their modes, as `func(ref list<i64>, i64) bool`, and the type returned after them, as in the lambdas. Without it the function returns nothing.

### D186. A function received as a parameter is called indirectly
*2026-10-29.* The value of a function is a pair: the captured data and the address of the code, as the parameters of interface type (D49). LLVM makes the call direct, and puts it inline, when it sees both, for example after putting inline the function that receives it. One code serves every lambda.

### D187. Lambdas: the details of the captures
*2026-10-29.*
- A name alone reads: a value of a simple type (D23) is copied when the lambda is made; the others are read where they are, by address.
- `move` takes the value of a local variable or of a `move` parameter, which can no longer be used after (D23); for a simple type it is a copy.
- A field cannot be captured: it is copied into a variable first. A name is captured once.

### D188. Lambdas: inside the body
*2026-10-29.*
- What is captured is read-only, except with `ref`, and cannot be moved out, because the lambda can run again: to return a captured value, `.copy()` it.
- A variable of outside that is not captured is an error that says so. `break` and `continue` do not leave the lambda.

### D189. Function values
*2026-10-29.*
- They are moved, as a `string`, and have no `copy()`. They cannot be compared or printed.
- A variable of a function type starts empty: calling it is a panic (D45).

### D190. Calls of function values
*2026-10-29.*
- A variable or a field of a function type is called as a function: `less(a, b)`, `button.onClick(1)`; a method with the same name comes first. Also the value of an expression: `handlers[i](x)`, `make()(x)`.
- The call of a function value can always throw (D36): every lambda takes the slot of the exceptions.

### D191. Lambdas and exclusivity
*2026-10-29.* What a lambda passed as an argument captures by address counts as used by the call (D22): `add(ref total, [ref total](i64 x) { ... })` is an error.

### D192. Where the data of a lambda are
*2026-10-29.* As an argument, the data of a lambda are in the frame of the caller, and what they own is destroyed at the end of the statement. Otherwise they are on the heap, and the value frees them when it is destroyed. A lambda without captures has no data.

### D193. The precedence of the bitwise operators
*2026-10-29.* It resolves the point left open by D59. `&`, `|` and `^` bind more than the comparisons, as in Go and Rust: `flags & MASK == 0` is `(flags & MASK) == 0`, while in C it is `flags & (MASK == 0)`. From the strongest: `* / %`, `+ -`, `<< >>`, `&`, `^`, `|`, the comparisons, `&&`, `||`.

### D194. The bitwise not is `!`
*2026-10-29.* As in Rust, `!` negates a `bool` and inverts the bits of an integer. `~` stays only in front of the name of a destructor.

### D195. Shifts beyond the width
*2026-10-29.* As overflow (D30, D57): a count that is negative, or not smaller than the width of the type, as `x << 40` on an `i32`, is a panic in debug; in `--release` the count is taken modulo the width, without undefined behavior. `>>` keeps the sign of a signed integer.

### D196. Fixed-size arrays
*2026-10-29.* `i32[4] a`: the type before the name, as every type of Cromo. An array is a value, as a struct: it lives where it is declared, it is copied when its elements are simple, `a[i]` is checked (D30), `a.len()` is its size, and `for (i32 x in a)` visits it.

### D197. `map`
*2026-10-29.* It amends D13: `map<K, V>` is the second container of the library. It is used as a list: `m[k] = v` inserts or replaces, `m[k]` reads and is a panic if the key is missing, as an index out of a list (D30). Also `m.contains(k)`, `m.remove(k)` and `for (K k, V v in m)`.

### D198. The keys of a map
*2026-10-29.* A map is a hash table. Its keys are numbers, `bool`, `string` and the structs that implement the interface `Key`, with `hash()` and `equals()`. It amends D87: the built-in types implement `Key` by themselves.

### D199. A number from a text
*2026-10-29.* `i64("42")`, `f64("2.5")`: a conversion, as `i32(x)` (D33), for every integer type and for `f32` and `f64`. It throws an exception, as the errors of D16, when the text is not a valid number or the number does not fit in the type.

### D200. Strings and bytes
*2026-10-29.* `s[i]` is a byte, a `u8`, coherent with the length in bytes (D12). `s.bytes()` gives a `list<u8>`; `string(bytes)` checks the UTF-8 and throws an exception when it is not valid; `s.slice(from, to)` copies a part; `format("{}", x)` writes into a string, as `println` writes on the screen (D21).

### D201. The library: a core and modules
*2026-10-29.* The core stays visible everywhere without import: `list`, `string`, `map`, `format`, the numbers from texts. Files, time and the system are modules to import: `import std.fs`, `import std.time`, `import std.os`. It closes the point of D130 on the modules of `std`.

### D202. Code for each system
*2026-10-29.* As in Go: next to `fs.cro` there can be `fs_windows.cro` and `fs_linux.cro`, and `cromo` uses only the one of the system it compiles for. It holds for the library and for the modules of the programs.

### D203. Time
*2026-10-29.* A monotonic clock to measure (`Instant` and `Duration`), the date and the time in UTC and in the time zone of the system. On Linux the zone is read from `/etc/localtime`. To wait, as in C: `sleep(s)` in seconds, `msleep(ms)` in milliseconds and `usleep(us)` in microseconds.

### D204. A string for C
*2026-10-29.* It resolves the open point of M7. In the modules with `import unsafe`, `s.cstring()` gives a copy that ends with a zero, destroyed at the end of the statement: the C APIs want that zero, and the string does not change.

### D205. Bitwise operators: assignments, counts and literals
*2026-10-29.*
- The compound assignments `&=`, `|=`, `^=`, `<<=` and `>>=`.
- The count of a shift can be any integer type; the result has the type of the value shifted.
- `1 << n` and `!0` take their type from where they are used, as the literals (D55).

### D206. Arrays: size, dimensions, default values and copies
*2026-10-29.*
- The size is an integer literal, at least 1; an array takes at most 2 GB.
- `i32[3][4]` is three arrays of four: `a[i][j]` has `i < 3` and `j < 4`, as in C.
- Every element starts with its default value; there is no literal of an array yet.
- An index that is a constant outside the array is a compile error.
- An array of values that are not simple (D23) is moved, never copied: it has no `copy()`.
- Arrays cannot be compared or printed yet.
- A local array is on the stack, which is 1 MB on Windows: a large array goes in a `list`.

### D207. Strings and bytes: read-only bytes, slices and the bytes of C
*2026-10-29.*
- `s[i]` is read-only: a string is changed by building a new one.
- `s.slice(from, to)` is a panic when the bounds are outside the string, or when it cuts a character of UTF-8 in two.
- `string(bytes)` throws an `Error` that says where the bytes stop being UTF-8.
- In the modules with `import unsafe`, `string(data, length)` copies bytes from memory, with the same check: the opposite of `cstring()` (D204).

### D208. Numbers from text: the texts accepted and the errors
*2026-10-29.*
- The text is the number alone, without spaces: an optional `+` or `-`, then decimal digits. For `f32` and `f64` also a decimal point and an exponent with `e` or `E`, and `inf`, `infinity` and `nan` in any case.
- `u8("-0")` is 0; another negative number does not fit in an unsigned type.
- The errors are `Error`, with messages as `'300' does not fit in u8` and `'abc' is not a valid i32`.
- A float is the nearest to the text, rounded half to even, as `strtod` does.

### D209. Map: missing keys, remove, order and the keys
*2026-10-29.*
- `m[k] = v`, `m[k] += v` and `m[k]++` add a missing key with the default value first, as `operator[]` of C++. Any other use of a missing key is a panic, also `m[k].add(x)`.
- `m.remove(k)` gives `true` if the key was there; `m.copy()` copies the map, when keys and values can be copied.
- `for (K k, ref V v in m)` can modify the values, never the keys. The order of the keys is not defined.
- `hash()` gives a `u64`, and the numbers and `bool` have `hash()` and `equals()` by themselves. For the floats, `-0.0` and `0.0` are the same key, and a `nan` is never found.
- `Key` uses `Self`, so it is only a constraint (D84): a number is a key only through a generic.

### D210. Files for each system: modules of one system and the language server
*2026-10-29.*
- In a folder, `x_linux.cro` without `x.cro` is a module of its own, which exists only on Linux.
- The language server analyzes the programs with the files of Windows.

### D211. The first modules of the library: std.fs, std.time and std.os
*2026-10-29.*
- `std.fs`: `readText`, `readBytes`, `writeText`, `writeBytes`, `appendText`, `exists`, `isFolder`, `remove` (a file, or an empty folder), `createFolder` and `listFolder`. A whole file is read or written at once; an error is an `Error` as `cannot read 'notes.txt': no such file or folder`.
- `std.time`: `now()` gives an `Instant`, with `elapsed()` and `since()`; a `Duration` has `nanoseconds`, `seconds()`, `milliseconds()` and `microseconds()`. `utcNow()` and `localNow()` give a `DateTime` with its fields, the offset in minutes and `text()` in ISO 8601. `sleep`, `msleep` and `usleep` take an integer.
- `std.os`: `name()`, `env(name)`, which gives `""` for a variable that is not set, `hasEnv(name)`, `processId()`, `currentFolder()` and `exit(code)`.

### D212. The library and the compiler: new built-in functions and embedded modules
*2026-10-29.*
- New built-in functions of the `unsafe` dialect: `systemCall` for Linux, whose arguments can be integers or addresses, `environment`, `exitProcess`, `decimalToFloat` and `hashBytes`.
- The modules of the library are files `library/std/*.cro`, embedded in `cromo` as the core.

### D213. Cromo, and cromolang on the web
*2026-10-29.* It completes D1. The language stays Cromo, and its command `cromo`. On the web, where *cromo* is a common word and close to Chrome, its name is **cromolang**: the domain `cromolang.org`, the accounts and the searches, as Go does with *golang* and Zig with `ziglang.org`. The site is in `site/`, with the logo: the tile of chromium, element 24, Cr.

### D214. Threads: an object to start and to wait for
*2026-10-29.* It resolves the open point of D149. The module `std.thread` has `Thread`, made from a lambda (D183), started with `start()` and waited for with `join()`, as in Java and close to `std::thread` of C++:
```
thread.Thread t = thread.Thread([ref left]() { sort(ref left) })
t.start()
sort(ref right)
t.join()
```

### D215. Threads of a block and free threads
*2026-10-29.* A thread whose lambda captures with `ref`, or reads an object without copying it, is a thread of its block, as that lambda (D184): it cannot be moved, returned or kept, and at the end of the block it is waited for. While it runs, from `start()` to `join()` or to the end of the block, the block cannot use what the thread has with `ref`, nor modify what it reads: the rule of the elements of a `for` (D101), checked by the compiler, so that two threads never write the same data. A thread that captures only with `move`, or values of simple types, is free: it owns its data, it can be moved and kept in lists, and it lives beyond its block.

### D216. Mutex
*2026-10-29.* `thread.Mutex<T>` holds its data, which are reached only while it is locked, inside a lambda: `total.lock([](ref i64 t) { t += 1 })`. Several threads of a block can read the same mutex, and `lock` gives `ref` to its data. In M15 it is the only means of synchronization: shared values with an atomic counter, channels and atomic integers can come later.

### D217. The exceptions of a thread
*2026-10-29.* An exception that leaves the lambda of a thread is thrown again by `join()`, as `std::future` of C++. A thread whose exception is never taken by `join()` stops the program, as an exception that leaves `main` (D121): its destructor waits for it but cannot throw (D113). A panic stops the whole program (D62).

### D218. Threads of a block: where they are declared, started and joined
*2026-10-29.*
- It is declared as a variable whose value is `thread.Thread(...)` with the lambda written there: `thread.Thread t = thread.Thread([ref v]() { ... })`.
- `t.start()` and `t.join()` are statements written directly in the block that declares `t`, not inside an `if` or a loop: so the compiler knows where the thread runs.
- It cannot be moved, assigned, returned, passed with `ref` or captured.
- Two threads can read the same variable at the same time; one that has it with `ref` excludes the others, and the start of the second is an error.

### D219. Starting and joining a thread: the panics and the stack
*2026-10-29.*
- `start()` twice, `join()` before `start()` and `join()` twice are a panic; for a thread of a block, a second `start()` in its block is already a compile error.
- A thread that is never started does nothing, and its destructor does not wait.
- The stack of a thread is that of the main thread: 1 MB on Windows, 8 MB on Linux, with a guard page.

### D220. Mutex: default value, copies, recursion and exceptions
*2026-10-29.*
- Its default value is unlocked, with the default value of its data: `thread.Mutex<i64> counter` is ready.
- It cannot be copied; it can be moved while no thread uses it.
- `lock` is not recursive: locking again the same mutex inside its lambda blocks the thread forever.
- If the lambda of `lock` throws, the data stay in the mutex and the mutex is unlocked, then the exception goes on.

### D221. Printing among threads, and their uncaught exceptions
*2026-10-29.*
- Each `print` and `println` writes its whole line: two threads never mix their pieces. `format` writes into its own string, without waiting for the others.
- A panic in a thread waits for a print in progress, then stops the program.
- An exception never taken by `join()` stops the program with `uncaught exception in a thread: <message>` and exit code 1.

### D222. Threads and the compiler: built-in functions and values passed by address
*2026-10-29.*
- New built-in functions of the `unsafe` dialect, for `std.thread`: `threadStart`, `threadWait`, `threadRethrow`, `threadUncaught`, `threadFree`, `mutexLock`, `mutexUnlock` and `selfAddress`.
- A `Mutex`, and every value that holds one, is always passed by address, and the compiler does not tell LLVM that a call leaves it unchanged: other threads write it.

### D223. Network I/O blocks the thread
*2026-10-29.* It resolves the point of the model of I/O for servers (D150). `accept`, `read` and `write` stop the thread that calls them, until they are done or their timeout. A server gives each connection to a thread (D215), which takes it with `move`. Lightweight tasks will come later under the same API, without changing the code; there is no `async`/`await`, which splits the functions in two kinds.

### D224. Listener and Connection
*2026-10-29.* The module `std.net` has two types, as Go and Rust: `net.listen(host, port)` gives a `Listener`, and its `accept()` gives a `Connection`, as `net.connect(host, port)` does. One cannot read from a listener nor accept from a connection. A socket is closed by its destructor (RAII).
```
net.Connection c = net.connect("example.com", 80)
c.writeText("GET / HTTP/1.0

")
list<u8> reply = c.read(4096)
```

### D225. Network errors and timeouts
*2026-10-29.* An error of the network is an exception, as for files (D16, D211). A timeout in milliseconds is set once on a connection or on a listener, `c.timeout(5000)`, and holds for each of its operations; passing it throws an exception. Without a timeout an operation waits as long as it takes.

### D226. Names and addresses
*2026-10-29.* `net.connect` and `net.listen` take a name or an address, IPv4 or IPv6. On Linux, without the C library, a resolver written in Cromo reads `/etc/hosts`, then asks the servers of `/etc/resolv.conf` with UDP; on Windows `GetAddrInfoW` of `ws2_32` asks the resolver of the system, which is part of Windows and not of a C library (D202).

### D227. The lambda of a thread modifies what it takes with `move`
*2026-10-29.*
It amends D187 for the threads: the lambda given to `thread.Thread(...)` runs once, so what it captures with `move` is its own and it can modify it, as in `thread.Thread([move client]() { serve(ref client) })` (D223). The other lambdas can run again, and their captures stay read-only.

### D228. Reading and writing on the network
*2026-10-29.*
- `c.read(most)` gives up to `most` bytes as soon as some arrive, and an empty list when the other side has closed the connection.
- `c.write(bytes)` and `c.writeText(text)` write everything before returning.
- `close()` closes before the destructor; an operation on a closed socket is an exception.
- `net.listen(host, 0)` asks the system for a free port, which `listener.port()` gives.
- `net.connect` tries the addresses of the name in turn; it waits for the connection as long as the system does, and the timeout of D225 holds from the connection on.

### D229. UDP, and the addresses of a name
*2026-10-29.*
- `net.udp(host, port)` gives a `Udp` bound to that address, with `send(host, port, bytes)`, `receive(most)`, which gives a `Datagram` with `data`, `host` and `port`, `timeout`, `port()` and `close()`.
- `net.resolve(host)` gives the addresses of a name as text, IPv6 written as in RFC 5952: `2001:db8::ff00:42:8329`.

### D230. The messages of the errors of the network
*2026-10-29.*
They say what could not be done and why: `cannot connect to example.com:80: connection refused`, `cannot read: timed out after 5000 ms`, `cannot find the host 'x': no such host`.

### D231. The resolver for Linux: what it reads and in which order
*2026-10-29.*
- `/etc/hosts` first; then the servers of `/etc/resolv.conf` in order, asking the addresses of IPv4 and then those of IPv6, two seconds each; `127.0.0.1` when the file names none.
- Only the records of the name asked count, following its aliases (CNAME): the server of WSL adds others among the answers.
- Not yet: the `search` domains for short names, and TCP for the answers that do not fit in UDP.
- A listener uses `SO_REUSEADDR`, so that a server started again can take its port at once.

### D232. Modules of the library that only the library imports
*2026-10-29.*
`std.winsock` declares `connect` and `listen` of ws2_32 with other names, apart from those of `std.net`; the programs cannot import it.

### D233. TLS with mbedTLS
*2026-10-29.* It resolves the point of the TLS library (D150). mbedTLS, a C library made for systems without the C library, is compiled with the clang of LLVM and included in `cromo` like the runtime (D58): the executables stay without dependencies (D19, D28). It has TLS 1.2 and 1.3, as client and as server, and X.509; the runtime gives it memory, random numbers and the time. The same code serves Windows and Linux.

### D234. The root certificates of the system
*2026-10-29.* A server is verified with the certificates of the system: on Windows the store ROOT, through crypt32, and Windows itself for the roots it has not downloaded yet (D246); on Linux the file of the distribution, as `/etc/ssl/certs/ca-certificates.crt`. They are updated with the system and include the authorities added by a company.

### D235. TLS in the API of the network
*2026-10-29.* `net.connectTls(host, port)` gives a `Connection` (D224) that encrypts: `read`, `write` and the timeouts stay the same. The certificate of the server and its name are always verified. A server writes `net.listenTls(host, port, certificate, key)`, with the texts in PEM, and its `accept()` gives connections that are already encrypted, whose handshake comes with their first read or write (D244). The client of HTTP comes with M18.

### D236. The module std.crypto
*2026-10-29.* The hashes SHA-256, SHA-512, and SHA-1 and MD5 for compatibility, with HMAC; secure random bytes from the system; base64, also for URLs, and hex; the ciphers AES-GCM and ChaCha20-Poly1305, and Ed25519 and X25519.

### D237. Cromo is distributed as an archive
*2026-10-29.* It amends D28 and D58. A version of Cromo is an archive for each system, as for Go and Zig: `bin/cromo`, which keeps LLVM and LLD inside, and next to it `lib/` with the standard library in `.cro` (`core.cro` and `std/`), the runtime and the C libraries of each target, as mbedTLS (D233), in `lib/windows` and `lib/linux`, and the extension for VS Code. `cromo` finds `lib/` from the folder of its executable. Nothing else is installed: no Visual Studio, no Windows SDK, no C library (D19, D28). The library can be read, the editor goes to its definitions, and it changes without rebuilding `cromo`.

### D238. The installation is one line
*2026-10-29.* A script for each system downloads the archive of the latest version and installs it for the user, without the rights of an administrator, in `~/.cromo` (`%USERPROFILE%\.cromo` on Windows), as rustup and Go do. It adds its `bin` to the `PATH`, and installs the extension if it finds VS Code:

```
irm https://cromolang.org/install.ps1 | iex          # Windows, PowerShell
curl -sSf https://cromolang.org/install.sh | sh      # Linux
```

### D239. `cromo update` installs next to the old version
*2026-10-29.* It amends D153. `cromo update` downloads the archive of the new version into `~/.cromo/versions/<version>`, next to the one in use, and then makes it the active version: the `cromo` that is running is never overwritten, and the old version stays until it is removed. How the active version is chosen, and how a download is checked, stay open with D153.

### D240. The functions of the runtime: `extern "cromo"`
*2026-10-29.* It amends D133. The standard library declares the C functions of the runtime, as those of TLS and of `std.crypto`, in a block `extern "cromo" { ... }`, as the functions of a DLL. Only the files of the library can write it: in a program it is an error. In the other blocks the names that start with `cromo_` stay reserved.

### D241. What a program does not use is not in the executable
*2026-10-29.* Every function of a program has internal linkage, and LLVM removes those that nothing calls, also at `-O0`. A `Connection` reaches mbedTLS only through its session, which `connectTls` and `listenTls` create: a program of `std.net` without TLS stays at about 44 KB, one with TLS is about 500 KB.

### D242. mbedTLS without what is not needed
*2026-10-29.* TLS 1.2 and 1.3 over TCP, without DTLS, renegotiation, DES, ARIA and Camellia, and without files, sockets or timers of its own. It is compiled from `third_party/` without the C library and without the Windows SDK: the few functions of C that it calls are in `runtime/tls/libc.c`.

### D243. TLS through memory, with the timeouts of the connection
*2026-10-29.* mbedTLS reads and writes its records in memory, and `std.net` moves them to and from the socket. `timeout()` works as with TCP, for each wait for the socket, also in the handshake. Like `connect`, `connectTls` has no timeout of its own.

### D244. The handshake of a server comes with the first read or write
*2026-10-29.* It specifies D235. `accept()` gives at once a `Connection` that encrypts; its handshake happens at its first `read()` or `write()`, in the thread that serves it, as in Go: a slow or hostile client does not keep `accept()` waiting. An error of the handshake is then the exception of that `read()` or `write()`.

### D245. The end of a connection with TLS
*2026-10-29.* `read()` gives an empty list when the other side sends close_notify, and also when the socket ends without it, as in Go: the protocols, as HTTP, know their lengths. `close()` and the destructor send close_notify after a handshake that went well, ignoring the errors.

### D246. On Windows, the roots that Windows downloads
*2026-10-29.* It specifies D234. Windows keeps only some roots on the disk (78 on the PC of the tests) and downloads the others when a chain needs them: `example.com` needs one that was not there. A chain that mbedTLS does not accept with the store ROOT is checked again by Windows, with `CertGetCertificateChain` and the rules of HTTPS for the host, as Go does. `bcrypt.dll` and `crypt32.dll` are loaded only by the programs that use them, from System32.

### D247. `SSL_CERT_FILE`
*2026-10-29.* When it is set, the file in PEM that it names replaces the roots of the system, on Windows and on Linux, as for OpenSSL, curl and Go; the tests use it to trust an authority of their own. On Linux, without it, the roots are in the first of the files of the distributions that Go knows, from `/etc/ssl/certs/ca-certificates.crt`.

### D248. No revocation
*2026-10-29.* OCSP and the lists of revoked certificates are not checked, as in Go: they need further connections, and the certificates of the web last a few months.

### D249. The interface of `std.crypto`
*2026-10-29.* It details D236. The bytes are `list<u8>`.
- `md5`, `sha1`, `sha256` and `sha512` of the data; `hmac(Hash, key, data)` with `enum Hash { Md5, Sha1, Sha256, Sha512 }`; `Hasher(Hash)` with `add()` and `finish()`, after which it starts again.
- `random(count)`, an exception if the system gives no random bytes.
- `hex()` in lowercase, `fromHex()` in any case; `base64()` with the padding, which `fromBase64()` requires; `base64Url()` without the padding, which `fromBase64Url()` accepts or not.
- `encrypt(Cipher, key, nonce, data, extra)` with `enum Cipher { AesGcm, ChaCha20Poly1305 }` gives the data encrypted followed by the tag of 16 bytes; `decrypt` with the same arguments gives the data back, or throws if they are not authentic. The nonce has 12 bytes and is chosen by the caller, as in the RFCs and in Go; the key has 16, 24 or 32 bytes for AES-GCM and 32 for ChaCha20-Poly1305.
- `ed25519PublicKey`, `ed25519Sign` and `ed25519Verify`: the private key is the seed of 32 bytes of RFC 8032, and `ed25519Verify` gives false also for sizes that are wrong. `x25519PublicKey` and `x25519`, which gives the shared secret and throws for a public key that makes it zero.
- A key of the wrong size, and a text that is not hex or base64, are exceptions, as the errors of the data (D225), not panics.

### D250. The messages of TLS
*2026-10-29.* As D230: `cannot connect to example.com:443 with TLS: the certificate is not for example.com`, `... has expired`, `... is not valid yet`, `... is not signed by an authority that the system trusts`; `cannot listen with TLS: the key is not the one of the certificate`.

### D251. Structs to JSON and back: the compiler generates the conversion
*2026-10-30.* It resolves the open point of JSON. `json.encode(value)` gives the text of a value, and `json.decode<T>(text)` gives a `T` back, without declaring anything, as in Go. The compiler generates the conversion of a struct the first time it is needed, as it does for `copy()` (D35): an object with a member for each field. A field whose type cannot be converted is a compile error that names it. Reflection at compile time, which would also serve printing (D38), is not needed for this: it would change D52.

### D252. An HTTP server is one function for every request
*2026-10-30.* `http.serve(host, port, handler)` waits for the requests and gives each one to `handler`, which receives `ref` the request (D258) and `ref` the response. Each connection is served on a thread (D223), so the handler runs on several threads at once. The handler chooses the route itself, with patterns: `req.matches("GET /users/{id}")`, then `req.param("id")`. What the requests share is captured by reading, as a `thread.Mutex` (D216).

### D253. `const func`: a function that several threads can call at once
*2026-10-30.* A parameter `const func(...)` receives a function that does not modify what it captures, as a lambda of C++ without `mutable`: a lambda that does not capture with `ref`, a named function, or a function value (D184), whose captures are read-only (D227). Then several threads can call it at the same time. The handler of `http.serve` is one (D252).

### D254. The HTTP client: functions, a connection for each request
*2026-10-30.* `http.get(url)`, `http.post(url, body, type)`, and `http.send(request)` for the other methods. Each request opens its own connection, with TLS for `https` (D235), and the redirects are followed. The reuse of the connections comes later, with an `http.Client`.

### D255. The numbers of JSON: integers and floats
*2026-10-30.* `json.Value` has `Int(i64)` for a number without a point or an exponent that fits in an `i64`, and `Float(f64)` for the others, as fcl and serde_json do: identifiers of 64 bits keep all their digits. A float can be read also from an `Int`.

### D256. Attributes on the fields: the names of JSON
*2026-10-30.* An attribute is written before a field with the syntax of C++11: `[[json("user_id")]] i64 id` gives the field another name in JSON, `[[json(skip)]] string passwordHash` leaves it out of the conversion (D251). It is a general means for the attributes that will come; for now `json` is the only one.

### D257. A null of JSON gives the default value
*2026-10-30.* Reading a type, `null` gives its default value (D51), as a member that is missing, also for the elements of a list and the values of a map, as in Go: there are no optionals (D47). Who needs to tell a null apart declares the field `json.Value`, asks `isNull()`, and converts it with `json.fromValue<T>(v)`.

### D258. The handler receives the request with `ref` too
*2026-10-30.* It amends D252: the handler is `const func(ref http.Request, ref http.Response)`, because `req.matches(...)` keeps in the request the parameters that `req.param(...)` then reads.

### D259. A thread cannot capture a parameter `func` that is not `const`
*2026-10-30.* It amends D215. The lambda of a thread cannot capture a parameter `func` that is not `const` (D253), also through the captures of nested lambdas: that function may modify what it captures while another thread reads it. Before, a lambda that captured with `ref` could reach several threads this way.

### D260. The details of the HTTP server
*2026-10-30.* It details D252.
- A fixed pool of 128 threads, changed with `server.threads(n)` before `serve`; each thread accepts from the same listener and serves one connection at a time, with keep-alive.
- It waits 5 s for the next request of a connection, and 30 s for each read and write.
- The head of a request is at most 64 KB, otherwise 431; the body at most 16 MB, otherwise 413. `Expect: 100-continue` is answered.
- HTTP/1.0 and HTTP/1.1; the others give 505. HEAD is answered without the body.
- An exception of the handler gives 500 and a line on the error output: `http: GET /fail: boom`.

### D261. The patterns of the routes
*2026-10-30.* It details D252. `"METHOD /path/{name}/{rest...}"`: the method can be left out and then any method goes; GET takes also HEAD. The other segments are compared exactly, also a final slash; `{rest...}` takes the rest of the path, and only at the end. The parameters are without their escapes. `param()` of a name that the pattern has not is an exception. `queryValue(name)` decodes `+` and `%XX`, and gives `""` without it.

### D262. The interface of `Request` and `Response`
*2026-10-30.* `write(text)`, `writeJson(text)` and `writeBytes(bytes, type)` add to the body and set the `Content-Type` if it is missing; `text()` reads the body. The JSON is written as `res.writeJson(json.encode(x))`, because there are no generic methods. `Headers` compares the names without case; a header with a new line is an exception.

### D263. The details of the HTTP client
*2026-10-30.* It details D254. 30 s for each read and write; `Connection: close` and `User-Agent: cromo`. At most 10 redirects; 303, and 301 or 302 after a method other than GET, become GET; `Authorization` and `Cookie` are dropped when the host changes. A status of error is a response, not an exception; the 1xx are skipped. No gzip and no user and password in the URL; `http.escape(text)` escapes a part of it.

### D264. Writing JSON
*2026-10-30.* It details D251. Compact with `encode`, with 2 spaces with `pretty`. A float is written with the shortest text that reads back the same, with `.0` when it is integral; NaN and infinity are an exception. Only the escapes that RFC 8259 requires. The keys of a map are sorted. All the fields are written, the private ones too, as `copy()` (D35). An enum is the name of its case. Fixed-size arrays and enums with data are not converted yet.

### D265. Reading JSON
*2026-10-30.* It details D251 and D255. RFC 8259 strictly, nested up to 512 levels; of a member written twice the last one wins. An integer field takes only an `Int` in its range: `1.0` is an error, as in Go. A `u64` above 2^63 is not read yet. A lone surrogate is an error. The errors say where: the line and the column for the text, the path of the value for the conversion: `at grid[1][1]: expected an integer, found a string`.

### D266. `json.Value` and `json.Data`
*2026-10-30.* The interface of `json.Value` is the one in language.md §21: `isNull`, `asBool`, `asInt`, `asFloat`, `asText`, `len`, `has`, `get`, `at`, `keys`, `set`, `add`, with `json.object()` and `json.array()`. `json.Data` is the interface that every convertible type satisfies by itself; a generic that converts its `T` writes `<T : json.Data>` (D52).

### D267. Additions to the parser
*2026-10-30.* `module.function<Types>(args)`, as `json.decode<User>(text)`. An attribute can be on its own line before the field. A method of an enum can return the enum itself: a constructor is a name followed by `(`.

### D268. `net.Listener.accept()` is `const`
*2026-10-30.* Several threads accept from the same listener (D260), which they capture by reading.

### D269. Lightweight tasks: `thread.Task`, with the API of `thread.Thread`
*2026-10-30.* It resolves the lightweight tasks of D152 and D223. `thread.Task` has the interface and the rules of `thread.Thread`: made from a lambda, `start()` and `join()`, tasks of a block and free tasks (D215, D218, D219), the exception given back by `join()` (D217). Many tasks run on a few threads of the system; a task that waits for the network, for `time.sleep`, for a mutex or for `join()` leaves its thread to the others, without `async`/`await` (D223). `thread.Thread` stays a thread of the system, for C code and for long work that blocks. It is the model of the virtual threads of Java 21.
```
thread.Task t = thread.Task([ref left]() { sort(ref left) })
t.start()
sort(ref right)
t.join()
```

### D270. The HTTP server gives each connection a task
*2026-10-30.* It amends D252 and D260. Each connection is served by its own task (D269), without a fixed limit: thousands of idle connections with keep-alive cost little memory. `server.threads(n)` is removed.

### D271. A call that blocks the thread of a task
*2026-10-30.* A task that calls something which blocks its thread of the system, as a file, a C function or the names of the hosts on Windows, keeps that thread; when it stays there more than about a millisecond while other tasks wait, the runtime gives them another thread, as Go does. Any C library works inside a task.

### D272. The stack of a task
*2026-10-30.* It details D219 for the tasks. A task has 1 MB of stack reserved, with a guard page, and the memory is taken page by page when it is used: an idle task costs a few KB. Code that runs in a thread runs in a task too.

### D273. `thread.Group`: a variable number of tasks
*2026-10-30.* A group starts tasks with `g.start(lambda)`, also in a loop, as a server does with a task for each connection; its tasks can read the variables declared before the group, and take others with `move`. Those variables stay constant until the end of the block of the group, which then waits for all its tasks. `g.wait()` waits for them earlier and throws again the first exception of a task. It is the structured concurrency of the `TaskGroup` of Swift and of the nurseries of Trio.
```
thread.Mutex<Stats> stats
thread.Group connections
for {
    net.Connection c = listener.accept()
    connections.start([move c, stats]() { serve(ref c, stats) })
}
```

### D274. The workers of the tasks
*2026-10-30.* It details D269. There are as many workers as logical processors: all the groups of processors on Windows, the affinity of the process on Linux. For now the number cannot be set.

### D275. How the runtime sees a call that blocks
*2026-10-30.* It details D271. The compiler does not mark the calls: every millisecond, while tasks wait and no worker is idle, the monitor gives another worker the place of one that has run the same task for the whole millisecond. It covers the files of Linux and any C function at no cost for each call; it counts also a long computation, so the workers added are at most 256 beyond the processors.

### D276. The stacks kept, and the limit of Linux
*2026-10-30.* It details D272. The stack of a task that ends is kept, with its memory, for the next task, up to 1024 stacks; the others give their memory back. On Linux every stack is two areas of the kernel, the guard page and the stack: with the usual limit, `vm.max_map_count` of 65530, about 32 000 tasks can live at once, as the threads of glibc. Beyond it, starting a task is a panic.

### D277. `main` is a thread of the system
*2026-10-30.* `main` is not a task: it waits as a thread, and starts tasks as any thread does.

### D278. The sockets do not block
*2026-10-30.* It details D223 and D269. Every socket of `std.net` does not block: a call that would wait returns, and the library waits for the socket, with the poller in a task and with `poll` in a thread, as before. `connect` waits without a timeout, as before.

### D279. The details of a group
*2026-10-30.* It details D273. The variables that its tasks read stay constant from the declaration of the group, not from its first `start()`. `wait()` throws the first exception of a task and the others are lost; an exception that no `wait()` takes stops the program, as D217. A group cannot be moved, assigned, returned, passed with `ref` or captured. After `wait()` it can start other tasks.

### D280. The messages of the tasks
*2026-10-30.* The errors and the panics say "task" for a task: `'values' is used by the task 'a' until a.join() (D215)`, `the task is already started (D269)`, `uncaught exception in a task: lost`.

### D281. The mutex among tasks and threads
*2026-10-30.* It details D216 and D220. A mutex taken by another spins briefly, then a task parks and a thread sleeps. It is not fair; D220 does not change.

### D282. The waits of the sockets
*2026-10-30.* On Linux epoll, edge-triggered. On Windows the polls of `\Device\Afd` on an IOCP, as libuv and mio do; without it, the tasks wait for a socket as threads, and the monitor gives the others a worker (D275).

### D283. The locks of the runtime on Linux
*2026-10-30.* They spin up to 100 times before sleeping, as the SRWLOCK of Windows does: they are held briefly, and a sleep and a wake for each would slow the tasks.

### D284. `sleep(0)` in a task
*2026-10-30.* It returns at once, as in a thread. There is no `thread.yield()`.

### D285. The manifest of a version
*2026-10-30.* It resolves where the archives are published (D153, D239). Each version has a manifest in JSON at `https://cromolang.org/release/<version>`, and `https://cromolang.org/latest` is a copy of the manifest of the last one. It gives the version, as a text, and for each system the address of the archive and its SHA-256:
```
{
    "version": "0.19.0",
    "archive": {
        "win_x86_64": {"package": "https://cromolang.org/package/win_x86_64-0.19.0.zip", "sha256": "5ab5...e9"},
        "linux_x86_64": {"package": "https://cromolang.org/package/linux_x86_64-0.19.0.tar.gz", "sha256": "0ef2...31"}
    }
}
```

### D286. A version is signed
*2026-10-30.* It resolves how a download is checked (D153, D239). Next to each manifest, at its address with `.sig`, is the signature with Ed25519 of its exact bytes, as minisign and signify do. `cromo` contains the public key: it checks the signature of the manifest, then the SHA-256 of the archive, and refuses a version that does not pass both. The private key never goes on the site, so a site that is attacked cannot give an archive of its own.

### D287. The archives: `.zip` on Windows, `.tar.gz` on Linux
*2026-10-30.* It details D237. As for Go: the scripts of the installation (D238) open them with the tools of the system, `Expand-Archive` of PowerShell and `tar` of Linux, and `cromo` decompresses only Deflate.

### D288. The active version is the link `~/.cromo/current`
*2026-10-30.* It resolves how the active version is chosen (D239). `~/.cromo/current` points to the folder `~/.cromo/versions/<version>` of the active version: a junction on Windows, which needs no administrator, and a symbolic link on Linux. The `PATH` holds `~/.cromo/current/bin`, which the installation adds once (D238); changing version changes the link.

### D289. `cromo update` and `cromo update <version>`
*2026-10-30.* It details D239. `cromo update` takes the last version from `latest`; `cromo update 0.18.0` takes that version from its manifest, also to go back to an older one. A version already installed only becomes the active one; another is downloaded into `~/.cromo/versions/<version>`, checked (D286) and unpacked, then becomes the active one (D288).

### D290. The archives now, the compiler in Cromo when the language is mature
*2026-10-30.* It details D147 and D152. M20 makes the archives of D237 from the repository: `cromo` reads `lib/` from its folder, it is built for Linux too, and a script makes the two archives, their manifest and its signature (D285–D287). The compiler written in Cromo stays the last milestone, when the language and its library are mature (D147); it will have to make the same archives. The archives do not wait for it: LLVM stays C++ in both compilers.

### D291. The compiler in Cromo reaches LLVM through its C API
*2026-10-30.* It details D44. The compiler written in Cromo calls the C API of LLVM (`LLVM-C`) through `extern` blocks, to read the IR, optimize it and write the object file, and LLD through a few lines of C++, since LLD has no C API; it is the way of Zig. All the code of the compiler is in Cromo. `cromo` contains the runtime of C++ of its system, as today: D19 holds for the programs that it compiles, not for `cromo` itself.

### D292. `cromo` for Linux is built in WSL with glibc
*2026-10-30.* With the official archive of LLVM 23.1.4 for Linux, with its static libraries, and the C++ compiler of WSL. `cromo` for Linux depends on the glibc of the system, as `rustc` does; a static `cromo` with musl would need LLVM built from its sources.

### D293. What an archive holds
*2026-10-30.* It details D237. `bin/`, `lib/` and `extension/` at the root of the archive, without a folder around them, so that it unpacks into `~/.cromo/versions/<version>` (D288).

### D294. Where `cromo` looks for `lib/`
*2026-10-30.* It details D237. In `../lib` from the folder of the executable, as in an archive, then in `lib` inside it, as in a build (`build/lib`). There is no copy of the library inside `cromo`: without `lib/` it stops, with `cannot find the library: lib/ is missing next to the folder of cromo (D237)`.

### D295. The names of the files of the library
*2026-10-30.* In the messages of a program, as a panic, a file of the library is named `cromo:std/fs.cro`, without the folder where `cromo` is installed; the errors of the compiler give its real path, which the editor can open.

### D296. The target given by default
*2026-10-30.* It amends D142: without `--target`, `cromo` compiles for the system it runs on.

### D297. The DLLs of Windows, from Linux
*2026-10-30.* It details D137. On Linux there is no folder of Windows where to check the C functions of a DLL: they are taken as the `extern` blocks declare them, and a function that is missing is found when the program starts on Windows.

### D298. `cromo run` on Linux
*2026-10-30.* It details D156. The program runs with `fork` and `execv`; ended by a signal, its exit code is 128 plus the signal, as the shells say.

### D299. `cromo` for Linux: glibc 2.29, and its size
*2026-10-30.* It details D292. It needs glibc 2.29 or later: the C++ runtime, libxml2 with ICU, zlib, lzma and zstd are inside it, and two functions of a newer glibc that LLVM uses are written in `cromo` itself. In the archive its symbols are stripped. The archive for Linux is larger than the one for Windows, 52 MB against 33, for the data of ICU and the backends of LLVM that LLD brings: to be reduced later.

### D300. The keys and the signatures
*2026-10-30.* It details D286. The private key is the seed of 32 bytes of RFC 8032 in hex, in a file outside the repository; the public key is its 32 bytes in hex, and a signature its 64 bytes in hex, in a file `.sig` next to the one it signs. `tools/sign.cro`, a program in Cromo, makes a key, never over an existing one, signs and checks.

### D301. The modules in the message of a missing one
*2026-10-30.* `the standard library has no module std.io: it has std.crypto, std.fs, ...` lists the modules found in `lib/std`, not a list written in the compiler.

### D302. The server of the language from a copy
*2026-10-30.* It details D170. On Windows the extension for VS Code runs `cromo lsp` from a copy of `cromo.exe` in the temporary folder, next to a junction `lib` to the library of the original, so that a build can write `cromo.exe` again while VS Code is open.

### D303. Making the archives
*2026-10-30.* `tools/package.ps1` writes `dist/` with the layout of the site (D285), so that publishing is copying it. It stops if the two builds of `cromo` are not of the version of `CMakeLists.txt`; without `-Key` it warns that the manifest is not signed, which `cromo update` would refuse.

### D304. The functions of text are methods of `string`
*2026-10-30.* `s.find("x")`, `s.split(",")`, `s.trim()`, `s.replace("a", "b")` and the others are methods of `string`, in the core of the library, without an import, as `len()` and `slice()` (D200); as in C++, Java and C#.
```
string line = "  a,b , c  "
list<string> parts = line.trim().split(",")
i64 at = line.find("b")
```

### D305. A text that is not found is -1
*2026-10-30.* `find()` gives the index in bytes of the first occurrence, or -1 if there is none, as `Index` of Go and `IndexOf` of Java and C#; `contains()` says whether there is one. There are no optionals (D47), and a search that finds nothing is not an error.

### D306. `upper()` and `lower()` change only ASCII
*2026-10-30.* As `toupper` of C: only A-Z and a-z change, and the rest of UTF-8 stays as it is. No tables of Unicode in the runtime; it is enough for the protocols and the formats.

### D307. `std.process`: `run()` and `start()`
*2026-10-30.* `process.run(program, args)` waits for the end of the program and gives its exit code, its standard output and its standard error. `process.start(program, args)` gives a `Process`, with `write()` to its standard input, `closeInput()`, `read()` from its standard output, `wait()` and `kill()`, for the programs that run long. An exit code other than 0 is a value; a program that cannot start is an exception.
```
process.Output r = process.run("git", ["status"])
println("{} {}", r.code, r.out)
```

### D308. The literals of a list: `[a, b, c]`
*2026-10-30.* As in Python, Go and C#. The type of the elements is the one expected, or else the one of the first element; an empty `[]` needs the type expected. The elements are given to the list as to `add()`: a value that needs it is moved or copied (D23). The parser tells it from a lambda, `[captures](...)`, whose `]` is always followed by `(`.
```
list<string> args = ["status", "--short"]
list<i64> primes = [2, 3, 5, 7]
list<f64> empty = []
```

### D309. The panics of the functions of text
*2026-10-30.* It details D304. `split("")`, `replace("", ...)` and `repeat()` of a negative count are a panic, as an index out of a string (D30); `find("")` is 0.

### D310. What `trim()` removes
*2026-10-30.* The spaces of ASCII: space, tab, line feed, vertical tab, form feed and carriage return; `trimStart()` and `trimEnd()` only at one end.

### D311. More searching, and `join`
*2026-10-30.* Besides `find`, `findFrom(part, from)` and `findLast(part)`. `join(parts, separator)` is a function of the core, not a method of a list, which would hold only for `list<string>`.

### D312. The details of `std.fs`
*2026-10-30.* `modified()` gives the seconds from 1970-01-01 UTC. `copy()` writes over the target, `rename()` replaces it. `createFolders()` makes the folders above too. `removeAll()` never follows a link, and does nothing when the path does not exist. `extension()` has its point, and is `""` for `.profile`. `joinPath()` uses the separator of the system; on Windows both `/` and `\` separate.

### D313. The output and the end of a `Process`
*2026-10-30.* It details D307. The errors of a program from `start()` go where the errors of this program go. The destructor of a `Process` closes its input and its output, then waits for its end. `run()` reads the errors on a thread of its own, so that a program that fills one pipe does not wait for ever.

### D314. The output of a program as text
*2026-10-30.* The bytes that are not UTF-8 become U+FFFD, since a program can write in the code page of a console; `string(bytes)` stays strict (D200).

### D315. SIGPIPE on Linux
*2026-10-30.* From the first program started, SIGPIPE is ignored: writing to a program that has ended is an exception, not a signal that ends this one. The children have SIGPIPE as by default.

### D316. Programs on Windows
*2026-10-30.* A child inherits only its three pipes (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), so that a program started at the same time by another thread does not keep them open. `kill()` ends it with the code 1. The arguments are quoted with the rules of the C runtime of Microsoft, and Windows finds the program in `PATH`, adding `.exe`.

### D317. The details of the literals of a list
*2026-10-30.* It details D308. A comma is allowed after the last element, and the elements can stand on several lines; an element with `ref` is an error.

### D318. A type is printable with a method `text()`
*2026-10-30.* It resolves the open point of D38. A struct or an enum with `string text() const` is printed by `{}` with the text it gives, and the format applies to that text, `{:>12}` as for a string; as `toString` of Java and `__str__` of Python.
```
struct Point {
    i64 x
    i64 y
    public string text() const {
        ret format("({}, {})", x, y)
    }
}
println("{} {:>12}", p, p)   // (1, 2)       (1, 2)
```

### D319. `{:?}`: the form of debug, generated by the compiler
*2026-10-30.* `{:?}` prints a value of any type with its parts, without writing anything: `Point { x: 1, y: 2 }`, `Circle(2.5)`, `[1, 2]`, a string between quotes. The compiler generates it as it generates the conversion to JSON (D251).
```
println("{:?}", p)        // Point { x: 1, y: 2 }
println("{:?}", [p, p])   // [Point { x: 1, y: 2 }, Point { x: 1, y: 2 }]
```

### D320. What `text()` is
*2026-10-30.* It details D318. `string text() const`, without parameters and visible where it is printed. An argument printed both with `{}` and with `{:?}` is an error: it is passed twice.

### D321. The forms of `{:?}`
*2026-10-30.* It details D319. A struct is `Name { field: value, ... }` with all its fields, the private ones too, as `copy()` (D35); without fields it is `Name`; a generic struct has its name without the type arguments, `Box { item: 7 }`. A case of an enum is `Circle(2.5)`, with its values in order. A list and an array are `[a, b]`, a map `{k: v}` in its order. A string is between quotes with the escapes of Cromo, and a float always has its point: `1.0`.

### D322. The format of `{:?}`
*2026-10-30.* `{:?}` takes the width and the alignment of a string: `{:>30?}`.

### D323. What `{}` does not print
*2026-10-30.* An array, a function, an interface and a `T` are not printed by `{}`; the message proposes `{:?}`. A struct or an enum without `text()` neither, and the message proposes both.

### D324. The syntax of the methods with type parameters
*2026-10-30.* It resolves D92. `R name<U>(...)`, in a struct, with the type arguments deduced as those of a function (D86) or written, `x.map<string>(f)`; a lambda does not help to deduce them. Their names cannot be those of the parameters of the struct. Only a method can have them, not a constructor; not a method of an interface, whose table has one entry for each method.

### D325. `map` and `filter` of a list
*2026-10-30.* `list<T>` has `map<U>(func(T) U f)` and `filter(func(T) bool keep)`, which copies the elements that it keeps.

### D326. `thread.Channel<T>`: a channel with a fixed capacity
*2026-10-30.* It resolves the channels of D216. `thread.Channel<T>(16)` holds up to 16 values: `send()` waits while it is full, `receive()` while it is empty, as the buffered channels of Go; `thread.Channel<T>(0)` hands every value over, the sender waiting for a receiver. A task that waits leaves its thread to the others (D269). The tasks of a group, and the threads of a block, read the same channel.
```
thread.Channel<string> lines = thread.Channel<string>(16)
thread.Group work
work.start([lines]() {
    for (string line in lines) {   // until close()
        println("{}", line)
    }
})
lines.send("a")
lines.close()
```

### D327. The end of a channel
*2026-10-30.* `close()` ends it: the values already in it can still be received, then `receive()` throws an exception, and `for (T v in channel)` ends by itself, which is the usual way to read a channel. `send()` to a closed channel throws. There are no optionals (D47).

### D328. `thread.Atomic<T>`: the order of the sequence only
*2026-10-30.* It resolves the atomic integers of D216. `thread.Atomic<T>` holds an integer or a bool, with `load`, `store`, `add`, `exchange` and `compareExchange`, all sequentially consistent, as `memory_order_seq_cst` of C++: simpler to explain, and almost always fast enough.

### D329. The default channel has capacity 0
*2026-10-30.* `thread.Channel<string> c` without a value is a channel of capacity 0, as `thread.Channel<string>(0)`: the runtime makes it at its first use, so the default value (D48) needs no constructor.

### D330. A negative capacity is a panic
*2026-10-30.* `thread.Channel<T>(-1)` stops the program with a panic, as an index out of range: it is an error of the program, not of its data.

### D331. A second `close()` does nothing
*2026-10-30.* In Go it is a panic; here two tasks that both end their work can close the same channel, which is harmless and simpler to explain.

### D332. `next(ref v)`: receiving without the exception
*2026-10-30.* `bool next(ref T v)` receives the next value into `v`, or returns false once the channel is closed and empty. It is the step of `for (T v in channel)`, and the way to receive without `try` when the end is expected.

### D333. The value of `for (T v in channel)` belongs to the body
*2026-10-30.* Unlike the elements of a list (D101), `v` is a local variable of the body: it can be modified or moved, for example sent to another channel. `ref` is an error, as there is nothing to modify in place. The channel is a variable or a field, read at each turn.

### D334. A sender and close()
*2026-10-30.* With capacity 0 the sender waits until a receiver has its value; if the channel is closed first, `send()` returns normally, and the value can still be received, or ends with the channel. A sender that waits on a full channel throws when it is closed, as `send()` to a closed channel (D327).

### D335. `thread.Atomic<T>(x)`, the default value, no copies
*2026-10-30.* The default value is 0 or `false`; `thread.Atomic<i64>(5)` starts from 5. An atomic cannot be copied, as a mutex: the copy would be a second value, which the other threads do not see.

### D336. What the operations of an atomic return
*2026-10-30.* `add` and `exchange` return the value before, as `fetch_add` and `exchange` of C++. `compareExchange(expected, desired)` returns only whether it wrote: C++ also writes the value seen into `expected`, here the program calls `load()`.

### D337. The errors of the atomics
*2026-10-30.* `thread.Atomic<f64>`, or any `T` that is not an integer or a bool, is an error where the type is written; `add` of an `Atomic<bool>` is an error at the call.

### D338. Debug information: lines and variables
*2026-10-30.* Every build without `--release` carries the lines, the functions and the local variables, with the types of Cromo, for a debugger: a PDB next to the `.exe` on Windows, for Visual Studio, WinDbg and VS Code, and DWARF in the executable on Linux, for gdb and lldb. With `-O2` a debugger can find some variables "optimized out"; `-O0` keeps them all.

### D339. Stack traces always, also with `--release`
*2026-10-30.* A panic, an exception that nobody catches and a stack overflow print the calls that led there, with function, file and line, as Go does. The compiler writes a compact table in the executable, which the runtime reads: no PDB or DWARF is needed when the program runs.

### D340. An uncaught exception prints the stack of its `throw`
*2026-10-30.* The exceptions of Cromo go up through the return values (D36), so when one reaches `main` the stack where it started is gone: `throw` saves the return addresses, and an exception that nobody catches prints them. `throw` without a value, in a `catch`, keeps the stack of the first one.

### D341. Walking the stack: frame pointers on Linux, the tables of Windows
*Changed by D375: Linux reads the unwind tables too.*
*2026-10-30.* On Linux every function keeps its frame pointer, and the runtime follows their chain: a few lines of C, about 1% slower, as Go and the distributions of Fedora and Ubuntu. On Windows LLVM places the frame pointer at a distance from the saved one that only the unwind tables know (`lea rbp, [rsp+128]`), so there the runtime asks `RtlCaptureStackBackTrace` of kernel32, which reads the tables that the ABI of Windows requires anyway: no cost when the program runs.

### D342. The form of a stack trace
*2026-10-30.* After the message, a line for each frame from the innermost, `    at main (main.cro:12)`, with the functions that the optimizer put inside others. The same frame one after the other, as in a recursion, is written once and then `    ... the same 57 more times`. The frames of the runtime and of C are left out; after 63 return addresses the trace ends with `    ...`.

### D343. The names in a stack trace
*2026-10-30.* As in the source: `main`, `Point.area`, `list<i64>.add`, `Point.Point` for a constructor and `Point.~Point` for a destructor, as in C++, `main.lambda` for a lambda written in `main`, and `fs.copy` for a function of another module; the functions of the program are written without the name of its module.

### D344. A stack overflow is a panic
*2026-10-30.* `panic: stack overflow` with the stack trace, and exit code 101, as the other errors of the program (D62). On Linux any other fault of memory, which only C code can cause, ends the program as usual, with its signal.

### D345. The files: full paths for a debugger, as written in a trace
*2026-10-30.* The debug information names each file with its full path, so that a debugger finds it from any folder; a stack trace writes it as the panics do (D62): as given to `cromo`, and `cromo:core.cro` for the library.

### D346. `this` and the fields of strings and lists in a debugger
*2026-10-30.* The object of a method has no name in Cromo (D70): a debugger shows it as `this`, as in C++. A `string` or a `list` shows its fields, `data`, `length` and `capacity`; a view of their text and of their elements, for Visual Studio and gdb, can come later.

### D347. `throw` keeps 63 return addresses, also with `--release`
*2026-10-30.* In front of the object of each exception, 512 bytes: the walk of the stack costs well under a microsecond with the frame pointers of Linux and a few microseconds with the tables of Windows, small next to the work that an exception reports.

### D348. On Windows the DWARF stays in the executables without `--release`
*2026-10-30.* The table of the stack traces is made from the DWARF, the PDB from CodeView. Without the DWARF a small executable built without `--release` was taken for malware by Bitdefender, which quarantined it and then blocked its path; with it, never, and neither the executables of `--release`, which have neither. So it stays, about as large as the code, which the debuggers of Windows ignore; `--release` has none.

### D349. A test is a function with `[[test]]`
*2026-10-30.* `[[test]] void sumsTheList() { ... }`: the syntax of the attributes is already there (D256), so there is no new keyword. A test takes no parameters and returns nothing; `cromo build` leaves it out, `cromo test` runs it.

### D350. The tests are in the file of the code and in `_test.cro` files
*2026-10-30.* A test can stay next to the code it checks, in the same file, or in a file `geometry_test.cro` next to the module, which is part of it and sees its private names, as the files of a system (D202). Only `cromo test` compiles the `_test.cro` files.

### D351. `assert(condition)`
*2026-10-30.* A built-in function, as `panic`: when the condition is false it stops with a panic that writes the condition as written and, for a comparison, the values of its sides, `assertion failed: total == 7 (total is 6)`, with the stack trace (D339). One function, that the compiler helps, instead of a family of checks.

### D352. Each test in a process of its own
*2026-10-30.* `cromo test` builds one executable with all the tests and starts it once for each test, in parallel on every processor: a panic or a stack overflow stops only its test. A static executable starts in about a millisecond.

### D353. What assert shows
*2026-10-30.* The values of the sides of a comparison of numbers or bools, each computed once, except the literals: `assertion failed: next(ref c) == 5 (next(ref c) is 2)`. For any other condition, the condition as written. `assert` works in any function, also with `--release`, as `panic`.

### D354. What cromo test writes
*2026-10-30.* A line for each test in the order of the files, `ok    name` or `FAIL  name`, then for a failed test what it wrote, indented, and its exit code when it is neither a panic nor an exception; at the end `3 passed, 1 failed`, without times, so that the output repeats. The output of the tests that pass is not shown. The exit code is 1 when a test failed; a test has no time limit for now.

### D355. Choosing the tests
*2026-10-30.* `cromo test shapes.cro area` runs the tests whose name contains `area`, or any of the names written after the file.

### D356. The names of the tests, and what cromo test takes
*2026-10-30.* A test of the program is named as its function, one of an imported module after the module, as `modules.geometry.areaOfSquare`. `cromo test` takes the options of `cromo run` (`--release`, `-O`) and runs the tests on the system where it runs; a program does not need `main` to be tested.

### D357. The values of an interface everywhere
*2026-10-30.* It changes D49 and D91. A variable, a field, a value returned, a `move` parameter, the elements of `list<Shape>` and the values of `map<string, Shape>` can be of an interface. `Shape s = Circle(2.0)` moves the circle to the heap, with the table of its methods, and the destructor of `s` frees it: a hidden allocation, as those of `list` and `string`. A struct becomes the value of an interface as it would become a variable of its own type: copied when it can be copied, otherwise with `move`. A read or `ref` parameter of an interface takes a struct as before, without allocating, and also the value of an interface.

### D358. The value of an interface is not copied
*2026-10-30.* It moves, with `move` or as the value of `ret`, as a struct that needs destruction (D75): copying it would need its type.

### D359. No way back to the type, for now
*2026-10-30.* Only the methods of the interface are called on its value; when the type is needed, a generic or an enum does it.

### D360. The initial value of a field is any expression
*2026-10-30.* It replaces D73. `list<string> names = ["a"]`, `i64 size = 4 * 1024`, the calls of functions and constructors: the expression is computed each time the default value of the struct is made, in the order of the fields, and it cannot read the other fields.

### D361. The default value of an interface is empty
*2026-10-30.* `Shape s` without a value, a field after `move` (D78) and a new value of a map have the empty value of the interface: calling a method on it is a panic, `the value of Shape is empty: it was moved, or never given`, as an index out of range (D30). So every type keeps its default value (D48).

### D362. A field moved away has the default of its type
*2026-10-30.* After `move obj.names` the field has the default of its type, an empty list, not its initial value (D78, D360): a move computes nothing and allocates nothing.

### D363. The initial value of a field cannot throw
*2026-10-30.* The default value of a struct is made in many places, a declaration without a value, a constructor, a new value of a map, none of which expects an exception: an initial value that can throw is an error.

### D364. The structs inside first
*2026-10-30.* The default value of a struct takes first the default values of the structs in its fields, in their order, then its own initial values, in the order of the fields.

### D365. The enums with data in JSON
*2026-10-30.* The case is the key of an object that holds its data, as serde of Rust by default: `Circle(f64 radius)` is `{"Circle": {"radius": 2.5}}`, and a case without data stays a text, `"Empty"`, as the enums without data. `[[json("circle")]]` before a case changes its name in JSON, as for a field (D256).

### D366. `http.Client` reuses the connections
*2026-10-30.* It completes D254. `http.Client client` has `get`, `post` and `send`, and keeps the connections open for each host, to use them again for the next requests, also from several tasks. `http.get` and the other functions keep a connection for each request.

### D367. gzip: the client, and the module std.gzip
*2026-10-30.* The client asks for gzip and decompresses the responses by itself. `std.gzip` has `compress` and `decompress` for the programs; the server does not compress by itself: a handler can, with std.gzip.

### D368. WebSocket, server and client
*2026-10-30.* A handler of the server turns its request into a WebSocket, a client opens one with a URL; both send and receive texts and bytes, answer the pings, and close.

### D369. `json.Value.UInt` for the integers above the i64
*2026-10-30.* It completes D255. An integer of JSON between 2^63 and 2^64 - 1 is `UInt(u64)`, so that a `u64` field reads all its digits; `asFloat` reads it too, `asInt` does not.

### D370. A fixed array in JSON
*2026-10-30.* An array of JSON with exactly the elements of the array (D196), each converted as its type; another length is an error that says both.

### D371. Reading an enum with data
*2026-10-30.* A text names a case without data; an object must have exactly one member, the case, whose data are read as the fields of a struct: a missing member, or `null`, leaves the default value (D257). A `null` in place of the enum gives its first case.

### D372. The connections of `http.Client`
*2026-10-30.* It details D366. At most 8 idle connections for each host, the oldest go first. A kept connection that fails before any byte of the response arrives, closed by the server meanwhile, is replaced by a new one, and the request goes again, also a POST: the server has not received it.

### D373. The details of gzip
*2026-10-30.* It details D367. The client sends `Accept-Encoding: gzip` only when the request names none, and then gives the body decompressed, without `Content-Encoding`; the decompressed body has the limit of the bodies (16 MB). `std.gzip.compress` writes the fixed codes of deflate, about a fifth larger than `gzip -9`, and `decompressUpTo(data, limit)` is for the data of the network.

### D374. The details of WebSocket
*2026-10-30.* It details D368. `req.webSocket(work)` runs `work` inside the handler, on the task of the connection, so that it can read what the handler reads, as a mutex; a request that is not one of WebSocket gets 400. `receive()` gives a `websocket.Message`, `Text` or `Binary`, joins the fragments and answers the pings, and throws `websocket.Closed`, with `code` and `reason`, after a close or when the connection ends (1006); `close()` sends 1000 and waits for the answer. Without `timeout(ms)` a WebSocket waits as long as it takes; a message is at most 16 MB.

### D375. Walking the stack on Linux: the unwind tables
*2026-10-30.* It changes D341. The frame pointers cost about 20% on code made almost only of calls, as fib, not 1%: Linux too walks the stack with the tables that LLVM writes for every function (`.eh_frame`), read by the runtime, as Windows does with its own. Nothing is paid while the program runs; the runtime has an interpreter of the rules of DWARF for them.

### D376. The packages are Git repositories with tags
*2026-10-30.* As in Go: a dependency is named by the URL of its repository, `github.com/anna/geometry`, and a version by a tag, `v1.2.0`. There is no central registry to keep; one can come later above the same names.

### D377. `cromo.mod`: the dependencies in lines
*2026-10-30.* As go.mod: `module github.com/anna/app`, then a line for each dependency, `require geometry github.com/anna/geometry v1.2.0`, with comments `//`. Trivial to read and to write by hand.

### D378. Minimal version selection
*2026-10-30.* As Go (MVS): of each package, the highest of the minimal versions that the project and its dependencies require; the same every time, without a lock file. A newer version comes in only when someone asks for it.

### D379. The packages arrive as archives over HTTPS
*2026-10-30.* `cromo` asks the forge for the archive of the tag, as GitHub, GitLab and Codeberg give it, and opens it with its own gzip and a reader of tar: no git is needed, and `cromo` stays one program that needs nothing else (D237). For the other hosts it runs the command `git`, if there is one. A proxy on cromolang.org, as proxy.golang.org, can come later above the same names (D376).

### D380. `cromo.sum`
*2026-10-30.* As go.sum: next to `cromo.mod`, the SHA-256 of each version used, written the first time that version is downloaded and checked every time after. A tag moved on the server, which gives other files, is an error. Both files go in the repository of the project.

### D381. `cromo get url[@version]`
*2026-10-30.* It adds a dependency to `cromo.mod`, or raises its version. `cromo build`, `cromo run` and `cromo test` download on their own what is missing. `cromo update` stays the update of `cromo` itself (D153, D289).

### D382. A package is a URL and a major version
*2026-10-30.* `v1` and `v2` of the same repository are two packages: a program can use both, with two names in `cromo.mod`, and minimal version selection (D378) chooses one version of each. The URL does not change with the major version, unlike the `/v2` of Go.

### D383. A dependency is the first name of an import
*2026-10-30.* The name of a `require` line is the first name of the path: `import geometry` is the root folder of the repository, `import geometry.shapes` its file `shapes.cro` or its folder `shapes` (D130). A folder of the project with the name of a dependency is an error. Every package reads its own `cromo.mod`, so the names are local to the package that writes them.

### D384. Where `cromo.mod` is
*2026-10-30.* In the folder of the main file, which is already the root of the imports (D130); without `cromo.mod` a program has no dependencies, as today. The `module` line of a dependency must be the URL it was downloaded from.

### D385. The versions
*2026-10-30.* Tags `vX.Y.Z`, without pre-releases for now; `v0` is a major version as the others. `cromo get url` takes the last version of the major version already required, or the last of all for a new dependency; `@v2` the last `v2.x.y`, `@v1.4.0` that one.

### D386. The tags, and the hosts
*2026-10-30.* The tags are read with the HTTP protocol of git (`info/refs?service=git-upload-pack`), a list of lines that every host gives without its API. The archive comes from GitHub, GitLab or Codeberg by its address; another host, or a private repository whose archive needs a login, goes through `git`, with its credentials.

### D387. The hash is of the files, not of the archive
*2026-10-30.* The SHA-256 of the sorted list of the paths, each with the SHA-256 of its file, as the `h1:` of Go: an archive that the forge makes again with another compression, as GitHub did in 2023, and the files that come from git give the same hash. A line is `github.com/anna/geometry v1.2.0 sha256:5ab5...`; `cromo` writes the file again with only the versions that the selection uses.

### D388. The cache
*2026-10-30.* `~/.cromo/packages/github.com/anna/geometry@v1.2.0` (D238), shared by all the projects, read-only: a version is unpacked once, checked, and never changed.

### D389. The name in `cromo get`
*2026-10-30.* The last name of the URL, or the one written after it: `cromo get github.com/anna/geometry@v2 geometry2`. A name already taken by another package is an error. A dependency is removed by deleting its line.

### D390. A local folder in place of a version
*2026-10-30.* `require geometry ../geometry`, to develop two packages together: no version and no line in `cromo.sum`. It holds only in the project being built; a downloaded package with such a line is an error.

### D391. The rest as in the modules of the project
*2026-10-30.* A package can `import unsafe` and bring its `.lib`, `.a` and DLLs next to its modules (D127, D136). Its symbols have the URL in front, and the major version from `v2`: `@"github.com/anna/geometry.area"`, `@"github.com/anna/geometry@v2.area"` (D134).

---

## Open

The points still to be defined, each when its milestone needs it (D152):
- the content of the standard library;
- where the completion of the other names goes among the milestones (D171, D179);
