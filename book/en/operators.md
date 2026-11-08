# Numbers and operators

This chapter is about numbers: the integer and floating-point types, the type of a literal, the operators and their precedence, what happens when a result does not fit, and the conversions, also from text. Most rules are those of C; where C leaves the result undefined, Cromo gives a rule, and the program stops instead of going on with a wrong value.

## The number types

| Type | Size | Values |
|---|---|---|
| `i8` | 8 bits | −128 to 127 |
| `u8` | 8 bits | 0 to 255 |
| `i16` | 16 bits | −32 768 to 32 767 |
| `u16` | 16 bits | 0 to 65 535 |
| `i32` | 32 bits | −2 147 483 648 to 2 147 483 647 |
| `u32` | 32 bits | 0 to 4 294 967 295 |
| `i64` | 64 bits | −9 223 372 036 854 775 808 to 9 223 372 036 854 775 807 |
| `u64` | 64 bits | 0 to 18 446 744 073 709 551 615 |
| `f32` | 32 bits | IEEE 754 single precision, about 7 significant digits |
| `f64` | 64 bits | IEEE 754 double precision, about 16 significant digits |

The `i` types are signed, in two's complement, and the `u` types are unsigned. Every type has the same size on every system: there is no `int` whose size depends on the machine, and no `size_t`. Lengths and positions are `i64`. There are no 128-bit integers.

`char` is not a type of its own: it is another name for `u8`, one byte of a UTF-8 string, as [Common concepts](common-concepts.md) shows. What this chapter says of `u8` holds for `char` too.

The default value of every number is 0.

## Literals

An integer literal is written in decimal, in hexadecimal after `0x`, or in binary after `0b`, with `_` to separate the digits: `1_000_000`, `0xFF`, `0b1010`, `0xFFFF_FFFF`. A zero in front does not make it octal, unlike C: `017` is seventeen. There are no suffixes as `5u` or `5L`.

A floating-point literal has a decimal point with digits on both sides, an exponent after `e`, or both: `3.14`, `1e-9`, `2.5e3`. `.5` and `5.` are not literals: write `0.5` and `5.0`. A negative number is a literal with `-` in front.

### The type of a literal

A literal has no type of its own: it takes the one that its place requires, the type of the variable, of the parameter, or of the other operand.

```cromo
i32 main() {
    i32 apples = 5
    i64 total = apples + 10  // 10 is an i32, as apples
    u8 mask = 0xF0           // 0xF0 is a u8
    f64 half = 10 / 4        // 10 and 4 are f64: 2.5, not 2
    f64 whole = 5            // an integer literal where a float is expected
    println("{} {} {} {}", total, mask, half, whole)
    ret 0
}
```

```output
15 240 2.5 5
```

`f64 half = 10 / 4` is 2.5, while in C it is 2: the two literals are `f64`, so the division is between floats. An integer literal can be used where a floating-point number is expected, but not the opposite:

```cromo error
i32 main() {
    i32 n = 2.5
    ret 0
}
```

```output
error: this decimal literal cannot be used as i32
```

A literal must fit in its type. The compiler checks it, also when the type comes from the other operand:

```cromo error
i32 main() {
    u8 small = 300
    u8 negative = -1
    i32 count = 3
    i64 big = 5000000000 + count
    ret 0
}
```

```output
error: value 300 is out of range for u8 (0 .. 255)
error: value -1 is out of range for u8 (0 .. 255)
error: value 5000000000 is out of range for i32 (-2147483648 .. 2147483647)
```

In the last line `5000000000` takes the type of `count`, an `i32`: writing `i64(count)` makes the sum an `i64`.

Where nothing says the type, the compiler asks for it:

```cromo error
i32 main() {
    println("{}", 10 / 3)
    ret 0
}
```

```output
error: the type of this expression cannot be deduced
```

`i32(10) / 3`, or a variable, solves it.

## Mixing types

A number becomes a number of another type by itself only when that type holds all of its values:

- a signed integer becomes a larger signed integer: `i8` → `i16` → `i32` → `i64`;
- an unsigned integer becomes a larger unsigned or signed integer: `u8` → `u16`, `u8` → `i16`, `u32` → `i64`;
- an integer becomes a float that holds it exactly: those of 8 and 16 bits an `f32`, those up to 32 bits an `f64`;
- an `f32` becomes an `f64`.

