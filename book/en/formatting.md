# Formatting text

Five built-in functions write values as text: `print`, `println`, `eprint`, `eprintln` and `format`. They take a format string with placeholders, `{}`, and the values to put in them, with the syntax of `std::format` in C++ and of `format` in Python. The compiler reads the format string while it compiles the program: it checks the placeholders against the values, and it turns the call into the code that writes each piece, so nothing is parsed while the program runs.

## Printing

```cromo
i32 main() {
    string name = "Anna"
    i64 age = 30
    print("Hello, ")
    println("{}!", name)
    println("{} is {} years old", name, age)
    string line = format("{}:{}", name, age)
    println("{} has {} bytes", line, line.len())
    println("")
    println("the end")
    ret 0
}
```

```output
Hello, Anna!
Anna is 30 years old
Anna:30 has 7 bytes

the end
```

- `print` writes the text as it is; `println` adds a new line at the end. `println("")` writes an empty line.
- `format` writes into a new `string`, which it returns, instead of the screen.
- `eprint` and `eprintln` write to the standard error, the stream for warnings and errors, which stays on the screen when the standard output goes to a file or a pipe:

```cromo
i32 main() {
    i64 missing = 3
    println("checking the files")
    eprintln("warning: {} files are missing", missing)
    ret 0
}
```

```output
checking the files
warning: 3 files are missing
```

Each call writes its whole text at once: when several threads print, their lines do not mix.

### The compiler checks the format

The format string is a literal, written in the call. The compiler counts its placeholders, checks that every value is used, and checks that each value can be printed in the way the placeholder asks. A text held in a variable is printed with a placeholder, `println("{}", text)`: so a `{` inside the text is never taken for a placeholder.

A number literal among the values needs its type, since nothing else says which type it is: `println("{}", i64(5))`, or a variable.

```cromo error
i32 main() {
    string message = "50% done"
    println(message)
    println("{} of {}", message)
    println("{}", message, message)
    println("{}", 5)
    ret 0
}
```

```output
error: the first argument of println must be a string literal with the format
error: the format string needs at least 2 arguments, but 1 were given
error: this argument is not used by the format string
error: the type of this literal cannot be deduced: write it explicitly, for example i32(5)
```

## Placeholders

`{}` takes the next value. `{0}`, `{1}` and so on take a value by its position, from 0, so a value can be used more than once and in any order. `{{` and `}}` write a brace:

```cromo
i32 main() {
    println("{} and {}", "first", "second")
    println("{0} and {1}, then {0} again", "first", "second")
    println("{1} before {0}", "a", "b")
    println("{{ and }} are braces; {} is a value", "{}")
    ret 0
}
```

```output
first and second
first and second, then first again
b before a
{ and } are braces; {} is a value
```

A format uses either `{}` or numbers, not both. A brace alone is an error, as is a placeholder with anything but a number before the `:`:

```cromo error
i32 main() {
    i64 n = 5
    println("{} {1}", n, n)
    println("a } b", n)
    println("a { b", n)
    println("{n}", n)
    ret 0
}
```

```output
error: cannot mix automatic '{}' and numbered '{0}' placeholders
error: single '}' in the format string: write '}}' for a brace
error: '{' without a matching '}' in the format string: write '{{' for a brace
error: invalid placeholder '{n}'
```

## The specification

After the position, a `:` begins the specification of the value, which says how to write it. Its parts are all optional, but they come in this order, where the brackets mark what can be left out:

```text
{position:[[fill]align][sign][#][0][width][.precision][type]}
```

| Part | Meaning |
|---|---|
| fill | the character that fills the width, a space by default; it is written only before an alignment |
| align | `<` left, `^` center, `>` right |
| sign | `+` writes a sign also before the positive numbers; a space writes a space before them, `{: }`; `-` writes only the minus of the negative ones, the default |
| `#` | the prefix of the base: `0x`, `0X`, `0b`, `0` |
| `0` | fill a number with zeros after its sign, up to the width |
| width | the minimum number of characters, at most 1000 |
| .precision | for floats the digits, for text the maximum number of characters; at most 500 |
| type | a letter, as `x` for hexadecimal or `f` for a float with a fixed point, or `?` for the form of debug |

### Width, fill and alignment

The width is a minimum: a shorter value is filled up to it, a longer one is written whole. Numbers go to the right by default, text and `bool` to the left:

```cromo
i32 main() {
    i64 n = 42
    string word = "città"
    println("[{:8}] [{:8}] [{:8}]", n, word, true)
    println("[{:<8}] [{:^8}] [{:>8}]", n, word, true)
    println("[{:*^9}] [{:-<8}] [{:.>8}] [{:2}]", "mid", "left", n, "longer")
    ret 0
}
```

