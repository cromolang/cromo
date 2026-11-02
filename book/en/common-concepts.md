# Common concepts

This chapter presents what every language has: variables, types, functions, comments and control flow. A C or C++ programmer will recognize most of it; the differences are few, and they matter.

## Variables

A variable is declared with its type before its name, always:

```cromo
i32 main() {
    i32 apples = 5
    i64 total = apples + 10
    string name = "Cromo"
    bool ready = true
    println("{} {} {} {}", apples, total, name, ready)
    ret 0
}
```

```output
5 15 Cromo true
```

There is no type inference: the type is written even when the value would say it. The code is longer by a few characters and easier to read, because the type of every variable is in front of you.

### Every variable has a value

A variable declared without a value takes the **default value** of its type: 0 for numbers, `false` for `bool`, the empty string and the empty list.

```cromo
i32 main() {
    i32 count
    f64 average
    string label
    list<i32> numbers
    println("[{}] [{}] [{}] [{}]", count, average, label, numbers.len())
    ret 0
}
```

```output
[0] [0] [] [0]
```

There are no uninitialized variables in Cromo, and so no undefined behavior that comes from them.

### Constants

Variables can change. `const` makes one immutable:

```cromo error
i32 main() {
    const f64 PI = 3.14159
    PI = 3.0
    ret 0
}
```

```output
error: 'PI' is const
```

A `const` must be given its value where it is declared.

Outside the functions, a module can declare constants too, as limits and tables of numbers. They hold numbers, `bool` and `char`, or fixed-size arrays of them, written with literals, and only their module sees them:

```cromo
const i64 minute = 60
const u8[4] masks = [1, 2, 4, 8]

i32 main() {
    println("{} {}", 3 * minute, masks[3])
    ret 0
}
```

```output
180 8
```

## Types

### Integers

| Type | Size | Range |
|---|---|---|
| `i8`, `u8` | 8 bits | −128 to 127, 0 to 255 |
| `i16`, `u16` | 16 bits | −32 768 to 32 767, 0 to 65 535 |
| `i32`, `u32` | 32 bits | about ±2.1 billion, 0 to 4.2 billion |
| `i64`, `u64` | 64 bits | about ±9.2 × 10¹⁸, 0 to 1.8 × 10¹⁹ |

Every type has the same size on every system: there is no `int` whose size depends on the machine, and no `size_t`. Lengths and positions are `i64`.

Integer literals can be written in decimal, hexadecimal and binary, with `_` to separate the digits: `1_000_000`, `0xFF`, `0b1010`.

### Overflow

An integer operation whose result does not fit in its type is an error. In a debug build, the default, the program stops:

```cromo file=overflow.cro
i32 main() {
    i8 small = 120
    for (i32 i = 0; i < 10; i++) {
        small += 1
    }
    println("{}", small)
    ret 0
}
```

```output
panic: integer overflow at overflow.cro:4:9
    at main (overflow.cro:4)
```

A *panic* stops the program, prints where it happened and the calls that led there, and gives the exit code 101. In a release build, with `--release`, integers wrap around instead, as in C: the check costs a little, and a tested program can do without it. Division by zero is always a panic.

### Floating point, booleans and characters

- `f32` and `f64` are the floating-point numbers of IEEE 754: `3.14`, `1e-9`.
- `bool` is `true` or `false`.
- `char` is a byte, a synonym of `u8`. A character literal is written between single quotes and must be ASCII: `'a'`, `'\n'`, `'\x41'`. Printed with `{}`, a `char` is a number.

### Strings

`string` is text in UTF-8. Its length is in bytes, and `s[i]` is a byte:

```cromo
i32 main() {
    string city = "Torino"
    string greeting = "Hello, " + city + "!"
    println("{} has {} bytes, the first is {}", greeting, greeting.len(), greeting[0])
    ret 0
}
```

```output
Hello, Torino! has 14 bytes, the first is 72
```

Chapter 7 shows the functions of text: searching, splitting, replacing.

### Conversions

A number is converted implicitly only when no information can be lost: an `i32` becomes an `i64` or an `f64` by itself. In every other case the conversion is written as a call with the name of the type:

```cromo
i32 main() {
    i32 x = 7
    i64 big = x
    f64 exact = f64(x) / 2.0
    i32 back = i32(exact)
    i32 wide = 300
    u8 byte = u8(wide)
    println("{} {} {} {}", big, exact, back, byte)

    i32 parsed = i32("42") + 1
    println("{}", parsed)
    ret 0
}
```

```output
7 3.5 3 44
43
```

