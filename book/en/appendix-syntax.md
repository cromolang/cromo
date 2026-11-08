# Appendix A: Keywords, operators and attributes

This appendix lists the words, the symbols and the built-in names of Cromo, each with the chapter that explains it.

## Keywords

These words cannot be used as names:

| Keyword | Use | Chapter |
|---|---|---|
| `break` | leaves a loop | [Common concepts](common-concepts.md) |
| `case` | a branch of a `switch` | [Enums and switch](enums.md) |
| `catch` | handles the exceptions of a `try` | [Errors](errors.md) |
| `const` | a variable that does not change; a constant of a module; a method that does not modify its object; a `move const` parameter; a `const func` | [Common concepts](common-concepts.md), [Structs](structs.md), [Ownership](ownership.md), [Anonymous functions](lambdas.md) |
| `continue` | goes to the next round of a loop | [Common concepts](common-concepts.md) |
| `default` | the branch of a `switch` for the values not listed | [Enums and switch](enums.md) |
| `else` | the alternative of an `if` | [Common concepts](common-concepts.md) |
| `enum` | declares an enum | [Enums and switch](enums.md) |
| `extern` | declares C functions, with `import unsafe` | [Calling C](c-interop.md) |
| `false`, `true` | the values of `bool` | [Common concepts](common-concepts.md) |
| `finally` | runs at every exit of a `try` | [Errors](errors.md) |
| `for` | the loop, in all its forms | [Common concepts](common-concepts.md) |
| `func` | the type of a function | [Anonymous functions](lambdas.md) |
| `if` | a condition | [Common concepts](common-concepts.md) |
| `import` | imports a module | [Modules and projects](modules.md) |
| `in` | in a `for` over the elements of a list, an array, a map or a channel | [Lists, maps and text](collections.md) |
| `interface` | declares an interface | [Interfaces and generics](interfaces-generics.md) |
| `move` | moves a value; a parameter or a capture that takes ownership | [Ownership](ownership.md) |
| `public` | makes a declaration or a member visible outside its module | [Modules and projects](modules.md) |
| `ref` | a parameter or a capture that modifies; the element of a `for`, or the data of a `case`, that can be modified | [Ownership](ownership.md) |
| `ret` | returns from a function | [Common concepts](common-concepts.md) |
| `struct` | declares a struct | [Structs](structs.md) |
| `switch` | chooses among cases | [Enums and switch](enums.md) |
| `throw` | throws an exception | [Errors](errors.md) |
| `try` | the block whose exceptions are handled | [Errors](errors.md) |
| `unsafe` | `import unsafe`: the dialect of C functions and raw memory | [The unsafe dialect](unsafe.md) |
| `void` | no result | [Common concepts](common-concepts.md) |

There is no `while`, `do`, `goto`, `class`, `new`, `delete`, `null` or `this`. The built-in functions, as `println`, and the built-in types, as `i64`, are names, not keywords; still, a program cannot declare a function, a struct or an interface named as a built-in function or as a type of numbers, `bool` or `char`.

## Symbols

| Symbol | Use |
|---|---|
| `{ }` | blocks, and the bodies of functions, structs, enums, interfaces and `extern` blocks |
| `( )` | parameters and arguments; the conditions of `if`, `for` and `switch`; grouping in expressions |
| `[ ]` | an index, `v[i]`; a list literal, `[1, 2, 3]`; the captures of a lambda, `[ref total]`; the size of an array, `i32[4]` |
| `[[ ]]` | an attribute, `[[test]]` |
| `< >` | the type parameters and arguments of generics, `list<T>`, `max<T : Ordered>` |
| `:` | the interfaces of a struct or an enum, `struct Circle : Shape`; a constraint, `T : Key`; the conditional expression |
| `+` | between constraints, `T : Key + Debug`, besides the addition |
| `.` | a field or a method, `p.x`; a name of a module, `json.decode`; a case of an enum, `Color.Red` |
| `,` | separates parameters, arguments, elements and cases |
| `;` | only in `for (init; condition; step)`: a statement ends at the end of its line |
| `~` | the destructor, `~File()` |
| `//`, `/* */` | comments; the `//` lines right above a declaration document it |

