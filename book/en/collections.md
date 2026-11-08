# Lists, maps and text

The core of the library, which every program sees without importing it, has the containers that almost every program needs: `list`, fixed-size arrays, `map` and `string`. Each one owns its elements and frees them when it ends. At the end of the chapter, the vectors of SIMD work on several numbers at once.

## Lists

`list<T>` is a sequence of values of type `T` that grows as needed, as `std::vector` in C++. The elements are one after the other on the heap:

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

A list declared without a value is empty, as `list<i64>()`: every variable has a value.

### List literals

`[a, b, c]` makes a list. The type of its elements is the one that the place expects: the declared type of the variable, the type of the parameter, the type that the function returns. Where nothing is expected, it is the type of the first element:

```cromo
f64 average(list<f64> values) {
    f64 sum = 0.0
    for (f64 v in values) {
        sum += v
    }
    ret sum / f64(values.len())
}

i32 main() {
    list<string> names = ["anna", "bruno"]
    list<f64> empty = []
    list<i64> primes = [
        2,
        3,
        5,
        7,
    ]
    println("{:?} {} {:?}", names, empty.len(), primes)
    println("{}", average([1, 2.5, 3]))
    println("{:?}", [i64(10), 20, 30])
    ret 0
}
```

```output
["anna", "bruno"] 0 [2, 3, 5, 7]
2.1666666666666665
[10, 20, 30]
```

- In `average([1, 2.5, 3])` the parameter says that the elements are `f64`, so `1` and `3` are `f64` too.
- A literal can stand on several lines, and a comma after the last element is allowed.
- The elements are given to the list as to `add()`: a string or another value that owns memory is copied, unless it is written `move name`. The rules of copies and moves are in [Ownership](ownership.md).

Where nothing gives the type, a list of number literals does not compile, as a lone `5` does not; neither does an empty `[]`. An element cannot be written with `ref`: the list keeps its own elements, it does not refer to variables.

```cromo error
i32 main() {
    i64 x = 1
    println("{:?}", [1, 2])
    println("{:?}", [])
    list<i64> v = [ref x, 2]
    ret 0
}
```

```output
error: the type of the elements cannot be deduced: write the type of the list, as list<i64> values = [1, 2], or of the first element, as [i64(1), 2]
error: the type of an empty list cannot be deduced: write it, as list<i64> values = []
error: an element of a list is given to it, not passed with 'ref'
```

### Methods and elements

| Method | What it does |
|---|---|
| `len()` | the number of elements, an `i64` |
| `add(x)` | adds `x` at the end |
| `insert(i, x)` | puts `x` at position `i`, from 0 to `len()`; the elements from there move one place forward |
| `remove(i)` | removes the element at `i` and gives it back; the elements after it move one place back |
| `clear()` | removes all the elements |
| `reserve(n)` | makes room for `n` elements in all, so that adding them does not move the list in memory again |
| `copy()` | a new list with a copy of each element |
| `map<U>(f)` | a new list with what `f` gives for each element |
| `filter(f)` | a new list with a copy of the elements for which `f` gives `true` |

`numbers[i]` is the element itself: it can be read, assigned, passed with `ref` and moved with `move numbers[i]`, which leaves the default value of the type in its place:

```cromo
i32 main() {
    list<string> names = ["anna", "bruno", "carla"]
    string first = move names[0]
    list<string> copied = names
    copied.add("dario")
    println("{} {:?} {:?}", first, names, copied)
    ret 0
}
```

```output
anna ["", "bruno", "carla"] ["", "bruno", "carla", "dario"]
```

`copied = names` copies the list, with all its strings: the two lists are independent. `move` gives a list to another variable without copying it.

An index outside the list, in `numbers[i]` as in `remove(i)` and `insert(i, x)`, is a panic, which stops the program:

```cromo file=scores.cro
i32 main() {
    list<i64> scores = [7, 9, 4]
    println("{}", scores[3])
    ret 0
}
```

```output
panic: index out of bounds: the index is 3 but the length is 3 at scores.cro:3:25
    at main (scores.cro:3)
```

Two lists cannot be compared with `==`: compare their elements in a loop.

### for over a list

`for (T x in v)` visits the elements in their order. Without `ref`, `x` is read-only; with `ref`, the body can modify each element through `x`. The type of `x` is written, and it must be exactly the type of the elements:

