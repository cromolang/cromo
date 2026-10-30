# Appendix C: Cromo for C++, Go and Rust programmers

Cromo borrows from each of these languages. This appendix lists, for each, what will feel familiar and what is different.

## From C++

**Familiar:** the syntax, braces and types before names; structs with constructors, methods, `const` methods and destructors; RAII and the order of destruction; exceptions with `try`, `catch` and `throw`; generics compiled for each type, as templates; `std::format` for formatting.

**Different:**

| C++ | Cromo |
|---|---|
| `return` | `ret` |
| `;` at the end of statements | the end of the line |
| pointers, `nullptr`, `new`, `delete` | no pointers and no `null`; the heap only through containers |
| `T&`, `const T&` | a parameter is read by default; `ref` modifies; no references outside parameters |
| copy constructors, implicit copies | `copy()`, always explicit for types that own resources |
| `std::move` | `move`, checked: a moved variable cannot be used |
| `class`, inheritance, `virtual` | `struct` and interfaces |
| templates with any type | generics constrained by interfaces, checked once |
| `while`, `do` | `for (condition)`, `for { }` |
| `switch` with fall-through | no fall-through; on enums every case must be handled |
| `#include`, headers | `import` of modules; no preprocessor |
| `int`, `long`, `size_t` | `i32`, `i64`, `u64`: fixed sizes; lengths are `i64` |
| undefined behavior on overflow, bounds | a panic in debug builds; bounds always checked |
| uninitialized variables | every variable has its default value |
| `std::thread`, `std::mutex` | `thread.Thread`, `thread.Mutex<T>` that holds its data |

## From Go

**Familiar:** one tool that builds, runs, tests and formats; lightweight tasks that block without `async`; channels; a standard library for networks and HTTP; static executables that cross-compile; and, once packages arrive, dependencies named by the URL of their repository, with minimal version selection.

**Different:**

| Go | Cromo |
|---|---|
| garbage collector | ownership and destructors; memory is freed at the end of the owner |
| `go f()` | `thread.Task` or `thread.Group`, whose captures the compiler checks |
| `defer` | destructors, and `finally` |
| `error` values and `if err != nil` | exceptions |
| `nil` | no `null`; every variable has a value |
| `x := 1` | `i64 x = 1`: the type is always written |
| capital letters for exported names | `public` |
| `select` | not yet |
| slices that share memory | lists that own their elements; `slice()` makes a copy |

## From Rust

**Familiar:** ownership checked by the compiler, moves by default for types that own resources, no `null`, no data races; enums with data and exhaustive matching; generics constrained by interfaces, as traits; LLVM; tests built into the tool.

**Different:**

| Rust | Cromo |
|---|---|
| references with lifetimes, the borrow checker | `ref` only for parameters: a reference never outlives a call, and there are no lifetimes |
| `&T`, `&mut T` | a read parameter, a `ref` parameter |
| `Clone`, `.clone()` | `copy()` |
| `Copy` types | simple types: numbers, `bool`, structs made only of them |
| `Result<T, E>` and `?` | exceptions; enums with data for results that are values |
| `Option<T>` | a conventional value, a `bool` with a `ref` parameter, or an enum |
| `match` with patterns | `switch` on cases, with the data named; no nested patterns |
| `Drop` | destructors |
| `impl Trait for Type` | `struct Type : Interface`, with the methods in the struct |
| `async`/`.await` | tasks that block, as in Go |
| `Arc<Mutex<T>>` | `thread.Mutex<T>` captured by the tasks that use it |
| macros | not available |

## In short

Cromo writes like C++, manages memory like Rust with fewer rules, and runs concurrent code like Go. When in doubt, the simplest rule wins.
