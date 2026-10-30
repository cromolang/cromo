# Structs

A struct groups related values under one name, as in C. In Cromo it can also have a constructor, methods and a destructor, as a class of C++, but there is no inheritance: what different types have in common is expressed with interfaces, in chapter 9.

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

`Rectangle r` declares a rectangle with its default value: each field has the default of its type, here 0. A field can declare its own initial value:

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

The initial value can be any expression, and it is computed every time a default `Settings` is made.

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

A struct has at most one constructor. A parameter cannot have the name of a field, so that a name inside the struct always means one thing: `width = w`, never `width = width`.

When a constructor cannot do its job, for example because a file does not open, it throws an exception: the object then does not exist, and the fields already made are destroyed.

A struct without a constructor can still be made as a value, with `Rectangle()`: it is its default value. A variable declared without a value always gets the default value, even when the struct has a constructor: the constructor runs only when it is called.

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
    ret 0
}
```

```output
area 6, square true
a contains b: false
```

### const methods

`const` after the parameters, as in C++, says that the method does not modify the object. Only `const` methods can be called on a value that cannot be modified: a read parameter, a `const` variable, the element of a `for` over a list.

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

Without pointers, `const` is deep: a `const` method cannot modify even the strings and the lists inside the object.

## Destructors

The destructor, `~Name()`, runs when the object ends: at the end of its scope, when it is removed from a list, when the struct that contains it ends. It releases what the object holds. After the destructor, the fields are destroyed by themselves, the last declared first, so a destructor only deals with what the fields cannot release alone: a handle of the system, a temporary file, a message to print.

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

A destructor is never called by hand, and it cannot let an exception out: the compiler checks it.

A struct with a destructor is not a simple type: it is moved, and copied only if it writes its own `copy()` method.

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

`format` works as `println`, but returns the text instead of printing it.

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

## Summary

- A struct has fields, at most one constructor, methods and a destructor; inside it, the fields are used by name.
- Every field has a default value, or the initial value that it declares.
- `const` methods do not modify the object, and they are the only ones that can be called on a read-only value.
- The destructor releases what the object holds, automatically, at the right moment.
- `text()` prints a struct with `{}`; `{:?}` prints any value for debugging.