```output
[      42] [città   ] [true    ]
[42      ] [ città  ] [    true]
[***mid***] [left----] [......42] [longer]
```

- The width counts characters, not bytes: `città` has six bytes, but it takes five places.
- With `^`, a space that does not divide in two goes to the right.
- The fill is one character of ASCII, other than `{` and `}`, and it is written only before an alignment: `{:*^9}`, never `{:*9}`.

### Signs and zeros

```cromo
i32 main() {
    i64 n = 42
    i64 m = -42
    f64 x = 3.14159
    println("[{:+}] [{:+}] [{: }] [{: }] [{:-}]", n, m, n, m, n)
    println("[{:06}] [{:06}] [{:+08.2f}] [{:<06}]", n, m, x, n)
    ret 0
}
```

```output
[+42] [-42] [ 42] [-42] [42]
[000042] [-00042] [+0003.14] [42    ]
```

The zeros go after the sign and after the prefix of `#`, and they count in the width. With an alignment, the `0` has no effect: the fill and the alignment decide.

### Precision

For a float, the precision is the number of digits: after the point with `f` and `e`, in all with `g` or without a type letter. For a string, it is the maximum number of characters, and the rest is cut:

```cromo
i32 main() {
    f64 x = 3.14159
    f64 y = 1234.5
    println("[{:.2f}] [{:.0f}] [{:.3e}] [{:.3g}] [{:.3}]", x, x, y, y, y)
    println("[{:.3}] [{:>6.2}]", "abcdef", "città")
    ret 0
}
```

```output
[3.14] [3] [1.234e+03] [1.23e+03] [1.23e+03]
[abc] [    ci]
```

The last digit is rounded to the nearest, from the exact value that the float holds: `2.675` is stored as 2.67499999..., so `{:.2f}` writes `2.67`. A precision is an error for an integer and for a `bool`.

### Type letters

| Letter | For | Writes |
|---|---|---|
| `d` | integers | decimal, the default |
| `x`, `X` | integers | hexadecimal, with small or capital letters; `#` adds `0x` or `0X` |
| `b` | integers | binary; `#` adds `0b` |
| `o` | integers | octal; `#` adds `0` |
| `c` | `u8` (`char`) | the character of the byte |
| `f` | floats | a fixed point, with six decimals by default |
| `e` | floats | an exponent, `6.666667e-01`, with six decimals by default |
| `g` | floats | the shorter of `f` and `e`, with six digits by default |
| `s` | strings, `bool`, types with `text()` | the text, the default |
| `?` | every value but functions and interfaces | the form of debug |

```cromo
i32 main() {
    i64 n = 1234567
    println("[{:d}] [{:x}] [{:#x}] [{:X}] [{:#X}]", n, n, n, n, n)
    println("[{:b}] [{:o}] [{:#o}] [{:#010x}]", n, n, n, n)
    u8 letter = 'A'
    println("[{}] [{:c}] [{:>3c}] [{:#04x}]", letter, letter, letter, letter)
    f64 ratio = 2.0 / 3.0
    println("[{:f}] [{:e}] [{:g}] [{:8.2f}]", ratio, ratio, ratio, ratio)
    i32 m = -42
    println("[{:x}] [{:x}] [{:b}]", m, u32(m), u8(m))
    ret 0
}
```

```output
[1234567] [12d687] [0x12d687] [12D687] [0X12D687]
[100101101011010000111] [4553207] [04553207] [0x0012d687]
[65] [A] [  A] [0x41]
[0.666667] [6.666667e-01] [0.666667] [    0.67]
[-2a] [ffffffd6] [11010110]
```

A negative number keeps its sign in every base, as in `std::format`: `-42` is `-2a`. To see its bits, convert it to the unsigned type of the same size, as `u32(m)`, which keeps the bits.

A letter that does not fit the type of the value is an error, and so are the parts that a type does not have:

```cromo error
i32 main() {
    i64 n = 5
    f64 x = 1.5
    string s = "hi"
    i32 code = 65
    println("{:f}", n)
    println("{:x}", x)
    println("{:.2}", n)
    println("{:+}", s)
    println("{:c}", code)
    println("{:q}", n)
    ret 0
}
```

```output
error: 'f' is not a format for integers
error: 'x' is not a format for floating-point numbers
error: a precision is not allowed for integers
error: a string only allows fill, alignment, width and precision
error: 'c' prints a character: the value must be a char (u8)
error: invalid format specification 'q'
```

