# Calling C

Every system offers its services through C functions, and many libraries are written in C. Cromo calls them directly, without a C compiler and without header files, from modules that write `import unsafe`. The rest of the program uses those modules through safe functions and structs, so the unsafe part stays small and easy to find.

This chapter covers the C functions: how they are declared, where their libraries are found, which types cross the boundary, and how strings and memory go to C and back. The rest of the dialect, as the raw memory and the module variables, is in [The unsafe dialect](unsafe.md).

## Declaring C functions

A module that writes `import unsafe` can declare C functions in an `extern` block, which names their library in quotes:

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

This program is for Windows, whose `kernel32.dll` has these functions. The examples of this chapter use the libraries of Windows, which every Windows has; on Linux the same rules hold for static libraries, as shown later.

The rules of the block:

- The library is a name without extension: `"kernel32"`, not `"kernel32.dll"`. Each system makes its own file name from it: `kernel32.dll` on Windows, `libkernel32.a` on Linux.
- Inside, each C function is written as a Cromo function without a body: the result type, the name of C, and the parameters with their types and names. The names of the parameters are needed, though C ignores them.
- A module can have several blocks, one for each library, and a block can be empty.
- The C function is called as any function of the module.

The compiler explains the mistakes:

```cromo error
import unsafe

extern "kernel32.dll" {
    u32 GetLastError() {
        ret 0
    }
}

i32 main() {
    ret 0
}
```

```output
error: write the library without extension, as "kernel32"
error: a C function has no body: it is in the library
```

Without `import unsafe` there are no `extern` blocks:

```cromo error
extern "kernel32" {
    u32 GetLastError()
}

i32 main() {
    ret 0
}
```

```output
error: an extern block needs 'import unsafe' at the top of the module
```

## Where the library is found

### Windows: DLLs

On Windows, `extern "name"` is `name.dll`. `cromo` writes the import library of the DLL by itself, from the functions that the blocks declare: neither the Windows SDK nor a `.lib` file is needed.

`name.dll` is looked for in the folders of the modules that declare the library, in the order in which the modules are loaded, and then in the system folder of Windows, `System32`. The first one found is used. A project with a DLL of its own keeps it next to the module that uses it:

```text
main.cro           import maths
maths/
    maths.cro      import unsafe, extern "maths" { ... }
    maths.dll
```

While it compiles, `cromo` reads the list of the functions that the DLL exports, so a misspelled name is an error of the compiler, not a crash at start-up:

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

The check is made for every function of the block, also those that nothing calls. A function that the DLL forwards to another DLL, as `HeapAlloc`, which `kernel32` takes from `ntdll`, counts as exported. A DLL that cannot be found is an error too, and so is a DLL that is not for x64:

```cromo error
import unsafe

extern "sqlite3" {
    i32 sqlite3_libversion_number()
}

i32 main() {
    ret sqlite3_libversion_number()
}
```

```output
error: cannot find the library 'sqlite3': there is no sqlite3.lib or sqlite3.dll next to the modules that use it
```

**A `.lib` next to the module.** When `name.lib` is in one of those folders, it is used instead of writing an import library: it can be a static C library, or the import library of a DLL with another name. Then `cromo` checks nothing, and a missing function is reported by the linker, with its message:

```text
cromo: linking failed:
lld-link: error: undefined symbol: callThrice
```

**The copy of the DLLs.** A DLL found next to a module is needed next to the executable when the program starts. So, after linking, `cromo` copies it to the folder of the executable, also when a `.lib` with the same name was used with it:

- only when the executable is in another folder, and the copy there is missing or different, in size or date: a program that is running keeps its DLL;
- never the DLLs of `System32`, which every Windows has;
- `cromo run`, which builds in a temporary folder, copies them there; `--emit-ir` writes no executable, and copies nothing;
- a copy that fails is an error of `cromo`.

**Compiling for Windows on Linux.** `cromo` on Linux builds for Windows too, with `--target windows`, but there it has no `System32` in which to read the DLLs of the system. Their functions are taken as the blocks declare them, without the check: a misspelled name compiles, and is found only when the program starts on Windows, which refuses to run it, because the entry point is not found in the DLL. Compiling on Windows finds the same mistake at once.

### Linux: static libraries

When compiling for Linux, `extern "name"` is the static library `libname.a`, looked for in the folders of the modules that declare the library, in the order in which they are loaded, as on Windows. Its code goes into the executable, which needs nothing next to it. When it is not found, the compiler says so:

```text
error: cannot find the library 'maths': there is no libmaths.a next to the modules that use it
```

