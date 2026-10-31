# A first program: counting words

Before the details, a complete program: it reads a text file and prints the words it uses most often. It shows functions, loops, strings, lists, maps and structs at work; the next chapters explain each of them.

## The program

Here is the text we will count:

```text file=poem.txt
The fog comes on little cat feet.
It sits looking over harbor and city
on silent haunches and then moves on.
The fog, the cat, the city: on and on.
```

And here is the heart of the program, `words.cro`: its `main`. The functions that it calls stand in place of `// ...`, and the rest of the chapter shows them one at a time; on the site, *Show all* opens the whole file.

```cromo file=words.cro args=poem.txt
import std.fs

// ...
#
# // A word is made of letters; the bytes of UTF-8 above 127 count as letters.
# bool isLetter(u8 c) {
#     ret (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128
# }
#
# // The words of a text, in lower case.
# list<string> words(string text) {
#     list<string> found
#     i64 start = -1
#     for (i64 i = 0; i <= text.len(); i++) {
#         bool letter = i < text.len() && isLetter(text[i])
#         if (letter && start < 0) {
#             start = i
#         } else if (!letter && start >= 0) {
#             found.add(text.slice(start, i).lower())
#             start = -1
#         }
#     }
#     ret found
# }
#
# struct Count {
#     string word
#     i64 times
# }
#
# // The most frequent first; with the same count, in alphabetical order.
# bool before(Count a, Count b) {
#     if (a.times != b.times) {
#         ret a.times > b.times
#     }
#     ret a.word < b.word
# }
#
# void sort(ref list<Count> counts) {
#     for (i64 i = 1; i < counts.len(); i++) {
#         for (i64 j = i; j > 0 && before(counts[j], counts[j - 1]); j--) {
#             Count moving = move counts[j]
#             counts[j] = move counts[j - 1]
#             counts[j - 1] = move moving
#         }
#     }
# }

i32 main(list<string> args) {
    if (args.len() < 2) {
        eprintln("usage: words <file>")
        ret 1
    }
    string text = fs.readText(args[1])

    map<string, i64> seen
    for (string word in words(text)) {
        seen[word] += 1
    }

    list<Count> counts
    for (string word, i64 times in seen) {
        Count c
        c.word = word.copy()
        c.times = times
        counts.add(move c)
    }
    sort(ref counts)

    for (i64 i = 0; i < counts.len() && i < 5; i++) {
        println("{:>3}  {}", counts[i].times, counts[i].word)
    }
    ret 0
}
```

Run it with the name of the file after `--`:

```sh
cromo run words.cro -- poem.txt
```

```output
  5  on
  4  the
  3  and
  2  cat
  2  city
```

## How it works

### Importing a module

```cromo ignore
import std.fs
```

The standard library is made of a core, which every program sees, and of modules, which a program imports. `std.fs` works with files: `fs.readText(path)` returns the whole content of a file as a `string`. Lists, maps and strings are in the core and need no import.

### Functions and types

```cromo ignore
bool isLetter(u8 c) {
    ret (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128
}
```

A function is written as in C: the type of the result, the name, and the parameters, each with its type before its name. `u8` is an unsigned byte; `'a'` is a character, which in Cromo is a byte. A `string` is text in UTF-8, so a letter such as `è` is made of two bytes above 127, which the program counts as letters.

### Loops

```cromo ignore
for (i64 i = 0; i <= text.len(); i++) {
```

Cromo has one loop, `for`, with the forms of C and some more: here the classic one, with an `i64` that goes from 0 to the length of the text. `text.len()` is the length in bytes and `text[i]` is the byte at position `i`. Lengths and positions are always `i64`. An index out of the text stops the program with an error, instead of reading memory that is not there.

### Lists

```cromo ignore
list<string> found
found.add(text.slice(start, i).lower())
ret found
```

`list<string>` is a list of strings that grows as needed. Declared without a value, it is empty: in Cromo every variable has a value, and the default of a list is the empty list. `ret found` gives the list to the caller without copying it.

### Structs

```cromo ignore
struct Count {
    string word
    i64 times
}
```

A struct groups values, as in C. `Count c` declares one with its default value, an empty `word` and `times` equal to 0; the fields are then assigned one by one.

### Ownership: `ref`, `move` and `copy()`

Three words of the program deserve attention, because they are the heart of Cromo:

- `sort(ref counts)`: the function **modifies** the list, and the call says so with `ref`, as the declaration `void sort(ref list<Count> counts)` does.
- `counts.add(move c)`: the struct **moves** into the list. After the `move`, `c` can no longer be used: its content now belongs to the list.
- `c.word = word.copy()`: a string is never copied without saying so. `word` belongs to the map, so the program asks for a copy.

Without `ref`, a parameter is read-only, as `text` in `words(string text)`: the function can read the string, but not change it, and the string is not copied. Chapter 4 explains these rules, which are the core of the language.

### Maps

```cromo ignore
map<string, i64> seen
seen[word] += 1
for (string word, i64 times in seen) {
```

A `map` associates keys with values, as a hash table. `seen[word]` reads or writes the value of a key; `seen[word] += 1` adds a missing key with the value 0 first, as `operator[]` does in C++. A `for` with two variables visits every key with its value, in no particular order: that is why the program sorts the counts.

### Printing

```cromo ignore
println("{:>3}  {}", counts[i].times, counts[i].word)
```

`{:>3}` prints a value aligned to the right in three characters. The formats are those of `std::format` in C++ and of Python: chapter 7 lists them.

### Errors

If the file does not exist, `fs.readText` throws an exception. Nobody catches it, so the program stops and prints what happened, with the calls that led there (shortened here):

```sh
cromo run words.cro -- missing.txt
```

```text
uncaught exception: cannot read 'missing.txt': no such file or folder
    at std.fs.readText (cromo:std/fs.cro:34)
    at main (words.cro:52)
```

Chapter 8 shows how to catch exceptions with `try` and `catch`.

## Summary

In a few lines the program used most of what you will meet in this guide: functions, the `for` loop, strings, lists, maps, a struct, and the three ways of passing a value, read, `ref` and `move`. Now let us look at the language from the beginning.
