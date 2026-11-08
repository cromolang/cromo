# Common concepts

This chapter presents what every language has: variables and constants, types, functions, comments, and the statements that choose and repeat. A C or C++ programmer will recognize most of it; the differences are few, and they matter. The numbers, with their operators and conversions, have the next chapter, [Numbers and operators](operators.md).

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

The value is always given with `=`. The form of C++ with parentheses, `string name("Cromo")`, is not Cromo, and a struct is made the same way as the other values: `Counter c = Counter(5)`.

```cromo error
i32 main() {
    string name("Cromo")
    ret 0
}
```

```output
error: expected a newline at the end of the statement (one statement per line), found '('
```

`=` also gives a variable a new value, and `+=`, `-=` and the other compound assignments change it; `i++` and `i--` add and subtract one. They are statements on a line of their own, never parts of an expression: [Numbers and operators](operators.md) lists them all.

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

### One name, one variable

A variable lives until the end of the block, between braces, where it is declared. Inside that block its name cannot be declared again, not even in an inner block: there is no shadowing, the name always means the same variable. The same holds for the parameters of a function.

```cromo error
i32 main() {
    i32 count = 1
    for (i32 i = 0; i < 3; i++) {
        i32 count = i * 2
        println("{}", count)
    }
    ret 0
}
```

```output
error: a variable named 'count' is already visible here (declared at line 2)
```

Once its block has ended, a name is free again: two loops one after the other can both declare `i32 i`, and two blocks side by side can declare the same name. Inside a method, a variable cannot have the name of a field of the struct either, as [Structs](structs.md) shows.

## Constants

Variables can change. `const` makes one immutable: it must be given its value where it is declared, and that value never changes. The value can come from any expression, also one computed while the program runs, as `final` in Java: a `const` is not necessarily known to the compiler.

```cromo
i64 square(i64 x) {
    ret x * x
}

i32 main() {
    i64 side = 12
    const i64 area = square(side)
    const list<string> days = ["Mon", "Tue", "Wed"]
    println("{} {} {}", area, days.len(), days[1])
    ret 0
}
```

```output
144 3 Tue
```

`const` is deep: a `const` list or string cannot be changed in any part, so only its methods that do not modify it can be called, and it cannot be passed to a function that modifies it, with `ref`.

```cromo error
i32 main() {
    const f64 PI = 3.14159
    const list<i32> primes = [2, 3, 5]
    PI = 3.0
    primes.add(7)
    ret 0
}
```

```output
error: 'PI' is const and cannot be modified
error: cannot call 'add', which is not const
```

A `const` without a value is an error too: `const i32 n` alone has nothing to keep. A `const` can still be given away with `move`, because its value does not change, it only goes to another owner: [Ownership](ownership.md) explains it.

### Constants of a module

Outside the functions, a module can declare constants too, as limits and tables of numbers. They hold numbers, `bool` and `char`, or fixed-size arrays of them. Their value is a literal, with `-` in front of a negative number, and for an array a list of literals between square brackets, one for each element. Only their module sees them.

```cromo
const i64 minute = 60
const i64 below = -5
const u8[4] masks = [1, 2, 4, 8]

i32 main() {
    println("{} {} {}", 3 * minute, below, masks[3])
    ret 0
}
```

```output
180 -5 8
```

The value of a module constant cannot be an expression, not even of other constants. There are no global variables: a declaration outside the functions without `const` is an error.

```cromo error
const i64 minute = 60
const i64 hour = 60 * minute
i64 counter = 0

i32 main() {
    ret 0
}
```

```output
error: the value of a constant of a module is a number, a character, true or false
error: a variable outside the functions is only for the modules with 'import unsafe'
```

The second message speaks of the dialect in which the standard library talks to the system, [The unsafe dialect](unsafe.md). In a normal program, a value shared by several functions is passed as a parameter, or kept in a struct.

## Types

The types built into the language are few:

| Type | Values |
|---|---|
| `i8` `i16` `i32` `i64` | signed integers of 8, 16, 32 and 64 bits |
| `u8` `u16` `u32` `u64` | unsigned integers of 8, 16, 32 and 64 bits |
| `f32` `f64` | floating-point numbers of IEEE 754 |
| `bool` | `true` or `false` |
| `char` | a byte, another name for `u8` |
| `string` | text in UTF-8 |

