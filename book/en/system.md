# Files, processes and time

This chapter is about the modules that talk to the operating system:

| Module | For |
|---|---|
| [std.fs](std.fs.html) | files and folders, paths, links |
| [std.os](std.os.html) | the system, the environment, the standard input, the end of the program |
| [std.process](std.process.html) | other programs, with their input and output |
| [std.time](std.time.html) | clocks, dates and pauses |
| [std.cmdline](std.cmdline.html) | the options of the command line, and the help |
| [std.zip](std.zip.html), [std.tar](std.tar.html) | archives |

They work in the same way on Windows and on Linux, and they follow the two kinds of errors of [Errors](errors.md): what can go wrong in the world, as a file that does not exist or a program that cannot start, is an exception, with a message that says what failed and why; a wrong use of a function, as a negative position in a file, is a panic. The chapter explains the ideas and the common uses; the page of each module, linked above, lists all its functions.

## The arguments of the program

The arguments of the command line arrive in `main`, as a list of strings. `args[0]` is the program, as it was started; the arguments follow it:

```cromo args=report.txt,--verbose
i32 main(list<string> args) {
    println("{} arguments", args.len() - 1)
    for (i64 i = 1; i < args.len(); i++) {
        println("{}: {}", i, args[i])
    }
    ret 0
}
```

```output
2 arguments
1: report.txt
2: --verbose
```

With `cromo run`, the arguments of the program come after `--`, so that they are not read as options of `cromo`:

```sh
cromo run show.cro -- report.txt --verbose
```

The parameter can have any name, as `argv`, but it must be a `list<string>` that `main` only reads: `ref`, `move`, another type or a second parameter are an error. `main` can also have no parameters, when it does not need the arguments.

```cromo error
i32 main(ref list<string> args) {
    ret 0
}
```

```output
error: main has no parameters, or one with the arguments of the program: list<string> args
```

How the arguments reach the program depends on the system:

- On **Linux** the shell splits the command line, and the program receives the arguments as the system gives them, as bytes. They are not checked: they are normally UTF-8, but a file name written in another encoding arrives as it is.
- On **Windows** a program receives the whole command line, as text in UTF-16. The runtime of Cromo converts it to UTF-8 and splits it with the rules of the C runtime of Microsoft, the same that programs written in C and C++ follow:

| On the command line | The argument |
|---|---|
| `"two words"` | `two words`: the quotes keep the spaces together |
| `a\"b` | `a"b`: a backslash before a quote makes it a quote |
| `"c""d"` | `c"d`: two quotes inside quotes are one |
| `e\\f` | `e\\f`: the backslashes that are not before a quote stay as they are |
| `"g\\"` | `g\`: two backslashes before a quote are one |
| `città` | `città`, in UTF-8 |

A program rarely sees these rules: the shell, and `std.process` when it starts a program, write the command line so that each argument arrives as it was given.

## Paths

A path is a `string`: absolute, or relative to the current folder, which `os.currentFolder()` gives. The names in a path are separated by `/` on every system, and on Windows by `\` too, so `"data/notes.txt"` works everywhere.

`std.fs` takes a path apart, without looking at the disk:

```cromo
import std.fs

i32 main() {
    string path = "photos/2026/beach.jpg"
    println("{}", fs.parent(path))
    println("{}", fs.fileName(path))
    println("{}", fs.extension(path))
    println("{}", fs.extension("backup.tar.gz"))
    println("{:?} {:?}", fs.extension(".profile"), fs.parent("beach.jpg"))
    ret 0
}
```

```output
photos/2026
beach.jpg
.jpg
.gz
"" ""
```

- `parent(path)` is the folder of the path, `""` for a name alone. The root stays itself: the parent of `/etc` is `/`, and on Windows the parent of `C:\data` is `C:\`.
- `fileName(path)` is the last name; for `"a/b/"` it is `"b"`.
- `extension(path)` is the last extension of the name, with its point; a name that starts with a point, as `.profile`, has none.

`joinPath(first, second)` puts the separator of the system between two parts, `/` on Linux and `\` on Windows; `fs.separator()` gives it:

```cromo ignore
fs.joinPath("photos", "beach.jpg")   // "photos/beach.jpg" on Linux, "photos\beach.jpg" on Windows
fs.joinPath("photos/", "beach.jpg")  // "photos/beach.jpg": the first part ends with a separator already
fs.joinPath("", "beach.jpg")         // "beach.jpg"
```

## Reading and writing whole files

The simplest way to use a file is to read it or write it whole:

```cromo
import std.fs

