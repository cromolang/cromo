# Modules and projects

A program grows beyond one file by dividing its code into **modules**. Cromo has no header files and no preprocessor: a file is a module, a folder is a module, and `import` makes the public names of a module visible. A **project** with more programs lists them in a file `cromo.mod`.

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

The program imports it by its name, the name of the file without `.cro`:

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

`cromo build main.cro` or `cromo run main.cro` compiles `main.cro`, the modules it imports, and the modules they import, all together, into one executable. There is nothing to compile apart and nothing to link: a module is compiled into each program that imports it, and the executable keeps only the functions that it calls.

The main file is a module too, named after its file: `main` here. In a stack trace the functions of the other modules have their module in front, as `geometry.square`, and those of the main file only their name.

The comments at the beginning of a module, separated by a blank line from what follows, document it: the editor shows them on the name of the module, in its `import` and in `geometry.distance`. [Tools: formatting, the editor and the debugger](tools.md) explains the documentation comments.

## Visibility

Everything in a module is private to it, unless it is marked `public`: functions, structs, enums and interfaces, and also the fields, the methods and the constructor of a struct. Above, `Point`, its fields `x` and `y`, and `distance` are public; `square`, `root` and the field `visits` are not:

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

Inside its module a private name is used freely: `distance` calls `root` and `square`. A private field of a public struct is still part of every value of it, with its default value: other modules only cannot name it.

Some declarations are public with what contains them, and do not take `public`:

- the **cases** of an enum, with their enum;
- the **methods** of an interface, with their interface;
- the method of a struct that **implements an interface**: it can be called through the interface anyway, so in another module it is visible as the struct is;
- the **destructor**: it runs wherever the object ends.

The **constructor** instead is private unless it is marked `public`, as a method: only its module can call it, and it can offer a public function that does. The other modules can still declare a variable of the struct, `Ticket t`, which has its default value. A module `figures.cro` shows all of these:

```cromo file=figures.cro
public enum Unit {
    Meter
    Foot
}

public interface Shape {
    f64 area() const
}

public struct Square : Shape {
    public f64 side
    public Unit unit

    f64 area() const {
        ret side * side
    }
}

public struct Ticket {
    i64 number

    Ticket(i64 n) {
        number = n
    }

    public i64 value() const {
        ret number
    }
}

public Ticket nextTicket() {
    ret Ticket(42)
}
```

```cromo
import figures

void show(Shape s) {
    println("area {}", s.area())
}

i32 main() {
    Square q
    q.side = 2.0
    q.unit = Unit.Foot
    show(q)
    println("{} {}", q.area(), q.unit == Unit.Foot)
    println("ticket {}", nextTicket().value())
    ret 0
}
```

```output
area 4
4 true
ticket 42
```

The constructor of `Ticket` is not public, so only `nextTicket` makes tickets:

```cromo error
import figures

i32 main() {
    Ticket t = Ticket(7)
    ret 0
}
```

```output
error: the constructor of Ticket is private to its module: it is not marked public
```

Writing `public` where it does not belong is an error that says why: `the cases of an enum are public with it`, `the methods of an interface are public with it`, `a destructor has no 'public': it runs wherever its object ends`. A constant of a module, as `const i64 limit = 1024`, is private unless it is written `public const`, as the next section shows.

What a public declaration shows must be public too: a public function cannot take or return a private struct, and a public field or method cannot have a private type, since another module could not write that type:

```cromo error
struct Order {
    i64 id
}

public Order newest() {
    Order o
    ret o
}

i32 main() {
    ret 0
}
```

```output
error: function 'newest' is public, but its type Order is private to the module
```

### Public constants

A constant of a module follows the same rule: with `public` the modules that import it see it, with its name alone or with the module in front; without it, only its module does.

```cromo file=limits.cro
public const i64 maxUsers = 100
public const f64 half = 0.5
const i64 hidden = 7
```

```cromo file=quota.cro
import limits

i32 main() {
    println("{} {}", maxUsers, limits.half)
    ret 0
}
```

```output
100 0.5
```

`limits.hidden` in `quota.cro` would be the error `the module limits has no public constant 'hidden'`. A module variable of the unsafe dialect is never public: it is state ([The unsafe dialect](unsafe.md)).

## Imports

The imports are written at the top of the file, before the declarations, one module for each `import`:

```cromo error
i32 main() {
    ret 0
}

import geometry
```

```output
error: the imports come before the declarations of the module
```