Every type has the same size on every system, and there are no 128-bit integers. The numbers, their literals, their operators and their conversions are in [Numbers and operators](operators.md). Lists, maps and fixed-size arrays are in [Lists, maps and text](collections.md); structs and enums, the types a program defines, have chapters of their own.

### Booleans

A `bool` is `true` or `false`. The comparisons give one, and `&&`, `||` and `!` combine them:

```cromo
i32 main() {
    i32 age = 20
    bool adult = age >= 18
    bool teen = age >= 13 && age <= 19
    println("{} {} {}", adult, teen, !adult)
    ret 0
}
```

```output
true false false
```

A `bool` is not a number: it does not convert to 0 and 1, and a number is not true or false. The conditions of `if` and `for` want a `bool`.

### Characters

`char` is a byte: it is another name for `u8`, one byte of a UTF-8 string. A character literal is written between single quotes and must be ASCII: `'a'`, `'7'`, `' '`. The escape sequences are `'\n'` (new line), `'\t'` (tab), `'\r'` (carriage return), `'\0'` (zero), `'\\'`, `'\''` and `'\x41'`, a byte in hexadecimal.

Since a `char` is a number, the arithmetic of numbers works on it. Printed with `{}` it is a number; `{:c}` prints the character:

```cromo
i32 main() {
    char letter = 'c'
    char upper = letter - 'a' + 'A'
    bool isLetter = letter >= 'a' && letter <= 'z'
    println("{} {:c} {:c} {}", letter, letter, upper, isLetter)
    ret 0
}
```

```output
99 c C true
```

A character outside ASCII takes more than one byte in UTF-8, so it is not a `char`, and the compiler says what to write instead:

```cromo error
i32 main() {
    char e = 'é'
    ret 0
}
```

```output
error: the character 'é' does not fit in one byte: use the string "é" or the code point 0xE9
```

### Strings

`string` is text in UTF-8. `+` joins two strings. The length is in bytes, and `s[i]` is a byte, a `char`:

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

`s[i]` can be read but not written: a string is changed by building a new one.

```cromo error
i32 main() {
    string city = "Torino"
    city[0] = 't'
    ret 0
}
```

```output
error: the bytes of a string are read-only: build a new string
```

[Lists, maps and text](collections.md) shows the functions of text: searching, splitting, replacing, and the bytes of the letters outside ASCII.

## Functions

A function declares the type of its result before its name, and the type of each parameter before the parameter. `ret` returns the value; a function that returns nothing is `void`, and can end without `ret`.

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

The order of the functions in a file does not matter: `main` can come first and call functions written after it. Two functions cannot have the same name, even with different parameters, and the parameters have no default values.

A parameter is read-only: the function reads the value of the caller and cannot change it. A function that modifies an argument says so with `ref`, and one that takes it for itself with `move`, as [Ownership](ownership.md) explains.

```cromo error
void reset(i32 n) {
    n = 0
}

i32 main() {
    reset(5)
    ret 0
}
```

```output
error: parameter 'n' cannot be modified: parameters are read-only
```

A function has one result, and there is no `null`. When it may have nothing to give, it returns a conventional value, as -1 for a position not found, throws an exception, or returns a `bool` and writes the result in a parameter passed with `ref`.

### Every path ends with ret

A function that returns a value must end, on every path, with a statement that does not go on: `ret`, `throw`, which [Errors](errors.md) explains, a call of `panic`, an `if` with an `else` whose branches all end so, an infinite `for` without a `break`, or a `switch` whose cases all end so and cover every value. Otherwise the program does not compile:

```cromo error
i32 sign(i64 n) {
    if (n > 0) {
        ret 1
    } else if (n < 0) {
        ret -1
    }
}

i32 main() {
    ret sign(5)
}
```

```output
error: function 'sign' must end with 'ret' on every path: it returns i32
```

When `n` is 0, `sign` reaches its end without a value. An `else { ret 0 }` solves it. The two functions below end in the other ways:

```cromo
// The first power of two that is not smaller than n.
i64 nextPower(i64 n) {
    i64 p = 1
    for {
        if (p >= n) {
            ret p
        }
        p *= 2
    }
}

i64 firstNegative(list<i64> values) {
    for (i64 v in values) {
        if (v < 0) {
            ret v
        }
    }
    throw Error("no negative value")
}

i32 main() {
    println("{} {}", nextPower(100), firstNegative([3, -4, 5]))
    ret 0
}
```

