# Lists, maps and text

The core of the library, which every program sees without importing it, has the containers that almost every program needs: `list`, fixed-size arrays, `map` and `string`. Each one owns its elements and frees them when it ends.

## Lists

`list<T>` is a sequence of values of type `T` that grows as needed, as `std::vector` in C++:

```cromo
i32 main() {
    list<i64> numbers = [5, 3, 8]
    numbers.add(13)
    numbers.insert(0, 1)
    i64 removed = numbers.remove(2)
    println("{:?}, removed {}, length {}", numbers, removed, numbers.len())

    numbers[0] = 10
    for (ref i64 n in numbers) {
        n *= 2
    }
    println("{:?}", numbers)
    ret 0
}
```

```output
[1, 5, 8, 13], removed 3, length 4
[20, 10, 16, 26]
```

- `[a, b, c]` makes a list; an empty one is `list<i64>()`, or simply a variable without a value.
- `add`, `insert(i, x)`, `remove(i)`, `clear()` and `reserve(n)` change the list; `len()` gives its length.
- `numbers[i]` is the element itself: it can be read, assigned, passed with `ref` and moved. An index out of the list is a panic.
- `for (ref i64 n in numbers)` visits the elements and can modify them; without `ref` they are read-only.

While a `for` visits a list, the list cannot change its length: adding an element in the loop would move the elements in memory under the loop. The compiler refuses it.

### map and filter

`map` makes a new list from each element, and `filter` keeps the elements that pass a test. Both take a function, here an anonymous function, explained in chapter 10:

```cromo
i32 main() {
    list<i64> numbers = [1, 2, 3, 4, 5, 6]
    list<i64> even = numbers.filter([](i64 n) bool { ret n % 2 == 0 })
    list<string> labels = even.map<string>([](i64 n) string { ret format("#{}", n) })
    println("{:?}", labels)
    ret 0
}
```

```output
["#2", "#4", "#6"]
```

### Sorting

`sort(ref items)` sorts a list of numbers, of strings, or of a struct that implements `Ordered` with its `compare` method; `sortBy` takes a function that says whether one element comes before another. Both keep in their order the elements that are equal, as `kiwi` and `pear` here:

```cromo
i32 main() {
    list<string> words = ["pear", "fig", "banana", "kiwi"]
    sort(ref words)
    println("{}", join(words, " "))
    sortBy(ref words, [](string a, string b) bool { ret a.len() < b.len() })
    println("{}", join(words, " "))
    ret 0
}
```

```output
banana fig kiwi pear
fig kiwi pear banana
```

## Fixed-size arrays

An array has a size fixed in its type, and lives where its variable is, without memory on the heap:

```cromo
i32 main() {
    i32[4] scores
    scores[1] = 7
    f64[2][3] grid
    grid[1][2] = 0.5
    println("{:?} has {} elements", scores, scores.len())
    println("{:?}", grid)
    ret 0
}
```

```output
[0, 7, 0, 0] has 4 elements
[[0.0, 0.0, 0.0], [0.0, 0.0, 0.5]]
```

`f64[2][3]` is two arrays of three. An array is a value: it is copied when its elements are simple, moved otherwise.

## Maps

`map<K, V>` associates keys with values, with a hash table:

```cromo
i32 main() {
    map<string, i32> stock
    stock["apples"] = 10
    stock["pears"] = 4
    stock["apples"] -= 3
    stock["plums"] += 6

    println("{} apples, plums known: {}", stock["apples"], stock.contains("plums"))
    bool removed = stock.remove("pears")
    println("removed: {}, {} kinds left", removed, stock.len())

    i32 total = 0
    for (string fruit, i32 count in stock) {
        total += count
    }
    println("{} fruits", total)
    ret 0
}
```

```output
7 apples, plums known: true
removed: true, 2 kinds left
13 fruits
```

- `m[k] = v`, `m[k] += v` and `m[k]++` add a missing key, with the default value, before changing it, as `operator[]` of C++. Reading a missing key in any other way is a panic: ask first with `contains`.
- `for (K key, V value in m)` visits the map in no particular order; with `ref V value` it can modify the values.
- The keys can be numbers, `bool`, `string`, and the structs that implement the interface `Key`, with `u64 hash() const` and `bool equals(Self other) const`.