`cromo` does not read the names of a static library: a missing function is reported by the linker, which often suggests the right one:

```text
cromo: linking failed:
ld.lld: error: undefined symbol: sumAl
>>> did you mean: sumAll
```

Shared libraries, the `.so` files, are not supported on Linux yet.

A module that calls the DLLs of Windows compiles only for Windows. When a program must run on both systems, the calls go in the files of each system, as `clock_windows.cro` and `clock_linux.cro` next to `clock.cro`, which offer the same functions: see [Modules and projects](modules.md).

For Android, `libname.a` is looked for in the same way; there a C library can always call the C library of Android.

### The C library on Linux

A Cromo program for Linux is normally a static executable that does not use the dynamic loader: it talks to the kernel by itself, and runs on any Linux of its processor, x64 or ARM64.

A C library, though, often calls the functions of the C library of the system, as `malloc`, `snprintf` or `dlopen`. When the static libraries of the `extern` blocks do, `cromo` links the executable with glibc by itself: the program is then an executable for the dynamic loader of the system, which loads glibc when it starts. There is nothing to write; it works also when compiling for Linux from Windows. Every program with a window is in this case, since the library that draws the windows calls the C library.

Such an executable:

- needs glibc 2.29 or later where it runs, as Ubuntu 20.04 and the distributions after it;
- does not run on the distributions built on musl, as Alpine;
- can be made only for x64, for now: with `--target linux-arm64`, a C library that calls the C library cannot be linked, and the linker reports its functions, as `undefined symbol: malloc`.

The programs whose C libraries do not call the C library, and all the programs without C libraries, stay static.

## Types at the boundary

A C function takes and returns a few kinds of values, the ones that C and Cromo store in the same way:

| Cromo | C |
|---|---|
| `i8`, `i16`, `i32`, `i64` | `int8_t` ... `int64_t`: `int` is an `i32`, `long long` an `i64` |
| `u8`, `u16`, `u32`, `u64` | `uint8_t` ... `uint64_t`: `size_t` is a `u64` |
| `f32`, `f64` | `float`, `double` |
| `bool` | `bool` of C, one byte |
| `char` | `char`, one byte |
| `addr` | any pointer: `void*`, `const char*`, a handle |
| `ref T` | `T*`, the address of a Cromo variable |

- The `BOOL` of Windows is not the `bool` of C: it is an `int`, so an `i32` in Cromo.
- The `long` of C is 32 bits on Windows and 64 bits on Linux: write the Cromo type of the system.
- `addr` is an address without a type: Cromo code reads and writes the memory behind it only with the functions of [The unsafe dialect](unsafe.md). Without `import unsafe`, the type does not even exist.

A `ref` parameter gives the C function the address of a variable, of a field or of an element: a number, a `bool`, a fixed-size array of them, or a struct made of them, which can contain other such structs and arrays. A struct has the layout of C, with its fields in order and the same alignment, so the structs of a C API are written field by field:

```cromo
import unsafe

extern "kernel32" {
    i32 FileTimeToSystemTime(ref FileTime time, ref SystemTime result)
}

// FILETIME of Windows: the 100 ns since 1601, in two halves.
struct FileTime {
    u32 low
    u32 high
}

// SYSTEMTIME of Windows.
struct SystemTime {
    u16 year
    u16 month
    u16 dayOfWeek
    u16 day
    u16 hour
    u16 minute
    u16 second
    u16 milliseconds
}

i32 main() {
    u64 ticks = 125911584000000000  // 2000-01-01 at midnight
    FileTime time
    time.low = u32(ticks & 0xFFFFFFFF)
    time.high = u32(ticks >> 32)
    SystemTime date
    if (FileTimeToSystemTime(ref time, ref date) != 0) {
        println("{}-{:02}-{:02}, day {} of the week", date.year, date.month, date.day, date.dayOfWeek)
    }
    ret 0
}
```

```output
2000-01-01, day 6 of the week
```

The address lasts for the call only, as every `ref` of Cromo: the C function must not keep it after it returns. Memory that C keeps, as a buffer that a library fills later, is allocated with `alloc` or is a module variable, both in [The unsafe dialect](unsafe.md).

What cannot cross the boundary is a compile error:

```cromo error
import unsafe

struct Pair {
    i32 a
    i32 b
}

struct Holder {
    string name
}

extern "kernel32" {
    void TakesText(string text)
    void TakesMove(move i32 x)
    void TakesCallback(func(i32) i32 f)
    Pair ReturnsPair()
    void TakesHolder(ref Holder h)
}

i32 main() {
    ret 0
}
```