## Operators

From the strongest to the weakest, as in [Numbers and operators](operators.md):

| Operators | Meaning | Grouping |
|---|---|---|
| `.` `()` `[]` | member, call, index | left to right |
| `!` `-` | not, inversion of the bits, negation | before the value |
| `*` `/` `%` | multiplication, division, remainder | left to right |
| `+` `-` | addition, subtraction; `+` also joins strings | left to right |
| `<<` `>>` | shifts | left to right |
| `&` | bitwise and | left to right |
| `^` | bitwise exclusive or | left to right |
| `\|` | bitwise or | left to right |
| `<` `<=` `>` `>=` | comparisons | left to right |
| `==` `!=` | equal, not equal | left to right |
| `&&` | logical and, short-circuit | left to right |
| `\|\|` | logical or, short-circuit | left to right |
| `? :` | conditional: `c ? a : b` | right to left |

- The bitwise operators bind more than the comparisons, unlike C: `flags & 4 == 4` is `(flags & 4) == 4`. As in C, `==` and `!=` bind less than `<` and the others.
- `c ? a : b` is the weakest: `a || b ? x : y` is `(a || b) ? x : y`, and `a ? b : c ? d : e` is `a ? b : (c ? d : e)`. The condition is a `bool`, and only the branch chosen is computed.
- `!` on a `bool` is the logical not, on an integer the inversion of its bits; there is no `~` operator.
- `%` works only on integers; the comparisons do not chain, `a < b < c` is an error.
- In debug builds an overflow and a shift beyond the width of the type are panics, and an integer division by zero in every build; see [Numbers and operators](operators.md).
- `ref x` and `move x` before an argument say how it is passed: they are not operators, see [Ownership](ownership.md).

The assignments are statements, never parts of an expression, so `if (a = b)` is not Cromo:

| Statement | Meaning |
|---|---|
| `x = v` | assigns |
| `x += v`, `-=`, `*=`, `/=`, `%=` | `x = x + v`, and so on |
| `x &= v`, `\|=`, `^=`, `<<=`, `>>=` | the same with the bitwise operators |
| `x++`, `x--` | adds or subtracts 1 |

## Literals

| Literal | Examples | Chapter |
|---|---|---|
| integer | `42`, `1_000_000`, `0xFF`, `0b1010` | [Numbers and operators](operators.md) |
| floating point | `3.14`, `1e-9`, `2.5e3` | [Numbers and operators](operators.md) |
| character, one byte | `'a'`, `'\n'`, `'\t'`, `'\r'`, `'\0'`, `'\\'`, `'\''`, `'\x41'` | [Common concepts](common-concepts.md) |
| string, UTF-8 | `"text"`, with the escapes of the characters, `\"` and `\u{e9}` | [Common concepts](common-concepts.md) |
| `bool` | `true`, `false` | [Common concepts](common-concepts.md) |
| list | `[1, 2, 3]`, `[]` | [Lists, maps and text](collections.md) |
| array of a constant of a module | `[0xFF, 0xFF00]` | [Common concepts](common-concepts.md) |

An integer literal takes the type that the context asks for, and must fit in it. There are no literals of maps.

## Built-in types

