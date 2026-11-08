# Errors

Cromo separates two kinds of errors:

- **Errors that a program expects**, as a file that does not exist or a connection that drops: they are **exceptions**, which a caller can catch and handle. An exception that nobody catches stops the program with exit code 1.
- **Programming errors**, as an index out of a list or an integer overflow: they are **panics**, which stop the program with exit code 101, because a program that has broken its own rules cannot know what to do next.

Both print the calls that led there, a *stack trace*, also in the builds made with `--release`.

## Throwing and catching exceptions

```cromo
void checkAge(i64 age) {
    if (age < 0) {
        throw Error(format("invalid age {}", age))
    }
}

i32 main() {
    try {
        checkAge(30)
        println("30 is fine")
        checkAge(-3)
        println("not reached")
    } catch (Exception e) {
        println("error: {}", e.message())
    }
    ret 0
}
```

```output
30 is fine
error: invalid age -3
```

`throw` stops the function and goes back through the callers until a `try` whose `catch` accepts the exception. `Error` is a ready-made exception with a message; `catch (Exception e)` accepts any exception, and `e.message()` gives its text.

Exceptions are not declared in the signature of a function: any call can throw, also the call of a lambda. Like `ret`, `throw` ends a path of the function, so a function that returns a value can end with a `throw` instead of a `ret`.

The library reports its errors in the same way, mostly with an `Error` and a message:

```cromo
import std.fs

i32 main() {
    try {
        string text = fs.readText("settings.txt")
        println("{}", text)
    } catch (Exception e) {
        println("using the defaults: {}", e.message())
    }
    ret 0
}
```

```output
using the defaults: cannot read 'settings.txt': no such file or folder
```

## Exception types

An exception implements the interface `Exception`, which has one method, `string message() const`. A type of its own lets a caller recognize the error and read its fields:

```cromo
struct NotFound : Exception {
    string name

    NotFound(move string what) {
        name = move what
    }

    string message() const {
        ret format("{} was not found", name)
    }
}

i64 position(list<string> items, string wanted) {
    for (i64 i = 0; i < items.len(); i++) {
        if (items[i] == wanted) {
            ret i
        }
    }
    throw NotFound(wanted.copy())
}

i32 main() {
    list<string> names = ["Ada", "Alan"]
    try {
        println("{}", position(names, "Alan"))
        println("{}", position(names, "Grace"))
    } catch (NotFound e) {
        println("missing: {}", e.name)
    } catch (Exception e) {
        println("other error: {}", e.message())
    }
    ret 0
}
```

```output
1
missing: Grace
```

The `catch` blocks are tried in order; the first that accepts the type of the exception handles it.

What is thrown is a new value, as `NotFound(...)` above, or a variable: `throw e` throws a copy, `throw move e` the value itself. Only a type that implements `Exception` can be thrown:

```cromo error
i32 main() {
    throw "something went wrong"
}
```

```output
error: only a struct that implements Exception can be thrown, found string
```

An enum can implement `Exception` too ([Enums and switch](enums.md)), and be thrown: each case is a kind of error, with its own data, and the `catch` tells them apart with a `switch`:

```cromo
enum ParseError : Exception {
    Empty
    BadChar(char c)

    string message() const {
        switch {
            case Empty {
                ret "the text is empty"
            }
            case BadChar(char c) {
                ret format("unexpected '{:c}'", c)
            }
        }
    }
}

i64 digits(string text) {
    if (text.len() == 0) {
        throw ParseError.Empty
    }
    i64 n = 0
    for (i64 i = 0; i < text.len(); i++) {
        char c = text[i]
        if (c < '0' || c > '9') {
            throw ParseError.BadChar(c)
        }
        n = n * 10 + i64(c - '0')
    }
    ret n
}

i32 main() {
    list<string> inputs = ["42", "", "4x"]
    for (string s in inputs) {
        try {
            println("{}", digits(s))
        } catch (ParseError e) {
            switch (e) {
                case Empty {
                    println("nothing to read")
                }
                case BadChar {
                    println("{}", e.message())
                }
            }
        }
    }
    ret 0
}
```