In an operation between two types, the value of the smaller one is converted to the larger one: `u8 + i32` is an `i32`, `i32 * f64` an `f64`. When neither type holds the other, as `i32` and `u32`, or a signed integer and an unsigned one of 64 bits, the operation is an error:

```cromo error
i32 main() {
    i32 a = -5
    u32 b = 7
    i64 sum = a + b
    ret 0
}
```

```output
error: cannot mix i32 and u32: convert one of them explicitly, for example i64(x)
```

In C, `-1 < 1u` is false, because -1 silently becomes the largest unsigned number. In Cromo the conversion is written, toward a type that holds both values:

```cromo
i32 main() {
    i32 a = -5
    u32 b = 7
    u8 small = 200
    f64 ratio = 0.5
    println("{} {} {}", i64(a) + i64(b), small + a, a * ratio)
    ret 0
}
```

```output
2 195 -2.5
```

`i64` and `f64` do not mix either, because an `f64` cannot hold every `i64`: an `i64` times an `f64` is the error `mismatched types i64 and f64`, and `f64(n) * ratio` is the solution.

The type of an operation is that of its operands, never that of the place where the result goes. With `i32 a = 100000`, `i64 big = a * 100000` computes an `i32` product, which does not fit and stops the program; the product becomes an `i64` only after. To compute it as an `i64`, convert first:

```cromo
i32 main() {
    i32 a = 100000
    i64 big = i64(a) * 100000
    println("{}", big)
    ret 0
}
```

```output
10000000000
```

## Operators

From the strongest to the weakest:

| Operators | Meaning | Grouping |
|---|---|---|
| `.` `()` `[]` | member, call, index | left to right |
| `!` `-` | not, inversion of the bits, negation | before the value |
| `*` `/` `%` | multiplication, division, remainder | left to right |
| `+` `-` | addition, subtraction | left to right |
| `<<` `>>` | shifts | left to right |
| `&` | bitwise and | left to right |
| `^` | bitwise exclusive or | left to right |
| `\|` | bitwise or | left to right |
| `<` `<=` `>` `>=` | comparisons | left to right |
| `==` `!=` | equal, not equal | left to right |
| `&&` | logical and, short-circuit | left to right |
| `\|\|` | logical or, short-circuit | left to right |
| `? :` | conditional | right to left |

The order is that of C with one difference: the bitwise operators bind more than the comparisons, so `flags & 4 == 4` means `(flags & 4) == 4`, as one would expect, while in C it means `flags & (4 == 4)`. As in C, `==` and `!=` bind less than the other comparisons: `a < b == c < d` compares two `bool` values.

```cromo
i32 main() {
    i32 a = 20
    i32 b = 5
    i32 c = 2
    println("{} {} {}", a - b - c, a / b * c, a + b * c)
    i32 flags = 6
    println("{} {}", flags & 4 == 4, 1 + flags << 2)
    bool x = true
    bool y = false
    println("{}", !x || y && x)
    ret 0
}
```

```output
13 8 30
true 28
false
```

`a - b - c` is `(a - b) - c`, and `a / b * c` is `(a / b) * c`. `1 + flags << 2` is `(1 + flags) << 2`, and `!x || y && x` is `(!x) || (y && x)`. When the order is not obvious to the reader, parentheses cost nothing.

The comparisons do not chain: `a < b < c` compares the `bool` of `a < b` with a number, and is an error. Write `a < b && b < c`.

### Arithmetic

`+`, `-`, `*` and `/` work on every number, and `+` also joins strings. `%`, the remainder, works only on integers: there is no remainder of floats.

```cromo error
i32 main() {
    f64 angle = 7.5
    u32 count = 3
    println("{}", angle % 2.0)
    println("{}", -count)
    ret 0
}
```

```output
error: '%' only works on integers
error: cannot negate an unsigned value (u32)
```

`-x` on an unsigned integer is an error too: its result would almost never fit. Convert it first, `-i64(count)`.

### Comparisons and logic

The comparisons give a `bool`. `&&`, `||` and `!` work only on `bool` values, not on numbers. `&&` and `||` compute their right side only when the left one does not decide already, so the right side can rely on the left:

```cromo
bool positiveAt(list<i32> values, i64 i) {
    ret i < values.len() && values[i] > 0
}

i32 main() {
    list<i32> values = [4, -1]
    println("{} {} {}", positiveAt(values, 0), positiveAt(values, 1), positiveAt(values, 5))
    ret 0
}
```

```output
true false false
```

With `i` equal to 5, `values[i]` is never read: the index would be outside the list.

### Bitwise operators

`&`, `|` and `^` are the and, or and exclusive or of the bits of two integers. `!` before an integer inverts its bits: Cromo has no `~`, which is only in the name of a destructor.

```cromo
i32 main() {
    u8 flags = 0b0110
    println("{} {} {} {}", flags & 0b0100, flags | 0b0001, flags ^ 0b0010, !flags)
    i32 n = 6
    println("{}", !n)
    ret 0
}
```

```output
4 7 4 249
-7
```

The bitwise operators work only on integers. On `bool` values they are errors: the logical operators are `&&`, `||` and `!`, and the exclusive or of two `bool` values is `!=`.

```cromo error
i32 main() {
    bool a = true
    bool b = false
    bool both = a & b
    ret 0
}
```

```output
error: '&' needs numbers, found bool
```

### The conditional expression

`c ? a : b` is the value `a` when `c` is true and `b` otherwise, as in C. Only the branch chosen is computed. It binds less than `||` and groups from the right, so a chain of them reads as a chain of `else if`: `a ? b : c ? d : e` is `a ? b : (c ? d : e)`.

```cromo
string size(i64 n) {
    ret n < 10 ? "small" : n < 1000 ? "medium" : "large"
}

i32 noisy(i32 n) {
    println("computing {}", n)
    ret n
}

i32 main() {
    println("{} {} {}", size(5), size(50), size(5000))
    i64 n = 7
    i64 next = n % 2 == 0 ? n / 2 : 3 * n + 1
    println("{}", next)
    bool first = false
    i32 chosen = first ? noisy(1) : noisy(2)
    println("{}", chosen)
    ret 0
}
```

```output
small medium large
22
computing 2
2
```

The condition must be a `bool`, as in an `if`:

```cromo error
i32 main() {
    i32 count = 3
    i32 shown = count ? count : 1
    ret 0
}
```

```output
error: the condition must be a bool, found i32
```

The two branches have the same type, or one converts to the other by itself, as `i32` to `i64`. A literal in a branch takes the type of the other branch, or of the context: in `f64 weight = heavy ? 1 : 0.5` the `1` is an `f64`. A line that ends with `?` or `:` goes on in the next one.

A branch that is a variable owning resources, as a `string`, gives a copy of it, and the variable stays as it was. `move` cannot be written in a branch for now: to move one of two values, use an `if`.

```cromo
i32 main() {
    string short = "Ada"
    string long = "Grace"
    bool wantShort = true
    string chosen = wantShort ? short : long
    chosen += "!"
    println("{} {}", chosen, short)
    ret 0
}
```

```output
Ada! Ada
```

```cromo error
i32 main() {
    string a = "first"
    string b = "second"
    bool useA = true
    string taken = useA ? move a : b
    ret 0
}
```

```output
error: 'move' is not written in a branch of '?' for now
```

### Assignments

