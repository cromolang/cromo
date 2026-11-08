# Interfaces and generics

Different types often share a behavior: a circle and a square both have an area. In Cromo the behavior is described by an **interface**, and code that works with any type that has it is written once, either as a function that takes the interface, decided while the program runs, or as a **generic**, decided by the compiler. There is no inheritance between structs.

## Interfaces

An interface lists methods without a body. A struct implements it by naming it after a colon and writing those methods:

```cromo
interface Shape {
    f64 area() const
    string name() const
}

struct Circle : Shape {
    f64 radius

    Circle(f64 r) {
        radius = r
    }

    f64 area() const {
        ret 3.14159 * radius * radius
    }

    string name() const {
        ret "circle"
    }
}

// ...
#
# struct Square : Shape {
#     f64 side
#
#     Square(f64 s) {
#         side = s
#     }
#
#     f64 area() const {
#         ret side * side
#     }
#
#     string name() const {
#         ret "square"
#     }
# }

void describe(Shape s) {
    println("{} with area {:.2f}", s.name(), s.area())
}

i32 main() {
    Circle c = Circle(1.0)
    describe(c)
    describe(Square(2.0))
    ret 0
}
```

```output
circle with area 3.14
square with area 4.00
```

A parameter of type `Shape` accepts any struct that implements `Shape`, without copying it and without allocating anything: the struct stays where it is, and the call goes through a table of its methods, as a virtual call in C++.

### What an interface contains

An interface contains only the signatures of methods: the type they return, their name, their parameters with their modes, and `const` when the method does not modify the value. It has no fields, no bodies, no constructors, and it does not extend another interface. The compiler refuses each of them:

```cromo error
interface Shape {
    f64 size
    f64 area() const
}

interface Named {
    string name() const {
        ret ""
    }
}

interface Solid : Shape {
    f64 volume() const
}

i32 main() {
    ret 0
}
```

```output
error: an interface contains only methods
error: the methods of an interface have no body
error: an interface cannot extend another interface
```

Code shared by several types goes in a function that takes the interface, as `describe` above; a type that needs several interfaces implements them all, as shown below.

The methods of an interface are public when the interface is. In another module, a method that implements an interface needs no `public` of its own, because it can be called through the interface anyway: [Modules and projects](modules.md) explains `public`.

### Implementing an interface exactly

A struct that names an interface must write each of its methods with the same signature: the same return type, the same types and modes of the parameters, and `const` where the interface has it. Only the names of the parameters can change. The compiler says which method is missing or different, and how it must be written:

```cromo error
interface Shape {
    f64 area() const
    void scale(f64 factor)
}

struct Square : Shape {
    f64 side

    f64 area() {
        ret side * side
    }
}

i32 main() {
    ret 0
}
```

```output
error: 'area' does not match the interface Shape: it must be 'f64 area() const'
error: Square does not implement 'scale' of Shape: add 'void scale(f64 factor)'
```

The struct can have other methods and fields, which the interface does not see. A struct implements an interface only when it names it: a struct with the right methods, but without `: Shape`, is not a `Shape`, and passing it as one is the error `Square does not implement Shape`. After the colon only interfaces can be written: `struct Big : Square` is the error `'Square' is not an interface: a struct can only implement interfaces`.

### Several interfaces

A type that implements several interfaces lists them with commas. An enum implements interfaces too, with its methods after the cases, as [Enums and switch](enums.md) shows:

```cromo
interface Shape {
    f64 area() const
}

interface Named {
    string name() const
}

struct Circle : Shape, Named {
    f64 radius

    f64 area() const {
        ret 3.14159 * radius * radius
    }

    string name() const {
        ret "circle"
    }
}

enum Tile : Shape, Named {
    Empty
    Square(f64 side)

    f64 area() const {
        switch {
            case Empty {
                ret 0.0
            }
            case Square(f64 side) {
                ret side * side
            }
        }
    }

    string name() const {
        ret "tile"
    }
}

void describe(Named n, Shape s) {
    println("{}: {:.2f}", n.name(), s.area())
}

i32 main() {
    Circle c
    c.radius = 2.0
    describe(c, c)
    Tile t = Tile.Square(3.0)
    describe(t, t)
    ret 0
}
```

```output
circle: 12.57
tile: 9.00
```

