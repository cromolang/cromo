# Cromo for an AI

This is what you need to write correct Cromo. Cromo looks like C++ and is not C++: read "The rules that are easy to get wrong" before writing a line. For a module of the library, run `cromo ai std.fs` (any module listed at the end); `cromo ai core` gives the built-in functions and the methods of `string`, `list` and `map`. The full guide is at https://cromolang.org/book/en/.

Cromo is a compiled, general-purpose language: the syntax of C and C++, no pointers, no `null`, no garbage collector. Every value has one owner and is destroyed at the end of its scope (RAII). The compiler checks ownership; there are no lifetimes to write. One tool does everything: `cromo build`, `cromo run`, `cromo test`, `cromo fmt`.

```cromo
i32 main() {
    println("Hello, {}!", "world")
    ret 0
}
```

Save it as `hello.cro` and run `cromo run hello.cro`. Check your code by compiling it: `cromo build file.cro` prints every error with its line and what to write instead.

## The rules that are easy to get wrong

1. **`ret`, not `return`.** There is no `while` or `do`: `for (condition) { }` and `for { }`.
2. **No `;`.** A statement ends at the end of its line, one statement per line. A line continues only after a binary operator, a comma or an open `(` or `[`.
3. **The type is always written:** `i64 n = 5`, never `auto`, `var` or `let`. Types come before names, in variables, parameters and fields.
4. **No pointers, no `null`, no `new`, no references in variables or fields.** A parameter is read-only by default; `ref` lets the function modify the caller's value; `move` gives it away. The same word is written in the call: `fill(ref numbers)`, `take(move text)`.
5. **A moved variable cannot be used again.** A struct with a destructor is not copied: pass it with `ref` or `move`.
6. **No shadowing.** A local cannot have the name of another variable, parameter, field or constant that is visible there.
7. **Keywords are not names:** `move`, `ref`, `in`, `ret`, `case`, `default`, `func`, `public`, `unsafe`, `const`... A variable called `move` is an error; write `mv`. `extend` and `Self` are not names either, nor the words reserved for later: `while`, `do`, `goto`, `return`, `yield`, `defer`, `class`, `trait`, `impl`, `typedef`, `namespace`, `using`, `operator`, `static`, `virtual`, `override`, `null`, `this`, `delete`, `as`, `is`, `auto`, `let`, `var`, `mut`, `async`, `await`, `private`, `protected`, `macro`.
8. **No block without a statement in front of it:** `{ ... }` alone is an error.
9. **Assignments are statements**, not expressions: no `if (a = b)`, no `x = y = 0`, and `i++` only on a line of its own or as the step of a `for`.
10. **Numbers do not mix.** Signed and unsigned never mix, and a conversion that can lose something is written: `i32(x)`, `f64(n)`, `u8(c)`. Lengths and indices are `i64`.
11. **Overflow is a panic** in a debug build. For hashes and checksums use `wrapAdd(a, b)`, `wrapSub`, `wrapMul`.
12. **`!` on an integer inverts its bits**; there is no `~`. `&`, `|` and `^` bind more than the comparisons: `flags & 4 == 4` is `(flags & 4) == 4`.
13. **A shift needs a typed left side:** `u64(1) << n`, not `1 << n`.
14. **No overloading and no default arguments:** each function has its own name.
15. **A struct without a constructor is made with its default value**, `Point p`, and then its fields are set. `Point(1.0, 2.0)` works only if the struct declares that constructor.
16. **Fields, methods and declarations are private to their module** unless they say `public`.
17. **No global variables.** A module has only `const` constants: `public const i64 limit = 1024` is seen by the modules that import it, as `limit` or `geometry.limit`.
18. **`switch` uses braces, not colons, and does not fall through:** `case A, B { ... }`. On an enum every case must be handled, or `default`; the data of a case are named with their types, `case Circle(f64 r)`.
19. **Lambdas list their captures:** `[total, ref count, move text](i64 x) { ... }`. A lambda that is kept, as the body of a thread that is detached or a handler of a window, cannot capture with `ref`, and takes strings, lists and structs with `move`.
20. **A literal alone has no type:** `println("{}", 42)` and `[1, 2, 3]` as an argument are errors. Give it one, `i64(42)`, or put it in a variable first.
21. **`==` and `!=` work on numbers, `bool`, strings, structs, enums, arrays, lists and maps**, field by field, or with the method `bool equals(Name other) const` of the struct when it has one. A function value and what holds one cannot be compared. `<` and the others are for numbers and strings.
22. **Errors are exceptions**, `throw Error("...")` with `try` and `catch`. `panic` stops the program and cannot be caught. For "nothing found" return an `Option<T>` of the core: `ret Option<i64>.Some(i)` or `ret Option<i64>.None`, read with `switch` (`case Some(i64 at)`, `case None`) or with `has()`, `value()` and `or(other)`. There is no `Result`, and no `T?`.
23. **There is no `min`, `max` or `abs` in the core:** `import std.math` for those of `f64`, or write the comparison.
24. **`string` is UTF-8 bytes:** `text[i]` is a `u8`, `'a'` is a `char`, the same type. `text.slice(from, to)` makes a copy.

