# Regular expressions

A regular expression is a pattern for a text: "four digits, a dash, two digits". The module [std.regex](std.regex.html) says whether a text has a pattern, finds where, takes its parts, replaces and splits.

## A first expression

```cromo
import std.regex

i32 main() {
    regex.Regex date = regex.Regex("(\\d{4})-(\\d{2})-(\\d{2})")
    println("{}", date.matches("released on 2026-11-12"))
    println("{}", date.matchesWhole("released on 2026-11-12"))

    regex.Match found
    if (date.find("released on 2026-11-12", ref found)) {
        println("{} at {}", found.text(), found.start())
        println("year {}, month {}, day {}", found.group(1), found.group(2), found.group(3))
    }
    ret 0
}
```

```output
true
false
2026-11-12 at 12
year 2026, month 11, day 12
```

`regex.Regex(pattern)` reads the expression, which takes some work: a program makes a `Regex` once and keeps it. It can be copied and given to the tasks.

- `matches(text)` is true when the text has the expression somewhere in it; `matchesWhole(text)` when the whole text is the expression.
- `find(text, ref found)` finds the first match, the leftmost one: it gives true when there is one, and puts it in the `regex.Match`. `findFrom(text, from, ref found)` starts from a position.
- A `Match` has its `text()`, and where it is in the text: `start()` and `end()`, in bytes, as the positions of a string.
- `group(1)` is the text of the first group, the part of the expression between the first `(` and its `)`; `group(0)` is the whole match. `has(index)` says whether the match went through a group: in `a|(b)` on `"a"` it did not, and `group(1)` is empty. `groupStart(index)` and `groupEnd(index)` give where a group is, or -1.

## The syntax