```output
42
nothing to read
unexpected 'x'
```

## The rules of try and catch

- A `try` has at least one `catch`, or a `finally`, or both.
- A `catch` takes a type that implements `Exception`, or `Exception` itself, which accepts every exception.
- A `catch` that could never run is an error: one after `catch (Exception e)`, or a second one for the same type.
- The exception of a `catch` is read-only, as a read parameter: its fields cannot be modified, and only its `const` methods can be called.

```cromo error
i32 main() {
    try {
        println("working")
    }
    ret 0
}
```

```output
error: 'try' needs at least one 'catch' or a 'finally'
```

```cromo error
struct Failure : Exception {
    i64 tries

    string message() const {
        ret "failure"
    }
}

struct Point {
    i64 x
}

i32 main() {
    try {
        throw Failure()
    } catch (Failure e) {
        e.tries++
    } catch (Point p) {
    }
    try {
        throw Failure()
    } catch (Exception e) {
        println("{}", e.message())
    } catch (Failure e) {
        println("never")
    }
    ret 0
}
```

```output
error: 'e' is the exception of a 'catch', which is read-only
error: a 'catch' takes a struct that implements Exception, or Exception itself, found Point
error: this 'catch' is never reached: a previous one already catches every exception
```

An exception that no `catch` of a `try` accepts, or that is thrown inside a `catch`, goes on to the `try` around it, after the `finally` of its own `try`:

```cromo
void step(i64 n) {
    if (n == 2) {
        throw Error("step 2 failed")
    }
}

i32 main() {
    try {
        try {
            step(2)
        } catch (Exception e) {
            println("inner: {}", e.message())
            throw Error(format("while working: {}", e.message()))
        } finally {
            println("inner finally")
        }
    } catch (Exception e) {
        println("outer: {}", e.message())
    }
    ret 0
}
```

```output
inner: step 2 failed
inner finally
outer: while working: step 2 failed
```

## finally

`finally` runs whenever the `try` ends: normally, with `ret`, with `break` or `continue`, or with an exception, caught or not:

```cromo
i32 main() {
    for (i32 i = 0; i < 2; i++) {
        try {
            if (i == 1) {
                throw Error("second round")
            }
            println("round {}", i)
        } catch (Exception e) {
            println("caught: {}", e.message())
        } finally {
            println("end of round {}", i)
        }
    }
    ret 0
}
```

```output
round 0
end of round 0
caught: second round
end of round 1
```

With `ret`, the value is computed first, then `finally` runs, then the function returns it. With an exception that the `try` does not catch, `finally` runs and the exception goes on:

```cromo
i64 value() {
    println("computing the value")
    ret 7
}

i64 compute() {
    try {
        ret value()
    } finally {
        println("finally")
    }
}

void fail() {
    try {
        throw Error("lost connection")
    } finally {
        println("cleanup")
    }
}

i32 main() {
    println("got {}", compute())
    try {
        fail()
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
computing the value
finally
got 7
cleanup
caught: lost connection
```

A `finally` always ends normally: `ret`, `break` and `continue` cannot leave it, because they would silently drop the exception or the value on its way out. A loop written inside a `finally` can still use `break` and `continue` for itself.

```cromo error
i64 compute() {
    try {
        ret 1
    } finally {
        ret 2
    }
}

i32 main() {
    for (i32 i = 0; i < 3; i++) {
        try {
            println("{}", i)
        } finally {
            break
        }
    }
    ret 0
}
```

```output
error: 'ret' cannot leave a 'finally' block
error: 'break' cannot leave a 'finally' block
```

## Exceptions and destructors

While an exception goes back through the functions, every object of the scopes it leaves is destroyed, as at the end of a normal block. This is what makes RAII reliable: a resource held by an object is released even when something goes wrong.

