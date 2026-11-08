# Structs

A struct groups related values under one name, as in C. In Cromo it can also have a constructor, methods and a destructor, as a class of C++, but there is no inheritance: what different types have in common is expressed with interfaces, in [Interfaces and generics](interfaces-generics.md).

## Defining a struct

```cromo
struct Rectangle {
    f64 width
    f64 height
}

i32 main() {
    Rectangle r
    r.width = 3.0
    r.height = 2.0
    println("{} x {}", r.width, r.height)
    ret 0
}
```

```output
3 x 2
```

A field is declared as a variable: the type, then the name. The fields stay in memory in the order in which they are written, with the alignment of C, so a struct made of numbers has the same layout as the same struct in C ([Calling C](c-interop.md)).

In a program of several modules, a struct, its fields, its constructor and its methods are private to their module unless they are marked `public`: [Modules and projects](modules.md) explains it.

## Default values

`Rectangle r` declares a rectangle with its **default value**: each field has the default of its type, here 0. A field can declare its own initial value instead:

```cromo
struct Settings {
    string host = "localhost"
    i32 port = 8080
    list<string> paths = ["/", "/health"]
    bool verbose
}

i32 main() {
    Settings s
    println("{}:{} {} {}", s.host, s.port, s.paths.len(), s.verbose)
    ret 0
}
```

```output
localhost:8080 2 false
```

The initial value can be any expression, the call of a function or of a constructor too, and it is computed every time a default value of the struct is made: for a variable declared without a value, before a constructor runs, for `Settings()`.

```cromo
struct Size {
    i64 width
    i64 height

    Size(i64 w, i64 h) {
        width = w
        height = h
    }
}

i64 kib(i64 n) {
    ret n * 1024
}

struct Window {
    string title = "untitled"
    Size size = Size(800, 600)
    i64 buffer = kib(64)
}

i32 main() {
    Window w
    println("{} {}x{} {}", w.title, w.size.width, w.size.height, w.buffer)
    ret 0
}
```

```output
untitled 800x600 65536
```

### The order of the initial values

The default value of a struct is made in two steps: first the default values of the structs in its fields, in the order of the fields, then its own initial values, in the order of the fields:

```cromo
string label(string what) {
    println("  computing {}", what)
    ret what
}

struct Inner {
    string a = label("inner.a")
    string b = label("inner.b")
}

struct Outer {
    string first = label("outer.first")
    Inner inner
    string last = label("outer.last")
}

i32 main() {
    println("one Outer:")
    Outer o
    ret 0
}
```

```output
one Outer:
  computing inner.a
  computing inner.b
  computing outer.first
  computing outer.last
```

An initial value cannot read the other fields, which may not have their value yet:

```cromo error
struct Box {
    i64 width = 4
    i64 twice = width * 2
}

i32 main() {
    ret 0
}
```

```output
error: the initial value of a field cannot read the field 'width'
```

And it cannot throw an exception: the default value is made in many places, a declaration, a constructor, a new key of a map, none of which expects one. A function that can throw is called in the constructor instead.

```cromo error
i64 parsePort(string text) {
    if (text == "") {
        throw Error("no port")
    }
    ret 8080
}

struct Config {
    i64 port = parsePort("8080")
}

i32 main() {
    ret 0
}
```

```output
error: the initial value of a field cannot throw an exception: it is computed wherever the default value is made
```

### Designing the default value

As in Go, the default value of a struct is used often: for a variable declared without a value, for the fields that a constructor does not assign, and for a field whose value has been moved away ([Ownership](ownership.md)). A struct must be designed so that its default value is sensible, and its destructor must handle it.

For a struct that holds a handle of the system, as the descriptor of a file, the default value says "nothing open", and the destructor releases only what was opened. Here `fd` stands for that handle:

```cromo
struct Handle {
    i32 fd = -1

    ~Handle() {
        if (fd >= 0) {
            println("close {}", fd)
        }
    }
}

struct Server {
    string name
    Handle listener
}

i32 main() {
    Handle unused
    Server s
    s.listener.fd = 4
    Handle taken = move s.listener
    println("left in the server: {}", s.listener.fd)
    ret 0
}
```

```output
left in the server: -1
close 4
```

`unused` and the field left in `s` have `fd` at -1, and their destructors do nothing; only `taken` closes its handle. A moved field gets the default of its type, here a `Handle` with `fd` at -1, which includes the initial values written inside `Handle`. An initial value written on the field of `Server`, as `Handle listener = open(5)`, would not be computed again: a move computes nothing and allocates nothing.