## Strings

A `string` is text in UTF-8. It owns its bytes, and is moved or copied as a list:

```cromo
i32 main() {
    string text = "  Hello, World  "
    string clean = text.trim()
    println("[{}]", clean)
    println("{} {} {}", clean.find("World"), clean.contains("lo"), clean.startsWith("Hello"))
    println("{} / {}", clean.upper(), clean.replace("World", "Cromo"))
    println("{:?}", "a,b,,c".split(","))
    println("{}", join(["x", "y", "z"], " - "))
    println("{}", "ab".repeat(3))
    ret 0
}
```

```output
[Hello, World]
7 true true
HELLO, WORLD / Hello, Cromo
["a", "b", "", "c"]
x - y - z
ababab
```

The functions of text are methods of `string`: `find`, `findFrom`, `findLast`, `contains`, `startsWith`, `endsWith`, `split`, `trim`, `trimStart`, `trimEnd`, `replace`, `upper`, `lower`, `repeat`, and the function `join(parts, separator)`. Positions are in bytes, and a text that is not found gives -1. `upper` and `lower` change only the letters of ASCII.

### Strings and bytes

The length of a string is in bytes, and a letter outside ASCII takes more than one:

```cromo
i32 main() {
    string word = "città"
    list<u8> bytes = word.bytes()
    println("{} bytes, the first three are '{}'", word.len(), word.slice(0, 3))
    string again = string(bytes)
    println("{} {}", again, again == word)
    ret 0
}
```

```output
6 bytes, the first three are 'cit'
città true
```

`slice(from, to)` takes the bytes from `from` to `to`, excluded. `string(bytes)` checks that the bytes are valid UTF-8, and throws an exception otherwise.

Strings are compared with `==`, `!=`, `<`, `<=`, `>`, `>=`, byte by byte, and joined with `+` and `+=`.

## Formatting

`println`, `print`, `eprintln` (to the standard error) and `format` (which returns a `string`) share the format of `std::format` in C++ and of Python. Each `{}` takes the next value; inside the braces, after `:`, a specification says how to write it:

```cromo
i32 main() {
    f64 ratio = 2.0 / 3.0
    i64 n = 1234567
    println("[{:.3f}] [{:8.2f}] [{:e}]", ratio, ratio, ratio)
    println("[{:<6}] [{:^7}] [{:>6}]", "ab", "mid", "r")
    println("[{:+}] [{:09}] [{:x}] [{:#x}] [{:b}]", n, n, n, n, n)
    println("{0} and {1}, then {0} again", "first", "second")
    println("{{ and }} are braces")
    ret 0
}
```

```output
[0.667] [    0.67] [6.666667e-01]
[ab    ] [  mid  ] [     r]
[+1234567] [001234567] [12d687] [0x12d687] [100101101011010000111]
first and second, then first again
{ and } are braces
```

| Specification | Meaning |
|---|---|
| `<` `^` `>` | align to the left, the center, the right, in the width that follows |
| a number | the minimum width; with a `0` before it, filled with zeros |
| `.n` | digits after the point, or the maximum length of a text |
| `+` | the sign also for positive numbers |
| `x` `X` `b` `o` | hexadecimal, binary, octal; `#` adds the prefix |
| `f` `e` `g` | fixed point, exponent, the shorter of the two |
| `?` | the form for debugging, for any value |

The format string must be a literal: the compiler checks the number and the types of the values against it.

## Summary

- `list<T>` grows as needed; `[a, b]` makes one; `for` visits it, with `ref` to modify the elements.
- `T[n]` is an array of fixed size, without the heap.
- `map<K, V>` is a hash table; `m[k] += v` adds a missing key; the order is not defined.
- `string` is UTF-8, measured in bytes, with methods for searching, splitting and replacing.
- `format` and `println` use the format of `std::format`, checked by the compiler.