`=` gives a variable a new value. Each arithmetic and bitwise operator has its compound assignment, `x op= y`, which is `x = x op y` with the same checks: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=` and `>>=`. `x++` and `x--` add and subtract one.

```cromo
i32 main() {
    i32 x = 10
    x *= 3
    x -= 2
    x /= 4
    x <<= 2
    x |= 1
    x++
    println("{}", x)
    ret 0
}
```

```output
30
```

An assignment is a statement, not an expression, and so are `x++` and `x--`: they stand on a line of their own. `if (x = 5)`, `a = b = 0` and `v[i++]` are not Cromo:

```cromo error
i32 main() {
    i32 a = 0
    i32 b = 0
    a = b = 3
    ret 0
}
```

```output
error: expected a newline at the end of the statement (one statement per line), found '='
```

## Integer division and remainder

`/` between integers truncates toward zero, and `%` has the sign of the dividend, the number on its left, as in C. So `(a / b) * b + a % b` is always `a`:

```cromo
i32 main() {
    i32 a = 7
    i32 b = -7
    println("{} {} {} {}", a / 2, b / 2, a % 3, b % 3)
    println("{} {}", a % -3, b % -3)
    ret 0
}
```

```output
3 -3 1 -1
1 -1
```

`-7 / 2` is -3, not -4, and `-7 % 3` is -1. A remainder that is never negative, as a position on a circle of `n` places, is `((a % n) + n) % n`.

## Overflow

An integer operation whose result does not fit in its type is an overflow. In a debug build, the default, it stops the program:

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

A *panic* stops the program, prints where it happened and the calls that led there, and gives the exit code 101. The check covers `+`, `-`, `*`, the compound assignments, `++` and `--`, and two cases that are easy to forget: the negation of the smallest value, `-x` with `x` equal to −2 147 483 648 for an `i32`, and the division of the smallest value by -1. Both results would be 2 147 483 648, one more than the largest `i32`.

In a release build, with `--release`, there is no check and the integers wrap around, as the unsigned integers of C do: the result keeps the low bits, so 127 + 1 in an `i8` is −128. The check costs a little, and a program that has been tested can do without it. The result is the same on every system, and never undefined, also for the signed integers.

### Wrapping on purpose

Some computations want the wrap around, as hashes, checksums and random numbers. `wrapAdd(a, b)`, `wrapSub(a, b)` and `wrapMul(a, b)` add, subtract and multiply two integers of the same type keeping the low bits, without the check, in debug builds too:

```cromo
// The FNV-1a hash of a text: its arithmetic wraps around on purpose.
u64 fnv1a(string text) {
    u64 hash = 0xcbf29ce484222325
    for (i64 i = 0; i < text.len(); i++) {
        hash = wrapMul(hash ^ text[i], 0x100000001b3)
    }
    ret hash
}

i32 main() {
    println("{:x}", fnv1a("hello"))
    u8 a = 200
    u8 b = 100
    println("{} {} {}", wrapAdd(a, b), wrapSub(b, a), wrapMul(a, b))
    ret 0
}
```

```output
a430d84680aabd0b
44 156 32
```

They work on integers only. Their type comes from the arguments, so with two literals, as `wrapAdd(1, 2)`, the compiler asks to write it: `wrapAdd(u32(1), 2)`.

## Division by zero

An integer divided by zero, with `/` or `%`, is a panic, in debug and in release builds alike: there is no value that could stand for the result.

```cromo file=average.cro
i32 average(i32 total, i32 count) {
    ret total / count
}

i32 main() {
    println("{}", average(10, 2))
    println("{}", average(10, 0))
    ret 0
}
```

```output
5
panic: division by zero at average.cro:2:15
    at average (average.cro:2)
    at main (average.cro:7)
