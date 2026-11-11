# Appendix D: What Cromo checks, and what it does not

Cromo aims to be safe enough for everyday programs with rules that fit in a page, and not to be the safest language there is. This appendix says plainly what the language checks, when, what it leaves to the programmer, and what its simplicity costs.

## What is checked

| The mistake | In Cromo | When |
|---|---|---|
| Using memory that was freed, a reference that outlives its object | It cannot be written: there are no pointers, and `ref` exists only during a call or a block ([Ownership](ownership.md)) | Compiling |
| Freeing twice, or forgetting to free | The destructor of a value runs once, when its owner ends | Always |
| Using a value after it was moved | An error | Compiling |
| `null`, a value that was never set | There is no `null`, and every variable starts with a value | Always |
| Two arguments of a call that are the same object, a list changed while a `for` goes over it | An error; for two elements of a list, whose indexes are known only when the program runs, a panic | Compiling, running |
| An index outside a list, a string or an array | A panic, in debug and in release builds | Running |
| A division of integers by zero | A panic, in debug and in release builds | Running |
| An integer overflow, a shift count out of the width, a float that does not fit an integer | A panic in debug builds; with `--release` a defined result, the same on every system ([Numbers and operators](operators.md)) | Running |
| Two threads that change the same data | The compiler checks what the lambda of a thread or of a task captures: the data that they share go in a `Mutex`, a `Shared`, an `Atomic` or a `Channel` ([Threads, tasks and channels](concurrency.md)) | Compiling |
| A stack that overflows | The program stops, with a stack trace | Running |

A panic stops the program with a message and a stack trace. It never goes on with wrong data.

## What is not checked

- **Deadlocks.** Two threads that wait for each other wait for ever, and so does a `Shared` locked again inside its own `lock`.
- **Memory that is kept.** There are no leaks from forgetting to free, but a program can keep what it no longer needs: a list that only grows, and the value of a `thread.Shared`, which stays until the end of the program.
- **An index that is no longer the right one.** Where another language keeps a pointer, a Cromo program keeps an index into a list, or a handle, as the windows and the components of `std.gui`. After the element was removed, the index gives another element or a panic, and the handle a panic: a mistake in the logic of the program, not a corruption of its memory, but the compiler does not find it.
- **The logic.** A wrong result, a file not flushed, a lock held too long.

## The unsafe dialect and C

A file that writes `import unsafe` can reach raw memory and call C, and there the mistakes of C come back: [The unsafe dialect](unsafe.md) lists them. The guarantees above hold for a program as long as that code is right, and this includes:

- the standard library, which is written in the dialect: about 28 000 lines, in the folder `library` of the repository;
- the runtime, in C, and the libraries that it uses: SDL for the windows, mbedTLS for TLS, and those that read the images and the fonts.

A program that does not write `import unsafe` itself trusts only these.

## What simplicity costs

- **No references in fields or in variables.** A graph, a tree with links to the parents and an observer are written with indexes or handles, which are looked up, and checked, each time. In Rust they would be references with lifetimes, or `Rc` and `RefCell`.
- **Copies.** A value that owns memory is copied where it is given without `move`, as in C++. The program is right, and may be slower than it needs to be: `move`, and read and `ref` parameters, avoid the copies.
- **Checks when the program runs.** The bounds of the indexes are checked also in release builds. The integer overflow is checked only in debug builds: a program released with `--release` wraps around where its tests panicked.

## How sure are these guarantees

They are the rules of the design, enforced by a compiler that is young.

- There is no formal proof that the rules are sound, as there is for parts of Rust.
- The compiler has a suite of tests, more than 450 programs in `tests`, and every example of this guide is compiled and run when the guide is built. It has not yet been used by many people on large programs.
- A program without `import unsafe` that corrupts memory, or that compiles while it breaks one of the rules above, is a bug of Cromo. Please report it in the [issues](https://github.com/cromolang/cromo/issues) of the repository: these reports are the most useful ones.

## Summary

- Without `import unsafe`, a Cromo program has no dangling references, no double frees, no `null`, no values never set and no data races, and an index out of bounds stops it.
- Deadlocks, memory that is kept, and indexes and handles that are no longer the right ones are left to the programmer.
- The standard library and the runtime are trusted code, written in the unsafe dialect and in C.
- The price of the simple rules is indexes and handles in place of references that are kept, some copies, and checks when the program runs.
- The rules are not proven, and the compiler is young: a hole in them is a bug to report.