An import is never public, and importing is not transitive: each file sees only the modules that it imports itself, so the imports at the top of a file say everything it uses. Two modules can import each other: the program is compiled all together, so there is no order to respect. Here two modules call each other:

```cromo file=even.cro
import odd

public bool isEven(i32 n) {
    if (n == 0) {
        ret true
    }
    ret isOdd(n - 1)
}
```

```cromo file=odd.cro
import even

public bool isOdd(i32 n) {
    if (n == 0) {
        ret false
    }
    ret isEven(n - 1)
}
```

```cromo
import even

i32 main() {
    println("{} {}", isEven(10), isEven(7))
    ret 0
}
```

```output
true false
```

The program imports only `even`, so `isOdd` is not visible in it, although `even` imports `odd`:

```cromo error
import even

i32 main() {
    println("{}", isOdd(3))
    ret 0
}
```

```output
error: unknown function 'isOdd'
```

`public import` is an error: `an import is not public: each module writes its own`.

## Using the names

After `import geometry`, the public names of the module are used directly: `distance(a, b)`, `Point p`. The name of the module can be written in front, `geometry.distance(a, b)`, `geometry.Point p`, `figures.Unit.Foot`: it is needed only when two imported modules give the same name, and it can make the code clearer.

The name in front is the last name of the path of the import: after `import tools.geometry` it is `geometry.distance`, never `tools.geometry.distance`. A name is looked for in three places, in order:

1. the module itself;
2. the modules that the file imports;
3. the core of the standard library, which every module sees.

So a name of the module hides the same name of an import, and a name added to the core of the library never breaks a program that already uses it. Two modules that give the same name to a file that imports both are an error only where the name is used without the module in front. With two modules `disk.cro` and `tile.cro`:

```cromo file=disk.cro
public f64 area(f64 radius) {
    ret 3.14159 * radius * radius
}
```

```cromo file=tile.cro
public f64 area(f64 side) {
    ret side * side
}
```

```cromo error
import disk
import tile

i32 main() {
    println("{}", area(1.0))
    ret 0
}
```

```output
error: 'area' is in both disk and tile: write disk.area or tile.area
```

```cromo
import disk
import tile

// A name of the module comes before those of the imports.
string join(list<string> parts, string separator) {
    ret "my own join"
}

i32 main() {
    println("{} {}", disk.area(1.0), tile.area(2.0))
    println("{}", join(["a", "b"], ","))
    ret 0
}
```

```output
3.14159 4
my own join
```

Since the last name of a module is written in front of its names, two imports with the same last name in one file are an error. Cromo has no way to rename an import: rename one of the modules.

```cromo file=plot/geometry.cro
public f64 scale() {
    ret 2.0
}
```

```cromo error
import geometry
import plot.geometry

i32 main() {
    ret 0
}
```

```output
error: another import is also called geometry
```

A variable, a parameter or a field can have the name of an imported module: where it is visible, the name is the variable, as in Go. A module cannot be called `llvm`, and neither can the file of a program: LLVM keeps that name for its own symbols.

```cromo
import tile

i32 main() {
    f64 before = tile.area(2.0)
    f64 tile = 3.0  // from here to the end of the block, tile is the variable
    println("{} {}", before, tile)
    ret 0
}
```

```output
4 3
```

## A folder is a module

`import shapes` imports the folder `shapes`, with all its `.cro` files, as one module:

```cromo file=shapes/circle.cro
public f64 circleArea(f64 r) {
    ret pi() * r * r
}
```

```cromo file=shapes/square.cro
public f64 squareArea(f64 s) {
    ret s * s
}
```

```cromo file=shapes/constants.cro
// Private: only the files of the folder see it.
f64 pi() {
    ret 3.14159
}
```

```cromo file=areas.cro
import shapes

i32 main() {
    println("{:.2f} {}", circleArea(1.0), shapes.squareArea(2.0))
    ret 0
}
```

```output
3.14 4
```

The files of a folder are parts of one module, as the files of a package in Go:

- each file sees all the names of the others, also the private ones, as `circle.cro` uses `pi` of `constants.cro`;
- two files cannot declare the same name: it is the error of a name declared twice, as `function 'pi' is already defined`;
- each file writes its own imports, which the other files of the folder do not see;
- outside the folder only the public names are seen, with the name of the folder in front when it is needed: `shapes.squareArea`.

