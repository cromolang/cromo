# The Cromo Guide

Welcome to *The Cromo Guide*, an introduction to the Cromo programming language.

Cromo is a general-purpose, compiled language. It has the syntax of C and C++, no pointers and no garbage collector. Every value has exactly one owner, and the resources a value holds, such as memory, files and connections, are released by its destructor when the owner goes out of scope. The compiler checks ownership with a small set of rules, and LLVM turns the program into fast native code.

## Why Cromo

Cromo brings together ideas that have proven themselves in other languages:

- the **syntax**, the constructors and the **destructors** of C and C++, so that a C or C++ programmer can read Cromo code from the first day;
- **ownership checked by the compiler**, as in Rust, with simpler rules: references exist only as parameters, so no lifetime ever needs to be written;
- **lightweight tasks and channels**, as in Go: thousands of tasks wait for the network at the same time, with ordinary blocking code and without `async` or `await`;
- **one tool** that builds, runs, tests and formats, as in Go and Zig;
- the **LLVM** optimizer and linker, which Clang, Rust and Swift also use.

The guiding criterion of the design is simplicity: when two rules give the same safety, Cromo chooses the one that is easier to explain to a C or C++ programmer.

## Who this guide is for

This guide assumes that you have written programs in at least one other language. It does not assume C or C++: when Cromo follows them, the guide says so, and it explains everything that a C++ programmer would take for granted.

If you already know C++, Go or Rust, [Appendix C](appendix-coming-from.md) summarizes the differences, and you can skim the first chapters.

## How to use this guide

The chapters are meant to be read in order: each one builds on the previous ones. Chapter 2 is a small but complete program, written before the details are explained; chapters 3 to 12 present the language; chapters 13 to 16 the standard library; chapter 17 shows how to call C, and chapter 18 puts everything together in a web service.

Every program in this guide is compiled and run when the guide is built, and its output is the one shown below it:

```cromo
i32 main() {
    println("Hello from the guide!")
    ret 0
}
```

```output
Hello from the guide!
```

A block with a red border is a program that does **not** compile, shown on purpose, followed by the error of the compiler:

```cromo error
i32 main() {
    string name = "Cromo"
    string other = move name
    println("{}", name)
    ret 0
}
```

```output
error: 'name' has been moved and can no longer be used
```

The guide is also available as a PDF, in English and in Italian: use the **PDF** button at the top of the page.

## The state of Cromo

Cromo is young. The current version is 0.33, and the language can still change before version 1.0. Every design decision is recorded, with its reasons, in the file `docs/decisions.md` of the [repository](https://github.com/cromolang/cromo), next to the draft specification `docs/language.md`.

Cromo runs on Windows x64 and Linux x64, and each version compiles programs for both systems, and for Linux on ARM64.

## The name

*Cromo* is the Italian word for chromium, element 24 of the periodic table, whose symbol Cr appears in the logo. On the web the language is *cromolang*, as in [cromolang.org](https://cromolang.org).
