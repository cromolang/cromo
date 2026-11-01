# Files, processes and time

This chapter is a tour of the modules that talk to the operating system: `std.fs` for files and folders, `std.os` for the environment, `std.process` for other programs and `std.time` for clocks and dates. They work in the same way on Windows and on Linux, and they report errors with exceptions.

## Files

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

    fs.copy("data/notes.txt", "data/copy.txt")
    fs.rename("data/copy.txt", "data/old.txt")
    println("{} {}", fs.isFile("data/old.txt"), fs.isFolder("data/logs"))

    fs.removeAll("data")
    println("{}", fs.exists("data"))
    ret 0
}
```

```output
> first line
> second line
23 bytes
true true
false
```

| Function | Does |
|---|---|
| `readText(path)`, `readBytes(path)` | the whole content, as a `string` (checked as UTF-8) or as bytes |
| `writeText`, `writeBytes`, `appendText` | write a file, creating or replacing it, or add to its end |
| `exists`, `isFile`, `isFolder`, `isLink` | ask what a path is |
| `size`, `modified` | the size in bytes, and the time of the last change in seconds from 1970 |
| `copy`, `rename`, `remove` | copy, move or delete a file |
| `createFolder`, `createFolders` | create a folder, or a folder with the folders above it |
| `listFolder(path)` | the names in a folder |
| `removeAll(path)` | delete a folder with all it contains, without following links |
| `joinPath`, `parent`, `fileName`, `extension` | the parts of a path |

A file that cannot be read or written is an exception, with a message that says which and why:

```cromo
import std.fs

i32 main() {
    try {
        string text = fs.readText("missing.txt")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot read 'missing.txt': no such file or folder
```

## The environment

`std.os` gives the name of the system, `"windows"` or `"linux"`, the variables of the environment and the current folder; `os.exit(code)` ends the program at once. The arguments of the program arrive in `main`:

```cromo args=alpha,beta
import std.os

i32 main(list<string> args) {
    for (i64 i = 1; i < args.len(); i++) {
        println("argument {}: {}", i, args[i])
    }
    if (os.hasEnv("CROMO_GUIDE_EXAMPLE")) {
        println("{}", os.env("CROMO_GUIDE_EXAMPLE"))
    }
    ret 0
}
```

```output
argument 1: alpha
argument 2: beta
```

`args[0]` is the name of the program, as in C.

## Other programs

`process.run` runs a program, waits for it, and gives its exit code, its output and its errors:

```cromo norun
import std.process

i32 main() {
    process.Output result = process.run("git", ["status", "--short"])
    if (result.code != 0) {
        eprintln("git failed: {}", result.err)
        ret 1
    }
    print("{}", result.out)
    ret 0
}
```

The program is looked for in the folders of the `PATH`, and the arguments are passed as they are, without a shell that interprets them. An exit code other than 0 is a value to check; a program that cannot start is an exception.

`process.start` starts a program without waiting, and gives a `process.Process` to talk with it: `write` and `closeInput` for its standard input, `read` and `readAll` for its output, `wait` for its end and `kill` to stop it.

## Archives

`std.zip` and `std.tar` read the archives, and with `std.gzip` the `.tar.gz`:

```cromo norun
import std.fs
import std.gzip
import std.tar
import std.zip

i32 main() {
    for (zip.Entry entry in zip.read(fs.readBytes("photos.zip"))) {
        println("{}: {} bytes", entry.name, entry.data.len())
    }
    tar.extract(gzip.decompress(fs.readBytes("site.tar.gz")), "site")
    ret 0
}
```

- `zip.read` and `tar.read` give the entries of an archive, each with `name`, `folder`, `link`, `executable` and `data`.
- `zip.extract` and `tar.extract` write the files and the folders of an archive in a folder, created if it is missing. They leave out the links, and stop at a name that would leave the folder, as `../x`: an archive that comes from the network cannot write elsewhere.
- They read the zip files of Windows, of macOS and of the forges, also above 4 GB, and the tar files of GNU and of POSIX; they do not write archives.

## Time

```cromo
import std.time

i32 main() {
    time.Instant start = time.now()
    time.msleep(50)
    time.Duration waited = start.elapsed()
    println("at least 50 ms: {}", waited.milliseconds() >= 50)

    time.DateTime now = time.utcNow()
    println("the year has four digits: {}", now.year >= 1000)
    ret 0
}
```

```output
at least 50 ms: true
the year has four digits: true
```

- `time.now()` reads a clock that only goes forward, for measuring; `elapsed()` and `since()` give a `Duration`, with `seconds()`, `milliseconds()`, `microseconds()` and the field `nanoseconds`.
- `utcNow()` and `localNow()` give a `DateTime`, with its fields from the year to the nanosecond, the offset from UTC in minutes, and `text()` in the format of ISO 8601: `2026-10-30T18:21:33+01:00`.
- `sleep`, `msleep` and `usleep` pause; in a task, the thread runs other tasks meanwhile.

## Summary

- `std.fs` reads and writes files and folders; errors are exceptions with a clear message.
- `std.os` gives the environment; the arguments arrive in `main(list<string> args)`.
- `std.process` runs other programs, without a shell.
- `std.zip` and `std.tar` read and extract archives.
- `std.time` measures durations and gives the date and time.
