# Errors

Cromo separates two kinds of errors:

- **Errors that a program expects**, as a file that does not exist or a connection that drops: they are **exceptions**, which a caller can catch and handle.
- **Programming errors**, as an index out of a list or an integer overflow: they are **panics**, which stop the program, because a program that has broken its own rules cannot know what to do next.

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

`throw` stops the function and goes back through the callers until a `try` whose `catch` accepts the exception. `Error` is a ready-made exception with a message; `catch (Exception e)` accepts any exception, and `e.message()` gives its text. Exceptions are not declared in the signature of a function.

## Exception types

An exception is a struct that implements the interface `Exception`, which has one method, `string message() const`. A type of its own lets a caller recognize the error and read its fields:

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

The `catch` blocks are tried in order; the first that accepts the type of the exception handles it. The exception of a `catch` is read-only.

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

A destructor, and a `finally`, cannot let an exception out: the compiler requires them to catch it inside.

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

## Uncaught exceptions

An exception that leaves `main` stops the program with exit code 1, and prints the message and the calls from the `throw` up to `main`:

```cromo file=deep.cro
void deep(i64 n) {
    if (n == 0) {
        throw Error("too deep")
    }
    deep(n - 1)
}

i32 main() {
    deep(3)
    ret 0
}
```

```output
uncaught exception: too deep
    at deep (deep.cro:3)
    at main (deep.cro:9)
```

The trace is printed also in release builds, with the names, files and lines of the source. The executable does not need its debug information for it: the compiler writes a compact table of the code inside it.

## Panics

A panic is a programming error: an index out of bounds, an overflow, a division by zero, a key missing from a map, a stack overflow. It is not an exception and it cannot be caught: the program stops with exit code 101 and prints the calls that led there.

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

`panic("reason")` stops the program on purpose, when it reaches a state that should be impossible.

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

`assert` works in every build, also with `--release`. It is also the tool of the tests of chapter 12.

## Choosing between them

- If the caller can do something about the error, as asking for another file name, throw an **exception**.
- If the error means that the program is wrong, as an index computed badly, it is a **panic**: fix the program.
- If the absence of a result is normal, as a word not found in a text, return a conventional value or a `bool`, as `find` does with -1.

## Summary

- `throw`, `try`, `catch` and `finally` work as in C++, Java and C#; exceptions implement `Exception`.
- Destructors run while an exception goes back, so resources are always released.
- An uncaught exception, a panic and a stack overflow print the calls that led there, also in release.
- Panics are for programming errors; `assert` checks what must be true.
