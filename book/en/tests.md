# Tests

Cromo has tests built in: a test is a function marked `[[test]]`, `assert` checks a condition, and `cromo test` runs the tests and reports the results. No library or framework is needed.

## Writing a test

A test is a function without parameters that returns nothing, with the attribute `[[test]]` before it. It passes if it ends normally, and fails with a panic, an exception that it does not catch, or a stack overflow:

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

Tests can be in the file of the code, or in a file next to it whose name ends with `_test.cro`. That file is part of the same module, so its tests see the private names too, and `cromo build` leaves it out of the executable:

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

```cromo file=main.cro
import stats

i32 main() {
    list<f64> values = [2.0, 4.0]
    println("{}", mean(values))
    ret 0
}
```

`cromo test` in the folder of `main.cro` runs the tests of the program and of all its modules. The second test above is wrong on purpose, since the mean of 1 and 2 is 1.5:

```sh
cromo test
```

```text
ok    stats.meanOfThree
ok    stats.meanOfOne
FAIL  stats.meanOfTwo
      panic: assertion failed: m == 2.0 (m is 1.5) at stats_test.cro:11:5
          at stats.meanOfTwo (stats_test.cro:11)
2 passed, 1 failed
```

- Each test runs in a process of its own, as many at a time as the processors: a test that crashes stops only itself.
- The output of a test is shown only when it fails.
- The exit code of `cromo test` is 1 if a test failed, so that a script or a continuous integration can stop.
- The names of the tests have their module in front: `stats.meanOfThree`.

Names after the file choose the tests to run: `cromo test main.cro meanOf` runs the tests whose name contains `meanOf`.

## assert

`assert(condition)` stops the test with a panic when the condition is false. For a comparison of numbers or `bool`, the message gives the value of each side that is not a literal, computed once, as `m is 1.5` above: most of the time it is enough to understand what went wrong without a debugger.

`assert` works in every function, not only in tests, and also in release builds.

## Testing errors

A test that expects an exception catches it:

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

## Summary

- `[[test]] void name()` is a test; `assert(condition)` checks.
- Tests live in the code or in `name_test.cro`, which sees the private names of the module.
- `cromo test` runs each test in its own process, in parallel, and reports the failures with their cause.