The syntax is the one of RE2, which Go and Rust use too. A character means itself, but these: `. * + ? ( ) [ ] { } | ^ $ \`.

Characters:

- `.` is any character but the end of line.
- `[abc]` is one of those characters, `[a-z0-9_]` one of those ranges, `[^a-z]` one that is not among them.
- `\d` is a digit, `\w` a letter, a digit or `_`, `\s` a space, a tab or an end of line; `\D`, `\W` and `\S` are the characters that are not of them. They can be inside a class: `[\d.]`.
- `[[:alpha:]]` and the other classes of POSIX, inside a class: `alnum`, `digit`, `upper`, `lower`, `space`, `punct`, `xdigit`.
- `\.` `\*` `\\` and the others are the character itself; `\n` `\t` `\r` an end of line, a tab, a return; `\x41` and `\x{1F600}` the character with that number; `\Q...\E` a text as it is.

Repetitions, after a character, a class or a group:

- `x*` is any number of `x`, also none; `x+` one or more; `x?` one or none.
- `x{3}` is three, `x{2,}` two or more, `x{2,5}` from two to five.
- They take as many as they can; with a `?` after them, `x*?`, `x+?`, `x??`, `x{2,5}?`, as few as they can.

Putting together:

- `ab` is `a` and then `b`; `a|b` is `a` or `b`, the first that matches.
- `(x)` is a group, counted from 1 by its `(`; `(?:x)` is not counted; `(?P<name>x)`, or `(?<name>x)`, has a name too.

Places, which take no character:

- `^` and `$` are the start and the end of the text; `\A` and `\z` too, always.
- `\b` is the edge of a word, between a character of `\w` and one that is not; `\B` is where there is no edge.

Flags, for the rest of the group they are in, or with `(?i:x)` for `x` only:

- `(?i)` capitals or not, `(?m)` makes `^` and `$` the start and the end of each line, `(?s)` makes `.` take the end of line too, `(?U)` makes the repetitions take as few as they can, and as many with a `?`.

A string of Cromo has the `\` for its own escapes, so an expression writes each `\` twice: `"\\d+"` is the expression `\d+`.

## Groups with a name, and all the matches

```cromo
import std.regex

i32 main() {
    regex.Regex pair = regex.Regex("(?P<key>\\w+)=(?P<value>[^;]*)")
    for (regex.Match found in pair.findAll("name=Ada;role=admin;note=")) {
        println("{} is {:?}", found.named("key"), found.named("value"))
    }
    ret 0
}
```

```output
name is "Ada"
role is "admin"
note is ""
```

`named(name)` gives the text of a group by its name, and `findAll(text)` all the matches of the text, in order, each one after the end of the one before.

## Replacing and splitting

```cromo
import std.regex

i32 main() {
    regex.Regex date = regex.Regex("(\\d{4})-(\\d{2})-(\\d{2})")
    println("{}", date.replace("from 2026-01-31 to 2026-02-28", "$3/$2/$1"))
    println("{}", date.replaceFirst("from 2026-01-31 to 2026-02-28", "${1}"))
    println("{:?}", regex.Regex("\\s+").replace("  too   many spaces ", " "))

    list<string> names = regex.Regex("\\s*[,;]\\s*").split("ada , grace;linus")
    println("{} {}", names.len(), names[1])
    println("{}", regex.Regex(regex.escape("1+1")).replace("1+1=2", "two"))
    ret 0
}
```

```output
from 31/01/2026 to 28/02/2026
from 2026 to 2026-02-28
" too many spaces "
3 grace
two=2
```

- `replace(text, replacement)` replaces every match, and `replaceFirst` the first. In the replacement `$1` is the group 1 of the match, `${1}` the same when a digit follows, `${name}` a group by its name, `$0` the whole match and `$$` a `$`.
- `split(text)` gives the pieces of the text between the matches.
- `regex.escape(text)` puts a `\` before the characters that mean something: an expression that finds a text as it is, for the texts that come from outside.

## As many, as few, and the flags

```cromo
import std.regex

i32 main() {
    string page = "<b>one</b> and <b>two</b>"
    regex.Match found
    regex.Regex("<b>.*</b>").find(page, ref found)
    println("{}", found.text())
    regex.Regex("<b>.*?</b>").find(page, ref found)
    println("{}", found.text())

    println("{}", regex.Regex("(?i)hello").matches("HELLO"))
    println("{}", regex.Regex("(?m)^\\w+$").findAll("one\ntwo\n").len())
    println("{}", regex.Regex("^.$").matches("è"))
    ret 0
}
```

```output
<b>one</b> and <b>two</b>
<b>one</b>
true
2
true
```

`.*` takes as many characters as it can, up to the last `</b>`; `.*?` as few as it can. The characters are the ones of Unicode: `.` takes `è`, two bytes, whole. `\d`, `\w`, `\s`, `\b` and `(?i)` know the letters of ASCII only.

## What is not there, and why

```cromo
import std.regex

i32 main() {
    try {
        println("{}", regex.Regex("(a|b").groups())
    } catch (Exception e) {
        println("{}", e.message())
    }
    regex.Regex heavy = regex.Regex("(a+)+b")
    println("{}", heavy.matches("a".repeat(5000)))
    ret 0
}
```

```output
cannot read the regular expression '(a|b': a ( without its ), at 4
false
```

An expression that is not valid throws an `Error` that says what is wrong and where.

The time of a search grows with the length of the text, and never more. The engines that try one way after the other, as those of Perl, of JavaScript and `std::regex` of C++, can take years on `(a+)+b` with a text of some dozens of `a`, and a service that takes an expression or a text from outside can be stopped by one request. `std.regex` goes through all the ways together, one character of the text at a time, as RE2, Go and Rust do.

The price is what cannot be searched in that way, and is not there: the references to a group, as `\1`, and the lookahead and the lookbehind, `(?=x)`, `(?!x)`, `(?<=x)`. They are errors that say so. The classes of Unicode, as `\p{L}`, are not there yet.

## Summary

- `regex.Regex(pattern)` reads an expression with the syntax of RE2; it is made once and kept.
- `matches` and `matchesWhole` say yes or no; `find`, `findFrom` and `findAll` give the matches, each a `Match` with its text, its position and its groups, by number or by name.
- `replace`, `replaceFirst` and `split` change and cut a text; `regex.escape` makes an expression of a text as it is.
- A search takes a time that grows with the text, whatever the expression; there are no references to a group and no lookahead.