### Parameters of an interface

A parameter of an interface is a parameter as the others ([Ownership](ownership.md)): without a mode it is read-only, and only the `const` methods of the interface can be called on it. With `ref`, the function can call the methods that modify the value, and the caller writes `ref` too:

```cromo
interface Shape {
    f64 area() const
    void scale(f64 factor)
}

struct Square : Shape {
    f64 side

    f64 area() const {
        ret side * side
    }

    void scale(f64 factor) {
        side *= factor
    }
}

void enlarge(ref Shape s) {
    s.scale(2.0)
}

void show(Shape s) {
    println("area {:.2f}", s.area())
}

i32 main() {
    Square sq
    sq.side = 1.5
    enlarge(ref sq)
    show(sq)
    println("side {:.2f}", sq.side)
    ret 0
}
```

```output
area 9.00
side 3.00
```

Calling `s.scale(2.0)` in `show`, whose parameter is read-only, is the error `cannot call 'scale', which is not const`.

### Values of an interface

A variable, a field, a value returned, a `move` parameter, the elements of a list and the values of a map can be of an interface. A struct becomes the value of an interface by moving to the heap, together with its table of methods; when the value ends, the struct is destroyed:

```cromo
interface Shape {
    f64 area() const
}

struct Circle : Shape {
    f64 radius

    f64 area() const {
        ret 3.14159 * radius * radius
    }
}

// ...
#
# struct Square : Shape {
#     f64 side
#
#     f64 area() const {
#         ret side * side
#     }
# }

i32 main() {
    Circle small
    small.radius = 1.0
    Square big
    big.side = 3.0
    list<Shape> shapes = [small, big]
    f64 total = 0.0
    for (Shape s in shapes) {
        total += s.area()
    }
    println("{:.2f}", total)
    ret 0
}
```

```output
12.14
```

Here `small` and `big` are copied into the list, as a struct is copied when it is given without `move`; a struct that cannot be copied, as one with its own destructor, is given with `move`.

The other places work the same way: the value of an interface is passed with `move` and returned with `ret`, as a struct that owns resources:

```cromo
interface Shape {
    f64 area() const
}

struct Circle : Shape {
    f64 radius

    Circle(f64 r) {
        radius = r
    }

    f64 area() const {
        ret 3.14159 * radius * radius
    }
}

// ...
#
# struct Square : Shape {
#     f64 side
#
#     Square(f64 s) {
#         side = s
#     }
#
#     f64 area() const {
#         ret side * side
#     }
# }

struct Drawing {
    string title
    Shape main
}

Shape larger(move Shape a, move Shape b) {
    if (a.area() >= b.area()) {
        ret move a
    }
    ret move b
}

i32 main() {
    Shape first = Circle(1.0)
    Shape second = Square(2.0)
    Drawing plan
    plan.title = "plan"
    plan.main = larger(move first, move second)

    map<string, Shape> parts
    parts["door"] = Square(1.5)
    println("{:.2f} {:.2f}", plan.main.area(), parts["door"].area())
    ret 0
}
```

```output
4.00 2.25
```

- A value of an interface is never copied, only moved: copying it would need its type, which is not known. `Shape t = s` is the error `values of type Shape cannot be copied: write 'move s'`.
- Only the methods of the interface can be called on it: its fields cannot be reached (`a Shape has no fields`), and there is no way back to the type of the struct. When the type is needed, a generic or an enum does the work.
- Its default value is empty: a `Shape` variable declared without a value holds no struct, and neither does a field of type `Shape` after `move` took its struct away. Calling a method on an empty value is a panic, `the value of Shape is empty: it was moved, or never given`.

## Generics

A generic function has type parameters, written between `<` and `>` after its name. Each type parameter can be constrained by an interface, which says what the function can do with it:

```cromo
interface Comparable {
    bool less(Self other) const
}

struct Version : Comparable {
    i32 major
    i32 minor

    Version(i32 a, i32 b) {
        major = a
        minor = b
    }

    bool less(Version other) const {
        if (major != other.major) {
            ret major < other.major
        }
        ret minor < other.minor
    }
}

T largest<T : Comparable>(list<T> items) {
    T best = items[0]
    for (T item in items) {
        if (best.less(item)) {
            best = item
        }
    }
    ret best
}

i32 main() {
    list<Version> versions = [Version(1, 2), Version(2, 0), Version(1, 9)]
    Version newest = largest(versions)
    println("{}.{}", newest.major, newest.minor)
    ret 0
}
```