```

Floating-point numbers follow IEEE 754 instead: a division by zero gives infinity, `inf` or `-inf`, and 0.0 / 0.0 gives `nan`, "not a number", without a panic. A result too large for the type is `inf` too. `nan` is not equal to anything, itself included, so `x != x` is true only when `x` is `nan`:

```cromo
i32 main() {
    f64 one = 1.0
    f64 zero = 0.0
    f64 nan = zero / zero
    println("{} {} {}", one / zero, -one / zero, nan)
    println("{} {}", nan == nan, nan != nan)
    ret 0
}
```

```output
inf -inf nan
false true
```

How the floats are printed, with how many digits, is in [Formatting text](formatting.md).

## Shifts

`x << n` moves the bits of `x` to the left by `n` places, and `x >> n` to the right. The result has the type of `x`; the count `n` can be an integer of any type.

- `<<` brings in zeros on the right. The bits that go out on the left are lost, without an overflow check.
- `>>` on a signed integer keeps its sign, bringing in copies of the highest bit: `-16 >> 2` is -4. On an unsigned integer it brings in zeros.
- A literal on the left takes its type from the context, as `u64 bit = 1 << k`. Without a context, as in `println("{}", 1 << k)`, the compiler asks to write it: `u64(1) << k`.

```cromo
i32 main() {
    i32 negative = -16
    u32 high = 0xF0000000
    u8 n = 2
    println("{} {}", negative >> n, high >> n)
    u8 b = 0x81
    println("{}", b << 1)
    i64 k = 40
    u64 bit = 1 << k
    println("{}", bit)
    ret 0
}
```

```output
-4 1006632960
2
1099511627776
```

A count that is negative, or not smaller than the width of the type, as `x << 40` on an `i32`, is a panic in a debug build: `panic: shift count out of the width of i32`. In C it is undefined behavior. With `--release` the count is taken modulo the width, so `x << 40` on an `i32` is `x << 8`, and a count of -1 is 31.

## Bits

Five built-in functions work on the bits of an integer of any type. Each becomes one instruction of the processor, or a few where it has none:

- `countOnes(x)`: how many bits of `x` are 1;
- `trailingZeros(x)`: how many bits are 0 under the lowest bit at 1, which is the position of that bit;
- `leadingZeros(x)`: how many bits are 0 over the highest bit at 1;
- `rotateLeft(x, n)` and `rotateRight(x, n)`: the bits of `x` turned by `n` places, those that go out on one side coming in on the other.

The first three give an `i32`; for 0 they give the width of the type, 64 for a `u64`. The rotations give the type of `x`; `n` is an integer of any type, taken modulo the width, so a negative `n` turns the other way.

```cromo
i32 main() {
    u64 row = 0x00FF000000000000
    println("{} {} {}", countOnes(row), trailingZeros(row), leadingZeros(row))
    println("{:x} {:x}", rotateLeft(row, 16), rotateRight(row, 16))
    u8 zero = 0
    println("{} {}", trailingZeros(zero), leadingZeros(zero))
    u8 b = 0b1000_0001
    i32 back = -1
    println("{} {}", rotateLeft(b, 1), rotateLeft(b, back))

    // The positions of the bits at 1 of a set, from the lowest.
    u64 set = 0b1011_0100
    list<i32> positions
    for (set != 0) {
        positions.add(trailingZeros(set))
        set &= set - 1
    }
    println("{:?}", positions)
    ret 0
}
```

```output
8 48 8
ff ff00000000
8 8
3 192
[2, 4, 5, 7]
```

`set & (set - 1)` clears the lowest bit at 1, so the loop visits the bits of the set one by one.

The result of these functions depends on the width of the type, so an integer literal cannot be their argument: its type would be a guess.

```cromo error
i32 main() {
    println("{}", countOnes(5))
    ret 0
}
```

```output
error: the type of this literal cannot be deduced: write it, for example countOnes(u64(5))
```

A rotation of a literal takes the type expected, as in `u64 r = rotateLeft(1, 8)`, and a literal as the count `n` takes the type of `x`. A value that is not an integer is an error.

### The bits of a floating-point number

`x.bits()` gives the bits of a floating-point number as they are in memory: those of an `f64` as a `u64`, those of an `f32` as a `u32`. `f64.fromBits(b)` and `f32.fromBits(b)` make the number again from its bits. They serve the formats of files and of the network, and the hashes of numbers:

```cromo
i32 main() {
    f64 x = 1.0
    u64 bits = x.bits()
    println("{:x}", bits)
    println("{}", f64.fromBits(bits + 1) - x)
    f32 h = -2.5
    println("{:x} {}", h.bits(), f32.fromBits(h.bits()))
    f64 negativeZero = -0.0
    println("{} {:x}", negativeZero == 0.0, negativeZero.bits())
    ret 0
}
```

```output
3ff0000000000000
2.220446049250313e-16
c0200000 -2.5
true 8000000000000000
```

The bits that follow those of 1.0 are the next `f64`, and the difference is the precision of the type around 1. -0.0 is equal to 0.0, but its bits are not. The integers have no `bits()`: they are their bits already.

## Conversions

A conversion that can change the value is written as a call with the name of the type: `i32(x)`, `f64(n)`, `u8(c)`. The compiler asks for it wherever the conversion is not implicit:

```cromo error
i32 main() {
    i64 big = 5
    i32 small = big
    ret 0
}
```

```output
error: a value of type i64 cannot be used as i32 without an explicit conversion, for example i32(x)
```

What the conversion does depends on the types:

- **From an integer to another integer**, the value keeps its low bits when it does not fit, as in C: `u8(300)` is 44, and `u32(-1)` is 4 294 967 295. It is never a panic: the conversion is written, so the loss is asked for.
- **From an integer to a float**, the value is the nearest float: an `i64` above 2⁵³ can lose its last digits.
- **From a float to an integer**, the fraction is dropped, toward zero: `i32(2.9)` is 2 and `i32(-2.9)` is -2. A value that does not fit in the integer, or `nan`, is a panic in a debug build; with `--release` it gives the nearest value of the type, and 0 for `nan`.
- **From `f64` to `f32`**, the value is the nearest `f32`, and infinity when it is too large.

```cromo
i32 main() {
    i32 wide = 300
    i32 negative = -1
    i64 big = 5000000000
    println("{} {} {} {}", u8(wide), i8(wide), u32(negative), i32(big))
    f64 price = 2.9
    println("{} {}", i32(price), i32(-price))
    f64 tenth = 0.1
    println("{}", f64(f32(tenth)))
    ret 0
}
```

```output
44 44 4294967295 705032704
2 -2
0.10000000149011612
```

`f64(f32(tenth))` shows what an `f32` keeps of 0.1. A float out of the range of the integer stops the program:

```cromo file=convert.cro
i32 main() {
    f64 huge = 1e20
    println("{}", i32(huge))
    ret 0
}
```

```output
panic: floating-point value out of range for i32 at convert.cro:3:19
    at main (convert.cro:3)
