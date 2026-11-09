# Tests

Cromo has tests built in: a test is a function marked `[[test]]`, `assert` checks a condition, and `cromo test` runs the tests and reports the results. No library or framework is needed.

## Writing a test

A test is a function without parameters that returns nothing, with the attribute `[[test]]` before it:

```cromo file=stats.cro
public f64 mean(list<f64> values) {
    f64 sum = 0.0
    for (f64 v in values) {
        sum += v
    }
    ret sum / f64(values.len())
}

[[test]]
void meanOfThree() {
    list<f64> values = [1.0, 2.0, 3.0]
    assert(mean(values) == 2.0)
}
```

A test **passes** if it ends normally. It **fails** if it stops with a panic, as a false `assert` or an index out of range, with an exception that it does not catch, with a stack overflow, or by ending the process with an exit code other than 0, as `os.exit(3)`.

Anything else is an error of the compiler. A test with parameters, or with a result:

```cromo error
[[test]]
void meanOf(list<f64> values) {
}

i32 main() {
    ret 0
}
```

```output
error: a test takes no parameters and returns nothing: [[test]] void meanOf()
```

`[[test]]` goes before a function of a module, not before a method of a struct, nor before a struct.

## Where the tests are

A test can be in the file of the code it checks, as `meanOfThree` above, or in a file next to it whose name ends with `_test.cro`:

- for a module of a file, `stats.cro`, the file `stats_test.cro`;
- for a module of a folder, every file of the folder that ends with `_test.cro`, as `shapes/area_test.cro`;
- for the main file, `main.cro`, the file `main_test.cro`.

A `_test.cro` file is part of the module, as the files of a system are ([Modules and projects](modules.md)): its tests see the private names of the module too, and it can declare helpers of its own for them. Only `cromo test` reads it: `cromo build` and `cromo run` leave it out, so the tests and their helpers never reach the executable. A test in the file of the code is compiled with the program, but only `cromo test` runs it.

```cromo file=stats_test.cro
[[test]]
void meanOfOne() {
    list<f64> values = [5.0]
    assert(mean(values) == 5.0)
}

[[test]]
void meanOfTwo() {
    list<f64> values = [1.0, 2.0]
    f64 m = mean(values)
    assert(m == 2.0)
}
```

## Running the tests

A program that uses the module:

```cromo file=main.cro
import stats

i32 main() {
    list<f64> values = [2.0, 4.0]
    println("{}", mean(values))
    ret 0
}
```

```output
3
```

`cromo test` in the folder of `main.cro`, or `cromo test main.cro`, builds the program with its tests and with those of all its modules, and runs them. The second test of `stats_test.cro` is wrong on purpose, since the mean of 1 and 2 is 1.5:

```sh
cromo test
```

```text
ok    stats.meanOfThree
ok    stats.meanOfOne
FAIL  stats.meanOfTwo
      panic: assertion failed: m == 2.0 (m is 1.5) at C:\work\stats\stats_test.cro:11:5
          at stats.meanOfTwo (C:\work\stats\stats_test.cro:11)
2 passed, 1 failed
```

- There is a line for each test, in the order of the files: those of the main file first, then those of the modules, in the order in which they are imported. `ok` means that the test passed, `FAIL` that it failed.
- Under a failed test comes, indented, everything that it wrote, on the standard output and on the standard error: its own lines, then the panic or the exception with its stack trace. The files of the modules are written with their full path. The output of the tests that pass is not shown.
- The last line counts the tests that passed and those that failed. There are no times, so the output of two runs can be compared.
- The exit code of `cromo test` is 0 when every test passed and 1 when one failed, so that a script or a continuous integration can stop. If the program does not compile, `cromo test` writes the errors and ends with 1, as `cromo build`.

### The names of the tests

A test of the main file is named as its function, `meanOfThree`; a test of a module has the name of the module in front, with its whole path: `stats.meanOfThree`, or `geometry.shapes.unitSquare` for a test in the folder `geometry/shapes`. Two tests of different modules can have the same name.

### Choosing the tests

The names after the file choose the tests to run: those whose name contains one of them.

```sh
cromo test main.cro meanOf        # meanOfThree, meanOfOne and meanOfTwo
cromo test main.cro Three Two     # meanOfThree and meanOfTwo
cromo test Three                  # the same, with main.cro of this folder
```

A name that ends with `.cro` is the file of the program; the others choose the tests. When the name of no test contains any of the names given, `cromo test` writes `no test has those names` and ends with 1. A program without tests writes `no tests: write them as [[test]] void name()` and ends with 0.

A program does not need `main` to be tested: `cromo test shapes.cro` runs the tests of a file that has none, as a module tested alone.

### How the tests run