## Variables, numbers and text

```cromo
const i64 limit = 100  // a constant of the module

i32 main() {
    i64 count = 0          // integers: i8 i16 i32 i64 u8 u16 u32 u64
    f64 price = 9.5        // floating point: f32 f64
    bool ready = true
    string name = "Ada"    // every variable has a value: 0, false, "" when none is written
    const i64 twice = limit * 2
    count += 3
    i32 small = i32(count)             // a conversion that can lose something is written
    f64 total = price * f64(count)     // an integer becomes a float only when asked
    string line = name + " has " + format("{}", count)
    string number = "42"
    i64 parsed = i64(number)  // text to number: throws an Error for a text that is not one
    println("{} {} {:.2f} {} {} {}", line, small, total, ready, twice, name.len())
    list<i64> values = [1, 2, 3]
    println("{:x} {:#x} {:05} {:?}", count, small, parsed, values)
    println("{}", parsed + 1)
    ret 0
}
```

`println`, `print`, `eprintln`, `eprint` and `format` take a format with `{}` for each value: `{:.2f}` digits after the point, `{:x}` hexadecimal, `{:5}` and `{:05}` width, `{:?}` the debug form, needed for lists and structs.

## Control flow

```cromo
i32 main() {
    list<i64> numbers = [4, 8, 15]
    i64 sum = 0
    for (i64 i = 0; i < numbers.len(); i++) {  // the only place where ; is written
        sum += numbers[i]
    }
    for (i64 n in numbers) {  // each element, read
        sum += n
    }
    for (ref i64 n in numbers) {  // each element, to modify it
        n *= 2
    }
    i64 steps = 0
    for (sum > 10) {  // while
        sum /= 2
        steps++
    }
    for {  // forever, until break
        if (steps > 5) {
            break
        }
        steps++
    }
    string size = steps > 3 ? "many" : "few"
    if (sum == 0) {
        println("zero")
    } else if (sum < 10) {
        println("{} {} {}", sum, steps, size)
    } else {
        println("large")
    }
    ret 0
}
```

Conditions are `bool`: `if (n)` on a number is an error, write `if (n != 0)`.

## Functions and ownership

```cromo
// Read: the default. The function cannot change the list.
i64 total(list<i64> values) {
    i64 sum = 0
    for (i64 v in values) {
        sum += v
    }
    ret sum
}

// ref: the function changes the caller's list.
void addTwice(ref list<i64> values, i64 n) {
    values.add(n)
    values.add(n)
}

// move: the function takes the list; the caller cannot use it again.
list<i64> withZero(move list<i64> values) {
    values.add(0)
    ret values
}

// "Nothing found" without null: a bool, and the value through ref.
bool firstOver(list<i64> values, i64 least, ref i64 found) {
    for (i64 v in values) {
        if (v > least) {
            found = v
            ret true
        }
    }
    ret false
}

i32 main() {
    list<i64> numbers = [1, 2, 3]
    addTwice(ref numbers, 7)  // ref is written in the call too
    list<i64> more = withZero(move numbers)  // numbers is gone from here on
    i64 big = 0
    if (firstOver(more, 5, ref big)) {
        println("{} {}", total(more), big)
    }
    ret 0
}
```

Passing a value without `move` to something that keeps it, as `list.add(x)` or an assignment, copies it: numbers and structs of numbers freely, strings, lists and structs with their `copy()`. A struct with a destructor has no copy: `move` it.

## Structs