```output
error: a C function takes numbers, bool, addr and 'ref' to numbers or structs of numbers, not string
error: a C function has no 'move' parameters
error: a C function takes numbers, bool, addr and 'ref' to numbers or structs of numbers, not func(i32) i32
error: a C function returns a number, bool or addr, not Pair
error: 'ref' to a C function is a pointer to a number or to a struct of numbers, not Holder
```

Not supported yet:

- strings, lists, maps and enums: a string goes to C with `cstring()`, shown below, an enum as its number, `i32(color)`;
- structs passed or returned by value: pass them with `ref`;
- a struct with an `addr` field, and `ref addr`: for a C struct that holds pointers, allocate its bytes and fill them with the functions of [The unsafe dialect](unsafe.md);
- functions given to C, as callbacks: C can call a Cromo function only by its name, with `[[export]]`, shown below;
- variadic functions, as `printf`: the `...` of C cannot be written;
- generic C functions, and `move` parameters.

## Names and visibility

The C functions keep their name of C, and are private to their module, which offers safe functions instead: another module cannot call them, not even with the name of the module in front.

```cromo file=clock.cro
import unsafe

extern "kernel32" {
    i32 QueryPerformanceCounter(ref i64 count)
    i32 QueryPerformanceFrequency(ref i64 frequency)
}

// The microseconds of a clock that never goes back.
public i64 micros() {
    i64 count
    i64 frequency
    QueryPerformanceCounter(ref count)
    QueryPerformanceFrequency(ref frequency)
    ret count / frequency * 1000000 + count % frequency * 1000000 / frequency
}
```

```cromo error
import clock

i32 main() {
    i64 count
    QueryPerformanceCounter(ref count)
    ret 0
}
```

```output
error: 'QueryPerformanceCounter' of clock is private to its module: it is not marked public
```

So the block is never public, and neither is a function in it:

```cromo error
import unsafe

public extern "kernel32" {
    u32 GetLastError()
}

extern "kernel32" {
    public u32 GetTickCount()
}

i32 main() {
    ret 0
}
```

```output
error: an extern block is not public: the module offers safe functions instead
error: the C functions are private to the module: it offers safe functions instead
```

- Several modules can declare the same C function, each for itself, with the same signature. A different signature is an error: `the C function 'GetLastError' is already declared with another signature, in clock`. The modules of the standard library that the program uses count too: `std.fs` declares the handles of Windows as `i64`, so a program that uses it and declares `CloseHandle(addr handle)` gets the error `... in std.fs`. Inside one module a C function is declared once.
- A C function and a Cromo function of the same module cannot have the same name.
- The names that start with `cromo_` are reserved for the runtime of Cromo, whose functions only the standard library declares, in a block `extern "cromo"`. A program that uses that library name, or a name with that start, gets an error.
- In a file with `import unsafe`, the names of the built-in functions of the dialect, as `alloc`, `free` and `offset`, always mean the built-ins. A C function with one of those names, as the `free` of the C library, is never called: the built-in `free` runs instead.

## Strings and memory

A string of Cromo is not a string of C: it knows its length and has no zero at the end. Two functions, both only in the files with `import unsafe`, cross the boundary:

- `s.cstring()` gives an `addr` to a copy of the string with a zero at the end, as the C functions want it. The copy is destroyed at the end of the statement, so it is written in the call itself; the string does not change. A zero byte inside the string ends it, for C.
- `string(data, length)` makes a string from `length` bytes at `data`: a copy, which C can then free or reuse. The bytes must be UTF-8, as for `string(bytes)`; when they are not, it throws an `Error` that says where they stop being UTF-8.

Memory that C fills is allocated with `alloc` and given back with `free`. A struct with a destructor frees it on every path, also when an exception leaves the function:

```cromo
import unsafe

extern "kernel32" {
    i32 SetEnvironmentVariableA(addr name, addr value)
    u32 GetEnvironmentVariableA(addr name, addr buffer, u32 size)
}

// Memory for C, freed when the variable ends.
struct Memory {
    addr data

    Memory(i64 size) {
        data = alloc(size)
    }

    ~Memory() {
        free(data)
    }
}

// The value of a variable of the environment, "" when it is not set.
string variable(string name) {
    Memory buffer = Memory(1024)
    u32 length = GetEnvironmentVariableA(name.cstring(), buffer.data, 1024)
    if (length >= 1024) {
        throw Error(format("the value of {} is too long", name))
    }
    ret string(buffer.data, i64(length))
}

i32 main() {
    SetEnvironmentVariableA("GREETING".cstring(), "hello from C".cstring())
    println("[{}]", variable("GREETING"))
    println("[{}]", variable("NOT_SET_ANYWHERE"))
    ret 0
}
```