`i32(exact)` drops the fraction; `u8(wide)` keeps the lowest 8 bits. `i32("42")` reads a number from a text, and throws an exception if the text is not a number.

### Literals take the type of their context

A literal has no type of its own: it takes the one that its place requires. In `i64 total = apples + 10`, `10` is an `i32` like `apples`. Where nothing says the type, the compiler asks for it:

```cromo error
i32 main() {
    println("{}", 10 / 3)
    ret 0
}
```

```output
error: the type of this expression cannot be deduced
```

Writing `i32(10) / 3`, or using a variable, solves it.

## Operators

The operators are those of C, with one difference in precedence: the bitwise operators bind more than the comparisons, so `flags & MASK == 0` means `(flags & MASK) == 0`, as one would expect. From the strongest:

| Operators | Meaning |
|---|---|
| `*` `/` `%` | multiplication, division, remainder |
| `+` `-` | addition, subtraction |
| `<<` `>>` | shifts |
| `&` | bitwise and |
| `^` | bitwise exclusive or |
| `\|` | bitwise or |
| `==` `!=` `<` `<=` `>` `>=` | comparisons |
| `&&` | logical and |
| `\|\|` | logical or |

`!` negates a `bool` and inverts the bits of an integer. `i++` and `i--` exist, but only as statements on their own, never inside an expression: `a[i++]` is not Cromo.

## Functions

```cromo
i64 square(i64 x) {
    ret x * x
}

void greet(string name, i32 times) {
    for (i32 i = 0; i < times; i++) {
        println("Hello, {}!", name)
    }
}

i32 main() {
    println("{}", square(12))
    greet("Ada", 2)
    ret 0
}
```

```output
144
Hello, Ada!
Hello, Ada!
```

A function that returns a value must reach a `ret` on every path, or the program does not compile. A `void` function can end without `ret`. The order of the functions in a file does not matter: `main` can come first and call functions written after it.

A function has one result. When it may have nothing to give, it returns a conventional value, throws an exception, or returns a `bool` and writes the result in a parameter passed with `ref`, which chapter 4 explains. There is no `null`.

## Comments

```cromo ignore
// a comment until the end of the line
/* a comment
   on several lines */
```

## Statements and lines

A statement ends at the end of its line, without a semicolon. It continues on the next line when the line ends with an operator, a comma or an open parenthesis:

```cromo
i32 main() {
    i64 total = 1 +
        2 +
        3
    println("{}",
        total)
    ret 0
}
```

```output
6
```

There is one statement for each line.

## Conditions

```cromo
i32 main() {
    i32 temperature = 23
    if (temperature > 30) {
        println("hot")
    } else if (temperature < 10) {
        println("cold")
    } else {
        println("mild")
    }
    ret 0
}
```

```output
mild
```

The braces are always required, and the condition must be a `bool`: `if (count)` is an error, `if (count != 0)` is correct.

## Loops

Cromo has one loop, `for`, in four forms:

```cromo
i32 main() {
    // The classic form of C.
    for (i32 i = 0; i < 3; i++) {
        print("{} ", i)
    }
    println("")

    // Only a condition, where C has while.
    i32 n = 1
    for (n < 100) {
        n *= 3
    }
    println("{}", n)

    // Without anything, until a break.
    for {
        n -= 50
        if (n < 0) {
            break
        }
    }
    println("{}", n)

    // Over the elements of a list.
    list<string> names = ["Ada", "Alan", "Grace"]
    for (string name in names) {
        if (name == "Alan") {
            continue
        }
        println("{}", name)
    }
    ret 0
}
```

```output
0 1 2 
243
-7
Ada
Grace
```

`break` leaves the loop and `continue` goes to the next round. There is no `while`, no `do`, and no `goto`.

## switch

`switch` chooses among integers or strings. There is no fall-through: each `case` has its own block, and a `case` can list several values. `default` takes the values not listed:

```cromo
string describe(i32 status) {
    switch (status) {
        case 200 {
            ret "ok"
        }
        case 301, 302 {
            ret "moved"
        }
        case 404 {
            ret "not found"
        }
        default {
            ret "other"
        }
    }
}

i32 main() {
    println("{}, {}, {}", describe(200), describe(302), describe(500))
    ret 0
}
```

```output
ok, moved, other
```

`switch` is most useful with enums, as chapter 6 shows: there the compiler checks that every case is handled.

## Summary

- The type comes before the name; every variable has a value; `const` makes it immutable.
- Integers have a fixed size; overflow and division by zero stop the program.
- Conversions that can lose information are written: `i32(x)`.
- Functions return with `ret`; there is no `null`.
- `if` wants braces and a `bool`; `for` is the only loop; `switch` has no fall-through.
