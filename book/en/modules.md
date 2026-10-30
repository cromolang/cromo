# Modules

A program grows beyond one file by dividing its code into **modules**. Cromo has no header files and no preprocessor: a file is a module, a folder is a module, and `import` makes the public names of a module visible.

## A file is a module

Next to `main.cro`, a file `geometry.cro`:

```cromo file=geometry.cro
public struct Point {
    public f64 x
    public f64 y
    i32 visits
}

public f64 distance(Point a, Point b) {
    ret root(square(a.x - b.x) + square(a.y - b.y))
}

f64 square(f64 v) {
    ret v * v
}

// The square root with Newton's method, enough for an example.
f64 root(f64 v) {
    f64 guess = v / 2.0
    for (i32 i = 0; i < 20; i++) {
        guess = (guess + v / guess) / 2.0
    }
    ret guess
}
```

The program imports it by its name:

```cromo file=main.cro
import geometry

i32 main() {
    Point a
    Point b
    b.x = 3.0
    b.y = 4.0
    println("{}", distance(a, b))
    ret 0
}
```

```output
5
```

`cromo build` or `cromo run` in the folder compiles `main.cro` and the modules it imports, and the modules they import, all together.

## Visibility

Everything in a module is private to it, unless it is marked `public`: functions, structs, and also the fields, methods and constructors of a struct. Above, `Point` and `distance` are public; `square`, `root` and the field `visits` are not:

```cromo error
import geometry

i32 main() {
    println("{}", square(2.0))
    Point p
    println("{}", p.visits)
    ret 0
}
```

```output
error: 'square' of geometry is private to its module: it is not marked public
error: field 'visits' of Point is private to its module: it is not marked public
```

What a public declaration shows must be public too: a public function cannot return a private struct. The destructor has no `public`: it runs wherever the object ends.

## Using the names

After `import geometry`, its public names are used directly: `distance(a, b)`, `Point p`. The name of the module in front, `geometry.distance(a, b)`, is needed only when two imported modules give the same name, or to make the code clearer. A name is looked for first in the module itself, then in the imported modules, then in the standard library, so a name added to the library never breaks an existing program.

The imports are written at the top of the file, and each file writes its own: importing is not transitive. Two modules can import each other.

## A folder is a module

`import shapes` imports the folder `shapes`, with all its `.cro` files, as one module:

```cromo file=shapes/circle.cro
public f64 circleArea(f64 r) {
    ret 3.14159 * r * r
}
```

```cromo file=shapes/square.cro
public f64 squareArea(f64 s) {
    ret s * s
}
```

```cromo file=areas.cro
import shapes

i32 main() {
    println("{:.2f} {}", circleArea(1.0), squareArea(2.0))
    ret 0
}
```

```output
3.14 4
```

`import a.b` is the file `a/b.cro`, or the folder `a/b`. The paths start from the folder of the main file, so a module has the same name wherever it is imported. The files of a folder share their private names, as parts of one module; the subfolders are separate modules.

## The standard library

The core of the library, with `list`, `map`, `string`, `format` and `Exception`, is visible everywhere. The other modules are imported with the prefix `std`:

| Module | Content | Chapter |
|---|---|---|
| `std.fs` | files and folders | 14 |
| `std.os` | the environment of the process | 14 |
| `std.process` | running other programs | 14 |
| `std.time` | clocks, dates, pauses | 14 |
| `std.thread` | threads, tasks, mutexes, channels, atomics | 13 |
| `std.net` | TCP, UDP, TLS | 15 |
| `std.http` | HTTP client and server | 15 |
| `std.websocket` | WebSocket client and server | 15 |
| `std.json` | JSON | 16 |
| `std.crypto` | hashes, random bytes, encryption, signatures | 15 |
| `std.gzip` | compression | 15 |

After `import std.fs`, the functions of the module are written with its last name: `fs.readText(path)`.

## Code for each system

A module can have parts written for one system: next to `files.cro`, the files `files_windows.cro` and `files_linux.cro`. Only the file of the system the program is compiled for is used, and it is part of the module, with access to its private names. The standard library uses this to call the system of Windows and the one of Linux behind the same functions.

## Packages

The libraries of other people will be **packages**: Git repositories, named by their URL, with versions given by their tags, listed in a file `cromo.mod` and downloaded by `cromo`. The design is decided, and it is described in `docs/language.md`; it is not available yet.

## Summary

- A file is a module; a folder is a module made of its files; `import` makes their public names visible.
- Everything is private unless it is `public`, also the fields and methods of a struct.
- The names of an imported module are used directly, with the module in front when two clash.
- `std.x` are the modules of the standard library; `_windows` and `_linux` files hold code for one system.
