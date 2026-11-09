# Tools: formatting, the editor and the debugger

`cromo` is the only tool of Cromo: besides the compiler, it contains the formatter of the code, `cromo fmt`, and the language server that editors use, `cromo lsp`. This chapter presents them, the extension for Visual Studio Code that is built on them, the comments that document the code, and how to run a program in a debugger.

## Formatting: cromo fmt

All the code in Cromo has one style, and `cromo fmt` writes it. It has no options and no configuration file, as `gofmt` of Go: nobody needs to discuss the style, and code written by others looks as your own.

### The command

```sh
cromo fmt                    # every .cro file of this folder and of its subfolders
cromo fmt main.cro shapes    # these files, and the .cro files of these folders
cromo fmt --check            # changes nothing: lists the files to format
cromo fmt -                  # formats the standard input on the standard output
```

- Without names, `cromo fmt` formats every `.cro` file of the current folder and of its subfolders, except the folders whose name starts with a dot, as `.git`.
- The files are rewritten in place. `cromo fmt` writes the name of each file that it changes, and nothing for the files that are already formatted.
- `--check` changes nothing: it writes the names of the files that are not formatted, and ends with 1 if there are some, 0 otherwise. It is the check to run before a commit, or in a continuous integration.
- `-` reads a text from the standard input and writes it formatted on the standard output: this is what an editor uses. It takes no other names, and not `--check`.
- A file with syntax errors is not changed: `cromo fmt` writes the errors, as the compiler does, goes on with the other files, and ends with 1.
- A name that is neither a file nor a folder, or an unknown option, ends `cromo fmt` with 2 before it changes anything.

### What it does

A file written in a hurry:

```cromo ignore
struct Point
{
  f64 x // across
  f64 y   // down
  i32 visits=0     // how many times
}


i32 main( )
{
  list<i32>v=[1,2,3]
  i32 total=0
  for(i32 x in v){total+=x}
  if(total>5)
  {
     println( "big {}" ,total )
  }
  else
  {
     println("small")
  }

  i32 n = - total
  n ++
  Point p
  println("{} {}", larger(total,
  n), p.visits)

  ret 0
}
i32 larger(i32 a, i32 b) { ret a > b ? a : b }
```

after `cromo fmt`:

```cromo
struct Point {
    f64 x           // across
    f64 y           // down
    i32 visits = 0  // how many times
}

i32 main() {
    list<i32> v = [1, 2, 3]
    i32 total = 0
    for (i32 x in v) { total += x }
    if (total > 5) {
        println("big {}", total)
    } else {
        println("small")
    }

    i32 n = -total
    n++
    Point p
    println("{} {}", larger(total,
        n), p.visits)

    ret 0
}
i32 larger(i32 a, i32 b) { ret a > b ? a : b }
```

```output
big 6
6 0
```

The rules, all of them:

**Braces.** `{` goes at the end of the line that opens the block, and `else`, `catch` and `finally` on the line of the `}` before them: `} else {`. The compiler also accepts them on a line of their own, so code written in the style of Allman compiles, and `cromo fmt` moves them up. If the line before `{` ends with a comment, `{` goes before the comment; if both lines have a comment at the end, or a line with only a comment is between them, they stay where they are.

**Indentation.** Each open block indents by 4 spaces; tabs become spaces. A line that continues a statement gets one more level than the statement, and each further line that leaves a parenthesis or a square bracket open adds one level to the lines after it, until it closes. A line that starts by closing them goes back to their level:

```cromo
i32 sum(i32 a, i32 b) {
    ret a + b
}

i32 main() {
    list<list<i32>> grid = [
        [1, 2],
        [3, 4],
    ]
    println("{} {}",
        grid.len(),
        sum(1,
            2))
    ret 0
}
```

```output
2 3
```

**Spaces.** One space around the binary operators, the assignments, `?` and `:` of the conditional operator and the `:` of the interfaces, `struct Square : Shape`; after commas, after the `;` of a `for`, and after the keywords, `if (`, `for (`. No space inside parentheses and square brackets, before the `(` of a call, around `.`, inside the angle brackets of a generic, `list<string>`, `map<string, i32>`, after the unary `-` and `!`, and before `++` and `--`. One space before `{`, and inside a block that stays on one line: `{ total += x }`.

**Lines.** At most one blank line in a row, and none at the start or at the end of a block or of the file. No spaces at the end of the lines, and the file ends with one newline. The file keeps its line endings, LF or CRLF, and its UTF-8 BOM if it has one.

