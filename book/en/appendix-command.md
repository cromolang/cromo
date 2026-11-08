# Appendix B: The cromo command

`cromo` is the only tool of Cromo: the compiler, the linker, the test runner, the formatter and the language server, in one executable.

```text
cromo <command> [options] [file.cro] [-- arguments]
```

Without a file, the commands use the executables of the `cromo.mod` of the current folder, or its `main.cro` (chapter 11).

## Commands

| Command | Does |
|---|---|
| `cromo build [file.cro \| name]` | compiles the program into an executable; without a file, every executable of `cromo.mod`; with a name, that one |
| `cromo run [file.cro \| name] [-- arguments]` | compiles the program into a temporary folder, runs it with the arguments that follow `--`, and ends with its exit code |
| `cromo run --target android [file.cro]` | makes the APK, installs it on the phone or the emulator that `adb` sees, starts the app and shows its output until it ends |
| `cromo test [file.cro] [names]` | runs the `[[test]]` functions of the program and of its modules; the names choose the tests that contain them |
| `cromo fmt [--check] [files and folders \| -]` | formats the code, by default of the current folder and its subfolders; `--check` only lists the files to format; `-` formats the standard input |
| `cromo lsp` | the language server, for editors, on the standard input and output |
| `cromo update [version]` | installs the latest version of Cromo, or the one named, also an older one, and makes it the active one; the others stay in `~/.cromo/versions` |
| `cromo version` | prints the version of `cromo` and of LLVM |
| `cromo help` | prints the commands |

## Options of build and run

| Option | Meaning |
|---|---|
| `-o <file>` | the name of the executable; by default the name of the source, or of the folder without a file, with `.exe` for Windows |
| `--release` | integers wrap around on overflow, instead of stopping the program |
| `--target windows`, `--target linux`, `--target linux-arm64` | the system of the executable, also Linux on ARM64; by default the one `cromo` runs on |
| `--target android` | the APK of an app for Android, for phones and emulators; `cromo run` takes no other target |
| `-O0` … `-O3` | the level of optimization; the default is `-O2` |
| `--cpu <processor>` | the processor whose instructions the program uses: `x86-64`, the default, for every one; `x86-64-v2`; `x86-64-v3`, with AVX2; `x86-64-v4`, with AVX-512; `native`, the one of this computer; or a name as `haswell`. On a processor without them the program stops at the start and says which are missing |
| `--emit-ir` | prints the LLVM IR of the program instead of writing the executable |

## Debug and release

| | Debug (default) | `--release` |
|---|---|---|
| Optimization | `-O2` | `-O2` |
| Integer overflow | panic | wraps around |
| Bounds, division by zero, exclusivity | checked | checked |
| Stack traces | yes | yes |
| Information for a debugger | yes: PDB on Windows, DWARF on Linux | no |

With `-O0` a debugger shows every variable; with the optimizations, some can be *optimized out*.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | the program ended normally, or `main` returned 0 |
| 1 | an exception left `main`; or `cromo` found errors |
| 101 | a panic, or a stack overflow |
| other | the value returned by `main`, or given to `os.exit` |

## Files

| Path | Content |
|---|---|
| `~/.cromo/current/bin/cromo` | the active version, which the `PATH` reaches |
| `~/.cromo/versions/<version>` | the installed versions |
| `lib/` next to `bin/` | the standard library and the runtime, which `cromo` reads |
| `~/.cromo/android` | the key with which `cromo` signs the APKs for debugging, made the first time |

For `cromo run --target android`, `adb` is the one of `ANDROID_HOME` or of the `PATH`, and `ANDROID_SERIAL` chooses the device when more are connected.

On Windows, `~` is `%USERPROFILE%`.