| Type | Meaning | Chapter |
|---|---|---|
| `i8`, `i16`, `i32`, `i64` | signed integers | [Numbers and operators](operators.md) |
| `u8`, `u16`, `u32`, `u64` | unsigned integers | [Numbers and operators](operators.md) |
| `f32`, `f64` | floating-point numbers | [Numbers and operators](operators.md) |
| `bool` | `true` or `false` | [Common concepts](common-concepts.md) |
| `char` | a byte, the same type as `u8` | [Common concepts](common-concepts.md) |
| `string` | text in UTF-8 | [Lists, maps and text](collections.md) |
| `list<T>` | a list that grows | [Lists, maps and text](collections.md) |
| `map<K, V>` | a hash table | [Lists, maps and text](collections.md) |
| `T[n]`, `T[n][m]` | arrays of fixed size | [Lists, maps and text](collections.md) |
| `i32x4`, `f32x8`, ... | vectors of SIMD, of 128 or 256 bits | [Lists, maps and text](collections.md) |
| `func(A, B) R`, `const func(A) R` | functions, with the modes of their parameters, as `func(ref list<i64>) bool` | [Anonymous functions](lambdas.md) |
| `addr` | an address of memory, only with `import unsafe` | [The unsafe dialect](unsafe.md) |

`string`, `list` and `map` are structs of the core of the library, which every module sees without importing it. The sizes of the numbers are the same on every system; there is no `int`, `usize` or 128-bit integer.

## Built-in functions

| Function | What it does | Chapter |
|---|---|---|
| `print(format, ...)`, `println(format, ...)` | write on the standard output, `println` with a newline | [Formatting text](formatting.md) |
| `eprint(format, ...)`, `eprintln(format, ...)` | write on the standard error | [Formatting text](formatting.md) |
| `format(format, ...)` | the text that `print` would write, as a `string` | [Formatting text](formatting.md) |
| `panic(reason)` | stops the program, with the exit code 101 | [Errors](errors.md) |
| `assert(condition)` | a panic when the condition is false, also with `--release` | [Errors](errors.md), [Tests](tests.md) |
| `wrapAdd(a, b)`, `wrapSub(a, b)`, `wrapMul(a, b)` | arithmetic of integers that wraps around, without the overflow check | [Numbers and operators](operators.md) |
| `countOnes(x)`, `trailingZeros(x)`, `leadingZeros(x)` | count the bits of an integer, as an `i32` | [Numbers and operators](operators.md) |
| `rotateLeft(x, n)`, `rotateRight(x, n)` | turn the bits of an integer | [Numbers and operators](operators.md) |
| `embed(path)` | the bytes of a file, read by the compiler and put in the program, as a `list<u8>` | [Windows and apps](gui.md) |

The names of the types convert, and so do two methods of the floating-point numbers:

| Call | What it does | Chapter |
|---|---|---|
| `i64(x)`, `f64(x)`, ... | converts a number; `i64("42")` reads one from a text | [Numbers and operators](operators.md) |
| `string(bytes)` | a string from a `list<u8>` of UTF-8 | [Lists, maps and text](collections.md) |
| `Color(n)`, `i32(color)` | the case of an enum with that value, and back | [Enums and switch](enums.md) |
| `T()` | the default value of `T`, or its constructor | [Structs](structs.md) |
| `x.bits()`, `f64.fromBits(b)`, `f32.fromBits(b)` | the bits of a floating-point number, and back | [Numbers and operators](operators.md) |

The dialect of `import unsafe` has its own: `alloc`, `resize`, `free`, `sizeOf`, `offset`, `take`, `put`, `destroy`, `copyAt`, `copyMemory`, `compareMemory`, `checkIndex`, `variableAddress`, `addr(n)`, `s.cstring()` and `string(data, length)`; see [The unsafe dialect](unsafe.md).

## The core of the library

Every module sees these names without importing them; a name that the module declares comes first.

| Name | What it is | Chapter |
|---|---|---|
| `sort(ref items)` | sorts a list of `Ordered` values | [Lists, maps and text](collections.md) |
| `sortBy(ref items, less)` | sorts a list with a function that compares | [Lists, maps and text](collections.md) |
| `join(parts, separator)` | joins a list of strings | [Lists, maps and text](collections.md) |
| `debugText(value)` | the text that `{:?}` prints | [Formatting text](formatting.md) |
| `Exception` | the interface of what can be thrown: `string message() const` | [Errors](errors.md) |
| `Error` | a ready-made exception with its message | [Errors](errors.md) |
| `Key` | the interface of the keys of a map: `hash()` and `equals()` | [Lists, maps and text](collections.md) |
| `Ordered` | the interface of the values that `sort` orders: `compare()` | [Interfaces and generics](interfaces-generics.md) |
| `Debug` | the constraint of a `T` printed with `{:?}` | [Formatting text](formatting.md) |