## Constructors

A constructor is written as in C++, with the name of the struct and no type. It assigns the fields, which are used by their name: there is no `this`.

```cromo
struct Rectangle {
    f64 width
    f64 height

    Rectangle(f64 w, f64 h) {
        width = w
        height = h
    }
}

i32 main() {
    Rectangle r = Rectangle(3.0, 2.0)
    println("{} x {}", r.width, r.height)
    ret 0
}
```

```output
3 x 2
```

A constructor starts from the default value: when its body runs, every field already has its initial value, and the fields that it does not assign keep it. A variable declared without a value gets the default value even when the struct has a constructor: the constructor runs only when it is called.

```cromo
struct Counter {
    i64 value = 10
    i64 step = 1

    Counter(i64 start) {
        println("in the constructor: value {} step {}", value, step)
        value = start
    }
}

i32 main() {
    Counter a
    Counter b = Counter(3)
    println("{} {} / {} {}", a.value, a.step, b.value, b.step)
    ret 0
}
```

```output
in the constructor: value 10 step 1
10 1 / 3 1
```

A struct without a constructor can still be made as a value, with `Rectangle()`: it is its default value. There is no initialization by position as `Rectangle(3.0, 2.0)` without a constructor: the fields are assigned afterwards, or the struct gets a constructor.

```cromo error
struct Point {
    f64 x
    f64 y
}

i32 main() {
    Point p = Point(1.0, 2.0)
    ret 0
}
```

```output
error: Point has no constructor: Point() gives its default value
```

A struct has at most one constructor: the default value already covers the case without arguments.

```cromo error
struct Range {
    i64 low
    i64 high

    Range(i64 end) {
        high = end
    }

    Range(i64 start, i64 end) {
        low = start
        high = end
    }
}

i32 main() {
    ret 0
}
```

```output
error: a struct has only one constructor
```

A parameter cannot have the name of a field, and neither can a local variable of a constructor or of a method, so that a name inside the struct always means one thing: `width = w`, never `width = width`.

```cromo error
struct Rectangle {
    f64 width
    f64 height

    Rectangle(f64 width, f64 height) {
    }
}

i32 main() {
    ret 0
}
```

```output
error: 'width' is a field of Rectangle: choose another name
```

### A constructor that fails

When a constructor cannot do its job, for example because a file does not open, it throws an exception ([Errors](errors.md)). The object then does not exist: the variable is not created, the fields, which already had their values, are destroyed, and the destructor does not run, because the object was never complete.

```cromo
struct Part {
    string name

    ~Part() {
        println("part {} destroyed", name)
    }
}

struct Device {
    Part motor
    i32 port = -1

    Device(i32 p) {
        motor.name = "motor"
        if (p < 0) {
            throw Error("no such port")
        }
        port = p
    }

    ~Device() {
        println("device on port {} closed", port)
    }
}

i32 main() {
    try {
        Device d = Device(-5)
        println("not reached")
    } catch (Error e) {
        println("failed: {}", e.message())
    }
    Device ok = Device(2)
    println("port {}", ok.port)
    ret 0
}
```

```output
part motor destroyed
failed: no such port
port 2
device on port 2 closed
part motor destroyed
```

## Methods

Functions written inside the struct are its methods. They see the fields by name and can call the other methods by name:

```cromo
struct Rectangle {
    f64 width
    f64 height

    Rectangle(f64 w, f64 h) {
        width = w
        height = h
    }

    f64 area() const {
        ret width * height
    }

    bool isSquare() const {
        ret width == height
    }

    void scale(f64 factor) {
        width *= factor
        height *= factor
    }

    bool contains(Rectangle other) const {
        ret width >= other.width && height >= other.height
    }
}

i32 main() {
    Rectangle a = Rectangle(3.0, 2.0)
    Rectangle b = Rectangle(1.0, 1.0)
    println("area {}, square {}", a.area(), b.isSquare())
    b.scale(4.0)
    println("a contains b: {}", a.contains(b))
    println("{}", Rectangle(2.0, 5.0).area())
    ret 0
}
```

```output
area 6, square true
a contains b: false
10
```

A method can be called on a temporary too, as `Rectangle(2.0, 5.0).area()`. The parameters of a method follow the rules of all the parameters ([Ownership](ownership.md)): `other` is read, without a copy.

### Calls inside methods

