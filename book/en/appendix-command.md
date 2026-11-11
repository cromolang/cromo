# Appendix B: The cromo command

`cromo` is the only tool of Cromo: the compiler, the linker, the test runner, the formatter and the language server, in one executable.

```text
cromo <command> [options] [file.cro | name] [-- arguments]
cromo fmt [--check] [files and folders | -]
```

The command always comes first: `cromo hello.cro` is an error that shows the right form, `the command comes first: cromo build hello.cro`. `cromo help`, `cromo -h` and `cromo --help` print the commands; `cromo` alone prints them as an error.

## Commands

| Command | Does |
|---|---|
| `cromo build [file.cro \| name]` | compiles the program into an executable |
| `cromo run [file.cro \| name] [-- arguments]` | compiles the program into a temporary folder, runs it with the arguments that follow `--`, and ends with its exit code |
| `cromo test [file.cro] [names]` | runs the `[[test]]` functions of the program and of its modules; the names choose the tests that contain them ([Tests](tests.md)) |
| `cromo fmt [--check] [files and folders \| -]` | formats the code, by default of the current folder and its subfolders ([Tools](tools.md)) |
| `cromo lsp` | the language server, for editors, on the standard input and output ([Tools](tools.md)) |
| `cromo design [file.form]` | the designer of the windows: a window composed with the mouse, saved as a form and as a module of Cromo ([Windows and apps](gui.md#designing-a-window-cromo-design)) |
| `cromo update [version]` | installs the latest version of Cromo, or the one named, and makes it the active one |
| `cromo version` | prints the version of `cromo` and of LLVM, as `cromo 0.72.0 (LLVM 23.1.4)` |
| `cromo help` | prints the commands and the options |

### The program to compile

`cromo build`, `cromo run` and `cromo test` find the program in this way:

- **A file**, as `cromo build hello.cro`: that file is the main file. The executable is named after it, `hello.exe` on Windows and `hello` on Linux, next to it.
- **A name** without an extension that is not a file, as `cromo build server`: the executable of that name in the `cromo.mod` of the current folder. `cromo test` takes no name: the words after it that do not end with `.cro` choose the tests.
- **Nothing**, in a folder whose `cromo.mod` has `bin` lines: `cromo build` builds every executable, `cromo run` runs the only one, and is an error if there are more, `cromo test` tests each one ([Modules and projects](modules.md)).
- **Nothing**, elsewhere: the `main.cro` of the current folder, and the executable is named after the folder. Without `main.cro`: `there is no main.cro in this folder: write the file to compile, as cromo build hello.cro`.

## Options of build, run and test

| Option | Meaning | Commands |
|---|---|---|
| `-o <file>` | the path of the executable, exactly as written: no extension is added, and its folder must exist | `build` |
| `--target <system>` | the system of the executable, below; by default the one `cromo` runs on | `build`; `run` only with `android` |
| `--cpu <processor>` | the x86-64 processor whose instructions the program uses, below | `build`, `run`, `test` |
| `--release` | integers wrap around on overflow, instead of stopping the program, and there is no debug information | `build`, `run`, `test` |
| `-O0`, `-O1`, `-O2`, `-O3` | the level of optimization; the default is `-O2` | `build`, `run`, `test` |
| `--emit-ir` | prints the LLVM IR of the program instead of writing the executable | `build` |

`cromo run` makes a program for the system on which `cromo` runs, and deletes it when it ends, so `-o`, `--target` and `--emit-ir` are options of `cromo build` only: `cromo run -o x hello.cro` is the error `-o is an option of cromo build`, and `cromo run --target linux` is `cromo run takes only --target android, which runs the app on the phone`. `cromo test` runs the tests on the system where `cromo` runs, with the same rule. The arguments after `--` are only for `cromo run`.

### Targets

| `--target` | The executable |
|---|---|
| `windows` | Windows x64: `hello.exe`, which needs only the DLLs of Windows, and those of the C libraries the program declares |
| `linux` | Linux x64: `hello`, static, that runs on any Linux x64; a program whose C libraries call the C library, as every program with `std.gui`, is linked with glibc and needs glibc 2.29 or later |
| `linux-arm64` | Linux on ARM64, as a Raspberry Pi or a server with Graviton: static, for every ARMv8; `std.gui` is not available there yet |
| `android` | the APK of an app, `hello.apk`, with the program for phones, ARM64, and for emulators, x86-64; Android 8 or later ([Windows and apps](gui.md)) |
| `android-arm64`, `android-x86_64` | a program for one architecture of Android, `hello`, for the dynamic loader of Android: it runs from a shell, as `adb shell`, not as an app |

Without `--target`, the program is for the system on which `cromo` runs. `cromo` compiles for all of them, on Windows and on Linux, with nothing else to install.

### --cpu

By default a program for x86-64 uses only the instructions that every x86-64 has, SSE2 included. `--cpu` lets it use more, everywhere in the program, as the vectors and the bit functions do:

| `--cpu` | The processor |
|---|---|
| `x86-64` | every x86-64: the default |
| `x86-64-v2` | with SSE4.2 and POPCNT |
| `x86-64-v3` | with AVX2, BMI2 and FMA |
| `x86-64-v4` | with AVX-512 |
| `native` | the processor of the computer that compiles, with what it has and lacks |
| a name, as `haswell` or `znver4` | a processor that LLVM knows |

A program compiled with `--cpu` checks at its start that the processor has the instructions it uses. If it does not, the program writes which ones are missing on the standard error, and ends with the exit code 1 before running any of its code:

```text
this program was compiled with --cpu x86-64-v4, and this processor lacks AVX512F, AVX512BW, AVX512CD, AVX512DQ, AVX512VL
```

An unknown name is an error before the compilation, which lists the accepted ones. `--cpu` names a processor of x86-64: with `--target linux-arm64` or `android-arm64` it is the error `--cpu names a processor of x86-64, and linux-arm64 is ARM64`, since those programs run on every ARMv8. With `--target android` it applies to the part of the APK for x86-64. The C code of the runtime and of the libraries stays compiled for every x86-64.

## cromo build

`cromo build` writes the executable, and on Windows, without `--release`, its debug information next to it: `hello.pdb`. If a module uses a DLL that is next to it, and the executable is in another folder, the DLL is copied next to the executable ([Calling C](c-interop.md)). In a project, `cromo build` without a file builds each executable of `cromo.mod`, with the same options, and stops at the first that fails; `-o` is then an error.

## cromo run

`cromo run hello.cro -- a b` compiles the program into the folder `cromo-run-<number of the process>` of the temporary folder of the system, runs it with the arguments `a` and `b`, deletes the folder, and ends with the exit code of the program. The executable has the name of the source, and the DLLs of its modules are copied next to it. On Windows the exit code is the whole value that the program returned; on Linux, a program ended by a signal gives 128 plus the number of the signal, as the shells say: 137 for `SIGKILL`, 139 for `SIGSEGV`.

### cromo run --target android

`cromo run --target android app.cro` makes the APK, installs it on the phone or on the emulator connected, starts the app, and shows its output until it ends:

- `adb` is the one in `platform-tools` of `ANDROID_HOME` or `ANDROID_SDK_ROOT`, else the one in the `PATH`; without it, `cromo` says so.
- With one device connected, it is used; with more, `cromo` lists them and stops, and the variable `ANDROID_SERIAL`, which `adb` knows, chooses one.
- What the app writes on the standard output and error goes to the log of Android, and `cromo run` shows it on its own standard output and error.
- `cromo run` ends with the exit code of the app; an app that crashes ends it with 1. Ctrl+C in the terminal stops the app too, and `cromo run` ends with 130.
- An app has no arguments: `--` is an error.

## cromo test

`cromo test` builds the program with the `[[test]]` functions and the `_test.cro` files of its modules, runs each test in a process of its own, writes `ok` or `FAIL` for each one, and ends with 1 if one failed: [Tests](tests.md) explains it.

## cromo fmt

`cromo fmt` formats the files in place and writes the names of those it changed; `--check` changes nothing, lists them and ends with 1 if there are some; `-` formats the standard input on the standard output. A file with syntax errors is left as it is. [Tools: formatting, the editor and the debugger](tools.md) lists the rules.

## cromo update

`cromo update` installs a new version of Cromo next to the one in use, and makes it the active one:

- `cromo update` takes the latest version. If it is the active one, it says so and changes nothing. A latest version older than the active one is refused, since an attacked site could give an old manifest that was signed when it was the latest: going back is only for a version named.
- `cromo update 0.27.1` takes that version, newer or older. A version already in `~/.cromo/versions` only becomes the active one, without the network.
- A new version is downloaded from cromolang.org: its manifest, whose Ed25519 signature `cromo` checks with the key it contains, then its archive, whose SHA-256 must be the one of the manifest. A version that fails a check is refused. The archive is unpacked into `~/.cromo/versions/<version>`, which appears only when it is complete.
- The link `~/.cromo/current` then points to the new version, which the `PATH` already reaches; the old versions stay in `~/.cromo/versions`.
- When the command `code` of Visual Studio Code is there, the extension of the new version is installed too.
- The variable `CROMO_SITE` names another site, as for the scripts of the installation.

`cromo update` ends with 0, or with 1 and the reason, as `cromo update: there is no version 0.99.0 on https://cromolang.org`.

## Debug and release

| | Debug (default) | `--release` |
|---|---|---|
| Optimization | `-O2` | `-O2` |
| Integer overflow | panic | wraps around |
| Bounds, division by zero, exclusivity | checked | checked |
| `assert` | checked | checked |
| Stack traces | yes | yes |
| Information for a debugger | yes: PDB on Windows, DWARF on Linux | no |

With `-O0` a debugger follows the program line by line; with the optimizations, some variables are *optimized out* and small functions disappear inside their callers ([Tools: formatting, the editor and the debugger](tools.md)).

## Exit codes

Of a program:

| Code | Meaning |
|---|---|
| 0 | `main` returned 0, or nothing |
| 1 | an exception left `main` or a thread; or the processor lacks the instructions of `--cpu` |
| 101 | a panic, or a stack overflow |
| other | the value returned by `main`, or given to `os.exit` |

On Linux the exit code is the lowest byte of the value, as in C: `ret -1` gives 255, and `ret 300` gives 44. On Windows it is the whole value. `cromo run` on Linux gives 128 plus the signal for a program ended by a signal.

Of `cromo`:

| Code | Meaning |
|---|---|
| 0 | done; for `cromo run`, the exit code of the program |
| 1 | the program has errors; a test failed, or no test has the names given; `cromo fmt --check` found files to format, or `cromo fmt` found syntax errors |
| 2 | the command line is wrong: an unknown command or option, a file or a `main.cro` that is not there, an error in `cromo.mod` |

## Files and variables

| Path | Content |
|---|---|
| `~/.cromo/current/bin/cromo` | the active version, which the `PATH` reaches |
| `~/.cromo/versions/<version>` | the installed versions |
| `lib/` next to `bin/` | the standard library and the runtime, which `cromo` reads |
| `extension/` next to `bin/` | the extension for Visual Studio Code |
| `~/.cromo/android` | the key with which `cromo` signs the APKs for debugging, made the first time |

| Variable | Use |
|---|---|
| `ANDROID_HOME`, `ANDROID_SDK_ROOT` | where `cromo run --target android` looks for `adb` |
| `ANDROID_SERIAL` | the device of `cromo run --target android`, with more than one |
| `CROMO_SITE` | another site for `cromo update` and the installation |

On Windows, `~` is `%USERPROFILE%`.

## Summary

- `cromo build`, `run` and `test` take a file, a name of `cromo.mod`, or nothing for the executables of `cromo.mod` or the `main.cro` of the folder.
- `-o`, `--target` and `--emit-ir` are options of `cromo build`; `cromo run` builds only for the system it runs on, or an app with `--target android`.
- `--target` is `windows`, `linux`, `linux-arm64`, `android`, `android-arm64` or `android-x86_64`; `--cpu` chooses the x86-64 processor, checked when the program starts.
- A program ends with 0, 1 for an exception, 101 for a panic, or its own code; `cromo` ends with 1 for the errors of the program and 2 for those of the command line.
- `cromo update` installs the latest or a named version, checked with its signature, and makes it the active one.
- `cromo design` composes a window with the mouse, and writes it as a module of Cromo.
