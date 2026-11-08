# Getting started

In this chapter you install Cromo, write your first program, and meet `cromo`, the command that builds, runs, tests and formats Cromo code.

## Installation

Cromo is installed for your user, without administrator rights, with one line. On Windows, in PowerShell:

```powershell
irm https://cromolang.org/install.ps1 | iex
```

On Linux:

```sh
curl -sSf https://cromolang.org/install.sh | sh
```

The script downloads the archive of the latest version, checks its SHA-256, and unpacks it in `~/.cromo` (`%USERPROFILE%\.cromo` on Windows). It adds `~/.cromo/current/bin` to the `PATH` and, if it finds Visual Studio Code, installs the Cromo extension. Open a new terminal, then check the installation:

```sh
cromo version
```

```output
cromo 0.49.0 (LLVM 23.1.4)
```

That is all. `cromo` contains the compiler and the linker: it needs no C compiler, no Visual Studio and no Windows SDK. On Linux, `cromo` needs glibc 2.29 or later, as in Ubuntu 20.04, Debian 11 and their later versions; the distributions built on musl, as Alpine, are not supported.

When a new version comes out, `cromo update` installs it next to the one you have, after checking its signature, and makes it the active one; `cromo update 0.27.1` goes back to an older one. [The cromo command](appendix-command.md) explains the details.

> **Note:** the archive can also be installed by hand: download it from [the download page](https://cromolang.org/download/), with its SHA-256, unpack it anywhere and add its `bin` folder to the `PATH`. `cromo` finds the library in the `lib` folder next to `bin`.

## Hello, world!

Create a folder for your programs and, in it, a file named `hello.cro`:

```cromo file=hello.cro
i32 main() {
    println("Hello, world!")
    ret 0
}
```

Then run it:

```sh
cromo run hello.cro
```

```output
Hello, world!
```

Let us look at the program:

- `i32 main()` declares the function `main`, where every program starts. The type comes before the name, as in C: `main` returns an `i32`, a 32-bit integer, which becomes the exit code of the program.
- `println` prints a line. Its first argument is a format string, which can contain `{}` for the values that follow it: `println("{} + {} = {}", a, b, a + b)`.
- `ret` returns from a function. Cromo writes `ret` where C writes `return`.
- There are no semicolons: a statement ends at the end of its line.

A `main` that returns nothing is also allowed, and the exit code is then 0:

```cromo
void main() {
    println("Hello again!")
}
```

```output
Hello again!
```

## Building an executable

`cromo run` compiles the program into a temporary folder, runs it and deletes it. To keep the executable, use `cromo build`:

```sh
cromo build hello.cro          # writes hello.exe on Windows, hello on Linux
./hello
```

By default `cromo` builds a **debug** executable: it is optimized, but it checks every integer operation for overflow, and it contains the information that a debugger needs. With `--release` integers wrap around on overflow, as in C, and the executable is smaller:

```sh
cromo build --release hello.cro
```

The executable runs on any machine with the same system, without installing anything: on Windows it needs only Windows, and on Linux it is a static executable that runs on any Linux x64. The exception is a program for Linux that uses C libraries which call the C library of the system, as every program with windows: `cromo` links it with glibc, and it needs glibc 2.29 or later where it runs, as `cromo` itself.

`cromo` can also write an executable for the other system:

```sh
cromo build --target linux hello.cro     # on Windows: an executable for Linux
cromo build --target windows hello.cro   # on Linux: hello.exe for Windows
```

`--target linux-arm64` writes it for Linux on ARM64, as a Raspberry Pi or a server with Graviton, and `--target android` an app for Android ([Windows and apps](gui.md)). [The cromo command](appendix-command.md) lists every target and option.

## A project

A project is a folder with a file `main.cro`. Without a file name, `cromo build`, `cromo run` and `cromo test` use the `main.cro` of the current folder, and the executable is named after the folder:

```sh
mkdir greeter
cd greeter
# write main.cro
cromo run
```

The other files of the folder are modules, which `main.cro` imports by name. A project with more executables lists them in a file `cromo.mod`, and `cromo build` builds them all: [Modules and projects](modules.md) explains both.

## Formatting

Cromo code has one style, and `cromo fmt` writes it: four spaces of indentation, the opening brace on the same line, one space around operators. There are no options to discuss, and [Tools: formatting, the editor and the debugger](tools.md) lists its rules:

```sh
cromo fmt                 # every .cro file of this folder and its subfolders
cromo fmt hello.cro       # one file
cromo fmt --check         # only lists the files that are not formatted
```

## Visual Studio Code

The Cromo extension, which the installation adds to Visual Studio Code, colors the code and uses `cromo lsp`, the language server of Cromo, to show:

- the errors while you write, without compiling by hand;
- the type, the declaration and the documentation of a name, when the mouse is over it;
- the definition of a name, with F12;
- the members of a value after a dot, and the parameters of a call while you write it;
- the formatting of the file, when it is saved.

Any editor that speaks the Language Server Protocol can use `cromo lsp` in the same way. [Tools: formatting, the editor and the debugger](tools.md) explains the extension, its settings, the comments that document the code, and how to use a debugger.

## Summary

- `cromo run file.cro` compiles and runs a program; `cromo build` writes the executable.
- A program starts from `main`, which returns its exit code.
- The type comes before the name; `ret` returns; statements end at the end of the line.
- `cromo fmt` formats the code, and the extension for Visual Studio Code shows the errors while you write.
- The executables need nothing installed; on Linux, `cromo` and the programs with windows need glibc 2.29 or later.

In the next chapter you will write a complete program, before learning the details.