So a module can be split into files as it grows, and the programs that import it do not change. The subfolders of a folder are modules of their own, imported with their path: `import shapes.solid` is the folder `shapes/solid`, whose names are written `solid.volume` when needed, and `import shapes` does not include it.

A file of a folder module belongs to it: importing the file as well as its folder, in any file of the program, is an error at the second import. Imported alone, `import shapes.circle` would make the file a module of its own, without the names of the other files of its folder: import the folder.

```cromo error
import shapes
import shapes.circle

i32 main() {
    ret 0
}
```

```output
error: the file shapes/circle.cro is a part of the module shapes: import shapes
```

A folder without `.cro` files, or only with files of other systems, is an error too: `the folder empty has no .cro files for this system`.

## The methods of a struct in more files: extend

A struct with many methods does not have to stay in one file. A block `extend Name { ... }` adds methods to a struct or an enum of the same module, in another file of the module or in the same one:

```cromo file=bank/account.cro
public struct Account {
    string owner
    i64 balance

    public Account(string name) {
        owner = name
    }

    public i64 total() const {
        ret balance
    }
}
```

```cromo file=bank/moves.cro
extend Account {
    public void deposit(i64 amount) {
        balance += amount
    }

    public bool withdraw(i64 amount) {
        if (amount > balance) {
            ret false
        }
        balance -= amount
        ret true
    }
}
```

```cromo file=cash.cro
import bank

i32 main() {
    Account account = Account("Anna")
    account.deposit(100)
    bool done = account.withdraw(30)
    println("{} {}", account.total(), done)
    ret 0
}
```

```output
70 true
```

Inside the block the methods are written as in the struct: they see its fields, also the private ones, and each says whether it is `public`. The fields, the cases of an enum, the constructor, the destructor and the interfaces stay in the declaration, which alone says what the struct is. A generic struct is extended with its name alone, `extend Stack { ... }`, and its type parameters are those of the declaration.

Only the module of a struct can extend it: `extend` on a struct of another module is the error `there is no struct or enum 'Account' in this module to extend`. `extend` is a word of the language: it cannot be the name of a variable or of a function.

## Where the modules are

`import a.b` is the file `a/b.cro`, or the folder `a/b`. The paths start from the folder of the main file, or from the folder of `cromo.mod` in a project (below): so a module has the same name wherever it is imported, also from a file in a subfolder. A file and a folder with the same name, as `geometry.cro` and `geometry/`, are an error at the import: `both geometry.cro and the folder geometry exist: rename one of them`.

A module that is not there is an error that says where it was looked for:

```cromo error
import charts

i32 main() {
    ret 0
}
```

```output
error: cannot find the module charts: there is no charts.cro or folder charts next to
```

The message ends with `next to` the main file, as `next to main.cro`, or `next to cromo.mod` in a project.

## Code for each system

A module can have parts written for one system, in files whose names end with the name of the system: next to `platform.cro`, the files `platform_windows.cro` and `platform_linux.cro`. Only the file of the system the program is compiled for is used, and it is part of the module, with access to its private names. In the same way a part can be for one architecture, `platform_x86_64.cro` or `platform_arm64.cro`, or for a system and an architecture, `platform_linux_arm64.cro`.

```cromo file=platform.cro
// What the program runs on.
public string describe() {
    ret format("{} on {}", system(), architecture())
}

string prefix() {
    ret "the system"
}
```

```cromo file=platform_windows.cro
string system() {
    ret prefix() + " Windows"
}
```

```cromo file=platform_linux.cro
string system() {
    ret prefix() + " Linux"
}
```

```cromo file=platform_x86_64.cro
string architecture() {
    ret "x86-64"
}
```

```cromo file=platform_arm64.cro
string architecture() {
    ret "ARM64"
}
```

```cromo nocheck
import platform

i32 main() {
    println("{}", platform.describe())
    ret 0
}
```

```output
the system Windows on x86-64
```

Compiled with `--target linux` the same program prints `the system Linux on x86-64`, and with `--target linux-arm64` `the system Linux on ARM64`. Each version of `system` exists only for its system, so the two never clash, and a file of another system is not even read: it can call functions that exist only there.

In a folder module the same rule holds for each file: the files of another system or architecture are left out, those of the system and of the architecture of the build join the module. A file `network_linux.cro` in a folder is a part of the module only on Linux, also when there is no `network.cro`. The main file of a program has no such parts: the code of a system goes in a module.