```cromo error
i32 main() {
    list<i32> ages = [30, 40]
    for (i64 a in ages) {
        println("{}", a)
    }
    for (i32 a in ages) {
        a += 1
    }
    ret 0
}
```

```output
error: the elements of this list are i32: write for (i32 a in ...)
error: 'a' is an element of a read-only 'for': write 'for (ref i32 a in ...)' to modify it
```

While a `for` visits a list, the body can read the list, but it cannot change it except through `x`: no method that changes it, no assignment, no `ref` or `move` of the list or of its other elements. Adding an element could move all the elements in memory under the loop, and `ages[0] = 1` could be the same element as `x`:

```cromo error
i32 main() {
    list<i64> ages = [30, 40]
    for (ref i64 a in ages) {
        ages.add(a)
        ages[0] = 1
    }
    ret 0
}
```

```output
error: 'ages' cannot be modified while a 'for' goes over the elements of 'ages'
error: 'ages[0]' cannot be modified while a 'for' goes over the elements of 'ages'
```

To change the length of a list while going over it, use a loop with an index, which reads the length again at each step:

```cromo
i32 main() {
    list<i64> numbers = [1, 2, 3, 4, 5, 6]
    i64 i = 0
    for (i < numbers.len()) {
        if (numbers[i] % 2 == 0) {
            numbers.remove(i)
        } else {
            i++
        }
    }
    println("{:?}", numbers)
    ret 0
}
```

```output
[1, 3, 5]
```

### map and filter

`map` makes a new list from each element, and `filter` keeps the elements that pass a test. Both take a function, here an anonymous function, explained in [Anonymous functions](lambdas.md):

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

The list itself does not change.

### Sorting

`sort(ref items)` sorts a list from the smallest element; `sortBy(ref items, less)` sorts it with a function that says whether its first value comes before the second. Both are stable: elements that are equal keep their order, as `kiwi` and `pear` here, which have the same length:

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

`sort` works on the lists whose elements implement the interface `Ordered` of the core, with one method:

```cromo ignore
interface Ordered {
    i32 compare(Self other) const
}
```

`a.compare(b)` is negative when `a` comes before `b`, zero when they are equal, positive when `a` comes after. The numbers and `string` implement it by themselves; strings are ordered byte by byte. A struct implements it when it declares `: Ordered` and writes `compare`:

```cromo
struct Version : Ordered {
    i32 major
    i32 minor

    Version(i32 a, i32 b) {
        major = a
        minor = b
    }

    i32 compare(Version other) const {
        if (major != other.major) {
            ret major.compare(other.major)
        }
        ret minor.compare(other.minor)
    }
}

i32 main() {
    list<Version> versions = [Version(1, 10), Version(0, 9), Version(1, 2)]
    sort(ref versions)
    list<string> names
    for (Version v in versions) {
        names.add(format("{}.{}", v.major, v.minor))
    }
    println("{}", join(names, " "))
    println("{} {}", i64(3).compare(5), "b".compare("a"))
    ret 0
}
```

```output
0.9 1.2 1.10
-1 1
```

The versions are compared as numbers, so `1.10` comes after `1.2`, which a sort of their texts would not do.

A struct without `: Ordered` is not sorted by `sort`, also if it has a method `compare`:

```cromo error
struct Point {
    i64 x
    i64 y
}

i32 main() {
    list<Point> points
    sort(ref points)
    ret 0
}
```

```output
error: Point does not implement Ordered, required by T of 'sort'
```

For such a list, or for another order, use `sortBy`, as `sortBy(ref points, [](Point a, Point b) bool { ret a.x < b.x })`.

- Between two floats, `compare` gives 0 when one of them is a NaN: a NaN is "equal" to every number, so a list with NaNs has no well-defined order. Remove them before sorting.
- If `less` throws an exception, `sortBy` lets it go to the caller, and the list keeps all its elements, perhaps in another order.
- The sort is a merge sort: it takes O(n log n) comparisons, also in the worst case.

## Fixed-size arrays

An array has its size in its type, and lives where its variable is, without memory on the heap:

```cromo
i32 main() {
    i32[4] scores
    scores[1] = 7
    f64[2][3] grid
    grid[1][2] = 0.5
    println("{:?} has {} elements", scores, scores.len())
    for (f64[3] row in grid) {
        println("{:?}", row)
    }
    ret 0
}
```

