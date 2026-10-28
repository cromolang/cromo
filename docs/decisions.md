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
*2026-10-28.* Files containing Cromo code have the `.cro` extension.

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
*2026-10-28.* The parts of the standard library that touch raw memory or the operating system (`list`, `string`, allocator, I/O) are written in an `unsafe` dialect of Cromo, with an address type and intrinsic functions (`load`, `store`, system calls). User code cannot use it. Underneath there are a few primitives in LLVM IR and in assembly, such as `memcpy` and `memset` (D19).

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
*2026-10-28.* C functions are declared with `extern` and called from the `unsafe` dialect (D37). Normal code uses safe wrappers, without seeing the pointers of the C APIs. C resources (handles, descriptors) are encapsulated in structs with a destructor (RAII). The syntax is to be defined.

### D43. Concurrency postponed
*2026-10-28.* Threads, async and the like come after the first version.

### D44. Compiler in C++, then in Cromo
*2026-10-28.* The first compiler, the bootstrap one, is written in **C++**. The goal is to reach a Cromo compiler written in Cromo, which compiles itself.
- **Access to LLVM and LLD:** they are written in C++, so the compiler uses them directly and `cromo.exe` contains them from the start (D28), as Zig does.
- **Coverage:** the bootstrap compiler must support all of the language needed to write the compiler in Cromo.
- **Details** (C++ standard, build system, LLVM version, how to generate the IR): D53.

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
- **Interface-typed variables and fields:** not allowed in the first version, because they would require a hidden allocation. A generic is used instead.

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
*2026-10-28.* For now the initial value of a field (D51) must be a literal: a number, `true`, `false` or a character. The default value of a struct is then a constant known by the compiler. Expressions and constructor calls will come later.

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
- An interface cannot be the type of a variable, a field or a return value (D49), nor a type argument, until `list<Shape>` (M5).

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
- It is the only code that can use the `unsafe` dialect (D37). There is no syntax to mark it yet: it will come with modules (M7).

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
- Explicit numeric values of the cases (`Red = 1`) and conversions to numbers come with `extern` (M7, D42); printing with `{}` comes with the interface for printing (D38).

---
---

## Open

There are currently no open questions. Still to be defined later, when needed:
- bitwise operators (D59);
- the `extern` syntax (D42), before milestone M7;
- the symbol naming scheme for modules and generics (D39, D50);
- the interface for making user-defined types printable (D38);
- fixed-size arrays (`i32[4]`);
- the content of the standard library.