## Names with a meaning

| Name | Meaning | Chapter |
|---|---|---|
| `main` | where the program starts: `i32 main()` or `void main()`, with `list<string> args` or without | [Common concepts](common-concepts.md), [Files, processes and time](system.md) |
| `Name(...)`, `~Name()` | the constructor and the destructor of the struct `Name` | [Structs](structs.md) |
| `T copy() const` | the copy of a value, made where it is given without `move` | [Structs](structs.md) |
| `string text() const` | the text that `{}` prints | [Formatting text](formatting.md) |
| `Self` | in an interface, the type that implements it | [Interfaces and generics](interfaces-generics.md) |

## Attributes

Attributes are written as in C++11, `[[name]]` or `[[name(arguments)]]`, before what they apply to, on the line before or at the start of the same line. Each attribute has its own brackets, and an attribute that the compiler does not know is an error.

| Attribute | Before | Meaning | Chapter |
|---|---|---|---|
| `[[test]]` | a function `void name()` | a test, which `cromo test` runs | [Tests](tests.md) |
| `[[json("name")]]` | a field, or a case of an enum | its name in JSON | [JSON](json.md) |
| `[[json(skip)]]` | a field | left out of JSON | [JSON](json.md) |
| `[[window]]` | `main` | on Windows, an executable without a console | [Windows and apps](gui.md) |
| `[[android("org.example.app", "Name")]]` | `main` | the package and the name of the app for Android | [Windows and apps](gui.md) |
| `[[export("name")]]` | a function, with `import unsafe` | the name and the calling convention of C | [The unsafe dialect](unsafe.md) |
| `[[cold]]` | a function, with `import unsafe` | a function that runs rarely | [The unsafe dialect](unsafe.md) |

```cromo
import std.json

struct User : json.Data {
    [[json("user_id")]]
    i64 id
    [[json(skip)]] string password
    string name
}

i32 main() {
    User u
    u.id = 7
    u.name = "Ada"
    u.password = "secret"
    println("{}", json.encode(u))
    ret 0
}
```

```output
{"user_id":7,"name":"Ada"}
```

The attributes go only before the functions, the fields and the cases of the enums; before a struct or a method they are errors. A function has `[[test]]` once; `[[window]]` and `[[android(...)]]` go only before `main`, and `[[export]]` and `[[cold]]` only in the files with `import unsafe`:

```cromo error
[[inline]]
i32 twice(i32 x) {
    ret x * 2
}

struct Point {
    [[name("x")]] f64 x
}

[[window]]
void helper() {
}

i32 main() {
    ret 0
}
```

```output
error: unknown attribute 'name': for now there is only json
error: unknown attribute 'inline': a function has only [[test]]
error: [[window]] is only for main of the program, whose executable opens no console
```

## Summary

- 28 keywords; `println`, `i64` and the other built-in names are not keywords, but a program cannot declare them again.
- The operators have the order of C, with the bitwise ones above the comparisons and `c ? a : b` the weakest, grouping from the right; assignments, `++` and `--` are statements.
- The built-in functions print, stop the program, wrap, count and turn bits, and embed files; the types convert; the core adds `sort`, `sortBy`, `join`, `Exception`, `Error`, `Key`, `Ordered` and `Debug`.
- Seven attributes: `[[test]]`, `[[json(...)]]`, `[[window]]`, `[[android(...)]]`, and `[[export(...)]]` and `[[cold]]` of the unsafe dialect.