```output
[0, 7, 0, 0] has 4 elements
[0.0, 0.0, 0.0]
[0.0, 0.0, 0.5]
```

- Every element starts with the default value of its type. There is no literal of an array for a local variable: the elements are assigned one by one. The constants of a module can be arrays with a literal, as [Common concepts](common-concepts.md) shows.
- The size is an integer literal, at least 1. An array takes at most 2 GB.
- `f64[2][3]` is two arrays of three, as in C: `grid[i][j]` has `i < 2` and `j < 3`.
- `a.len()` is the size, and `for (T x in a)` and `for (ref T x in a)` visit the elements as for a list, with the same rules.
- An index outside the array is a panic, as for a list; when the index is a constant, it is an error of the compiler.
- Arrays are not compared with `==`, and they are printed only with `{:?}`.

An array is a value, as a struct: it can be a field, a parameter, a return value. It is copied when its elements are simple values, as numbers; an array of strings or of other values that own memory is moved, and it has no `copy()`:

```cromo error
i32 main() {
    i32[4] a
    a[4] = 1
    string[2] names
    string[2] other = names
    ret 0
}
```

```output
error: the index 4 is outside the array, which has 4 elements
error: values of type string[2] cannot be copied
```

A local array is on the stack of the thread, which is 1 MB on Windows and 8 MB on Linux: an array larger than that stops the program with `panic: stack overflow`. A large table goes in a `list`, on the heap.

## Maps

`map<K, V>` associates keys with values, as `std::unordered_map` in C++: it is a hash table.

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

- `m[k] = v`, `m[k] += v` and `m[k]++` add a missing key, with the default value, before changing it, as `operator[]` of C++.
- Any other use of a missing key is a panic: reading `m[k]`, but also `m[k].add(x)` on a map of lists, or `m[a][b] = v` on a map of maps when `a` is missing. Ask first with `contains`, or assign the key first: `groups["fruit"] = []`.
- `remove(k)` gives `true` if the key was there; `len()` is the number of keys; `copy()` copies the map, and `b = a` does the same.
- A key added to the map is a copy of the value given.

`for (K key, V value in m)` visits the map in no particular order: it is not the order in which the keys were added, it can change when keys are added or removed, and a program must not rely on it. With `ref V value` the loop can modify the values; the keys are always read-only, since changing a key would put it in the wrong place of the table. As for a list, the map cannot change while a `for` visits it:

```cromo
i32 main() {
    map<string, i32> stock
    stock["apples"] = 10
    stock["pears"] = 4
    map<string, i32> before = stock
    for (string fruit, ref i32 count in stock) {
        count *= 2
    }
    println("{} {} {}", stock["apples"], stock["pears"], before["apples"])
    ret 0
}
```

```output
20 8 10
```

```cromo error
i32 main() {
    map<string, i32> stock
    for (ref string fruit, i32 count in stock) {
    }
    for (string fruit, i32 count in stock) {
        stock.remove(fruit)
    }
    ret 0
}
```

```output
error: the keys of a map are read-only: only the value can be 'ref'
error: 'stock' cannot be modified while a 'for' goes over the elements of 'stock'
```

`{:?}` prints a map as `{"apples": 20, "pears": 8}`, in its order.

### Keys

The keys can be numbers, `bool`, `string`, and the structs that implement the interface `Key` of the core:

```cromo ignore
interface Key {
    u64 hash() const
    bool equals(Self other) const
}
```

Two keys that are equal must have the same hash. The numbers, `bool` and `string` have `hash()` and `equals()` by themselves, so a struct can build its hash from those of its fields:

```cromo
struct Cell : Key {
    i32 row
    i32 column

    u64 hash() const {
        ret row.hash() ^ column.hash() << 1
    }

    bool equals(Cell other) const {
        ret row == other.row && column == other.column
    }
}

i32 main() {
    map<Cell, string> board
    Cell c
    c.row = 1
    c.column = 2
    board[c] = "rook"

    Cell same
    same.row = 1
    same.column = 2
    println("{} {}", board.contains(same), board[same])
    ret 0
}
```

```output
true rook
```

A struct without `: Key` cannot be a key: `map<Point, i32>` gives `error: Point does not implement Key, required by K of map`.

With floats as keys, `-0.0` and `0.0` are the same key, and a NaN is never found, since it is not equal to itself: each `m[nan] = v` adds a new key.