```output
128 -4
```

Code that comes after a `ret`, `break`, `continue`, `throw` or `panic` in the same block can never run: the compiler accepts it, with the warning `unreachable code`.

## Comments

```cromo ignore
// a comment until the end of the line
/* a comment
   on several lines */
```

The `//` comments right above a declaration, with no blank line between, are its documentation: VS Code shows them under the mouse, in the completion and while the arguments of a call are written, as it does for the standard library. A comment at the end of the line of a field documents the field. The text is Markdown, with the code inside a sentence between single quotes, `'r'`, and a block of code between two lines `'''`:

```cromo ignore
// The area of a circle of radius 'r'.
//
// '''
// f64 a = area(2.0)
// '''
f64 area(f64 r) {
    ret 3.14159 * r * r
}

struct Point {
    f64 x  // from the left
    f64 y  // from the top
}
```

## Statements and lines

A statement ends at the end of its line, without a semicolon. It goes on in the next line when the line ends with an operator between two values, as `+` or `&&`, with a comma, an open parenthesis, `?` or `:`, or while a parenthesis or a square bracket is still open:

```cromo
i32 main() {
    i64 total = 1 +
        2 +
        3
    list<i64> values = [
        10,
        20
    ]
    println("{} {}",
        total, values.len())
    ret 0
}
```

```output
6 2
```

The operator goes at the end of the line, not at the start of the next one: there, the first line is already a whole statement.

```cromo error
i32 main() {
    i64 total = 1
        + 2
    ret 0
}
```

```output
error: expected an expression, found '+'
```

There is one statement for each line, and `;` appears only in the header of the classic `for`. The compiler also accepts the opening brace on the next line, and `else` on the line after the closing brace, but the style of Cromo keeps them on the same line, and `cromo fmt` moves them there: [Tools: formatting, the editor and the debugger](tools.md) explains it.

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

The parentheses and the braces are always required, also around a single statement, and the condition must be a `bool`: `if (count)` is an error, `if (count != 0)` is correct.

```cromo error
i32 main() {
    i32 count = 3
    if (count) {
        println("some")
    }
    ret 0
}
```

```output
error: the condition must be a bool, found i32
```

### Choosing a value

`c ? a : b` is the value `a` when `c` is true, `b` otherwise, as in C. Only the value chosen is computed, and the two have the same type:

```cromo
i32 main() {
    i64 n = 7
    string kind = n % 2 == 0 ? "even" : "odd"
    println("{} is {}", n, kind)
    ret 0
}
```

```output
7 is odd
```

[Numbers and operators](operators.md#the-conditional-expression) gives its precedence and its other rules.

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

`break` leaves the loop and `continue` goes to the next round; outside a `for` they are errors. There is no `while`, no `do`, and no `goto`.

`break` and `continue` have no labels: they act on the innermost loop. To leave several loops at once, put them in a function and leave it with `ret`:

```cromo
// The position of the first value that appears twice, or -1.
i64 firstRepeated(list<i32> values) {
    for (i64 i = 0; i < values.len(); i++) {
        for (i64 j = 0; j < i; j++) {
            if (values[i] == values[j]) {
                ret i
            }
        }
    }
    ret -1
}

i32 main() {
    println("{} {}", firstRepeated([4, 7, 1, 7, 4]), firstRepeated([1, 2]))
    ret 0
}
```

```output
3 -1
```

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

`switch` is most useful with enums, as [Enums and switch](enums.md) shows: there the compiler checks that every case is handled.

## Summary

- The type comes before the name, the value after `=`; every variable has a value, and a name is never declared twice in the blocks where it is visible.
- `const` makes a variable immutable, deeply; module constants hold numbers, `bool` and `char`, written as literals.
- The built-in types are the numbers, `bool`, `char` (a byte) and `string`; the bytes of a string are read-only.
- Functions return with `ret` on every path; parameters are read-only; there is no `null` and no overloading.
- A statement ends at the end of its line, and goes on after an operator, a comma or an open bracket.
- `if` wants parentheses, braces and a `bool`; `c ? a : b` chooses a value; `for` is the only loop; `break` and `continue` have no labels; `switch` has no fall-through.