There are no capital `E` and `G`.

## How each type prints with {}

- An integer is written in decimal. A `char` is a `u8`, so `{}` writes its number, and `{:c}` its character.
- A `bool` is `true` or `false`.
- A `string` is written as it is.
- A float is written in its shortest form that reads back as the same number: `0.1`, not `0.1000000000000000055`. A float with an integer value has no point, `100`, and the form with the exponent is used when it is shorter, `1e+20`.

```cromo
i32 main() {
    f64 a = 0.1
    f64 b = 100.0
    f64 c = 100000.0
    f64 d = 1e20
    f64 e = 0.1 + 0.2
    f64 tiny = 1.5e-7
    println("{} {} {} {} {} {}", a, b, c, d, e, tiny)
    f32 single = 0.1
    println("{} {:.10f}", single, single)
    f64 zero = 0.0
    println("{} {} {} {}", 1.0 / zero, -1.0 / zero, zero / zero, -zero)
    ret 0
}
```

```output
0.1 100 1e+05 1e+20 0.30000000000000004 1.5e-07
0.1 0.1000000015
inf -inf nan -0
```

An `f32` is written in the shortest form for an `f32`: `0.1`, although the number it holds is 0.1000000015. `0.1 + 0.2` is not exactly `0.3`, and the shortest form says so. A division by zero of floats gives an infinity or a NaN, written `inf`, `-inf` and `nan`.

To always see the decimals, give a precision, as `{:.2f}`; to always see that the number is a float, use `{:?}`, which writes `100.0`.

## Printing your own types: text()

A struct or an enum is printed by `{}` when it has a method `string text() const`. The text that it returns is then formatted as a string: the width, the alignment and the precision apply to it.

```cromo
struct Money {
    i64 cents

    string text() const {
        ret format("{}.{:02}", cents / 100, cents % 100)
    }
}

enum Level {
    Low
    High

    string text() const {
        switch {
            case Low {
                ret "low"
            }
            case High {
                ret "HIGH"
            }
        }
    }
}

i32 main() {
    Money price
    price.cents = 1250
    println("[{}] [{:>10}] [{:*<10}] [{:.4}]", price, price, price, price)
    Level level = Level.High
    println("[{}] [{:^8}] [{:?}]", level, level, level)
    ret 0
}
```

```output
[12.50] [     12.50] [12.50*****] [12.5]
[HIGH] [  HIGH  ] [High]
```

- `text()` takes no parameters, is `const` and returns a `string`. A method `text` of another shape does not count.
- It must be visible where the value is printed: printed in another module, the method is `public string text() const`, otherwise the compiler says `method 'text' of Money is private to its module`.
- The type letter, if any, is `s`, as for a string.
- `text()` serves only `{}`: `{:?}` still writes the form of debug, as `High` above.

## The form of debug: {:?}

`{:?}` writes any value in a form meant for the programmer: for the logs, for the tests, for a look at a value while a program is written. The compiler makes it for every type, without writing anything:

```cromo
struct Point {
    i64 x
    i64 y
}

struct Box<T> {
    T item
}

struct Marker {
}

enum Shape {
    Circle(f64 radius)
    Rect(f64 w, f64 h)
    Empty
}

i32 main() {
    Point p
    p.x = 1
    p.y = 2
    Box<i64> b
    b.item = 7
    Marker m
    println("{:?} {:?} {:?}", p, b, m)
    println("{:?} {:?} {:?}", Shape.Circle(2.5), Shape.Rect(1.0, 2.0), Shape.Empty)

    list<Point> points = [p, p]
    i32[3] counts
    map<string, f64> prices
    prices["fig"] = 3.0
    println("{:?} {:?} {:?}", points, counts, prices)

    f64 whole = 9.0
    u8 letter = 'A'
    println("{:?} {:?} {:?} {:?}", whole, letter, true, f32x4(1.5))
    println("{:?}", "say \"hi\"\n\tcittà")
    ret 0
}
```

```output
Point { x: 1, y: 2 } Box { item: 7 } Marker
Circle(2.5) Rect(1.0, 2.0) Empty
[Point { x: 1, y: 2 }, Point { x: 1, y: 2 }] [0, 0, 0] {"fig": 3.0}
9.0 65 true [1.5, 1.5, 1.5, 1.5]
"say \"hi\"\n\tcittà"
```

