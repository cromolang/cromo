# Appendix C: Cromo for C++, Go and Rust programmers

Cromo borrows from each of these languages. This appendix lists, for each, what will feel familiar and what is different, with the chapter that explains it.

## From C++

**Familiar:** the syntax, braces and types before names; structs with constructors, methods, `const` methods and destructors; RAII and the order of destruction; exceptions with `try`, `catch` and `throw`; generics compiled for each type, as templates; lambdas with their captures in square brackets; `c ? a : b`; attributes in double square brackets; `std::format` for formatting.

**Different:**

| C++ | Cromo |
|---|---|
| `return` | `ret` |
| `;` at the end of statements | the end of the line; one statement per line ([Common concepts](common-concepts.md)) |
| pointers, `nullptr`, `new`, `delete` | no pointers and no `null`; the heap only through containers. Raw addresses exist only in the unsafe dialect ([The unsafe dialect](unsafe.md)) |
| `T&`, `const T&` | a parameter is read by default; `ref` modifies; a reference lives only during a call or a loop, never in a field ([Ownership](ownership.md)) |
| copy constructors, implicit copies | implicit copies too, made by `copy()`, generated or written by hand ([Structs](structs.md)) |
| `std::move` | `move`, checked: a moved variable cannot be used ([Ownership](ownership.md)) |
| `class`, inheritance, `virtual` | `struct` and interfaces ([Interfaces and generics](interfaces-generics.md)) |
| templates with any type | generics whose body is checked where it is written, with what their constraints promise |
| overloading of functions and operators | not available: each function has its own name |
| `auto` | the type is always written |
| `while`, `do` | `for (condition)`, `for { }` |
| `switch` with fall-through | no fall-through; on enums every case must be handled ([Enums and switch](enums.md)) |
| `#include`, headers | `import` of modules; no preprocessor ([Modules and projects](modules.md)) |
| `int`, `long`, `size_t` | `i32`, `i64`, `u64`: fixed sizes; lengths are `i64` |
| implicit conversions between numbers | only those that lose nothing, as `i32` to `i64`; signed and unsigned do not mix ([Numbers and operators](operators.md)) |
| `std::string`, `std::vector`, `std::unordered_map` | `string`, `list<T>`, `map<K, V>` ([Lists, maps and text](collections.md)) |
| undefined behavior on overflow, bounds | overflow is a panic in debug builds and wraps around with `--release`; bounds are always checked |
| uninitialized variables | every variable has its default value |
| `std::thread`, `std::mutex` | `thread.Thread`, `thread.Mutex<T>` that holds its data ([Threads, tasks and channels](concurrency.md)) |
| `extern "C"`, headers of C | `extern "library" { ... }` blocks, without a C compiler ([Calling C](c-interop.md)) |

## From Go

**Familiar:** one tool that builds, runs, tests and formats; executables that need nothing installed and cross-compile; lightweight tasks that block without `async`; channels; a standard library for networks and HTTP; the dialect behind `import unsafe`; and packages, which come later.

On Linux the executables are static, unless the C libraries of the program call the C library of the system: then `cromo` links them with glibc, as Go does with cgo, and they need glibc 2.29 or later ([Calling C](c-interop.md)). Every program with a window is in this case.

**Different:**

| Go | Cromo |
|---|---|
| garbage collector | ownership and destructors; memory is freed at the end of the owner ([Ownership](ownership.md)) |
| `go f()` | `thread.Task` or `thread.Group`, whose captures the compiler checks ([Threads, tasks and channels](concurrency.md)) |
| `defer` | destructors, and `finally` |
| `error` values and `if err != nil` | exceptions ([Errors](errors.md)) |
| `panic` and `recover` | `panic` stops the program and cannot be caught; exceptions are for the errors that the caller handles |
| `nil` | no `null`; every variable has a value |
| `x := 1` | `i64 x = 1`: the type is always written |
| capital letters for exported names | `public` |
| interfaces satisfied implicitly | a struct names its interfaces: `struct File : Reader` ([Interfaces and generics](interfaces-generics.md)) |
| `for i, x := range v` | `for (T x in v)`, `for (K key, V value in m)` |
| `select` | not available |
| slices that share memory | lists that own their elements; `slice()` of a string makes a copy |
| cgo | `extern` blocks, without a C compiler ([Calling C](c-interop.md)) |

## From Rust

**Familiar:** ownership checked by the compiler, moves by default for types that own resources, no `null`, no data races outside the unsafe dialect; enums with data and exhaustive matching; generics constrained by interfaces, as traits; LLVM; tests built into the tool, with `[[test]]` as `#[test]`; an unsafe dialect for the code that the compiler cannot check.

**Different:**

| Rust | Cromo |
|---|---|
| references with lifetimes, the borrow checker | `ref` only for parameters, captures, the elements of a `for` and the data of a `case`: a reference never outlives the call or the block, and there are no lifetimes ([Ownership](ownership.md)) |
| `&T`, `&mut T` | a read parameter, a `ref` parameter |
| `Clone`, `.clone()` | `copy()`, called by itself where a value is given without `move` |
| `Copy` types | simple types: numbers, `bool`, and the structs made only of them, without a destructor |
| `Result<T, E>` and `?` | exceptions; enums with data for results that are values ([Errors](errors.md)) |
| `Option<T>` | a conventional value, a `bool` with a `ref` parameter, or a generic enum of your own |
| `match` with patterns | `switch` on cases, with the data named; no nested patterns ([Enums and switch](enums.md)) |
| `Drop` | destructors |
| `impl Trait for Type` | `struct Type : Interface`, with the methods in the struct |
| `String`, `Vec<T>`, `HashMap<K, V>` | `string`, `list<T>`, `map<K, V>` |
| `async`/`.await` | tasks that block, as in Go |
| `Arc<Mutex<T>>` | `thread.Mutex<T>` captured by the tasks that use it |
| `unsafe { }` blocks | `import unsafe`, for a whole file, which offers a safe interface ([The unsafe dialect](unsafe.md)) |
| `static mut` | module variables, only with `import unsafe` |
| `#[no_mangle] extern "C" fn`, `#[cold]` | `[[export("name")]]`, `[[cold]]` |
| macros | not available |
| Cargo | the `cromo` command and `cromo.mod` ([Appendix B: The cromo command](appendix-command.md)) |

## Summary

- Cromo writes like C++: braces, types before names, structs with constructors and destructors, exceptions, templates as generics.
- It manages memory like Rust, with fewer rules: ownership and moves checked by the compiler, references that never outlive a call or a loop, no lifetimes.
- It runs concurrent code like Go: tasks that block, channels, one tool for everything.
- When in doubt, the simplest rule wins.