**Comments.** The text of a comment never changes, `/* */` included. A comment alone on its line is indented as the code, unless it goes on, in the same column, a comment at the end of the line above: then it stays in that column. The comments at the end of consecutive lines are aligned in a column, two spaces after the longest line of code of the group, as the fields of `Point` above; a blank line, or a line without such a comment, ends the group.

### What it never changes

- **The line breaks of the author**, apart from those before `{`, `else`, `catch` and `finally`. A long line is not broken, and short lines are not joined: `cromo fmt` fixes the spaces and the indentation of the lines, also of those that continue a statement, and leaves to the author where to break them. A change to one line never moves the lines around it.
- **Where a statement ends**, so the meaning of the code.
- **The order** of the declarations, of the imports and of the fields, and the blank lines between declarations: it removes the extra ones, and does not add them where there are none, as between `}` and `i32 larger` above.
- **The blocks on one line**, as `{ ret a > b ? a : b }`.

### A formatting that cannot change the program

`cromo fmt` only changes spaces and line breaks, and checks it before it writes a file: the tokens of the result must be those of the source, apart from the line breaks before `{`, `else`, `catch` and `finally` that it removes; the result must have no syntax errors; and formatting it again must change nothing. If one of these checks fails, the file is not changed, and `cromo fmt` writes an internal error of the formatter, which is a bug of `cromo` to report. So `cromo fmt` can run on every save without risk.

## Visual Studio Code

The extension for Visual Studio Code colors the code, and takes everything else from `cromo lsp`, the language server: the analysis is the one of the compiler, so the editor and `cromo build` never disagree.

### Installation and settings

The script that installs Cromo also installs the extension, when it finds the command `code` of Visual Studio Code, and so does `cromo update` for each new version. The extension is also in the archive of each version, as a `.vsix` file in its folder `extension`, and can be installed by hand with `code --install-extension` and the path of that file, or with *Install from VSIX* in the panel of the extensions.

The extension has two settings:

| Setting | Meaning |
|---|---|
| `cromo.path` | the `cromo` executable that runs the language server. When it is empty, the default: `build\cromo.exe` in a folder of the workspace, if there is one, else `cromo` from the `PATH` |
| `cromo.trace` | writes every message between Visual Studio Code and `cromo lsp` in the *Cromo* panel of the output, to find a problem |

If `cromo` cannot start, a message says so, with a button that opens the setting. The files of Cromo are formatted when they are saved, by default: `editor.formatOnSave` can be turned off for `[cromo]` in the settings.

### What it offers

- **Colors** for keywords, types, numbers, strings with their placeholders, `{}` and `{:.2f}`, comments and attributes; `Ctrl+/` comments out lines, brackets close by themselves and the line after `{` is indented.
- **Errors while you write.** About 300 milliseconds after you stop typing, the server analyzes the text in the editor, also when it is not saved, and underlines the errors and the warnings, as `unreachable code`. It only checks the program, without generating code, so it is fast. Every file of the program gets its errors, also the files that are not open.
- **Hover.** The mouse over a name shows what it is: a variable its type and name; a function or a method its signature, with `ref` and `move`; a field its type and its struct; a type whether it is a struct, an enum or an interface; a case its enum; a module, in its `import` or in front of a name, its documentation. The name of the module where it is declared comes above, when it is another one, and the documentation of the declaration below. The built-in functions, as `println` and `assert`, and the types of numbers have their documentation too. Inside a generic function a value of type `T` shows `T`; at a call, the function shows the types of that call, as `i64 first<i64>(list<i64> items)`.
- **Go to definition**, with F12 or Ctrl+click: for the names of the program, and of the modules of the library, as `fs.readText`, which open their source in `lib/std`. The names of the core of the library, as the methods of `list` and `string`, and the built-in functions have a hover but no definition to go to yet.
- **Outline**: the structs, enums, interfaces and functions of the file, with their fields and methods, in the outline and with Ctrl+Shift+O. They come from the file alone, so they are there also when the program has errors.
- **Completion after a dot.** After the name of a variable, a parameter or a field, also at the end of a chain as `a.b.`, the fields and the methods of its type that the module can use, with their type or signature and their documentation, as `len`, `add` and `insert` after a `list`. A method is inserted by its name, without parentheses. After the name of an enum, as `Color.`, its cases; after the name of an imported module, as `fs.`, its public functions and types. The list opens when `.` is written, and Ctrl+Space opens it again right after a dot or inside the name after it.
- **Signature help.** After `(` and `,` in a call, a window shows the signature of the function, with the parameter being written in evidence, and its documentation: for a function, a constructor, a method as `c.area(` and a case with data as `Shape.Circle(`.
- **Formatting**, with the rules of `cromo fmt`, with Shift+Alt+F and when the file is saved.