```cromo
struct Resource {
    string name

    Resource(move string label) {
        name = move label
        println("acquire {}", name)
    }

    ~Resource() {
        println("release {}", name)
    }
}

void work() {
    Resource a = Resource("a")
    Resource b = Resource("b")
    throw Error("failure in work")
}

i32 main() {
    try {
        work()
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
acquire a
acquire b
release b
release a
caught: failure in work
```

### A constructor that throws

A constructor can throw, when it cannot make a valid object. The fields already made are destroyed, but the destructor of the object does not run, because the object was never complete; the variable being initialized is never created:

```cromo
struct Engine {
    ~Engine() {
        println("engine destroyed")
    }
}

struct Machine {
    Engine engine

    Machine(bool broken) {
        println("building")
        if (broken) {
            throw Error("broken")
        }
    }

    ~Machine() {
        println("machine destroyed")
    }
}

i32 main() {
    try {
        Machine m = Machine(true)
        println("not reached")
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
building
engine destroyed
caught: broken
```

## What cannot throw

Some code runs where nobody could catch an exception, so the compiler does not let one out of it:

- a **destructor** and a **`finally`** block: they run while an exception may already be going back, and a second one would have nowhere to go;
- a **`copy()`** and a **`message()`** written by hand: the compiler calls them in places of its own, as the copies it makes and the report of an uncaught exception;
- the **initial value of a field**, as `i64 port = parse(text)`: it is computed wherever the default value of the struct is made, as in a declaration without a value or a new value of a map.

A call that can throw is an error there, unless a `try` around it catches every exception:

```cromo error
void risky() {
    throw Error("boom")
}

struct Holder {
    i64 x

    ~Holder() {
        risky()
    }
}

struct Report : Exception {
    string message() const {
        risky()
        ret "report"
    }
}

i32 main() {
    try {
        println("working")
    } finally {
        risky()
    }
    ret 0
}
```

```output
error: a destructor cannot let an exception out: catch it inside
error: message() cannot let an exception out: catch it inside
error: a 'finally' block cannot let an exception out: catch it inside
```

```cromo
void risky() {
    throw Error("boom")
}

struct Holder {
    i64 x

    ~Holder() {
        try {
            risky()
        } catch (Exception e) {
            println("in the destructor: {}", e.message())
        }
    }
}

i32 main() {
    Holder h
    ret 0
}
```

```output
in the destructor: boom
```

## Throwing again

`throw` alone, inside a `catch`, throws the same exception again, for example after writing it in a log:

```cromo
void load() {
    throw Error("disk full")
}

i32 main() {
    try {
        try {
            load()
        } catch (Exception e) {
            println("load failed, passing it on")
            throw
        }
    } catch (Exception e) {
        println("main: {}", e.message())
    }
    ret 0
}
```

```output
load failed, passing it on
main: disk full
```

Outside a `catch` there is no exception to throw again, and `throw` alone is an error. The exception thrown again keeps the stack trace of its first `throw`, so the trace still shows where the error happened, not where it passed:

```cromo file=again.cro
void load() {
    throw Error("disk full")
}

void retry() {
    try {
        load()
    } catch (Exception e) {
        println("load failed: {}", e.message())
        throw
    }
}

i32 main() {
    retry()
    ret 0
}
```

```output
load failed: disk full
uncaught exception: disk full
    at load (again.cro:2)
    at retry (again.cro:7)
    at main (again.cro:15)
```

A new exception thrown in the `catch`, as `throw Error(...)`, starts a new trace, from the line of that `throw`.

## Uncaught exceptions

An exception that leaves `main` stops the program with exit code 1, and prints `uncaught exception:`, its message and the calls from the `throw` up to `main`:

```cromo file=deep.cro
void deep(i64 n) {
    if (n == 0) {
        throw Error("too deep")
    }
    deep(n - 1)
    println("back from {}", n - 1)
}

i32 main() {
    deep(3)
    ret 0
}
```