```output
[hello from C]
[]
```

Both strings given to `SetEnvironmentVariableA` live until the end of its statement, which is all that C needs. The standard library already reads the environment with `os.env(name)`; the example shows how such a function is written. The functions of Windows whose name ends with `A` read the text in the code page of the system, so they suit ASCII; for any text, the functions ending with `W` take UTF-16.

`cstring()` outside a file with `import unsafe` is an error:

```cromo error
i32 main() {
    string word = "hello"
    word.cstring()
    ret 0
}
```

```output
error: cstring() is for the C functions: the module must write 'import unsafe'
```

The compiler does not stop a program from keeping the `addr` of `cstring()` in a variable, but after the statement it points to memory already freed: a mistake that the dialect leaves to its author.

## Wrapping a resource

A resource of C, as a handle or a descriptor, belongs in a struct with a destructor. The rest of the program then uses the struct, and the resource is always released, as any other in Cromo:

```cromo file=logfile.cro
import unsafe

extern "kernel32" {
    addr CreateFileA(addr name, u32 access, u32 share, addr security, u32 creation, u32 flags, addr template)
    i32 WriteFile(addr file, addr data, u32 size, ref u32 written, addr overlapped)
    i32 CloseHandle(addr handle)
}

// A file open for writing, closed when the variable ends.
public struct LogFile {
    addr handle

    public LogFile(string path) {
        // GENERIC_WRITE, no sharing, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL
        handle = CreateFileA(path.cstring(), 0x40000000, 0, addr(0), 2, 0x80, addr(0))
        if (handle == addr(-1)) {
            throw Error(format("cannot create {}", path))
        }
    }

    ~LogFile() {
        CloseHandle(handle)
        println("closed")
    }

    public void write(string text) {
        u32 written
        if (WriteFile(handle, text.cstring(), u32(text.len()), ref written, addr(0)) == 0) {
            throw Error("cannot write")
        }
    }
}
```

The module offers `LogFile`; the modules that import it do not write `import unsafe`, and cannot touch the handle or the C functions:

```cromo
import logfile

void writeLog() {
    LogFile log = LogFile("log.txt")
    log.write("first line\n")
    println("written")
}

i32 main() {
    writeLog()
    println("after writeLog")
    try {
        LogFile bad = LogFile("no/such/folder/log.txt")
    } catch (Error e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
written
closed
after writeLog
cannot create no/such/folder/log.txt
```

- The handle is closed where `log` ends, also when an exception leaves `writeLog`.
- A constructor that throws leaves no object, so the destructor never sees an invalid handle.
- `LogFile` has a destructor and no `copy()`, so it cannot be copied, only moved: the handle is closed once.

The compiler checks that the rest of the program uses the struct correctly; only the few lines of the module that talk to C depend on the care of their author.

## C that calls Cromo

C cannot receive a Cromo function as a value, but a static C library linked with the program can call a Cromo function by its name, when the function is exported with `[[export("name")]]`:

```cromo ignore
import unsafe

extern "twice" {
    i64 callTwice(i64 x)  // in twice.lib, which calls twice_from_cromo
}

[[export("twice_from_cromo")]]
i64 twice(i64 x) {
    ret x * 2
}
```

```c
long long twice_from_cromo(long long x);

long long callTwice(long long x) {
    return twice_from_cromo(x) + 1;
}
```

The rules of `[[export]]` are in [The unsafe dialect](unsafe.md).

## Summary

- `import unsafe` allows `extern "name" { ... }` blocks of C functions, written as Cromo functions without a body.
- On Windows `extern "name"` is `name.dll`, next to the modules or in `System32`, whose functions are checked while compiling; a `name.lib` next to the module is used instead, without the check. The DLLs next to the modules are copied next to the executable.
- On Linux it is `libname.a` next to the modules, checked by the linker. When the C libraries call the C library, the executable is linked with glibc, and needs glibc 2.29 or later; not yet on ARM64.
- From Linux, the functions of the Windows DLLs are not checked: a missing one stops the program when it starts.
- Numbers, `bool`, `addr` and `ref` to numbers or to structs of numbers cross the boundary; strings go with `cstring()` and come back with `string(data, length)`.
- The C functions keep their name and are private to their module; C resources go in structs with destructors, and the rest of the program uses those.
