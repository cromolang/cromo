# Anonymous functions

An anonymous function, or *lambda*, is a function written where it is used, as an argument or a value. It can use the variables around it, and the way it takes them follows the rules of ownership of chapter 4.

## Writing a lambda

```cromo
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

i32 main() {
    list<i64> values = [3, 8, 12, 5, 20]
    i64 limit = 6
    i64 big = count(values, [limit](i64 x) bool { ret x > limit })
    println("{} values are above {}", big, limit)
    ret 0
}
```

```output
3 values are above 6
```

The lambda is `[limit](i64 x) bool { ret x > limit }`:

- between square brackets, the variables it **captures**, here `limit`;
- its parameters, as a function's;
- the type it returns, after the parameters; without it, the lambda returns nothing;
- its body.

`func(i64) bool` is the type of a function that takes an `i64` and returns a `bool`. A parameter of a function type is called as a function: `keep(v)`.

## Captures

A lambda says how it uses each variable it captures, with the same words as a parameter:

| Capture | The lambda |
|---|---|
| `[limit]` | reads the variable |
| `[ref total]` | modifies the variable |
| `[move name]` | takes the value, which the variable loses |

```cromo
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

i32 main() {
    list<i64> values = [3, 8, 12, 5, 20]
    i64 total = 0
    count(values, [ref total](i64 x) bool {
        total += x
        ret true
    })
    println("total {}", total)
    ret 0
}
```

```output
total 48
```

Without `ref`, a lambda cannot modify what it captures, and the compiler says so.

## Lambdas as values

A lambda that captures by address, with `ref` or by reading a value that is not simple, can only be passed as an argument: the variables it refers to live in the function that calls, and the lambda ends with the call. A lambda that captures only simple values, or takes its values with `move`, is a value: it can be kept in a variable, a field or a list, returned and moved.

```cromo
func(i64) i64 adder(i64 amount) {
    ret [amount](i64 x) i64 { ret x + amount }
}

struct Button {
    string label
    func() onClick

    void click() {
        onClick()
    }
}

i32 main() {
    func(i64) i64 plus3 = adder(3)
    println("{}", plus3(4))

    list<func(i64) i64> steps = [adder(1), adder(10), adder(100)]
    i64 x = 0
    for (func(i64) i64 step in steps) {
        x = step(x)
    }
    println("{}", x)

    string file = "notes.txt"
    Button save
    save.label = "Save"
    save.onClick = [move file]() { println("saving {}", file) }
    save.click()
    ret 0
}
```

```output
7
111
saving notes.txt
```

Returning a lambda that captures with `ref` does not compile: the variable would end with the function, and the lambda would refer to nothing.

```cromo error
func() makeCounter() {
    i64 n = 0
    ret [ref n]() { n++ }
}

i32 main() {
    ret 0
}
```

```output
error: this lambda captures by address
```

A function value is moved, not copied. An empty one, as a field never assigned, is a panic when called.

## const func

A parameter `const func(...)` accepts only a function that does not modify what it captures. Several threads can call such a function at the same time, safely: this is how the HTTP server of chapter 15 calls the same handler for many requests at once.

## Summary

- A lambda is `[captures](parameters) type { body }`; its type is `func(parameters) type`.
- Captures use the words of parameters: read, `ref`, `move`.
- A lambda that refers to variables of its caller lives only during the call; one that owns what it captures is a value.
- `const func` is a function that can be called from several threads at once.