While a file has syntax errors, hover and definition use the last analysis without them. The completion of the members of a variable needs the rest of the file without syntax errors: while another line is incomplete, after `p.` it proposes nothing.

### Which program it analyzes

An error depends on the whole program: a call to a function of a module is checked against its declaration. So the server analyzes each open file as a part of a program:

1. the file itself, if it has `main`;
2. otherwise, the nearest `main.cro` in its folder or in the folders above it, as `cromo build` without a file would; the search stops at the folder of the workspace;
3. otherwise, the first executable of the `bin` lines of the `cromo.mod` of its project, as in a project whose programs are in `cmd/`;
4. otherwise, the file alone, as a module that is not asked to have `main`.

The imports are resolved as the compiler does, from the folder of `cromo.mod` when there is one. A module that the program chosen does not import is analyzed alone: then the errors of the editor are those of the module by itself, and `cromo build` is the reference. The server reads the files of the system on which it runs, as `fs_windows.cro` on Windows: the errors of a file of another system, as `fs_linux.cro` on Windows, appear only when the program is compiled for it.

### Not available yet

References, renaming, the completion of the names that are not after a dot, the completion after a call or an index, as `f().` or `v[i].`, and the signature help after them.

### Other editors

Any editor that speaks the Language Server Protocol, version 3.17, can use `cromo lsp`: it starts the command and talks to it on the standard input and output. It offers diagnostics, hover, definition, the symbols of a document, completion, signature help and formatting. An editor without a language server can format with `cromo fmt -`.

## Documentation comments

The `//` comments right above a declaration, with no blank line between them, are its **documentation**, as in Go: the editor shows them in the hover, in the completion and in the signature help, and the pages of the library on the site of Cromo are made from them. There is nothing new to learn, and the library is documented in this way.

The rules:

- They document a function, a method, a constructor, a struct, an enum, an interface, a field or a case.
- An attribute can stand between the comment and the declaration, as `[[test]]`.
- The comment at the end of the line of a field or of a case documents it too, with the lines that go on in its column below. With both, the comment above comes first.
- Only `//` comments are documentation: a `/* */` comment never is.
- Every `//` comment right above a declaration is documentation, also a note as `// TODO: make it faster`: leave a blank line between a note and the declaration, to keep the note out.
- The comments at the beginning of a file, before its imports and its declarations, document the **module**, if a blank line or an `import` follows them; if a declaration follows them right away, they document that declaration. The editor shows the documentation of a module on its name, in the `import` and in front of its names.

The text is Markdown, without the backquote:

- code inside a sentence goes between single quotes, `'separator'`. A quote opens where no letter or digit precedes it, and closes where none follows it, so `it's` and `don't` stay apostrophes, and a character literal, `'/'`, shows as code;
- a block of code goes between two lines `'''`, and the editor colors it as Cromo;
- an empty comment line separates two paragraphs;
- `-` starts the items of a list, `*text*` is emphasis and `[text](url)` a link;
- the parameters are named in the text, as `'t'` below: there is no `@param`.

A module with its documentation:

```cromo file=temperature.cro
// Temperatures in Celsius and Fahrenheit.
//
// Each value keeps its scale, and 'convert' changes it.

// A scale of temperature.
public enum Scale {
    Celsius     // water freezes at 0
    Fahrenheit  // water freezes at 32
}

// A temperature on its 'scale'.
public struct Temperature {
    public f64 degrees
    public Scale scale  // Celsius, unless set
}

// The temperature 't' on the scale 'to'. It doesn't change 't': it
// returns a *new* value.
//
// - from Celsius to Fahrenheit, times 1.8 plus 32;
// - from Fahrenheit to Celsius, the other way.
//
// '''
// Temperature boiling
// boiling.degrees = 100.0
// f64 f = convert(boiling, Scale.Fahrenheit).degrees   // 212
// '''
public Temperature convert(Temperature t, Scale to) {
    Temperature out
    out.scale = to
    if (t.scale == to) {
        out.degrees = t.degrees
    } else if (to == Scale.Fahrenheit) {
        out.degrees = t.degrees * 1.8 + 32.0
    } else {
        out.degrees = (t.degrees - 32.0) / 1.8
    }
    ret out
}
```