```output
2.0
```

- `T : Comparable` says that `T` can be any type that implements `Comparable`: inside `largest`, a `T` has the method `less`.
- The type arguments are deduced from the arguments of the call: `largest(versions)` is `largest<Version>`. When they cannot be deduced, they are written: `largest<Version>(versions)`.
- The compiler makes a version of the function for each type it is used with, as the templates of C++: the calls are direct, and as fast as with a function written for that type.

### Self

In an interface, `Self` is the type that implements it: `less` compares a `Version` with another `Version`, and `Version` writes its method with its own name, `bool less(Version other) const`. `Self` can be written only in the methods of an interface.

An interface that uses `Self` can only be a constraint, not the type of a parameter or of a value: through a `Comparable` alone, the type of `other` would not be known.

```cromo error
interface Comparable {
    bool less(Self other) const
}

void show(Comparable c) {
}

i32 main() {
    ret 0
}
```

```output
error: Comparable uses Self, so it can only be a constraint: <T : Comparable>
```

An interface has no type parameters of its own: `interface Container<T>` is the error `an interface has no type parameters: its methods use Self`. What changes with the type is written with `Self`, or the generic is a struct.

### Constraints

A type parameter can have no constraint, one interface, or several joined with `+`, and a function can have several type parameters, separated by commas, as `apply<T, U>` below:

```cromo
interface Shape {
    f64 area() const
}

interface Named {
    string name() const
}

// ...
#
# struct Circle : Shape, Named {
#     f64 radius
#
#     f64 area() const {
#         ret 3.14159 * radius * radius
#     }
#
#     string name() const {
#         ret "circle"
#     }
# }

void report<T : Shape + Named>(T item) {
    println("{}: {:.2f}", item.name(), item.area())
}

T first<T>(list<T> items) {
    ret items[0]
}

i32 main() {
    Circle c
    c.radius = 1.0
    report(c)
    list<string> words = ["one", "two"]
    println("{}", first(words))
    ret 0
}
```

```output
circle: 3.14
one
```

A constraint must be an interface, and the type of each call must implement it; both are checked by the compiler:

```cromo error
struct Point {
    i64 x
    i64 y
}

void wrong<T : Point>(T value) {
}

T largest<T : Ordered>(list<T> items) {
    ret items[0]
}

i32 main() {
    list<Point> points
    Point p = largest(points)
    ret 0
}
```

```output
error: a constraint must be an interface, found 'Point'
error: Point does not implement Ordered, required by T of 'largest'
```

`Ordered` is an interface of the library, described below in its own section.

### What a generic can do with T

A generic knows nothing of its `T` but its constraints. With a value of type `T` it can:

- declare it without a value, which gives the default value of the type: `0`, `""`, an empty list;
- copy it, with `=` or `ret`, and with `x.copy()`, which every type that can be copied has, numbers included;
- move it with `move`, and pass it to parameters of type `T` and to other generics;
- call the methods of its constraints.

```cromo
void swap<T>(ref T a, ref T b) {
    T keep = move a
    a = move b
    b = move keep
}

T orDefault<T>(list<T> items, i64 index) {
    T empty
    if (index < items.len()) {
        ret items[index]
    }
    ret empty
}

list<T> repeat<T>(T value, i64 times) {
    list<T> out
    for (i64 i = 0; i < times; i++) {
        out.add(value.copy())
    }
    ret out
}

i32 main() {
    string a = "left"
    string b = "right"
    swap(ref a, ref b)
    println("{} {}", a, b)
    list<i64> numbers = [10, 20]
    println("{} {}", orDefault(numbers, 1), orDefault(numbers, 5))
    println("{:?}", repeat(i64(7), 3))
    ret 0
}
```

```output
right left
20 0
[7, 7, 7]
```

Operators, comparisons, conversions and printing with `{}` do not work on a `T`, because no interface provides them: a generic that compares asks for `Ordered` and calls `compare`, one that prints asks for `Debug` and prints with `{:?}`.