```cromo
struct Account {
    string owner
    i64 cents
    public bool open = true  // a default value; public: seen by other modules

    // A constructor: without one, a struct is made as `Account a` and its fields are set.
    public Account(string name, i64 start) {
        owner = name
        cents = start
    }

    // const: the method does not change the struct.
    public i64 balance() const {
        ret cents
    }

    public void deposit(i64 amount) {
        if (amount <= 0) {
            throw Error("the amount must be positive")
        }
        cents += amount
    }

    // The destructor runs when the value goes out of scope: files, locks, connections.
    ~Account() {
        open = false
    }
}

i32 main() {
    Account a = Account("Ada", 500)
    a.deposit(250)
    println("{}", a.balance())
    ret 0
}
```

Inside a method the fields are used by their names: there is no `this`. A parameter cannot have the name of a field.

## Enums and switch

```cromo
enum Color {
    Red
    Green
    Blue
}

enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
    Empty
}

f64 area(Shape s) {
    switch (s) {
        case Circle(f64 r) {  // the data of the case, each with its type
            ret 3.14159 * r * r
        }
        case Rect(f64 w, f64 h) {
            ret w * h
        }
        case Empty {
            ret 0.0
        }
    }
}

i32 main() {
    Color c = Color.Green
    Shape s = Shape.Rect(2.0, 3.0)
    switch (c) {
        case Red, Blue {
            println("not green")
        }
        default {
            println("{}", area(s))
        }
    }
    ret 0
}
```

`switch` works also on integers and strings, with `default`.

## Interfaces and generics

```cromo
interface Shape {
    f64 area() const
}

struct Square : Shape {
    f64 side

    f64 area() const {
        ret side * side
    }
}

// A generic function: T is any type that keeps the promise of Ordered, its `compare`.
// The operators < and > are for numbers and strings, not for a T.
T larger<T : Ordered>(T a, T b) {
    ret a.compare(b) > 0 ? a : b
}

// A parameter of an interface type takes any struct that names it.
f64 doubled(Shape s) {
    ret s.area() * 2.0
}

struct Pair<T> {
    T first
    T second
}

i32 main() {
    Square q
    q.side = 3.0
    Pair<i64> p
    p.first = 4
    p.second = 9
    println("{} {} {}", doubled(q), larger(p.first, p.second), larger("pear", "apple"))
    ret 0
}
```

A struct names its interfaces after `:`. The interfaces of the core: `Ordered` (`i32 compare(Self other) const`), `Key` (what a key of a `map` needs: `hash` and `equals`), `Debug`, `Exception`. In a generic function a `T` has only the methods of its interfaces: no operators. There is no inheritance.

## Lists, maps and text

```cromo
i32 main() {
    list<string> names = ["Grace", "Ada"]
    names.add("Alan")
    names.insert(0, "Barbara")
    string removed = names.remove(1)
    sort(ref names)
    sortBy(ref names, [](string a, string b) bool { ret a.len() < b.len() })
    list<i64> lengths = names.map<i64>([](string n) i64 { ret n.len() })
    println("{} {:?} {:?} {}", removed, names, lengths, join(names, ", "))

    map<string, i64> ages
    ages["Ada"] = 36
    ages["Alan"] = 41
    if (ages.contains("Ada")) {
        ages["Ada"] += 1
    }
    for (string who, i64 age in ages) {
        println("{} {}", who, age)
    }
    ages.remove("Alan")

    string text = "  one,two,three  "
    list<string> parts = text.trim().split(",")
    println("{} {} {} {}", parts.len(), text.contains("two"), text.trim().slice(0, 3).upper(), text.find("two"))
    i64[4] fixed  // an array of fixed size, on the stack
    fixed[0] = 7
    println("{}", fixed[0] + ages.len())
    ret 0
}
```

An index out of range is a panic, never undefined behavior. A list literal needs a type that can be seen: `list<i64> xs = [1, 2]`, or `[i64(1), 2]`. There are no map literals.

## Errors

```cromo
// An exception of your own: a struct that names Exception.
struct ParseError : Exception {
    string text

    ParseError(string what) {
        text = what
    }

    string message() const {
        ret text
    }
}

i64 positive(string text) {
    i64 n = i64(text)  // throws an Error for "abc"
    if (n <= 0) {
        throw ParseError(format("{} is not positive", n))
    }
    ret n
}

i32 main() {
    for (string text in ["12", "-3", "abc"]) {
        try {
            println("{}", positive(text))
        } catch (ParseError e) {
            println("refused: {}", e.message())
        } catch (Error e) {
            println("not a number: {}", e.message())
        } finally {
            println("done")
        }
    }
    ret 0
}
```