```cromo file=weather.cro
import temperature

i32 main() {
    Temperature boiling
    boiling.degrees = 100.0
    println("{}", convert(boiling, Scale.Fahrenheit).degrees)
    ret 0
}
```

```output
212
```

In `weather.cro` the mouse over `convert` shows, under its signature and the name of its module, `temperature`:

```text
The temperature t on the scale to. It doesn't change t: it returns a new value.

  • from Celsius to Fahrenheit, times 1.8 plus 32;
  • from Fahrenheit to Celsius, the other way.

  Temperature boiling
  boiling.degrees = 100.0
  f64 f = convert(boiling, Scale.Fahrenheit).degrees   // 212
```

with `t`, `to` and the block as code, and *new* in italics. Over `Fahrenheit` it shows `water freezes at 32`, and over `import temperature` the first comment of the file.

## Debugging

A program built without `--release` carries the information that a debugger needs: the functions, the lines, the variables and their types. On Windows it is a PDB file next to the executable, `hello.pdb` next to `hello.exe`, which Visual Studio, WinDbg and the C/C++ extension of Visual Studio Code read; on Linux it is DWARF, inside the executable, for gdb and lldb. The debug information names the source files with their full path, so the debugger finds them from any folder. A program built with `--release` has none: it keeps its stack traces, but a debugger sees only the machine code.

`cromo run` deletes the executable when the program ends: to debug, build it with `cromo build`, and run the executable in the debugger.

### What the debugger shows

```cromo file=dbg.cro
struct Counter {
    i32 count
    string name

    void add(i32 n) {
        count += n
    }
}

i32 main() {
    Counter c
    c.name = "clicks"
    list<i32> v = [1, 2, 3]
    c.add(i32(v.len()))
    println("{} {}", c.name, c.count)
    ret 0
}
```

```output
clicks 3
```

- The variables and the parameters have their names, and the structs their fields.
- In a method, the object has no name in Cromo, since its fields are used directly: the debugger shows it as `this`, as in C++.
- A `string` and a `list` show their three fields: `data`, the address of the bytes or of the elements, `length` and `capacity`. A view of their text and of their elements is not available yet.
- In the debug information the functions have their module in front, also those of the main file: `add` is `dbg.Counter.add`, which is the name to give to a breakpoint.

With gdb, on Linux, a session on the program above, built with `cromo build -O0 dbg.cro`, stops in `add` and looks at the values:

```text
(gdb) break 'dbg.Counter.add'
(gdb) run
(gdb) info args
this = 0x7fffffffe398
n = 3
(gdb) print *this
$1 = {count = 0, name = {data = 0x2002c0, length = 6, capacity = 0}}
(gdb) up
(gdb) print v
$2 = {data = 0x7ffff7bf9010, length = 3, capacity = 4}
```

### Optimized code

`cromo` optimizes by default, with `-O2`, also the debug builds: the program in the debugger is the one that runs. With the optimizations, the compiler puts small functions inside those that call them, keeps variables only in registers and removes those it does not need: the debugger then shows some variables as *optimized out*, jumps between the lines, and cannot stop in a function that no longer exists by itself, as `add` above, which `-O2` puts inside `main`. To follow a program line by line, build it with `-O0`:

```sh
cromo build -O0 dbg.cro
```

`-O0` is slower, and changes nothing else: the checks of overflow, of the indices and of exclusivity are the same.

## Summary

- `cromo fmt` formats the code with one style and no options: braces on the same line, 4 spaces, one space around operators, comments aligned. It keeps the line breaks of the author, changes only spaces and line breaks, and checks that the tokens stay the same.
- `cromo fmt --check` lists the files to format and ends with 1; `cromo fmt -` formats the standard input for editors.
- The extension for Visual Studio Code uses `cromo lsp`: errors while you write, hover, definition, outline, completion after a dot, signature help and formatting. `cromo.path` chooses the `cromo` to run.
- The server analyzes a file with its `main`, or the nearest `main.cro` above it, or alone, with the files of the system it runs on.
- The `//` comments right above a declaration, or at the end of the line of a field or a case, are its documentation, in Markdown with `'code'` and `'''` blocks; the comment at the top of a file documents its module.
- Without `--release` a program has debug information, PDB on Windows and DWARF on Linux; a debugger shows `this` and the fields of strings and lists. Build with `-O0` to follow it line by line.