- `cromo test` builds one executable with all the tests, in a temporary folder, and starts it once for each test. Each test runs in a process of its own, as many at a time as the processors: a test that crashes stops only itself, and the tests cannot share state through the variables of the program.
- Every test runs in the current folder of `cromo test`, so it finds the files of the project with their relative paths, and its standard input is empty.
- A test has no time limit: a test that never ends keeps `cromo test` waiting.
- The tests run on the system where `cromo` runs. `cromo test` takes the options of the build that make sense there: `--release`, `-O0` to `-O3`, `--cpu`. `-o`, `--target` and `--emit-ir` are options of `cromo build` only, and the arguments after `--` are only for `cromo run`.
- The tests of the standard library are not run with those of the program.

In a project with a `cromo.mod`, `cromo test` without a file runs the tests of each of its executables in turn, after a line with the name and the main file of each one; its exit code is 1 if any of them fails ([Modules and projects](modules.md)).

## How a test fails

This file has a test for each way to fail. It has no `main`: it is only tested.

```cromo file=checks.cro
import std.os

i64 parsePort(string text) {
    i64 port = i64(text)
    if (port < 1 || port > 65535) {
        throw Error(format("{} is not a port", port))
    }
    ret port
}

[[test]]
void quiet() {
    println("this line is not shown")
}

[[test]]
void wrongSum() {
    println("adding")
    i64 total = 2 + parsePort("3")
    assert(total == 6)
}

[[test]]
void badPort() {
    parsePort("70000")
}

[[test]]
void outOfRange() {
    list<i64> ports = [80, 443]
    println("{}", ports[2])
}

[[test]]
void exits() {
    os.exit(3)
}
```

```sh
cromo test checks.cro
```

```text
ok    quiet
FAIL  wrongSum
      adding
      panic: assertion failed: total == 6 (total is 5) at checks.cro:20:5
          at wrongSum (checks.cro:20)
FAIL  badPort
      uncaught exception: 70000 is not a port
          at parsePort (checks.cro:6)
          at badPort (checks.cro:25)
FAIL  outOfRange
      panic: index out of bounds: the index is 2 but the length is 2 at checks.cro:31:24
          at outOfRange (checks.cro:31)
FAIL  exits
      the test ended with the exit code 3
1 passed, 4 failed
```

A panic ends a process with the exit code 101 and an uncaught exception with 1: for those, the message on the standard error says what happened. A test that ends with any other code, without a message, gets the line `the test ended with the exit code` and its code. A stack overflow is a panic too, with its trace.

## assert

`assert(condition)` stops the program with a panic when the condition is false. The message writes the condition as it is in the source, the file, the line and the column of the `assert`, and the stack trace.

For a comparison of numbers or of `bool` values, with `==`, `!=`, `<`, `<=`, `>` or `>=`, the message also gives the value of each side that is not a literal. Each side is computed once, so a side with an effect, as a call that modifies its argument, runs only once also when the assertion fails:

```cromo file=counter.cro
i32 next(ref i32 counter) {
    counter++
    ret counter
}

i32 main() {
    i32 c = 1
    assert(next(ref c) == 2)
    assert(next(ref c) == 5)
    ret 0
}
```

```output
panic: assertion failed: next(ref c) == 5 (next(ref c) is 3) at counter.cro:9:5
    at main (counter.cro:9)
```

For any other condition, as a comparison of strings, a call that returns `bool` or two conditions joined by `&&`, the message gives only the condition. A comparison with the values of both sides, `assert(ready == wanted)`, writes `(ready is false, wanted is true)`.

`assert` is a built-in function: it works in every function, not only in tests, and also in the executables built with `--release`. The panic cannot be caught, so a false `assert` always ends the program, or the test, with the exit code 101. [Errors](errors.md) explains panics.

## Testing errors

A test that expects an exception catches it, and fails if it does not arrive:

```cromo
i64 parsePort(string text) {
    i64 port = i64(text)
    if (port < 1 || port > 65535) {
        throw Error(format("{} is not a port", port))
    }
    ret port
}

[[test]]
void rejectsZero() {
    bool thrown = false
    try {
        parsePort("0")
    } catch (Exception e) {
        thrown = true
        assert(e.message() == "0 is not a port")
    }
    assert(thrown)
}

i32 main() {
    println("{}", parsePort("8080"))
    ret 0
}
```

```output
8080
```

A panic instead cannot be caught: a test that reaches one fails, so the errors of a program that end it on purpose, as an index out of range, cannot be tested to happen.

## Summary

- `[[test]] void name()` is a test: it passes if it ends normally, and fails with a panic, an uncaught exception, a stack overflow or an exit code other than 0.
- Tests live in the code or in `_test.cro` files next to it, which are part of the module, see its private names, and are compiled only by `cromo test`.
- `cromo test [file.cro] [names]` runs each test in its own process, in parallel, in the current folder; it writes `ok` or `FAIL` for each, the output of the failed ones and the count, and ends with 1 if one failed.
- The tests of a module are named after it, as `stats.meanOfTwo`; the names after the file choose the tests that contain them.
- `cromo test` takes `--release`, `-O` and `--cpu`, and runs the tests on the system where it runs.
- `assert(condition)` is a panic when the condition is false, with the values of the sides of a comparison of numbers or `bool`, computed once.