```output
uncaught exception: too deep
    at deep (deep.cro:3)
    at deep (deep.cro:5)
    ... the same 2 more times
    at main (deep.cro:10)
```

An exception that ends a thread is thrown again by `join()`, in the thread that waits for it ([Threads, tasks and channels](concurrency.md)).

## Panics

A panic is a programming error: an index out of bounds, an overflow, an integer division by zero, a key missing from a map, a stack overflow. It is not an exception and it cannot be caught: the program stops with exit code 101 and prints the calls that led there.

The first line is `panic:`, the reason and the place in the source, `file:line:column`; the stack trace follows. The destructors and the `finally` blocks do not run.

```cromo file=bounds.cro
i64 third(list<i64> values) {
    ret values[2]
}

i32 main() {
    list<i64> short = [1, 2]
    println("{}", third(short))
    ret 0
}
```

```output
panic: index out of bounds: the index is 2 but the length is 2 at bounds.cro:2:15
    at third (bounds.cro:2)
    at main (bounds.cro:7)
```

These are the panics of the language; the chapters of the library name the panics of their functions.

| Panic | When |
|---|---|
| an index out of bounds, in a list, a string or an array | always |
| an integer division or `%` by zero | always |
| an integer overflow | without `--release`; with it, the value wraps around |
| a floating-point value converted to an integer that cannot hold it, or `nan` | without `--release`; with it, the nearest value |
| reading a key that is not in a map, `m[key]` | always |
| `Color(n)` with no case of the enum whose value is `n` | always |
| calling a method of an empty value of an interface | always |
| the same element passed twice with `ref`, as `swap(ref v[i], ref v[j])` with `i == j` | always |
| a stack overflow, `panic: stack overflow` | always |
| a false `assert`, and `panic(...)` | always |

A division of floating-point numbers by zero is not a panic: it gives `inf`, `-inf` or `nan`, as the IEEE 754 standard says ([Numbers and operators](operators.md)).

A panic inside a function of the library, as `remove` with an index out of the list, gives the place of the call in your program, and the trace shows the function of the library as its first line:

```cromo file=remove.cro
i32 main() {
    list<i64> values = [1, 2]
    values.remove(5)
    ret 0
}
```

```output
panic: index out of bounds: the index is 5 but the length is 2 at remove.cro:3:12
    at list<i64>.remove (cromo:core.cro:81)
    at main (remove.cro:3)
```

### panic

`panic("reason")` stops the program on purpose, when it reaches a state that should be impossible. Like `ret` and `throw`, it ends a path of the function:

```cromo file=codes.cro
string symbol(i32 code) {
    switch (code) {
        case 1 {
            ret "hearts"
        }
        case 2 {
            ret "spades"
        }
    }
    panic(format("unknown code {}", code))
}

i32 main() {
    println("{}", symbol(1))
    println("{}", symbol(9))
    ret 0
}
```

```output
hearts
panic: unknown code 9 at codes.cro:10:5
    at symbol (codes.cro:10)
    at main (codes.cro:15)
```

### assert

`assert(condition)` is a panic when its condition is false. The message repeats the condition as written, and for a comparison of numbers the values of its sides:

```cromo file=check.cro
i32 main() {
    i64 x = 3
    assert(x * 2 == 7)
    ret 0
}
```

```output
panic: assertion failed: x * 2 == 7 (x * 2 is 6) at check.cro:3:5
    at main (check.cro:3)
```

`assert` works in every build, also with `--release`. It is also the tool of the [Tests](tests.md).

## Stack traces

A panic, an exception that nobody catches and a stack overflow print the calls that led there, one line for each, from the innermost to `main`:

```
    at function (file:line)
```

The trace is printed also in the builds made with `--release`, with the names, files and lines of the source. The executable does not need its debug information for it: the compiler writes a compact table of the code inside it.