`Key` names `Self`, the type that implements it, so it is only a constraint of a generic, never the type of a parameter or of a variable: `void f(Key k)` is an error. A generic that makes a map of its `T` asks for it with `T : Key`:

```cromo
i64 distinct<T : Key>(list<T> items) {
    map<T, bool> seen
    for (T item in items) {
        seen[item] = true
    }
    ret seen.len()
}

i32 main() {
    println("{} {}", distinct<i64>([1, 2, 2, 3]), distinct<string>(["a", "b", "a"]))
    ret 0
}
```

```output
3 2
```

[Interfaces and generics](interfaces-generics.md) explains constraints.

## Strings

A `string` is text in UTF-8, as `std::string` in C++. It owns its bytes, and is copied or moved as a list:

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

The functions of text are methods of `string`. The methods of this table do not change the string: each gives a new value. Positions and lengths are in bytes, as `i64`.

| Method | What it gives |
|---|---|
| `len()` | the length in bytes |
| `find(part)` | the position of the first `part`, or -1 |
| `findFrom(part, from)` | the position of the first `part` at `from` or after it, or -1 |
| `findLast(part)` | the position of the last `part`, or -1 |
| `contains(part)`, `startsWith(part)`, `endsWith(part)` | whether `part` is in the text, at its beginning, at its end |
| `split(separator)` | the parts between the separators, as a `list<string>` |
| `replace(old, new)` | a copy where every `old` becomes `new` |
| `trim()`, `trimStart()`, `trimEnd()` | a copy without the spaces at both ends, at the beginning, at the end |
| `upper()`, `lower()` | a copy with the letters A-Z and a-z changed |
| `repeat(n)` | the text `n` times |
| `slice(from, to)` | a copy of the bytes from `from` to `to`, excluded |
| `bytes()` | a copy of the bytes, as a `list<u8>` |
| `copy()` | a copy of the string |

`join(parts, separator)` is a function, not a method: it puts the strings of a `list<string>` one after the other, with the separator between them.

### Searching

```cromo
i32 main() {
    string line = "one two one two"
    println("{} {} {}", line.find("two"), line.findFrom("two", 5), line.findLast("one"))
    println("{} {}", line.find("six"), line.contains("six"))
    println("{} {} {}", line.find(""), line.findFrom("", 3), line.findLast(""))
    ret 0
}
```

```output
4 12 8
-1 false
0 3 15
```

- A text that is not there is -1, not an error: a search that finds nothing is a normal result.
- An empty `part` is found everywhere: `find("")` is 0, `findFrom("", from)` is `from`, `findLast("")` is `len()`; `contains("")`, `startsWith("")` and `endsWith("")` are `true`.
- `findFrom` takes `from` between 0 and `len()`; outside, it is a panic.

### Splitting, replacing and trimming

```cromo
i32 main() {
    println("{:?} {:?} {:?}", "abc".split(","), "".split(","), ",a,".split(","))
    println("{:?}", "a, b, c".split(", "))
    println("[{}] [{}]", "aaa".replace("aa", "b"), "ab".repeat(0))
    println("{:?}", " \t\n x y \r\n".trim())
    println("{:?} {:?}", "  x  ".trimStart(), "  x  ".trimEnd())
    println("{} {}", "Città".upper(), "ÀB".lower())
    ret 0
}
```

```output
["abc"] [""] ["", "a", ""]
["a", "b", "c"]
[ba] []
"x y"
"x  " "  x"
CITTà Àb
```

- `split` gives one part more than the separators: two separators side by side give an empty part, and a text without the separator gives one part with all of it, also when the text is empty. The separator can be longer than one byte.
- `replace` goes from the left, without overlaps: `"aaa".replace("aa", "b")` is `"ba"`.
- `repeat(0)` is the empty string.
- `trim` removes the spaces of ASCII: the space, the tab, the line feed, the vertical tab, the form feed and the carriage return. The spaces of Unicode, as the no-break space, stay.
- `upper` and `lower` change only the letters of ASCII: `à` stays `à`.
- An empty separator of `split`, an empty `old` of `replace` and a negative count of `repeat` are a panic: they have no sensible result.

### Building strings

`+` joins two strings into a new one, and `+=` or `append` adds at the end of a string. `reserve(n)` makes room for `n` bytes, so that a string built a piece at a time is not moved in memory at each piece. A number becomes text with `format`, described in [Formatting text](formatting.md), and a text becomes a number with a conversion, as `i64("42")`, which throws an exception when the text is not a number:

```cromo
i32 main() {
    string path = "/home"
    path += "/anna"
    path.append("/notes")
    string line
    line.reserve(64)
    for (i64 i = 1; i <= 3; i++) {
        line += format("{}:{} ", i, i * i)
    }
    i64 n = i64("42") + 1
    println("{} [{}] {}", path, line.trimEnd(), n)
    println("{} {} {}", "abc" < "abd", "ab" < "abc", "Z" < "a")
    ret 0
}
```

```output
/home/anna/notes [1:1 2:4 3:9] 43
true true true
```

Strings are compared with `==`, `!=`, `<`, `<=`, `>`, `>=`, byte by byte: in UTF-8 that is the order of the code points, where every capital letter of ASCII comes before every small one. A string is not joined to a number with `+`: write `format("{}{}", text, n)`.

### Strings and bytes

The length of a string is in bytes, and a letter outside ASCII takes more than one. `s[i]` is the byte at position `i`, a `u8`:

```cromo
i32 main() {
    string word = "città"
    println("{} bytes; the first is {}, the letter {:c}", word.len(), word[0], word[0])
    println("'{}'", word.slice(0, 3))
    list<u8> bytes = word.bytes()
    string again = string(bytes)
    println("{} {} {}", bytes.len(), again, again == word)
    ret 0
}
```

```output
6 bytes; the first is 99, the letter c
'cit'
6 città true
```

- `s[i]` is read-only: a string is changed by building a new one. An index outside the string is a panic.
- `for` does not go over a string: a loop over its bytes uses an index, `for (i64 i = 0; i < s.len(); i++)`.
- `slice(from, to)` takes the bytes from `from` to `to`, excluded. Positions outside the string, `from` after `to`, or a slice that cuts a character of UTF-8 in two are a panic: `"città".slice(0, 5)` stops the program, since `à` takes the bytes 4 and 5.
- `bytes()` gives a copy of the bytes. `string(bytes)` makes a string of them again, after checking that they are valid UTF-8; otherwise it throws an `Error` whose message says where the bytes stop being UTF-8, as `the bytes are not valid UTF-8: the character at 4`.

```cromo error
i32 main() {
    string s = "abc"
    s[0] = 'x'
    ret 0
}
```

```output
error: the bytes of a string are read-only: build a new string
```

## Vectors

A vector of SIMD holds a fixed number of numbers, on which the processor works together: `f32x8` holds eight `f32`, and `i16x16` sixteen `i16`. Its operators act on all the numbers at once, often in one instruction of AVX2, SSE or NEON:

```cromo
i32 main() {
    list<f32> prices = [1.5, 2.0, 3.5, 4.0, 0.5, 6.0, 7.5, 8.0]
    f32x8 all = f32x8.load(prices, 0)  // eight numbers in one value
    f32x8 taxed = all * 1.2            // eight multiplications in one instruction
    i32x8 expensive = taxed > 5.0      // a mask: -1 where true, 0 where false
    f32x8 capped = expensive.select(f32x8(5.0), taxed)
    println("{:.2f} {:.2f} {}", taxed.sum(), capped.sum(), expensive.any())
    ret 0
}
```

```output
39.60 28.80 true
```

The name of a type is the type of its numbers, `x` and how many they are, for 128 or 256 bits:

| Numbers | 128 bits | 256 bits |
|---|---|---|
| `i8`, `u8` | `i8x16`, `u8x16` | `i8x32`, `u8x32` |
| `i16`, `u16` | `i16x8`, `u16x8` | `i16x16`, `u16x16` |
| `i32`, `u32` | `i32x4`, `u32x4` | `i32x8`, `u32x8` |
| `i64`, `u64` | `i64x2`, `u64x2` | `i64x4`, `u64x4` |
| `f32` | `f32x4` | `f32x8` |
| `f64` | `f64x2` | `f64x4` |

A vector is a simple value, as a number: it is copied, and it can be in a list, an array, a struct or a map. Its default value has 0 in each number.

