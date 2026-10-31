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

### Values of an interface

A variable, a field or a list can hold values of an interface. A struct becomes one by moving to the heap, together with its table of methods; when the value ends, the struct is destroyed:

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

Here `small` and `big` are simple values, so the list receives copies; a struct that owns resources would be added with `move`. A value of an interface is never copied, only moved, and only the methods of the interface can be called on it. Its default value is empty, and calling a method on an empty value is a panic.

## Generics

A generic function has type parameters, written between `<` and `>` after its name. Each type parameter is constrained by an interface, which says what the function can do with it:

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

- `T : Comparable` says that `T` can be any type that implements `Comparable`. In an interface, `Self` is the type that implements it: `less` compares a `Version` with another `Version`.
- The type arguments are deduced from the arguments of the call: `largest(versions)` is `largest<Version>`. When they cannot be deduced, they are written: `largest<Version>(versions)`.
- A generic is checked once, where it is written: on a `T`, only the methods of its constraints can be called, and the errors speak of the generic, not of its uses.
- The compiler makes a version of the function for each type it is used with, as the templates of C++: the calls are direct, and as fast as with a function written for that type.

An interface that uses `Self`, as `Comparable`, can only be a constraint, not the type of a parameter or of a value.

### Interface or generic?

| | `void f(Shape s)` | `void f<T : Shape>(T s)` |
|---|---|---|
| The call | through the table of methods | direct, often inlined |
| The code | one function | one version for each type |
| Different types in one list | yes, `list<Shape>` | no, a `list<T>` has one type |

Use an interface for collections of different types and for code that does not need the speed; use a generic for algorithms over one type at a time.

## Generic structs

A struct can have type parameters too, and so can its methods:

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

A generic struct is always written with its type arguments: `Stack<string>`, `Pair<string, i64>`. The containers of the library, `list<T>` and `map<K, V>`, are generic structs written in Cromo in the same way.

## Summary

- An interface lists methods; a struct implements it with `struct Name : Interface`.
- A parameter of an interface type takes any struct that implements it, without copying it.
- Values of an interface, as `list<Shape>`, own their struct on the heap.
- Generics have type parameters constrained by interfaces, are checked once, and are compiled for each type.
- There is no inheritance: shared behavior is an interface, shared data is a field.