```cromo error
T sum<T>(list<T> items) {
    T total
    for (T x in items) {
        total = total + x
    }
    ret total
}

bool same<T>(T a, T b) {
    ret a == b
}

void show<T>(T value) {
    println("{}", value)
}

i32 main() {
    ret 0
}
```

```output
error: '+' does not work on values of type T
error: '==' does not work on values of type T
error: a T cannot be printed with {}: print its form of debug with {:?}
```

These generics are never called, and the errors come all the same: why is explained below, in the section on when a generic is checked.

### Type arguments: deduced or written

The type arguments of a call are deduced from the types of its arguments, as in C++. They are written between `<` and `>` after the name when they cannot be deduced:

- A literal, as `5`, `1.5` or `[1, 2]`, has no type of its own: it takes the type that the other arguments give to `T`. When all the arguments of a `T` are literals, or there are none, `T` is written: `pick<i64>(true, 1, 2)`, `zero<string>()`.
- A lambda does not help: the type returned by `[](i64 n) string { ... }` does not deduce the `U` of `apply`.
- One `T` is one type: `pick(true, small, big)` with an `i32` and an `i64` is an error, even if one could become the other.

```cromo
T pick<T>(bool first, T a, T b) {
    if (first) {
        ret a
    }
    ret b
}

T zero<T>() {
    T value
    ret value
}

U apply<T, U>(T value, func(T) U f) {
    ret f(value)
}

i32 main() {
    i64 count = 4
    println("{}", pick(false, 5, count))
    println("{}", pick<f64>(true, 1.5, 2.5))
    println("{:?}", zero<string>())
    println("{}", apply<i64, string>(count, [](i64 n) string { ret format("<{}>", n) }))
    ret 0
}
```

```output
4
1.5
""
<4>
```

Without the type arguments, these calls do not compile, and the compiler says what to write:

```cromo error
T pick<T>(bool first, T a, T b) {
    if (first) {
        ret a
    }
    ret b
}

U apply<T, U>(T value, func(T) U f) {
    ret f(value)
}

i32 main() {
    i32 small = 1
    i64 big = 2
    i64 a = pick(true, 1, 2)
    i64 b = pick(true, small, big)
    string c = apply(big, [](i64 n) string { ret "x" })
    ret 0
}
```

```output
error: cannot deduce T in this call of 'pick': write the type arguments, pick<...>(...)
error: T is i32 for a previous argument, but i64 here: write the type arguments, pick<...>(...)
error: cannot deduce U in this call of 'apply': write the type arguments, apply<...>(...)
```

A function that is not generic takes no type arguments: `zero<i32>()` of a plain `zero()` is the error `'zero' is not generic`.

### When a generic is checked

The compiler checks the body of a generic once, where it is written, knowing of `T` only what the constraints promise. So the errors of a generic are found even if nobody calls it, they point to the generic, and a use with a new type does not discover new ones inside it.

Two things depend on the type itself, and are checked for each type the generic is used with.

**The copies of T.** A generic that copies a `T`, as `largest` with `T best = items[0]`, or `pick` with `ret a` of a read parameter, works with every type that can be copied. Used with a type that cannot, as a struct with its own destructor, the error is at the line of the generic that copies, and it names the use that asked for it:

```cromo error
struct File {
    i32 handle

    ~File() {
    }
}

T pick<T>(bool first, T a, T b) {
    if (first) {
        ret a
    }
    ret b
}

i32 main() {
    File a
    File b
    File c = pick(true, a, b)
    ret 0
}
```

```output
error: values of type File cannot be copied
(in pick<File>, used at line 18)
```

A generic that takes its values with `move` and gives them back with `ret move` copies nothing, and works with every type: `T pick<T>(bool first, move T a, move T b)`.

**The methods of a generic struct.** For each type, a method of a generic struct is checked, and compiled, only if the program uses it. So a `list<File>` works, although `File` cannot be copied, as long as the program does not call `copy()` of the list, the one method that copies the elements:

```cromo
struct File {
    string name

    File(string n) {
        name = n
    }

    ~File() {
        println("close {}", name)
    }
}

i32 main() {
    list<File> files
    files.add(File("a.txt"))
    files.add(File("b.txt"))
    println("{} files", files.len())
    ret 0
}
```