| Value | Form of debug |
|---|---|
| integer, `char` | as `{}`: `65` |
| float | as `{}`, with `.0` added when that form has neither a point nor an exponent: `9.0`, `0.1`, `1e+20` |
| `bool` | `true`, `false` |
| `string` | between quotes, with the escapes of Cromo: `\"`, `\\`, `\n`, `\r`, `\t`, and `\x01` for the other control bytes |
| list, array, vector | `[a, b, c]` |
| map | `{key: value, ...}`, in the order of the map |
| struct | `Name { field: value, ... }`, with all its fields, the private ones too; `Name` without fields; a generic struct without its type arguments, `Box { item: 7 }` |
| enum | the case, with its values in order: `Circle(2.5)`, `Empty` |

The parts are printed with their own form of debug, also when they have `text()`, so a string inside a list or a struct is between quotes. The name of a struct or of an enum is the one in its declaration, without its module.

The form of debug is a string, and the specification of a string applies to it: width, fill and alignment, and a precision that cuts it.

```cromo
struct Point {
    i64 x
    i64 y
}

i32 main() {
    Point p
    list<i64> v = [1, 2, 3]
    println("[{:>22?}] [{:*<12?}] [{:.5?}]", p, v, v)
    ret 0
}
```

```output
[  Point { x: 0, y: 0 }] [[1, 2, 3]***] [[1, 2]
```

### The form of debug in generics

A generic prints its `T` with `{:?}` only when it asks for it, with the constraint `T : Debug` of the core: every type implements `Debug` by itself, but the functions and the interfaces. `debugText(x)` gives the same text as a `string`:

```cromo
struct Point {
    i64 x
    i64 y
}

void show<T : Debug>(string label, T value) {
    println("{}: {:?}", label, value)
}

i32 main() {
    Point p
    show("point", p)
    show<list<f64>>("list", [1.0, 2.5])
    string d = debugText(p)
    println("{} bytes", d.len())
    ret 0
}
```

```output
point: Point { x: 0, y: 0 }
list: [1.0, 2.5]
20 bytes
```

Functions and interfaces have no form of debug, and neither have the structs and the lists that contain them. The message goes on with the part that has none, here `Handler.run has type func(i64) i64`:

```cromo error
struct Handler {
    string name
    func(i64) i64 run
}

void show<T>(T value) {
    println("{:?}", value)
}

i32 main() {
    Handler h
    println("{:?}", h)
    ret 0
}
```

```output
error: values of type Handler cannot be printed with {:?}
error: T can be printed with {:?} only with a constraint: write <T : Debug>
```

## What {} does not print

`{}` prints the numbers, `bool`, `string`, and the structs and enums with `text()`. The other values have no text of their own, and the compiler says what to use instead:

- a struct or an enum without `text()`, a `list` and a `map`: the message proposes `text()` and `{:?}`;
- an array, a vector, a function, an interface and the `T` of a generic: the message proposes `{:?}`, which works for the arrays, the vectors, and a `T : Debug`.

```cromo error
struct Point {
    i64 x
    i64 y
}

i32 main() {
    Point p
    list<i64> v = [1, 2]
    i32[3] a
    println("{}", p)
    println("{}", v)
    println("{}", a)
    ret 0
}
```

```output
error: a Point cannot be printed with {}: give it a method string text() const
error: a list<i64> cannot be printed with {}: give it a method string text() const
error: an i32[3] cannot be printed with {}: print its form of debug with {:?}
```

A value printed both with `{}` and with `{:?}` is passed twice, once for each placeholder, as `println("{} {:?}", p, p)`: the compiler turns each argument into one text, either that of `text()` or that of the form of debug.

```cromo error
struct Money {
    i64 cents

    string text() const {
        ret format("{}", cents)
    }
}

i32 main() {
    Money price
    println("{0} {0:?}", price)
    ret 0
}
```

```output
error: this argument is printed both with {} and with {:?}: pass it twice
```

## Summary

- `print` and `println` write to the standard output, `eprint` and `eprintln` to the standard error, `format` into a new `string`.
- The format string is a literal, checked by the compiler: the number of values, their types, and the specification of each placeholder.
- `{}` takes the next value, `{0}` a value by position; `{{` and `}}` are braces.
- After `:` come fill and alignment, sign, `#`, `0`, width, precision and a type letter: `{:*>10}`, `{:+08.2f}`, `{:#x}`.
- Width and precision count characters, not bytes.
- Integers print in decimal, `char` as a number unless `{:c}`, floats in their shortest form that reads back the same.
- A struct or an enum with `string text() const` prints with `{}`, and the specification applies to its text.
- `{:?}` prints every value but functions and interfaces in a form for debugging, with names, quotes and escapes; it takes width and alignment too.
- A value printed both with `{}` and with `{:?}` is passed twice.