i32 main() {
    fs.createFolders("data/logs")
    fs.writeText("data/notes.txt", "first line\n")
    fs.appendText("data/notes.txt", "second line\n")

    string text = fs.readText("data/notes.txt")
    for (string line in text.trimEnd().split("\n")) {
        println("> {}", line)
    }
    println("{} bytes", fs.size("data/notes.txt"))

    fs.writeBytes("data/header.bin", [0x43, 0x52, 0x01])
    println("{:?}", fs.readBytes("data/header.bin"))
    fs.removeAll("data")
    ret 0
}
```

```output
> first line
> second line
23 bytes
[67, 82, 1]
```

- `readText(path)` gives the content as a `string`, and checks that it is UTF-8: a file that is not, as a text in Latin-1 or an image, is an exception. `readBytes(path)` gives the bytes, as a `list<u8>`, without checks.
- `writeText(path, text)` and `writeBytes(path, bytes)` create the file, or replace it when it exists. The folder of the file must exist.
- `appendText(path, text)` adds the text at the end of the file, and creates the file when it is missing: the usual way to write a log.

A file that cannot be read or written is an exception, whose message has the action, the path and the reason:

```cromo
import std.fs

i32 main() {
    try {
        string text = fs.readText("missing.txt")
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        fs.writeText("no-folder/notes.txt", "hello")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot read 'missing.txt': no such file or folder
cannot write 'no-folder/notes.txt': no such file or folder
```

## Files and folders

These functions ask what a path is:

- `exists(path)`, `isFile(path)` and `isFolder(path)` never throw: a path that is not there, or that cannot be reached, gives `false`. `isFile` and `isFolder` look through a link at what it points to; `isLink` looks at the link itself.
- `size(path)` is the size of a file in bytes, and `modified(path)` the time of its last change, in seconds from 1970 (see [Time](#time)). Both throw when the path does not exist.

A folder is created with `createFolder`, which throws when the path exists already or when the folder above it is missing, or with `createFolders`, which creates the missing folders above it too, as `mkdir -p`, and does nothing when the folder is there already. `listFolder(path)` gives the names of the files, the folders and the links in a folder, without `.` and `..`, **in no order**: sort them when the order matters, and put the folder before each name with `joinPath`:

```cromo
import std.fs

// The bytes of the files in a folder, and in the folders inside it.
i64 totalSize(string folder) {
    i64 total = 0
    for (string name in fs.listFolder(folder)) {
        string path = fs.joinPath(folder, name)
        if (fs.isFolder(path)) {
            total += totalSize(path)
        } else {
            total += fs.size(path)
        }
    }
    ret total
}

i32 main() {
    fs.createFolders("project/src/util")
    fs.writeText("project/readme.md", "# Project\n")
    fs.writeText("project/src/main.cro", "i32 main() {\n    ret 0\n}\n")
    fs.writeText("project/src/util/text.cro", "")

    list<string> names = fs.listFolder("project/src")
    sort(ref names)
    println("{:?}", names)
    println("{} bytes", totalSize("project"))
    fs.removeAll("project")
    ret 0
}
```

```output
["main.cro", "util"]
35 bytes
```

Copying, moving and removing:

- `copy(from, to)` copies a file, and replaces `to` when it exists. It does not copy a folder. On Linux the copy does not keep the permissions of the original: a program copied must be made executable again, with `makeExecutable`.
- `rename(from, to)` renames or moves a file or a folder, also to another folder, and replaces `to` when it is a file. Both paths must be on the same disk; only on Windows a file can move to another one.
- `remove(path)` removes a file, a link or an empty folder, and throws when the path does not exist or the folder is not empty.
- `removeAll(path)` removes a file, or a folder with all that it holds. It never follows a link: it removes the link, not what it points to. When the path does not exist it does nothing, so it can clean up without asking first.

```cromo
import std.fs

i32 main() {
    fs.writeText("draft.txt", "version 1")
    fs.writeText("final.txt", "version 0")
    fs.copy("draft.txt", "final.txt")  // final.txt is replaced
    println("{}", fs.readText("final.txt"))

    fs.writeText("draft.txt", "version 2")
    fs.rename("draft.txt", "final.txt")  // replaced again, and draft.txt is gone
    println("{} {}", fs.readText("final.txt"), fs.exists("draft.txt"))

    fs.createFolders("old/logs")
    try {
        fs.remove("old")
    } catch (Exception e) {
        println("{}", e.message())
    }
    fs.removeAll("old")
    fs.removeAll("old")  // nothing left to remove: no error
    fs.remove("final.txt")
    println("{} {}", fs.exists("old"), fs.exists("final.txt"))
    ret 0
}
```

```output
version 1
version 2 false
cannot remove 'old': the folder is not empty
false false
```

The other errors read in the same way: `cannot create 'logs': it exists already`, `cannot list 'notes.txt': a part of the path is not a folder`, `cannot copy 'photos': it is a folder`.

## Links and executable files

`fs.createLink(target, path)` makes `path` a link to `target`, as `ln -s target path`. A link is followed by the functions that read and write through it, and `readLink(path)` gives where it points:

```cromo
import std.fs
import std.os

i32 main() {
    fs.createFolders("releases/1.2")
    fs.writeText("releases/1.2/notes.txt", "fixes")
    // the target as an absolute path, which works in the same way on every system
    string target = fs.joinPath(os.currentFolder(), "releases/1.2")
    fs.createLink(target, "releases/current")

    println("{} {}", fs.isLink("releases/current"), fs.isFolder("releases/current"))
    println("{}", fs.fileName(fs.readLink("releases/current")))
    println("{}", fs.readText("releases/current/notes.txt"))

    fs.remove("releases/current")  // the link, not the folder
    println("{}", fs.exists("releases/1.2/notes.txt"))
    fs.removeAll("releases")
    ret 0
}
```

```output
true true
1.2
fixes
true
```

The two systems make different links:

- On **Linux** the link is a symbolic link, to a file or to a folder, that holds `target` as it is written. A relative target is read from the folder of the link, not from the current folder: `createLink("1.2", "releases/current")` points to `releases/1.2`.
- On **Windows** the link is a *junction*, which any user can create, while a symbolic link needs the rights of an administrator or the developer mode of Windows. A junction points only to a folder, and holds its absolute path: a relative target is read from the folder of the link, as on Linux. A target that is not a folder is the exception `cannot link to 'notes.txt': on Windows a link is a junction, to a folder`.

So a link that works on both systems points to a folder, with an absolute path, as in the example. `readLink` gives the target as the link holds it: on Windows, the absolute path. A path that is not a link is the exception `cannot read the link 'releases': it is not a link`. A link whose target is missing is still a link: `isLink` is `true`, while `exists` is `false`.

`fs.makeExecutable(path)` lets a file run as a program: on Linux it adds the permission to run it for those who can read it, as `chmod +x`; on Windows, which runs a file by its extension, it does nothing. Call it after writing or copying a program or a script:

```cromo ignore
fs.writeText("hello.sh", "#!/bin/sh\necho hello\n")
fs.makeExecutable("hello.sh")  // now ./hello.sh runs on Linux
```

## Files in pieces

A file too large to be read whole, as a database or an index, is opened with `fs.File` and read and written at a position:

```cromo
import std.fs

i32 main() {
    fs.File file = fs.File("records.bin", fs.Mode.Create)
    for (i32 i = 0; i < 100; i++) {
        file.write([u8(i), u8(i * 2), 0, 0])  // a record of 4 bytes
    }
    list<u8> record = file.readAt(4 * 37, 4)  // the record 37, without reading the others
    println("{} bytes, record 37: {:?}", file.size(), record)
    ret 0
}
```

```output
400 bytes, record 37: [37, 74, 0, 0]
```

The second argument says how the file is opened:

| Mode | The file | Reads | Writes |
|---|---|---|---|
| `fs.Mode.Read` | must exist | yes | no |
| `fs.Mode.Update` | must exist, and stays as it is | yes | yes |
| `fs.Mode.Create` | is created, or emptied when it exists | yes | yes |
| `fs.Mode.Append` | is created when it is missing | no | at its end |

A `File` has a position, from 0, where the next read or write starts:

- `read(count)` reads up to `count` bytes from the position, and `write(bytes)` writes there; both move the position after the bytes. `position()` gives it and `seek(position)` moves it.
- `readAt(position, count)` and `writeAt(position, bytes)` work at a position of their own, and leave the position of the file where it is: the way to reach a record of an index.
- A read gives fewer bytes than asked at the end of the file, and none after it. A write beyond the end makes the file grow, with zeros between. In `Append` every write goes at the end, also with `writeAt`.
- `size()` is the size of the file now; `sync()` sends what was written to the disk before it returns, so that the data survive a crash of the system.

```cromo
import std.fs

i32 main() {
    fs.File log = fs.File("events.log", fs.Mode.Create)
    log.write("start\n".bytes())
    println("position {}, size {}", log.position(), log.size())

    log.writeAt(10, "!".bytes())  // beyond the end: zeros between
    println("size {}", log.size())

    log.seek(0)
    println("{:?}", log.read(100))  // fewer than 100: the file ends
    println("{:?}", log.read(100))  // at the end: none
    ret 0
}
```

```output
position 6, size 6
size 11
[115, 116, 97, 114, 116, 10, 0, 0, 0, 0, 33]
[]
```

What the system refuses is an exception, with the path, as for the other functions of `std.fs`: a file that cannot be opened, a write to a file opened to read, a read from a file opened to append. A negative position or count is a mistake of the program, and a panic: `the position in a file is from 0, found -1`.

```cromo
import std.fs

i32 main() {
    fs.writeText("config.ini", "port=8080\n")
    fs.File config = fs.File("config.ini", fs.Mode.Read)
    try {
        config.write("debug=1\n".bytes())
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        fs.File other = fs.File("missing.ini", fs.Mode.Update)
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot write 'config.ini': it is open to read
cannot open 'missing.ini': no such file or folder
```

The file closes when the variable ends, by its destructor, as every resource in Cromo ([Structs](structs.md)). For the same reason a `File` cannot be copied, only moved: two copies would close the same file twice.

```cromo error
import std.fs

i32 main() {
    fs.File first = fs.File("data.bin", fs.Mode.Create)
    fs.File second = first
    ret 0
}
```

```output
error: values of type File cannot be copied
```

`fs.File b = move a` moves it, and `a` can no longer be used ([Ownership](ownership.md)).

## Files mapped in memory

`fs.Mapping(path)` maps a whole file in memory, read only. The system reads only the parts that are used, so a mapping is fast also for a file of gigabytes, read at many places:

```cromo file=header.cro
import std.fs

i32 main() {
    fs.writeBytes("header.bin", [0x43, 0x52, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00])
    fs.Mapping header = fs.Mapping("header.bin")
    println("{} bytes: {}", header.size(), header.text(0, 2))
    println("version {}, count {}", header.u16At(2), header.u32At(4))
    println("{}", header.u32At(6))
    ret 0
}
```

```output
8 bytes: CR
version 1, count 10000
panic: 4 bytes from 6 go beyond the 8 bytes of the mapping at cromo:std/fs.cro:374:13
    at Mapping.check (cromo:std/fs.cro:374)
    at Mapping.little (cromo:std/fs.cro:380)
    at Mapping.u32At (cromo:std/fs.cro:322)
    at main (header.cro:8)
```

- `u8At`, `u16At`, `u32At` and `u64At` read a number without sign at a position, from 0; `i8At`, `i16At`, `i32At`, `i64At`, `f32At` and `f64At` the others. The numbers are little-endian, the order of the bytes of x86-64 and of ARM64, at any position.
- `bytes(position, count)` gives a part as a `list<u8>`, and `text(position, count)` as a `string`, which must be UTF-8.
- A read beyond the size, or with a negative position, is a panic, as an index out of a list: the mapping has no addresses that the program could read wrong.
- An empty file maps nothing, and has size 0. A file that changes while it is mapped may show the change.

The mapping ends with its variable, and, as a `File`, it cannot be copied. On Android, the documents that the dialogs of [Windows and apps](gui.md) give, as `content://...`, cannot be opened with `fs.File` or mapped: they are only read and written whole.

## The system and the environment

`std.os` describes the system where the program runs:

- `os.name()` is `"windows"` or `"linux"`; on Android it is `"linux"` too.
- `os.env(name)` is the value of a variable of the environment, or `""` when it is not set; `os.hasEnv(name)` tells a variable set to `""` from one that is missing. On Windows the names do not distinguish upper and lower case: `os.env("path")` is the `PATH`.
- `os.currentFolder()` is the absolute path of the current folder, from which the relative paths start. `os.processId()` is the number that the system gives to the program.

```cromo norun
import std.fs
import std.os

// Where the program keeps its settings: NOTES_HOME, if it is set, or the usual place.
string settingsFolder() {
    if (os.hasEnv("NOTES_HOME")) {
        ret os.env("NOTES_HOME")
    }
    if (os.name() == "windows") {
        ret fs.joinPath(os.env("APPDATA"), "notes")
    }
    ret fs.joinPath(os.env("HOME"), ".config/notes")
}

i32 main() {
    fs.createFolders(settingsFolder())
    println("settings in {}", settingsFolder())
    ret 0
}
```

`os.exit(code)` ends the program at once with the exit code, as `exit()` of C, from any function. What was printed is written, but the objects still alive are not destroyed: their destructors do not run.

```cromo
import std.os

struct Connection {
    ~Connection() {
        println("connection closed")
    }
}

i32 main(list<string> args) {
    Connection connection
    if (args.len() < 2) {
        println("usage: client <host>")
        os.exit(2)
    }
    ret 0
}
```

```output
usage: client <host>
```

To end with a code and run the destructors, return it from `main`.

## The standard input

`os.readLine(ref line)` reads the next line of the standard input into `line`, without its end, `\n` or `\r\n`, and gives `false` at the end of the input:

```cromo norun file=number.cro
import std.os

i32 main() {
    string line
    i64 count = 0
    for (os.readLine(ref line)) {
        count++
        println("{}: {}", count, line)
    }
    ret 0
}
```

```sh
printf 'apples\npears' | cromo run number.cro
```

```output
1: apples
2: pears
```

`os.readInput()` reads all the input that is left, to its end, as one `string`, with its line ends as they are: what a filter does, as `cromo fmt -`. The two share one buffer, so they can be mixed: here the first line is a title and the rest is the body.

```cromo norun
import std.os

i32 main() {
    string title
    os.readLine(ref title)
    string body = os.readInput()
    println("{}: {} lines", title, body.trimEnd().split("\n").len())
    ret 0
}
```

The rules:

- A last line without `\n` is read too; at the end, `readLine` gives `false` and leaves `line` empty. At the end `readInput` gives `""`.
- The input must be UTF-8: a line that is not is an exception, as for `fs.readText`. On a console of Windows the text arrives in UTF-16, and is converted.
- From a terminal, the input ends with Ctrl+D on Linux, and on Windows with Ctrl+Z at the start of a line and Enter.
- The standard input is read from one thread at a time.

## The command line

The arguments of `main` are often options, as `--port 9000` or `-v`. `std.cmdline` reads them: a `cmdline.CmdLine` declares the options, `parse` reads the arguments, and the methods that start with `get` give the values.

```cromo file=server.cro args=--config,site.ini,-v,--port=9000,notes.txt
import std.cmdline

i32 main(list<string> args) {
    cmdline.CmdLine cmd
    cmd.flag("verbose", 'v', "print more")
    cmd.addInt("port", 'p', "port to listen on", false, 8080)
    cmd.addString("format", 'f', "output format", false, "text")
    cmd.choices("format", ["text", "json"])
    cmd.addString("config", ' ', "configuration file", true, "")
    cmd.footer("files...")
    cmd.parse(args)

    println("config {}, port {}, format {}", cmd.getString("config"), cmd.getInt("port"), cmd.getString("format"))
    if (cmd.exists("verbose")) {
        println("files: {}", join(cmd.rest(), ", "))
    }
    ret 0
}
```

```output
config site.ini, port 9000, format text
files: notes.txt
```

Each option has a long name, written after `--`, a short name of one letter, written after `-` (or `' '` when it has none), and a description for the help:

- `flag(name, letter, description)` adds an option without a value; `exists(name)` says whether it was given.
- `addInt`, `addFloat` and `addString(name, letter, description, required, default)` add an option with an `i64`, an `f64` or a `string`. `getInt`, `getFloat` and `getString` give the value on the command line, or the default when the option was not given; `exists` tells the two apart. A required option must be on the command line, and its default is not used. Cromo has no overloading, so the type is in the name of the method.
- `choices(name, values)` lists the values that an option accepts, as texts; a number is compared as `format("{}", n)` writes it.
- `footer(text)` is what the line of usage shows after the options, as `files...`.
- `rest()` gives the arguments that are not options, in their order, without `args[0]`.

The options can be written in the usual forms:

- `--port 9000` and `--port=9000`; `-p 9000` and `-p9000`.
- Flags together, `-vq`; the last letter can take a value, as `-vp 9000`.
- An option given twice keeps its last value.
- A number is read as `i64("...")` and `f64("...")` read it ([Lists, maps and text](collections.md)): `-5` and `1e3` are numbers, `0x10` and `1_000` are not.
- After `--` every argument goes to `rest()`, also when it starts with `-`; `-` alone is an argument too, which many programs read as the standard input.

```cromo args=-vp,7000,--port,7001,-,--,-x
import std.cmdline

i32 main(list<string> args) {
    cmdline.CmdLine cmd
    cmd.flag("verbose", 'v', "print more")
    cmd.addInt("port", 'p', "port to listen on", false, 8080)
    cmd.parse(args)
    println("{} {} {:?}", cmd.exists("verbose"), cmd.getInt("port"), cmd.rest())
    ret 0
}
```

```output
true 7001 ["-", "-x"]
```

`-h` and `--help` print the help, made from the options, on the standard output, and end the program with the exit code 0. The line of usage has the name of the program, from `args[0]` without its folder and without `.exe`, the required options and the footer; then each option has a line with its type, its choices and its default:

```sh
cromo run server.cro -- --help
```

```output
usage: server --config <string> [options] files...
options:
  -v, --verbose          print more
  -p, --port <int>       port to listen on (default 8080)
  -f, --format <string>  output format (text, json; default text)
      --config <string>  configuration file (required)
  -h, --help             print this help
```

`cmd.help()` gives the same text, to print it at another moment; before `parse`, which reads the name of the program, the line of usage says `program`.

A wrong command line is a mistake of who runs the program: `parse` prints the error and the help on the standard error, and ends the program with the exit code 2, as the commands of the system do. Here the required option is missing:

```sh
cromo run server.cro -- --port 9000 notes.txt
```

```output
server: the option --config is required

usage: server --config <string> [options] files...
options:
  -v, --verbose          print more
  -p, --port <int>       port to listen on (default 8080)
  -f, --format <string>  output format (text, json; default text)
      --config <string>  configuration file (required)
  -h, --help             print this help
```

The other errors are written in the same way: `unknown option --nope`, `the option --port needs a value`, `the value of --port is not an integer: 'abc'`, `the value of --format is 'xml', and it can be text, json`, `the option --verbose takes no value`.

A wrong use of the module is instead a mistake of the program, and a panic: two options with the same name or the same letter (`cmdline: the short name -v is used twice`), the name `help` or the letter `h`, which the help takes, a default that is not among the choices, or `getInt` of an option of text (`cmdline: --format is not an option of int`).

## Running other programs

`process.run(program, args)` runs a program to its end, and gives a `process.Output` with its exit code, its standard output and its standard error. The examples of this section start the example itself, with an argument that makes it act as the other program, so that they need no other program:

```cromo
import std.process

i32 main(list<string> args) {
    if (args.len() > 1) {
        // the program started by run(): it shows its arguments, writes an error and ends with 3
        for (i64 i = 1; i < args.len(); i++) {
            println("[{}]", args[i])
        }
        eprintln("a warning")
        ret 3
    }
    process.Output result = process.run(args[0], ["two words", "a \"quote\"", "", "C:\\folder\\"])
    println("code {}", result.code)
    print("{}", result.out)
    print("errors: {}", result.err)
    ret 0
}
```

```output
code 3
[two words]
[a "quote"]
[]
[C:\folder\]
errors: a warning
```

- The arguments reach the program as they are, without a shell that interprets them: a space, a quote, `*` or `$` are only characters, and an empty argument stays an argument. On Windows `std.process` writes the command line with the quotes and the backslashes that the rules of [the arguments](#the-arguments-of-the-program) need.
- An exit code other than 0 is a value to check, not an exception: many programs use it to give an answer, as `grep` that finds nothing.
- The standard input of the program is empty. Its output and its errors are read at the same time, on two threads, so that a program that writes much on one of them does not wait for ever.
- The program has the environment and the current folder of this one.
- Programs can be started from several threads at the same time: each program receives only its own pipes.

The program is found as a shell finds it: a name without a folder, as `git`, is looked for in the folders of the `PATH`, and on Windows also in the current folder, with `.exe` added when it has no extension. `./tool` runs the program `tool` of the current folder on both systems. A program that cannot start is an exception:

```cromo
import std.process

i32 main() {
    try {
        process.Output result = process.run("no-such-program", ["--version"])
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot start 'no-such-program': no such program
```

### A program that runs alongside

`process.start(program, args)` starts a program without waiting for it, and gives a `process.Process` to talk with it while it runs:

```cromo
import std.os
import std.process

i32 main(list<string> args) {
    if (args.len() > 1) {
        // the program started below: it writes its input in upper case
        string line
        for (os.readLine(ref line)) {
            println("{}", line.upper())
        }
        ret 0
    }
    process.Process child = process.start(args[0], ["upper"])
    child.write("hello\n")
    child.write("world\n")
    child.closeInput()  // the end of its input: the loop of the child ends
    print("{}", child.readAll())
    println("code {}", child.wait())
    ret 0
}
```

```output
HELLO
WORLD
code 0
```

- `write(text)` and `writeBytes(bytes)` write to the standard input of the program, and wait while the program does not read. `closeInput()` closes it: the program sees the end of its input, which many programs, as `sort`, wait for before they answer.
- `read(most)` gives up to `most` bytes of the standard output, as soon as some arrive, and an empty list at its end; `readAll()` reads it to its end, as text.
- `wait()` waits for the end of the program, and gives its exit code; called again, it gives the same code.
- `kill()` ends the program at once; `wait()` then gives 1 on Windows, and 137 on Linux, which is 128 + the signal that ended it, as the shells say. A program ended by another signal gives 128 + that signal too.
- The errors of the program are not read: they go where the errors of this program go, as the terminal.

`read` serves the programs that answer a piece at a time, as a server or a long build:

```cromo ignore
for {
    list<u8> piece = child.read(4096)
    if (piece.len() == 0) {
        break  // the program has closed its output
    }
    total += piece.len()
}
```

The example writes all the input before it reads the output. That works when the program answers only at the end of its input, as `sort`, or when its answer is small: the pipe between two programs holds some kilobytes. A program that answers while it reads, given a large input, would fill its output while this one is still writing, and both would wait for ever: give it the input in a file instead.

When a `Process` is destroyed, as at the end of its block, it closes the input and the output of the program, and waits for its end, unless `wait()` did it already. So a program started in a function has ended when the function returns:

```cromo
import std.fs
import std.os
import std.process

void save(string program, list<string> lines) {
    process.Process child = process.start(program, ["save"])
    for (string line in lines) {
        child.write(line + "\n")
    }
}  // here the destructor closes the input of the child, and waits for its end

i32 main(list<string> args) {
    if (args.len() > 1) {
        fs.writeText("saved.txt", os.readInput())  // the child: its input to a file
        ret 0
    }
    save(args[0], ["one", "two"])
    print("{}", fs.readText("saved.txt"))
    ret 0
}
```

```output
one
two
```

As a `File`, a `Process` cannot be copied, only moved.

Writing to a program that has ended is an exception, as is writing after `closeInput()`. On Linux the system would end the program that writes with the signal `SIGPIPE`; a Cromo program ignores it from the first program it starts, so the write fails as any other. The programs it starts have `SIGPIPE` as usual.

```cromo
import std.process
import std.time

i32 main(list<string> args) {
    if (args.len() > 1) {
        if (args[1] == "slow") {
            time.sleep(60)
        }
        ret 5
    }
    process.Process slow = process.start(args[0], ["slow"])
    slow.kill()
    println("killed: {}", slow.wait() != 0)

    process.Process quick = process.start(args[0], ["quick"])
    println("code {}", quick.wait())
    try {
        quick.write("too late\n")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
killed: true
code 5
cannot write to the program: it does not read its input any more
```

### The output as text

`out`, `err` and `readAll()` give the output as a `string`, which is UTF-8. A program can write in another encoding, as the code page of a console of Windows: the bytes that are not UTF-8 become the character U+FFFD, `�`, instead of an exception. Here the system shows a file written in Latin-1, where `à` is the single byte 224:

```cromo
import std.fs
import std.os
import std.process

i32 main() {
    fs.writeBytes("latin1.txt", [99, 105, 116, 116, 224, 10])  // "città" in Latin-1
    process.Output result
    if (os.name() == "windows") {
        result = process.run("cmd", ["/c", "type", "latin1.txt"])
    } else {
        result = process.run("cat", ["latin1.txt"])
    }
    print("{}", result.out)
    println("{:?}", result.out.bytes())
    ret 0
}
```

```output
citt�
[99, 105, 116, 116, 239, 191, 189, 10]
```

The bytes 239, 191 and 189 are U+FFFD in UTF-8. To have the exact bytes of the output, read them with `read`.

## Time

`std.time` has two kinds of time: a clock that measures how long something takes, and the date with the time of day.

### Measuring

`time.now()` reads a monotonic clock, which only goes forward, also when the date of the system changes: an `Instant`. `elapsed()` gives the time from that instant to now, and `later.since(earlier)` the time between two instants, as a `Duration`:

```cromo
import std.time

i32 main() {
    time.Instant start = time.now()
    time.msleep(20)
    time.Instant middle = time.now()
    time.msleep(30)

    time.Duration first = middle.since(start)
    time.Duration whole = start.elapsed()
    println("{} {}", first.milliseconds() >= 20, whole.milliseconds() >= 50)
    println("{}", start.since(middle).nanoseconds < 0)
    ret 0
}
```

```output
true true
true
```

A `Duration` has the field `nanoseconds`, and `seconds()`, an `f64` with the fractions, `milliseconds()` and `microseconds()`, which leave out the rest. It is negative when the instants are in the other order.

`time.sleep(seconds)`, `time.msleep(milliseconds)` and `time.usleep(microseconds)` pause the program; a count of 0 or less does nothing. In a task, the pause leaves the thread to the other tasks ([Threads, tasks and channels](concurrency.md)).

### Dates

`time.utcNow()` gives the date and the time now in UTC, and `time.localNow()` in the zone of the system, as a `DateTime`. Its fields are `year`, with all its digits, `month` from 1, `day` from 1, `hour`, `minute`, `second`, `nanosecond`, and `offset`, the minutes from UTC: 60 in Rome in winter, 0 in UTC. On Linux the zone is that of `/etc/localtime`, and UTC without it.

`text()` writes it in the format of ISO 8601, with the milliseconds. A `DateTime` is a struct with public fields, so one made by hand prints in the same way:

```cromo
import std.time

i32 main() {
    time.DateTime launch
    launch.year = 2026
    launch.month = 3
    launch.day = 7
    launch.hour = 9
    launch.minute = 5
    launch.nanosecond = 250000000
    println("{}", launch.text())
    launch.offset = 60
    println("{}", launch.text())

    time.DateTime now = time.utcNow()
    println("{}", now.year >= 2026 && now.text().endsWith("Z"))
    ret 0
}
```

```output
2026-03-07T09:05:00.250Z
2026-03-07T09:05:00.250+01:00
true
```

`time.unix()` gives the seconds from 1970-01-01 UTC, the time of Unix, as an `i64`: the unit of `fs.modified()`, and of the times in a JSON Web Token. The difference of two of them is a number of seconds:

```cromo
import std.fs
import std.time

i32 main() {
    fs.writeText("stamp.txt", "now")
    i64 age = time.unix() - fs.modified("stamp.txt")
    println("written less than 2 seconds ago: {}", age < 2)
    ret 0
}
```

```output
written less than 2 seconds ago: true
```

So `time.unix() - fs.modified(path) > 24 * 3600` says that a file is older than a day.

## Archives

`std.zip` reads the archives of zip, as Windows, macOS and the forges make them, also above 4 GB; `std.tar` reads the archives of tar, as GNU tar and bsdtar make them. A `.tar.gz` is a tar compressed with gzip: `std.gzip` decompresses it first. They read and extract archives, and do not write them.

`zip.read(bytes)` and `tar.read(bytes)` give the entries of an archive, in its order. Each entry has its `name`, a path with `/` and none at the end, `folder`, whether it is a folder, `link`, the target of a symbolic link or `""`, `executable`, whether it can run, and `data`, the content of a file. The examples write their archives first, from the bytes in the hidden lines:

```cromo
import std.fs
import std.zip

i32 main() {
#     fs.writeBytes("notes.zip", notesZip())
    for (zip.Entry entry in zip.read(fs.readBytes("notes.zip"))) {
        if (entry.folder) {
            println("{}/", entry.name)
        } else {
            println("{}: {} bytes", entry.name, entry.data.len())
        }
    }
    zip.extract(fs.readBytes("notes.zip"), "out")
    print("{}", fs.readText("out/notes/todo.txt"))
    fs.removeAll("out")
    ret 0
}
#
# // The bytes of notes.zip: a folder, notes, with a file, todo.txt.
# list<u8> notesZip() {
#     ret [
#         80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 0, 96, 97, 93, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0,
#         110, 111, 116, 101, 115, 47, 80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 0, 96, 97, 93, 245, 208, 237, 14,
#         9, 0, 0, 0, 9, 0, 0, 0, 14, 0, 0, 0, 110, 111, 116, 101, 115, 47, 116, 111, 100, 111, 46, 116,
#         120, 116, 98, 117, 121, 32, 109, 105, 108, 107, 10, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 0,
#         96, 97, 93, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 237, 65,
#         0, 0, 0, 0, 110, 111, 116, 101, 115, 47, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 0, 96, 97, 93,
#         245, 208, 237, 14, 9, 0, 0, 0, 9, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 164, 129, 36,
#         0, 0, 0, 110, 111, 116, 101, 115, 47, 116, 111, 100, 111, 46, 116, 120, 116, 80, 75, 5, 6, 0,
#         0, 0, 0, 2, 0, 2, 0, 112, 0, 0, 0, 89, 0, 0, 0, 0, 0
#     ]
# }
```

```output
notes/
notes/todo.txt: 9 bytes
buy milk
```

`zip.extract(bytes, folder)` and `tar.extract(bytes, folder)` write the files and the folders of an archive in a folder, which is created when it is missing. A file that exists is replaced, and on Linux the executable files can run, as `makeExecutable` makes them. The symbolic links are left out, as they could point outside the folder:

```cromo
import std.fs
import std.gzip
import std.tar

i32 main() {
#     fs.writeBytes("tool.tar.gz", toolTarGz())
    list<u8> archive = gzip.decompress(fs.readBytes("tool.tar.gz"))
    for (tar.Entry entry in tar.read(archive)) {
        println("{} folder={} executable={} link={:?}", entry.name, entry.folder, entry.executable, entry.link)
    }
    tar.extract(archive, "out")
    list<string> names = fs.listFolder("out/tool")
    sort(ref names)
    println("{:?}", names)
    fs.removeAll("out")
    ret 0
}
#
# // The bytes of tool.tar.gz: a folder, tool, with two files and a link.
# list<u8> toolTarGz() {
#     ret [
#         31, 139, 8, 0, 0, 0, 0, 0, 2, 10, 237, 214, 77, 10, 194, 48, 16, 134, 225, 172, 61, 69, 78, 80,
#         147, 52, 63, 231, 41, 26, 168, 16, 13, 180, 17, 60, 190, 53, 59, 187, 16, 55, 169, 96, 223,
#         103, 51, 33, 155, 89, 12, 223, 48, 37, 231, 116, 20, 109, 169, 69, 112, 174, 214, 197, 186,
#         214, 183, 118, 38, 104, 111, 108, 168, 255, 62, 120, 39, 164, 19, 27, 184, 207, 101, 152, 150,
#         150, 98, 159, 202, 107, 254, 211, 253, 214, 205, 227, 239, 230, 175, 251, 247, 249, 107, 165,
#         141, 17, 82, 49, 255, 230, 226, 105, 204, 114, 140, 41, 229, 131, 192, 94, 243, 31, 135, 243,
#         53, 118, 229, 81, 154, 229, 223, 91, 251, 97, 255, 135, 117, 254, 131, 87, 228, 127, 11, 203,
#         234, 151, 151, 66, 246, 119, 157, 255, 52, 148, 56, 151, 102, 61, 106, 194, 67, 248, 254, 254,
#         211, 186, 183, 90, 72, 211, 248, 46, 33, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 254, 201, 19, 63,
#         156, 135, 9, 0, 40, 0, 0
#     ]
# }
```

```output
tool folder=true executable=false link=""
tool/run.sh folder=false executable=true link=""
tool/readme.txt folder=false executable=false link=""
tool/latest folder=false executable=false link="run.sh"
["readme.txt", "run.sh"]
```

An archive that comes from the network cannot write outside the folder: a name that would leave it, as `../x` or `/x`, stops the extraction with the exception `cannot extract the zip: the name '../x' would leave the folder`. The entries before it are already written. Data that are not an archive, or an archive damaged or cut, are exceptions too: `cannot read the zip: the data are not a zip`, `cannot read the tar: the data are not a tar`, and `std.zip` checks the CRC-32 of each file. A zip encrypted, or compressed with a method other than deflate, is refused.

## Summary

- `main(list<string> args)` receives the arguments, with the program in `args[0]`: on Windows split with the rules of the C runtime of Microsoft and converted to UTF-8, on Linux the bytes that the system gives.
- `std.fs` reads and writes files whole, or in pieces with `fs.File`, or maps them with `fs.Mapping`; it creates, lists, copies, moves and removes files and folders, makes links and executable files, and takes paths apart. Its errors are exceptions with the path and the reason.
- `std.os` gives the system, the environment, the current folder and the standard input, and `os.exit` ends the program without running the destructors.
- `std.cmdline` reads the options of the command line, writes the help, and handles a wrong command line with the exit code 2.
- `std.process` runs programs to their end with `run`, or alongside with `start`, without a shell; an exit code is a value, a program that cannot start is an exception, and a `Process` waits for its program when it is destroyed.
- `std.time` measures durations with a monotonic clock, gives the date and the time, the seconds of Unix, and pauses.
- `std.zip` and `std.tar`, with `std.gzip`, read and extract archives, without writing outside the folder.