```output
2 files
close a.txt
close b.txt
```

Adding `list<File> other = files.copy()` to `main` is the error `values of type File cannot be copied`, at the line of the library that copies, with `(in list<File>, used at line ...)` that names the call.

### Interface or generic?

| | `void f(Shape s)` | `void f<T : Shape>(T s)` |
|---|---|---|
| The call | through the table of methods | direct, often inlined |
| The code | one function | one version for each type |
| Different types in one list | yes, `list<Shape>` | no, a `list<T>` has one type |
| An interface with `Self` | no | yes |

Use an interface for collections of different types and for code that does not need the speed; use a generic for algorithms over one type at a time.

## The interfaces of the library

The library declares some interfaces, which its functions use as constraints: `json.Data` is in the module `std.json`, the others in the core, which every program sees. The built-in types implement them by themselves, and a struct implements them as any other interface:

| Interface | Its methods | Implemented by | Needed by | Chapter |
|---|---|---|---|---|
| `Ordered` | `i32 compare(Self other) const` | the numbers and `string`, by themselves | `sort`, a generic that compares | [Lists, maps and text](collections.md) |
| `Key` | `u64 hash() const`, `bool equals(Self other) const` | the numbers, `bool` and `string`, by themselves | the keys of a `map` | [Lists, maps and text](collections.md) |
| `Debug` | none | every type but functions and interfaces, by itself | `{:?}` of a `T` | [Formatting text](formatting.md) |
| `Exception` | `string message() const` | `Error`, and the exceptions of the program | `throw` and `catch` | [Errors](errors.md) |
| `json.Data` | none | the types that JSON can hold, by themselves | `json.encode` and `json.decode` of a `T` | [JSON](json.md) |

`compare` gives a negative number when the value comes before `other`, zero when they are equal, and a positive number when it comes after; between two floats where one is a NaN it gives zero. Two values that are `equals` must have the same `hash`.

```cromo
struct Version : Ordered, Key {
    i32 major
    i32 minor

    Version(i32 a, i32 b) {
        major = a
        minor = b
    }

    i32 compare(Version other) const {
        if (major != other.major) {
            ret major - other.major
        }
        ret minor - other.minor
    }

    u64 hash() const {
        ret major.hash() ^ minor.hash() << 1
    }

    bool equals(Version other) const {
        ret major == other.major && minor == other.minor
    }
}

T largest<T : Ordered>(list<T> items) {
    T best = items[0]
    for (T item in items) {
        if (item.compare(best) > 0) {
            best = item
        }
    }
    ret best
}

void show<T : Debug>(string label, T value) {
    println("{}: {:?}", label, value)
}

i32 main() {
    list<Version> versions = [Version(2, 0), Version(1, 9), Version(1, 2)]
    sort(ref versions)
    show("sorted", versions)

    map<Version, string> names
    names[Version(1, 9)] = "stable"
    println("{}", names[Version(1, 9)])

    list<i64> numbers = [3, 9, 4]
    list<string> fruits = ["pear", "apple", "plum"]
    println("{} {}", largest(numbers), largest(fruits))
    show("newest", largest(versions))
    ret 0
}
```

```output
sorted: [Version { major: 1, minor: 2 }, Version { major: 1, minor: 9 }, Version { major: 2, minor: 0 }]
stable
9 plum
newest: Version { major: 2, minor: 0 }
```

`Debug` and `json.Data` have no methods, and a type does not name them: the compiler knows which types have a form of debug, and which ones it can convert to JSON. They exist for generics: a `T` is printed with `{:?}` only when the generic asks for `T : Debug`, and converted to JSON only with `T : json.Data`; otherwise the compiler says `T can be printed with {:?} only with a constraint: write <T : Debug>`.

`Exception` has no `Self`, so it is also a type, as in `catch (Exception e)`.

## Generic structs

A struct can have type parameters too:

```cromo
struct Stack<T> {
    list<T> items

    void push(move T value) {
        items.add(move value)
    }

    T pop() {
        ret items.remove(items.len() - 1)
    }

    bool empty() const {
        ret items.len() == 0
    }
}

struct Pair<A, B> {
    A first
    B second
}

i32 main() {
    Stack<string> stack
    stack.push("first")
    stack.push("second")
    println("{} {}", stack.pop(), stack.empty())

    Pair<string, i64> entry
    entry.first = "answer"
    entry.second = 42
    println("{:?}", entry)
    ret 0
}
```

