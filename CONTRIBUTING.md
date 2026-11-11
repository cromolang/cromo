# Contributing to Cromo

Thank you for looking at Cromo. The project is young and has one author: every kind of help counts, and criticism first of all.

## What Cromo wants to be

A language that is simple, safe enough, and that comes with a library with the wheels already on, for backends, apps and tools. When two rules give the same safety, the one that is easier to explain to a C or C++ programmer wins. A proposal that makes the language safer or faster but harder to explain will probably be declined, and that is not a judgment on its quality.

## Ways to help

**Try it and say what happened.** Install Cromo, write a program that you care about, and open an issue for what did not work, what was hard to find in the guide and what surprised you. A report from a real program is worth more than a patch.

**Break the rules of ownership.** A program without `import unsafe` that corrupts memory, reads a value that is gone, or has a data race, is a bug of Cromo: [What Cromo checks, and what it does not](book/en/appendix-safety.md) lists what should hold. A small program that shows a hole is the most useful report there is.

**Criticize the names.** Many names of the standard library are drafts, marked *Proposed* in [docs/decisions.md](docs/decisions.md). Before 1.0 they can change at no cost: say which ones read badly, and what the languages you know call the same thing.

**Write code.** These are the areas where a contributor can work without waiting for the author:

- examples and real programs written in Cromo;
- tests for the corners of the language that have none;
- the guide: mistakes, unclear passages, the Italian and the English;
- the standard library: bugs, missing functions of the modules that exist;
- macOS, which Cromo does not support yet;
- editors other than Visual Studio Code, which can use the language server of `cromo`.

## Reporting a bug

Open an [issue](https://github.com/cromolang/cromo/issues) with the output of `cromo version`, your system, the smallest program that shows the problem, what you expected and what happened. For a crash of the compiler, the program that makes it crash is enough.

Please do not report a hole in the safety of the language in private: until 1.0 nobody should rely on Cromo for anything that an open report could put at risk, and an open report helps the others too.

## Proposing a change to the language or to the library

In Cromo the decision comes before the code. Every choice of the design is an entry of [docs/decisions.md](docs/decisions.md), *Decided* with its date and its reasons, *Proposed*, or *Open*, and the author takes each one.

1. Open an issue that says the problem, with a program that shows it, before the solution.
2. Give the options you see, with what each one costs, and how C++, Go and Rust solve it. Cromo prefers what those languages already do to a new mechanism.
3. If the author decides for it, the decision is written in `docs/decisions.md`, and then the code can follow.

A pull request that changes the language without a decision behind it will be asked to start from an issue.

## Sending code

[BUILD.md](BUILD.md) says how to build the compiler and run the tests. Then:

- Open an issue first for anything larger than a fix, to agree on the direction.
- Every new feature of the compiler comes with tests in `tests/`: `.cro` files that say in their comments what they must print, or the error they must give. The head of `tests/run-tests.ps1` lists the forms.
- A change to the library is tested on Windows and on Linux: `tests\run-tests.ps1` and `tests\run-tests.ps1 -Flags '--target linux'`.
- The library, the examples and `compiler/` are formatted with `cromo fmt`.
- Everything in the repository is in English: code, comments, messages and documents. The guide is also in Italian, in `book/it`.
- Keep a pull request to one subject, with a message that says what changes for who uses Cromo.

Your contributions are distributed under the licenses of Cromo, MIT and Apache 2.0, as the rest of the repository.

## AI assistants

Cromo itself is written with an AI assistant, under the direction of its author, and contributions written the same way are welcome on the same terms: you have read what you send, you have run its tests, and you can answer for it in the review. A pull request that its sender cannot explain will be closed.

## Conduct

Be kind, be specific, and criticize the code and the design, not the people. The author can remove comments and block who does otherwise.