The standard library uses this to call Windows and Linux behind the same functions: `std.fs` is `fs.cro`, `fs_windows.cro`, `fs_linux.cro` and the files of each architecture.

## The standard library

The core of the library, with `list`, `map`, `string`, `format`, `Exception`, `Error`, `join`, `sort` and the built-in functions as `println`, is visible in every module without an import. The other modules are imported with the prefix `std`:

| Module | Content | Chapter |
|---|---|---|
| `std.fs` | files and folders | [Files, processes and time](system.md) |
| `std.os` | the environment of the process | [Files, processes and time](system.md) |
| `std.cmdline` | the options of the command line | [Files, processes and time](system.md) |
| `std.process` | running other programs | [Files, processes and time](system.md) |
| `std.zip`, `std.tar` | reading archives | [Files, processes and time](system.md) |
| `std.time` | clocks, dates, pauses | [Files, processes and time](system.md) |
| `std.thread` | threads, tasks, mutexes, channels, atomics | [Threads, tasks and channels](concurrency.md) |
| `std.net` | TCP, UDP, TLS | [Networking and HTTP](networking.md) |
| `std.http` | HTTP client and server | [Networking and HTTP](networking.md) |
| `std.websocket` | WebSocket client and server | [Networking and HTTP](networking.md) |
| `std.crypto` | hashes, random bytes, encryption, signatures | [Networking and HTTP](networking.md) |
| `std.jwt` | JSON Web Tokens | [Networking and HTTP](networking.md) |
| `std.gzip` | compression | [Networking and HTTP](networking.md) |
| `std.json` | JSON | [JSON](json.md) |
| `std.regex` | regular expressions | [Regular expressions](regex.md) |
| `std.db` | SQLite and PostgreSQL | [Databases](databases.md) |
| `std.math` | roots, powers, logarithms, trigonometry | [Numbers and operators](operators.md) |
| `std.log` | the log of a program | [Files, processes and time](system.md) |
| `std.gui` | windows, components and apps for Android | [Windows and apps](gui.md) |
| `std.audio` | sounds and music | [Windows and apps](gui.md) |

The modules of the library follow the rules of the others: after `import std.fs`, its names are found also without the module in front. The guide and the library write them with their last name in front all the same, `fs.readText(path)`, `time.now()`, because a name as `readText` or `now` says less without it.

A module that the library does not have is an error that lists those it has:

```cromo error
import std.io

i32 main() {
    ret 0
}
```

```output
error: the standard library has no module std.io: it has std.audio, std.cmdline, std.crypto, std.db,
```

## Projects: cromo.mod

A program with a few modules needs nothing more: its main file and the modules next to it. A project that makes more executables, as a server, a client and a tool, keeps their main files in a folder of their own and the modules they share at the top. The file `cromo.mod` marks the top of the project and lists its executables:

```text
project/
  cromo.mod
  geometry.cro       a module of a file
  shapes/            a module of a folder
  cmd/
    areas.cro        a program, with main
    report.cro       another program
```

```text file=cromo.mod
// The executables of the project.
module example.com/project

bin areas cmd/areas.cro
bin report cmd/report.cro
```

`cromo.mod` has one thing on each line, its words separated by spaces, with blank lines and `//` comments where needed:

- `module name` is the first line, once: the name of the project, made of letters, digits and `.`, `-`, `_` and `/`, as `example.com/project`. It is the address of the project as a package, and it marks the top of the project.
- `bin name path` is an executable: its name, made of letters, digits, `-` and `_`, different for each line, and its main file, a `.cro` file written from the folder of `cromo.mod` with `/`.

### The root of the imports

`cromo` looks for `cromo.mod` in the folder of the main file and in the folders above it, up to the root of the drive, and stops at the first one it finds. The imports of every file of the program then start from the folder of `cromo.mod`, not from that of the main file. So `cmd/areas.cro` imports `shapes`, at the top of the project, as `areas.cro` did next to it:

```cromo file=cmd/areas.cro
import shapes

i32 main() {
    println("{:.2f}", circleArea(2.0))
    ret 0
}
```

```output
12.57
```

A module next to `cmd/areas.cro`, as `cmd/units.cro`, is `import cmd.units`: in a project every module has one name, from the top. Without `cromo.mod` the paths start from the folder of the main file, as in the first sections of this chapter.

### Building, running and testing a project

In the folder of `cromo.mod`, the commands take the executables of its `bin` lines:

| Command | Does |
|---|---|
| `cromo build` | builds every executable, one after the other, with the same options, each named after its line: `areas.exe` and `report.exe` on Windows, `areas` and `report` on Linux, in the folder of `cromo.mod`; it stops at the first that fails |
| `cromo build areas` | builds only `areas` |
| `cromo run areas -- x y` | builds `areas` in a temporary folder and runs it with the arguments after `--` |
| `cromo run` | runs the only executable of `cromo.mod`; with more than one it is an error that lists them |
| `cromo test` | runs the tests of each executable in turn, after a line with its name and its main file; the exit code is 1 if a test of any of them fails |

A name without an extension that is not a file, as `areas`, is an executable of `cromo.mod`. With `--target android` each executable is an APK, `areas.apk`. A `-o` with `cromo build` and no name is an error, since each executable has the name of its line; with a name, `cromo build areas -o tool.exe`, it names that one. After `cromo test` the names choose the tests, as for a single program ([Tests](tests.md)), in every executable.

```text
> cromo run
cromo: cromo.mod has the executables areas and report: write which one, as cromo run areas
> cromo build nothing
cromo: there is no file nothing, and cromo.mod has the executables areas and report
> cromo build -o all.exe
cromo: -o names one executable, and those of cromo.mod are named after their lines
```

Without `bin` lines, `cromo build`, `cromo run` and `cromo test` without a file use the `main.cro` of the current folder, as in a folder without `cromo.mod`. A file can always be named, as `cromo build cmd/areas.cro`, which writes `cmd/areas.exe` next to it: then `cromo` uses only the folder of `cromo.mod`, for the imports, and does not read its `bin` lines.

### The errors of cromo.mod

When `cromo` reads the executables, every wrong line of `cromo.mod` is an error with its number, and nothing is built. For this `cromo.mod`:

```text
module example.com/project
module again
bin areas cmd/areas.cro
bin areas cmd/report.cro
bin tool cmd/tool.cro
bin broken
library shapes
```

`cromo build` writes:

```text
cromo.mod:2: error: the line module comes first, once
cromo.mod:4: error: another executable is called areas
cromo.mod:5: error: there is no file cmd/tool.cro
cromo.mod:6: error: bin takes the name of an executable and its main file, as bin server cmd/server.cro
cromo.mod:7: error: unknown line library: cromo.mod has the lines module, bin, require, native, version, company, description, copyright and sign
```

A `cromo.mod` that does not start with `module`, or is empty, is `cromo.mod:1: error: cromo.mod starts with the line module, with the name of the project, as module example.com/app`.

### The version and the signature of an executable

An executable for Windows says what it is: Explorer shows its version, its description and who made it in the properties of the file. They are lines of `cromo.mod`, for all the executables of the project:

```text
module example.com/project
version 1.2.0
company Anna Rossi
description The areas of the shapes
copyright (c) 2026 Anna Rossi
bin areas cmd/areas.cro
```

`version` takes up to four numbers; the other three take a text, to the end of the line. Without any of them, and for a file without a project, the executable has no version. Every executable for Windows has also a manifest, which `cromo` writes by itself: the program starts with the rights of who starts it, the paths can be long, the C libraries linked with it take the texts in UTF-8, and a program with windows is sharp on every screen. On Linux these lines are not used.

An executable that other people download should be **signed**: without a signature Windows warns who starts it, and an antivirus can take a program that nobody knows for a dangerous one. `cromo` does not sign by itself, because the key of a certificate is in a token or in a service of its issuer: the line `sign` has the command that signs, and `cromo build --release` runs it on each executable it writes, with the path of the executable as its last argument:

```text
sign signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256
```

- An argument with spaces goes between `"`.
- A command that fails stops the build with an error.
- `cromo run`, `cromo test` and a build without `--release` do not sign, so the certificate is asked only for what is published.
- Only the `sign` of the project that is built is run, never that of a package. Building with `--release` a project of someone else runs its command: read its `cromo.mod` first.

### Libraries

A library of a project is a module, of a file or of a folder, that its executables import: `geometry` and `shapes` above. As in Go and Rust, it is compiled into each executable that imports it, so there is no library to build apart, no file to link and nothing to install next to the executables. A library for programs written in C, a DLL or a `.so`, is not available yet.

## Packages

The libraries of other people are **packages**: Git repositories, named by their address, with versions given by their tags. `cromo get` adds one to the project:

```sh
cromo get github.com/anna/geometry
```

It reads the tags of the repository, takes the last version, downloads it, and writes a line in `cromo.mod`:

```text
module example.com/app
require geometry github.com/anna/geometry v1.2.0
```

The first word after `require` is the name of the package in the imports of the project: `import geometry` is the module of the files in the root of the repository, and `import geometry.shapes` its file `shapes.cro` or its folder `shapes`. The name is the last one of the address, or the one written after it, as `cromo get github.com/anna/geometry maps`, when two packages would have the same.

- **Versions.** A version is a tag as `v1.2.0`. `cromo get github.com/anna/geometry@v1.4.0` takes that one, and `@v2` the last of the versions `v2`; without them it takes the last one, or the last of the major version already required. `v1` and `v2` of the same repository are two packages: a program can use both, under two names.
- **What is chosen.** When the project and its packages require different versions of the same package, the highest of those required is used, and never a newer one that nobody asked for: the same program is built from the same sources every time, with no file that pins the versions.
- **cromo.sum.** Next to `cromo.mod`, `cromo` writes `cromo.sum`, with the hash of the files of each version used. A tag that is moved on the server later gives other files, and is an error. Both files go in the repository of the project.
- **Downloads.** `cromo build`, `cromo run` and `cromo test` download what is missing by themselves, once: the versions are kept in `~/.cromo/packages`, for all the projects. `cromo` asks GitHub, GitLab and Codeberg for the archive of the tag over HTTPS, and needs no `git`; for another host it runs `git`.
- **A folder in place of a version.** `require geometry ../geometry` uses the package of a folder, to work on a program and on its package together. It holds only in the project that is built.

A package is written as any project: a repository with its modules, a `cromo.mod` whose `module` line is its address, and a tag for each version. There is no registry where to publish it.

### Packages with a native library

A package can bring a library written in C or C++, as a DLL, with the modules that call it ([Calling C](c-interop.md)). A small one is in the repository, next to its modules. A large one, as that of ONNX Runtime, stays where its authors publish it, and the `cromo.mod` of the package says where, with a line for each system:

```text
module github.com/cromolang/onnxruntime
native windows-x86_64 https://example.org/runtime-win-x64.zip sha256:aaa1...d127 runtime/lib/onnxruntime.dll
native linux-x86_64 https://example.org/runtime-linux-x64.tgz sha256:bb16...b503 runtime/lib/libonnxruntime.so.1.32.0=libonnxruntime.so.1
```

A line has the system, the address of an archive, its SHA-256, and the files of it to take; `path=name` gives a file another name. `cromo` downloads the archive of the system it compiles for, checks it, and puts the library next to the executable, where the program finds it when it starts. Who uses the package writes `cromo get` and nothing else:

```sh
cromo get github.com/cromolang/onnxruntime
```

```cromo ignore
import onnxruntime

i32 main() {
    onnxruntime.Session model = onnxruntime.load("digits.onnx")
    list<onnxruntime.Tensor> scores = model.run([onnxruntime.Tensor([1, 1, 28, 28], pixels())])
    println("{:?}", scores[0].data)
    ret 0
}
```

The packages of the project of Cromo are the repositories of [github.com/cromolang](https://github.com/cromolang).

## Summary

- A file is a module; a folder is one module made of its files, which share all their names, private ones too, while each writes its own imports; its subfolders are modules of their own.
- Everything is private unless it is `public`: functions, types, fields, methods and constructors. Enum cases, interface methods, the methods that implement an interface and destructors are public with their type; a module constant is private unless it is `public const`.
- A public declaration cannot show a private type.
- Imports come first in a file, are not public and not transitive; modules can import each other.
- Imported names are used directly; the module goes in front, with the last name of its path, when two imports give the same name. A name is looked for in the module, then in its imports, then in the core of the library.
- `import a.b` is `a/b.cro` or the folder `a/b`, from the folder of the main file or of `cromo.mod`.
- Files ending in `_windows`, `_linux`, `_x86_64` and `_arm64` are the parts of a module for one system or architecture.
- `std.x` are the modules of the library; its core is visible everywhere.
- `cromo.mod` marks the top of a project: the imports start from its folder, and its `bin` lines are the executables that `cromo build`, `cromo run name` and `cromo test` take. Its lines `version`, `company`, `description` and `copyright` go in the executables for Windows, and `sign` is the command that signs them after `cromo build --release`. A package is a repository named by its address: `cromo get` adds it to `cromo.mod` as a `require` line, and `cromo.sum` keeps its hash.