Inside a method, a call `f()` means the method `f` of the same struct, if there is one; otherwise it means a free function. The name closest to the code wins, as for the fields and as in C++:

```cromo
void report() {
    println("free function")
}

void log(string message) {
    println("log: {}", message)
}

struct Task {
    string name

    void report() const {
        println("task {}", name)
    }

    void run() const {
        report()
        log("running")
    }
}

i32 main() {
    Task t
    t.name = "build"
    t.run()
    report()
    ret 0
}
```

```output
task build
log: running
free function
```

In `run`, `report()` is the method; `log` is not a method of `Task`, so it is the free function. Outside the struct, `report()` is the free function.

### const methods

`const` after the parameters, as in C++, says that the method does not modify the object. Only `const` methods can be called on a value that cannot be modified: a read parameter, a `const` variable, the element of a `for` that reads a list.

```cromo error
struct Counter {
    i64 value

    void increment() {
        value++
    }
}

void show(Counter c) {
    c.increment()
}

i32 main() {
    Counter c
    show(c)
    ret 0
}
```

```output
error: cannot call 'increment', which is not const
```

The compiler checks the body of a `const` method: it cannot assign the fields, nor call the methods that are not `const`.

```cromo error
struct Counter {
    i64 value

    i64 next() const {
        value++
        ret value
    }
}

i32 main() {
    ret 0
}
```

```output
error: method 'next' is const and cannot modify the fields
```

Without pointers, `const` is deep: a `const` method cannot modify even the strings and the lists inside the object.

## Destructors

The destructor, `~Name()`, runs when the object ends: at the end of its scope, when it is removed from a list, when the struct that contains it ends ([Ownership](ownership.md) lists every case). It releases what the object holds.

```cromo
struct Timer {
    string label

    Timer(move string name) {
        label = move name
        println("{}: start", label)
    }

    ~Timer() {
        println("{}: end", label)
    }
}

void work() {
    Timer t = Timer("work")
    println("working")
}

i32 main() {
    work()
    Timer outer = Timer("main")
    ret 0
}
```

```output
work: start
working
work: end
main: start
main: end
```

After the destructor, the fields are destroyed by themselves, the last declared first, so a destructor only deals with what the fields cannot release alone: a handle of the system, a temporary file, a message to print. A struct without a destructor still destroys its fields.

```cromo
struct Part {
    string name

    ~Part() {
        println("  part {} ends", name)
    }
}

struct Machine {
    Part engine
    Part wheels

    Machine() {
        engine.name = "engine"
        wheels.name = "wheels"
    }

    ~Machine() {
        println("machine ends")
    }
}

i32 main() {
    Machine m = Machine()
    println("end of main")
    ret 0
}
```

```output
end of main
machine ends
  part wheels ends
  part engine ends
```

A destructor has no parameters and no type, and it is never called by hand: the compiler calls it, once for each object. It cannot let an exception out either, because it can run while another exception is going through: an error inside it is caught inside it.

```cromo error
struct Log {
    string path

    ~Log() {
        throw Error("cannot flush")
    }
}

i32 main() {
    ret 0
}
```

```output
error: a destructor cannot let an exception out: catch it inside
```

## Copying a struct

How a struct is copied depends on what it holds:

- A struct made only of numbers, `bool` and other such structs, without a destructor, is a **simple** type: it is copied byte by byte, as in C.
- A struct with strings, lists, maps or other structs that own resources, and without a destructor of its own, is copied with a `copy()` that the compiler writes: it copies each field.
- A struct with a destructor, or with a field that cannot be copied, is not copied: the compiler cannot know how to duplicate what the destructor releases. It is only moved, unless it writes its own `copy()`.

[Ownership](ownership.md) explains where the copies happen.

### `copy()` written by hand

A struct that the compiler cannot copy, because it has a destructor or a field that cannot be copied, can still be copied if it writes the method `copy()`, with exactly this signature: the type of the struct, `copy()`, no parameters, `const`. It is then called wherever the struct is copied, as for a `string`:

```cromo
struct Ticket {
    string owner
    i64 number

    Ticket(move string name, i64 n) {
        owner = move name
        number = n
        println("issue ticket {} to {}", number, owner)
    }

    ~Ticket() {
        println("cancel ticket {}", number)
    }

    Ticket copy() const {
        ret Ticket(owner, number + 100)
    }
}

i32 main() {
    Ticket a = Ticket("Ada", 1)
    Ticket b = a
    println("{} has ticket {}, and {}", b.owner, a.number, b.number)
    ret 0
}
```