### The names

The functions are named as in the source:

```cromo file=names.cro
struct Account {
    i64 balance

    Account(i64 start) {
        balance = start
        check()
    }

    void check() const {
        assert(balance >= 0)
    }
}

i32 main() {
    func(i64) Account open = [](i64 amount) Account {
        ret Account(amount)
    }
    Account a = open(-5)
    ret 0
}
```

```output
panic: assertion failed: balance >= 0 (balance is -5) at names.cro:10:9
    at Account.check (names.cro:10)
    at Account.Account (names.cro:6)
    at main.lambda (names.cro:16)
    at main (names.cro:18)
```

| In the source | In a trace |
|---|---|
| a function | `main`, `deep` |
| a method | `Account.check` |
| a constructor and a destructor | `Account.Account`, `Account.~Account` |
| a generic function, a method of a generic type | `first<i64>`, `list<i64>.remove` |
| a lambda, by the function that contains it | `main.lambda` |
| a function of another module of the program | `geometry.area` |
| a function of the library, with the whole name of its module | `std.fs.readText` |

The functions of the file given to `cromo` are written without the name of their module.

### The files

The file given to `cromo` is written as it was given, as `names.cro` above. The other files of the program, as the modules that it imports, are written with their full path. The files of the library are written `cromo:` and their place in the library, without the folder where `cromo` is installed: `cromo:core.cro`, `cromo:std/fs.cro`. Here, on Windows, the error comes from the part of `std.fs` written for Windows:

```cromo nocheck file=settings.cro
import std.fs

i32 main() {
    string text = fs.readText("settings.txt")
    ret 0
}
```

```output
uncaught exception: cannot read 'settings.txt': no such file or folder
    at std.fs.loadFile (cromo:std/fs_windows.cro:41)
    at std.fs.readText (cromo:std/fs.cro:69)
    at main (settings.cro:4)
```

### Recursion, optimized code and long traces

- The same function, called from the same line many times in a row, as in a recursion, is written once, then `... the same N more times`, as in `deep.cro` above.
- The compiler optimizes by default, and puts small functions inside the functions that call them: the trace still shows them, with their own lines. A function that calls itself as its last step can become a loop: then its calls leave a single line.
- The functions of the runtime and of C are left out.
- A trace has at most 63 calls; a longer one ends with `...`. A stack overflow is the usual case:

```cromo nocheck file=overflow.cro
i64 depth(i64 n) {
    ret depth(n + 1) + 1
}

i32 main() {
    println("{}", depth(0))
    ret 0
}
```

```output
panic: stack overflow
    at depth (overflow.cro:2)
    ... the same 58 more times
    ...
```

For more than a trace, as the values of the variables, the program can be run in a debugger: see [Tools: formatting, the editor and the debugger](tools.md).

## Choosing between them

- If the caller can do something about the error, as asking for another file name, throw an **exception**.
- If the error means that the program is wrong, as an index computed badly, it is a **panic**: fix the program.
- If the absence of a result is normal, as a word not found in a text, return a conventional value or a `bool`, as `find` does with -1.

## Summary

- `throw`, `try`, `catch` and `finally` work as in C++, Java and C#; what is thrown implements `Exception`, often the ready-made `Error`.
- The `catch` blocks are tried in order; one that could never run is an error. The exception of a `catch` is read-only, and `throw` alone throws it again, with its first trace.
- `finally` runs on every exit from the `try`, and cannot be left with `ret`, `break` or `continue`.
- Destructors run while an exception goes back, so resources are always released. A destructor, a `finally`, `copy()`, `message()` and the initial value of a field cannot let an exception out.
- An uncaught exception stops the program with exit code 1; a panic, for a programming error, with 101, and it cannot be caught.
- Both print a stack trace, also with `--release`, with the names of the source; the library files are written `cromo:core.cro`, `cromo:std/fs.cro`.
- `panic` and `assert` stop the program on purpose.