```cromo
i32 main() {
    i32[4] values
    values[0] = 5
    values[1] = -6
    values[2] = 7
    values[3] = -8
    i32x4 a = i32x4.load(values, 0)
    i32x4 ones = i32x4(1)
    a[1] = 100
    println("{:?} {} {}", a * 2 + ones, a[1], a.len())
    println("{:?} {:?}", a.abs(), a.max(ones))
    println("{} {} {}", a.sum(), a.smallest(), a.largest())

    i32x4 positive = a > 0
    println("{:?} {} {}", positive, positive.all(), positive.any())
    println("{:?}", positive.select(a, ones))

    f32x4 big = f32x4(1e20)
    println("{:?} {:?}", f32x4(a), i32x4(big))

    list<i32> out = [0, 0, 0, 0, 0, 0]
    a.store(ref out, 2)
    println("{:?}", out)
    ret 0
}
```

```output
[11, 201, 15, -15] 100 4
[5, 100, 7, 8] [5, 100, 7, 1]
104 -8 100
[-1, -1, -1, 0] false true
[5, 100, 7, 1]
[5.0, 100.0, 7.0, -8.0] [2147483647, 2147483647, 2147483647, 2147483647]
[0, 0, 5, 100, 7, -8]
```

- `T(x)` puts the number `x` in each number of the vector. `T(v)` converts the numbers of a vector with as many numbers, one by one: from a float to an integer the result is saturated, without a panic, so `1e20` becomes the largest `i32`.
- `T.load(values, at)` reads the numbers from position `at` of a `list` or an array of the same numbers, and `v.store(ref values, at)` writes them there. `v[i]` reads and writes one number, and `len()` is how many there are. An index outside, also in `load` and `store`, is a panic.
- `+`, `-` and `*` act number by number, `/` too for the floats, and `-v` negates the signed and the floats; `&`, `|`, `^` and `!` act on the bits of the integers, and `<<` and `>>` shift them by a count that is checked as for a single integer. A number on one side goes into each number, as `a * 2`. The integers wrap around, without the checks of overflow.
- `/` of integer vectors is an error of the compiler, since the processors have no division of integer vectors; `%` does not work on any vector.
- A comparison gives a mask, a vector of signed integers as wide as the numbers, with -1 where it is true and 0 where it is false: `f32x8 > f32x8` gives an `i32x8`. A mask has `all()`, `any()` and `select(a, b)`, which takes the numbers of `a` where it is true and those of `b` where it is false.
- `sum()`, `smallest()` and `largest()` give one number; the sum of floats is done in the order that SIMD prefers, which can differ in the last digits from a loop. `abs()` works on the signed and on the floats, `min(w)` and `max(w)` number by number, and `low()` and `high()` give the two halves of a vector of 256 bits.
- A vector is printed with `{:?}`, as an array; `{}` does not print it.

```cromo error
i32 main() {
    i32x4 a = i32x4(6)
    i32x4 half = a / i32x4(2)
    println("{}", a)
    ret 0
}
```

```output
error: '/' works on vectors of floats only: the processors have no division of integer vectors
error: a i32x4 cannot be printed with {}: print its form of debug with {:?}
```

By default a program uses the instructions that every x86-64 processor has, SSE2, and a vector of 256 bits is two of 128; `cromo build --cpu x86-64-v3` compiles the whole program for the processors with AVX2. On ARM64 the vectors use NEON.

## Formatting

`println`, `print` and `format` write numbers, strings and the other values with a format string, as `std::format` of C++ and Python do: `{:>8.2f}` writes a float with two decimals, aligned to the right in eight characters. [Formatting text](formatting.md) is the complete reference.

## Summary

- `list<T>` grows as needed; `[a, b]` makes one, with the type that the place expects; `v[i]` is the element itself, checked against the length.
- `for (T x in v)` reads the elements, `for (ref T x in v)` modifies them; while it runs, the list does not change in any other way.
- `sort` sorts the lists of `Ordered` values, with `compare`; `sortBy` takes a function; both are stable.
- `T[n]` is an array of fixed size, without the heap, copied when its elements are simple.
- `map<K, V>` is a hash table; `m[k] += v` adds a missing key, and any other read of a missing key is a panic; the order is not defined. A struct is a key with `Key`, `hash` and `equals`.
- `string` is UTF-8, measured in bytes; its methods search, split, replace and trim, and always give a new string. `s[i]` is a read-only byte.
- `string(bytes)` checks the UTF-8; `slice` cannot cut a character.
- `f32x8`, `i16x16` and the other vectors work on many numbers at once, with SIMD.