```output
issue ticket 1 to Ada
issue ticket 101 to Ada
Ada has ticket 1, and 101
cancel ticket 101
cancel ticket 1
```

A copy of a ticket is a new ticket, with a number of its own, and each one is cancelled by its destructor.

The compiler checks the signature:

```cromo error
struct Ticket {
    i64 number

    ~Ticket() {
        println("cancel ticket {}", number)
    }

    Ticket copy() {
        ret Ticket()
    }
}

i32 main() {
    ret 0
}
```

```output
error: copy() must be written 'Ticket copy() const'
```

A simple struct cannot write `copy()`: it is always copied byte by byte, and a `copy()` written by hand could do something else.

```cromo error
struct Point {
    i64 x
    i64 y

    Point copy() const {
        Point p
        ret p
    }
}

i32 main() {
    ret 0
}
```

```output
error: Point is copied implicitly: its copy() is generated by the compiler
```

Like a destructor, a `copy()` written by hand cannot let an exception out: the compiler calls it in places where nobody could catch it.

## Comparing structs

`==` and `!=` do not work on structs yet:

```cromo error
struct Point {
    i64 x
    i64 y
}

i32 main() {
    Point a
    Point b
    println("{}", a == b)
    ret 0
}
```

```output
error: '==' does not work on structs yet
```

Until they do, a struct compares its fields in a method. The name `equals` is the one that a key of a map uses ([Lists, maps and text](collections.md)):

```cromo
struct Point {
    i64 x
    i64 y

    bool equals(Point other) const {
        ret x == other.x && y == other.y
    }
}

i32 main() {
    Point a
    Point b
    b.x = 1
    println("{} {}", a.equals(b), a.equals(a))
    ret 0
}
```

```output
false true
```

## Printing a struct

`{:?}` prints any value in a form meant for debugging, with the names of the fields. To print a struct with `{}`, give it a method `string text() const`:

```cromo
struct Point {
    i64 x
    i64 y

    string text() const {
        ret format("({}, {})", x, y)
    }
}

i32 main() {
    Point p
    p.x = 1
    p.y = 2
    println("{} {:?}", p, p)
    ret 0
}
```

```output
(1, 2) Point { x: 1, y: 2 }
```

`format` works as `println`, but returns the text instead of printing it. Without `text()`, `{}` on a struct is an error, which proposes both ways. The widths, the alignment and the other options of `{}` apply to the text that `text()` gives: [Formatting text](formatting.md) explains them, and the forms of `{:?}`.

## Structs inside structs

A struct can contain other structs, lists of structs, maps: everything it contains is destroyed with it. It cannot contain itself directly, since it would have an infinite size, but it can through a list:

```cromo
struct Folder {
    string name
    list<Folder> children

    i64 count() const {
        i64 total = 1
        for (Folder child in children) {
            total += child.count()
        }
        ret total
    }
}

i32 main() {
    Folder root
    root.name = "/"
    Folder docs
    docs.name = "docs"
    docs.children.add(Folder())
    root.children.add(move docs)
    root.children.add(Folder())
    println("{} folders", root.count())
    ret 0
}
```

```output
4 folders
```

```cromo error
struct Node {
    i64 value
    Node next
}

i32 main() {
    ret 0
}
```

```output
error: struct 'Node' contains itself through field 'next': use a list instead
```

## Summary

- A struct has fields, at most one constructor, methods and a destructor; inside it, the fields and the methods are used by name, and there is no `this`.
- Every field has the default value of its type, or the initial value that it declares: any expression that does not read the other fields and does not throw. The structs inside are made first, then the initial values, in the order of the fields.
- The default value must be sensible, and the destructor must handle it: it is used for declarations without a value, for the fields a constructor does not assign, and for moved fields.
- A constructor starts from the default value and runs only when it is called; if it throws, the object does not exist and its fields are destroyed.
- Inside a method, `f()` is a method of the struct if there is one, otherwise a free function.
- `const` methods do not modify the object, and they are the only ones that can be called on a read-only value.
- The destructor releases what the object holds, automatically, then the fields end, the last first. It has no parameters and cannot throw.
- Simple structs are copied byte by byte, the others with a generated `copy()`; a struct with a destructor, or with a field that cannot be copied, is copied only if it writes `T copy() const`.
- `==` does not work on structs yet: write a method `equals`.
- `text()` prints a struct with `{}`; `{:?}` prints any value for debugging.