```

### bool is not a number

A `bool` does not convert to a number, and a number does not convert to a `bool`, not even when written:

```cromo error
i32 main() {
    bool ready = true
    i32 n = i32(ready)
    ret 0
}
```

```output
error: cannot convert bool to i32
```

The conditional expression and a comparison say what is meant: `ready ? 1 : 0` and `n != 0`.

## Numbers from text

`i32("42")` reads a number from a string. It works for every integer type and for `f32` and `f64`, and the argument can be any `string`, not only a literal.

The text must be the number alone, without spaces: an optional `+` or `-`, then decimal digits. Hexadecimal, `_` and a decimal point are not accepted in an integer. For `f32` and `f64` the text can also have a decimal point and an exponent with `e` or `E`, and it can be `inf`, `infinity` or `nan`, in capitals or not. A float is the one nearest to the text, as `strtod` gives it in C.

When the text is not a number, or the number does not fit in the type, the conversion throws an exception, an `Error`, which the program can catch, as [Errors](errors.md) explains:

```cromo
void show(string text) {
    try {
        println("'{}' is {}", text, i32(text))
    } catch (Error e) {
        println("'{}' is not an i32", text)
    }
}

i32 main() {
    show("42")
    show("-7")
    show("+007")
    show(" 42")
    show("4.0")
    show("0x1F")
    show("3000000000")
    println("{} {} {} {}", f64("2.5"), f64("-1e3"), f64("INF"), f32("0.1"))
    ret 0
}
```

```output
'42' is 42
'-7' is -7
'+007' is 7
' 42' is not an i32
'4.0' is not an i32
'0x1F' is not an i32
'3000000000' is not an i32
2.5 -1000 inf 0.1
```

The message of the exception says which of the two errors it is: `'abc' is not a valid i32`, or `'300' does not fit in u8`. A negative number does not fit in an unsigned type, except `u8("-0")`, which is 0. A text read from a file or typed by a user can end with a newline or a space: `trim()` removes them before the conversion.

## Summary

- The integers are `i8` to `i64` and `u8` to `u64`, the floats `f32` and `f64`; `char` is `u8`; there are no 128-bit integers.
- A literal takes the type of its context and must fit in it; an operation has the type of its operands, not that of the variable that receives the result.
- A number converts by itself only to a type that holds all of its values; signed and unsigned integers of the same size do not mix.
- The bitwise operators bind more than the comparisons; `c ? a : b` binds least and groups from the right; assignments, `++` and `--` are statements.
- Integer division truncates toward zero, and `%` has the sign of the dividend.
- Integer overflow, an integer division by zero and a shift count out of the width are panics in debug builds; with `--release` overflow wraps around, the count is taken modulo the width, and the division by zero is still a panic. Floats give `inf` and `nan`.
- `wrapAdd`, `wrapSub` and `wrapMul` wrap around on purpose; `countOnes`, `trailingZeros`, `leadingZeros`, `rotateLeft` and `rotateRight` work on bits; `x.bits()` and `f64.fromBits(b)` give and take the bits of a float.
- Conversions are written as `i32(x)`: integers keep their low bits, floats are truncated, and `bool` is not a number. `i32("42")` reads a number from a text and throws an `Error` when it cannot.