An exception that nobody catches ends the program with a stack trace. `assert(condition)` and `panic("reason")` stop the program at once.

## Anonymous functions

```cromo
// A function that takes a function: its type is func(parameters) result.
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

i32 main() {
    list<i64> values = [3, 8, 12, 5]
    i64 least = 5
    i64 seen = 0
    // The captures are listed: least is copied, seen is modified through ref.
    i64 large = count(values, [least, ref seen](i64 v) bool {
        seen++
        ret v > least
    })
    println("{} {}", large, seen)
    ret 0
}
```

## Modules, the library and projects

A file is a module, and a folder is one module made of its files. `import geometry` reads `geometry.cro`, or the folder `geometry`, next to the main file; `import tools.geometry` reads `tools/geometry.cro`. The public names of an imported module are used directly, or with the last name of the import in front: `geometry.distance(a, b)`. The modules of the library are always written with their name: `fs.readText(path)`.

```cromo
import std.fs
import std.os
import std.time

i32 main(list<string> args) {
    string path = fs.joinPath(os.currentFolder(), "notes.txt")
    fs.writeText(path, "first line\n")
    fs.appendText(path, "second line\n")
    for (string line in fs.readText(path).trim().split("\n")) {
        println("{}", line)
    }
    println("{} {} {}", fs.exists(path), fs.size(path), args.len())
    fs.remove(path)
    time.Instant start = time.now()
    println("{}", start.elapsed().milliseconds() < 1000)
    ret 0
}
```

`main` is `i32 main()`, `i32 main(list<string> args)` or `void main()`. A project has a `cromo.mod` at its top, `module example.com/app`, and its imports start from that folder; `bin server cmd/server.cro` lists an executable, and `cromo get github.com/cromolang/toml` adds a package. `cromo init` writes the `cromo.mod` of a folder, and `cromo create app` a new folder with its project.

## Threads

```cromo
import std.thread

i32 main() {
    list<i64> values
    for (i64 i = 1; i <= 1000; i++) {
        values.add(i)
    }
    // Shared<T>: a value with its lock, reached through a handle that is copied freely.
    thread.Shared<i64> total = thread.Shared<i64>(0)
    thread.Thread worker = thread.Thread([values, total]() {
        i64 sum = 0
        for (i64 v in values) {
            sum += v
        }
        total.lock([sum](ref i64 t) { t += sum })
    })
    worker.start()
    worker.join()
    total.lock([](ref i64 t) { println("{}", t) })
    ret 0
}
```

`thread.Task` and `thread.Group` are lightweight tasks that block without `async`; `thread.Channel<T>` passes values between them; `thread.Mutex<T>` and `thread.Atomic<T>` share data. The compiler refuses a data race.

## Tests

```cromo
i64 double(i64 n) {
    ret n * 2
}

[[test]]
void doublesANumber() {
    assert(double(21) == 42)
}

i32 main() {
    println("{}", double(4))
    ret 0
}
```

`cromo test file.cro` runs every `[[test]]` function of the program and of the modules of its project; `cromo test file.cro doublesANumber` only those named. A file named `x_test.cro` in a folder that is a module holds its tests.

## The command

| Command | What it does |
|---|---|
| `cromo run file.cro -- arguments` | compiles and runs |
| `cromo build file.cro` | writes `file.exe`, or `file` on Linux; `-o name`, `--release` without the checks of overflow, `-O0` for a debugger |
| `cromo build --target linux file.cro` | for another system: `windows`, `linux`, `linux-arm64`, `android` |
| `cromo test file.cro` | runs the tests |
| `cromo fmt file.cro` | formats in the one style: 4 spaces, `{` at the end of the line |
| `cromo lsp` | the language server for editors |
| `cromo ai`, `cromo ai std.fs` | this text, and the reference of a module |

Format with `cromo fmt` instead of arguing about style. Everything in a program without `import unsafe` is checked: no undefined behavior, no data races, no use after a move. `import unsafe` opens the dialect of raw memory and of C functions, `extern "library" { ... }`, for the files that need it.