```output
second false
Pair { first: "answer", second: 42 }
```

A generic struct is always written with its type arguments, also when its constructor is called: `Stack<string>`, `Pair<string, i64>("answer", 42)`. Unlike those of a function, they are not deduced: `Pair("answer", 42)` is the error `Pair is generic: write its type arguments, Pair<...>(...)`, and `Pair<string>` the error `'Pair' takes 2 type arguments, but 1 were given`.

Its type parameters can have constraints, and the struct can implement interfaces, as any other struct:

```cromo
interface Sized {
    i64 size() const
}

struct Sorted<T : Ordered> : Sized {
    list<T> items

    void insert(T value) {
        i64 i = 0
        for (i < items.len() && items[i].compare(value) < 0) {
            i++
        }
        items.insert(i, value)
    }

    i64 size() const {
        ret items.len()
    }
}

void report(Sized s) {
    println("{} elements", s.size())
}

i32 main() {
    Sorted<string> names
    names.insert("Carla")
    names.insert("Anna")
    names.insert("Bruno")
    println("{:?}", names.items)
    report(names)
    ret 0
}
```

```output
["Anna", "Bruno", "Carla"]
3 elements
```

The containers of the library, `list<T>` and `map<K, V>`, are generic structs written in Cromo in the same way. An enum can be generic too, as `enum Result<T, E>` in [Enums and switch](enums.md).

### Methods with their own type parameters

A method can have type parameters of its own, written after its name as for a function, `R name<U>(...)`: the method of `list<T>` that makes a list of another type is `list<U> map<U>(func(T) U f)`. Their type arguments are deduced from the arguments of the call, or written after the name of the method:

```cromo
struct Pair<A> {
    A first
    A second

    Pair<B> convert<B>(func(A) B f) const {
        Pair<B> result
        result.first = f(first)
        result.second = f(second)
        ret result
    }
}

struct Log {
    list<string> lines

    void add<T : Debug>(string label, T value) {
        lines.add(format("{} = {:?}", label, value))
    }
}

i32 main() {
    Pair<i64> size
    size.first = 3
    size.second = 4
    Pair<string> text = size.convert<string>([](i64 n) string { ret format("{} cm", n) })

    Log log
    log.add("size", size)
    log.add("text", text)
    list<i64> numbers = [1, 2]
    log.add("labels", numbers.map<string>([](i64 n) string { ret format("#{}", n) }))
    for (string line in log.lines) {
        println("{}", line)
    }
    ret 0
}
```

```output
size = Pair { first: 3, second: 4 }
text = Pair { first: "3 cm", second: "4 cm" }
labels = ["#1", "#2"]
```

- `log.add("size", size)` deduces `T` from `size`. `convert` and `map` take only a lambda, which does not help to deduce, so their type argument is written.
- The names of the type parameters of a method cannot be those of the struct: in `struct Box<T>`, a method `T get<T>()` is the error `'T' is already a type parameter of Box`.
- Only methods have them: not a constructor, and not a method of an interface, whose table has one entry for each method and cannot hold one for each type.

## Summary

- An interface lists the signatures of methods: no fields, bodies or constructors, and no interface extends another.
- A struct or an enum implements interfaces by naming them, `struct Circle : Shape, Named`, with each method written exactly as the interface has it.
- A parameter of an interface takes any type that implements it, without copying it; with `ref`, it can call the methods that modify.
- Values of an interface, as `list<Shape>`, own their struct on the heap; they are moved, never copied, and start empty.
- `Self` is the type that implements the interface; an interface that uses it is only a constraint.
- Generics have type parameters, constrained by interfaces joined with `+`; with a `T` they can only declare, copy, move and call the methods of the constraints.
- The type arguments are deduced from the arguments, except from literals alone and lambdas; otherwise they are written.
- A generic is checked once, where it is written; only the copies of `T`, and the methods of a generic struct that are used, are checked for each type.
- The library has `Ordered`, `Key`, `Debug`, `Exception` and `json.Data`, which the built-in types implement by themselves.
- There is no inheritance: shared behavior is an interface, shared data is a field.
