# Appendix A: Keywords and operators

## Keywords

| Keyword | Use |
|---|---|
| `break` | leaves a loop |
| `case` | a branch of a `switch` |
| `catch` | handles an exception |
| `const` | an immutable variable; a method that does not modify its object; a `const func` |
| `continue` | goes to the next round of a loop |
| `default` | the branch of a `switch` for the values not listed |
| `else` | the alternative of an `if` |
| `enum` | declares an enum |
| `extern` | declares C functions, in a module with `import unsafe` |
| `false`, `true` | the values of `bool` |
| `finally` | runs at every exit of a `try` |
| `for` | the loop, in all its forms |
| `func` | the type of a function |
| `if` | a condition |
| `import` | imports a module |
| `in` | in a `for` over the elements |
| `interface` | declares an interface |
| `move` | moves a value; a parameter or a capture that takes ownership |
| `public` | makes a declaration visible outside its module |
| `ref` | a parameter or a capture that modifies; the elements of a `for` that can be modified |
| `ret` | returns from a function |
| `struct` | declares a struct |
| `switch` | chooses among cases |
| `throw` | throws an exception |
| `try` | the block whose exceptions are handled |
| `unsafe` | `import unsafe`: the dialect of C functions and raw memory |
| `void` | no result |

`print`, `println`, `eprint`, `eprintln`, `format`, `panic` and `assert` are built-in functions, not keywords.

## Built-in types

| Type | Meaning |
|---|---|
| `i8`, `i16`, `i32`, `i64` | signed integers |
| `u8`, `u16`, `u32`, `u64` | unsigned integers |
| `f32`, `f64` | floating-point numbers |
| `bool` | `true` or `false` |
| `char` | a byte, a synonym of `u8` |
| `string` | text in UTF-8 |
| `list<T>` | a list that grows |
| `map<K, V>` | a hash table |
| `T[n]` | an array of fixed size |
| `func(A, B) R` | a function |
| `addr` | an address of C, only with `import unsafe` |

## Operators

From the strongest to the weakest:

| Operators | Meaning |
|---|---|
| `.` `()` `[]` | member, call, index |
| `!` `-` | not, inversion of the bits, negation |
| `*` `/` `%` | multiplication, division, remainder |
| `+` `-` | addition, subtraction; `+` also joins strings |
| `<<` `>>` | shifts |
| `&` | bitwise and |
| `^` | bitwise exclusive or |
| `\|` | bitwise or |
| `==` `!=` `<` `<=` `>` `>=` | comparisons |
| `&&` | logical and, short-circuit |
| `\|\|` | logical or, short-circuit |
| `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | assignments, statements on their own |

- `i++` and `i--` are statements on their own, never parts of an expression.
- There is no conditional operator `?:`: write an `if`.
- A shift beyond the width of the type is a panic in debug builds.
- Assignments are statements: `if (a = b)` is not Cromo.

## Literals

| Literal | Example |
|---|---|
| integer | `42`, `1_000_000`, `0xFF`, `0b1010` |
| floating point | `3.14`, `1e-9`, `2.5e3` |
| character | `'a'`, `'\n'`, `'\t'`, `'\\'`, `'\''`, `'\x41'` |
| string | `"text"`, with the same escapes and `\"` |
| list | `[1, 2, 3]` |

## Attributes

Attributes are written as in C++11, before what they apply to:

| Attribute | Meaning |
|---|---|
| `[[test]]` | a test function |
| `[[json("name")]]` | the name of a field or a case in JSON |
| `[[json(skip)]]` | a field left out of JSON |
