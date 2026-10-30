# Calling C

Every system offers its services through C functions, and many libraries are written in C. Cromo calls them directly, without a C compiler and without header files, from modules that declare it with `import unsafe`. The rest of the program uses those modules through safe functions and structs, so the unsafe part stays small and easy to find.

## Declaring C functions

A module that writes `import unsafe` can declare C functions in an `extern` block, which names their library:

```cromo
import unsafe

extern "kernel32" {
    i32 MulDiv(i32 number, i32 numerator, i32 denominator)
    i32 QueryPerformanceFrequency(ref i64 frequency)
}

i32 main() {
    println("{}", MulDiv(10, 3, 4))
    i64 ticks
    QueryPerformanceFrequency(ref ticks)
    println("the counter runs: {}", ticks > 0)
    ret 0
}
```

```output
8
the counter runs: true
```

This program is for Windows, whose `kernel32.dll` has these functions.

- On Windows, `extern "name"` is `name.dll`. `cromo` reads the functions that the DLL exports while it compiles, so a misspelled name is an error of the compiler, not a crash at start-up. It writes the import library by itself: the Windows SDK is not needed. A `name.lib` next to the module is used instead, for example a static C library.
- On Linux, `extern "name"` is the static library `libname.a` next to the module.
- A DLL next to a module is copied next to the executable.

```cromo error
import unsafe

extern "kernel32" {
    i32 MulDvi(i32 number, i32 numerator, i32 denominator)
}

i32 main() {
    ret MulDvi(1, 2, 3)
}
```

```output
error: kernel32.dll has no function 'MulDvi'
```

## Types at the boundary

C functions take and return numbers, `bool` and `addr`, an address of C that Cromo code cannot follow. A `ref` parameter is a C pointer to a number or to a struct of numbers, which has the layout of C. The `BOOL` of Windows is an `i32`.

The C functions keep their name, and are private to their module: other modules cannot call them.

## Wrapping a resource

A resource of C, as a handle or a descriptor, belongs in a struct with a destructor. The rest of the program then uses the struct, and the resource is always released, as any other in Cromo:

```cromo norun
import unsafe

extern "kernel32" {
    i32 QueryPerformanceCounter(ref i64 count)
    i32 QueryPerformanceFrequency(ref i64 frequency)
}

// Measures the time from its creation.
public struct Stopwatch {
    i64 start
    i64 frequency

    public Stopwatch() {
        QueryPerformanceFrequency(ref frequency)
        QueryPerformanceCounter(ref start)
    }

    public i64 micros() const {
        i64 now
        QueryPerformanceCounter(ref now)
        ret (now - start) * 1000000 / frequency
    }
}
```

The module offers `Stopwatch`; the modules that import it do not write `import unsafe` and cannot touch the C functions.

## The unsafe dialect

`import unsafe` also gives the type `addr` and the built-in functions that work with raw memory, as `alloc` and `free`. The standard library is written in this dialect: `list`, `string` and `map` are structs of Cromo that manage their memory with these functions, behind a safe interface.

A program normally has no reason to use the dialect, except to call C. A search for `import unsafe` finds every place where the safety of the program depends on the care of its author.

## Summary

- `import unsafe` allows `extern` blocks of C functions, the type `addr` and raw memory.
- `extern "name"` is `name.dll` on Windows, `libname.a` on Linux; the functions are checked while compiling.
- C resources go in structs with destructors, and the rest of the program uses those.
