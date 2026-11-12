# Databases

A program that must keep its data, and find them again by any of their parts, uses a database. The module [std.db](std.db.html) has SQLite inside it: a database that is one file, read and written by the program itself, with no server to install. The program is still one executable without dependencies. The same module talks to a server of PostgreSQL, at the end of the chapter.

## A first database

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT, done INTEGER)")

    db.Statement insert = c.prepare("INSERT INTO notes (title, done) VALUES (?, ?)")
    insert.setText(1, "Buy the bread")
    insert.setBool(2, false)
    insert.exec()
    insert.setText(1, "Call Ada")
    insert.setBool(2, true)
    insert.exec()

    db.Statement all = c.prepare("SELECT id, title, done FROM notes ORDER BY id")
    for (all.next()) {
        println("{} {} {}", all.getInt(0), all.getText(1), all.getBool(2))
    }
    ret 0
}
```

```output
1 Buy the bread false
2 Call Ada true
```

Three types do everything:

- a `db.Database` is the database, opened by a text;
- a `db.Connection` runs statements on it, one after the other, and transactions;
- a `db.Statement` is one statement of SQL with its parameters, which gives its rows one at a time.

## Opening a database

The text given to `db.Database` says which database it is:

| Text | Database |
|---|---|
| `"notes.db"`, `"data/notes.db"` | a file of SQLite, made if it is missing; its folder must be there |
| `":memory:"` | a database in memory, empty at the start and gone with the program: for tests, and for data that need not stay |

A text that cannot be opened is an `Error`:

```cromo
import std.db

i32 main() {
    try {
        db.Database data = db.Database("no/such/folder/notes.db")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot open the database no/such/folder/notes.db: unable to open database file
```

A `db.Database` is a handle, as a window of `std.gui`: its copies are the same database, and the threads and the tasks capture it as a simple value. A program opens it once, in `main`, and gives it to who needs it.

The file is a database of SQLite as any other: the `sqlite3` command and the other programs read it and write it, also while the program runs.

## Connections

`data.connect()` gives a connection. When its variable ends, the connection goes back to the database, which keeps it for the next `connect()`: taking one costs little, so a function takes a connection, uses it and lets it end. The handler of a server does so for each request:

```cromo ignore
void handle(db.Database data, ref http.Request req, ref http.Response res) {
    db.Connection c = data.connect()
    // ... the statements of this request
}  // the connection goes back to the database
```

- A connection cannot be copied, only moved, and runs one statement at a time.
- `data.setMaxIdle(count)` sets how many connections the database keeps when they are given back, 4 by default: the others are closed.
- A database in memory has one connection, which is the database: `connect()` waits for it while another part of the program has it.

`c.exec(sql)` runs statements that have no parameters, also more than one, separated by `;`, and gives how many rows they inserted, changed and deleted:

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT); CREATE INDEX byTitle ON notes (title)")
    i64 added = c.exec("INSERT INTO notes (title) VALUES ('one'), ('two'), ('three')")
    i64 gone = c.exec("DELETE FROM notes WHERE title LIKE 't%'")
    println("added {}, deleted {}", added, gone)
    println("{} {}", c.hasTable("notes"), c.hasColumn("notes", "author"))
    ret 0
}
```

```output
added 3, deleted 2
true false
```

`hasTable(name)` and `hasColumn(table, name)` ask what the database has: a program uses them to make its tables the first time, and to add a column that a new version needs.

## Statements and parameters

A value that comes from outside the program is never written inside the text of a statement: a title with a quote in it would break the statement, and a title written on purpose would change it, which is how the injections of SQL work. The statement has a `?` where each value goes, and the value is set by its number, from 1:

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT)")

    string title = "it's here'); DROP TABLE notes; --"
    db.Statement insert = c.prepare("INSERT INTO notes (title) VALUES (?)")
    insert.setText(1, title)
    insert.exec()

    db.Statement find = c.prepare("SELECT title FROM notes WHERE id = ?")
    find.setInt(1, 1)
    find.next()
    println("{}", find.getText(0))
    ret 0
}
```

```output
it's here'); DROP TABLE notes; --
```

Cromo has no overloading, so each type has its method:

| To set a parameter | To read a column | The value |
|---|---|---|
| `setInt(index, value)` | `getInt(index)` | an `i64` |
| `setFloat(index, value)` | `getFloat(index)` | an `f64` |
| `setText(index, value)` | `getText(index)` | a `string` |
| `setBytes(index, value)` | `getBytes(index)` | a `list<u8>`, as an image |
| `setBool(index, value)` | `getBool(index)` | a `bool`, kept as 1 or 0 |
| `setNull(index)` | `isNull(index)` | no value, the NULL of SQL |

- The parameters are counted from 1, as in SQL, and the columns from 0, as the elements of a list.
- A column without a value reads as 0, `""`, no bytes or `false`: `isNull` tells it from a real 0.
- SQLite converts between the types where it can: `getText` of a number gives its digits.
- `columnCount()` and `columnName(index)` say what the rows of the statement have.
- A date is kept as a text, as `"2026-11-11 18:30:00"`, or as a number of seconds: the dates of the library will come with their own module.

A statement runs in one of two ways:

- `exec()` runs it to its end and gives how many rows it inserted, changed or deleted. Then the statement is ready to run again, and the parameters keep their values until they are set again: a statement prepared once inserts a thousand rows.
- `next()` goes to the next row, the first one the first time, and gives `false` when there are no more: `for (rows.next())` reads them all. `reset()` makes the statement ready to give its rows again from the first.

A statement that is on a row is still reading: it sees the database as it was at its first row, and until it ends the other connections cannot write all that they want. A statement read to its last row, and one whose variable ends, have finished; one that is kept after reading only some rows, as the first, is let go with `reset()`:

```cromo ignore
db.Statement newest = c.prepare("SELECT title FROM notes ORDER BY id DESC")
newest.next()
string title = newest.getText(0)
newest.reset()  // it has read what it needed
```

A parameter or a column that the statement does not have, and a column read when the statement is not on a row, are mistakes of the program, and panics:

```cromo nocheck
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    db.Statement one = c.prepare("SELECT 1")
    println("{}", one.getInt(0))
    ret 0
}
```

```text
panic: db: the statement is not on a row: next() goes to one
```

## Transactions

Statements that must be done all or not at all go in a transaction: `begin()` starts it, `commit()` keeps what it did, and `rollback()` undoes it. A connection that ends inside a transaction rolls it back, so an exception that leaves the function undoes what the function had started:

```cromo
import std.db

void transfer(ref db.Connection c, i64 from, i64 to, i64 amount) {
    c.begin()
    db.Statement take = c.prepare("UPDATE accounts SET balance = balance - ? WHERE id = ? AND balance >= ?")
    take.setInt(1, amount)
    take.setInt(2, from)
    take.setInt(3, amount)
    if (take.exec() == 0) {
        c.rollback()
        throw Error(format("the account {} cannot pay {}", from, amount))
    }
    db.Statement give = c.prepare("UPDATE accounts SET balance = balance + ? WHERE id = ?")
    give.setInt(1, amount)
    give.setInt(2, to)
    give.exec()
    c.commit()
}

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE accounts (id INTEGER PRIMARY KEY, balance INTEGER)")
    c.exec("INSERT INTO accounts (balance) VALUES (100), (0)")
    transfer(ref c, 1, 2, 30)
    try {
        transfer(ref c, 1, 2, 500)
    } catch (Exception e) {
        println("{}", e.message())
    }
    db.Statement all = c.prepare("SELECT id, balance FROM accounts ORDER BY id")
    for (all.next()) {
        println("account {}: {}", all.getInt(0), all.getInt(1))
    }
    ret 0
}
```

```output
the account 1 cannot pay 500
account 1: 70
account 2: 30
```

A transaction also makes many insertions fast: SQLite writes the file once, at the commit, and not once for each row.

## Errors

An error of the database is an `Error` with the message of SQLite, after what the program was doing:

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT NOT NULL)")
    list<string> wrong = ["SELEC 1", "DELETE FROM tasks", "INSERT INTO notes (title) VALUES (NULL)"]
    for (string sql in wrong) {
        try {
            c.exec(sql)
        } catch (Exception e) {
            println("{}", e.message())
        }
    }
    ret 0
}
```

```output
cannot run the statement: near "SELEC": syntax error
cannot run the statement: no such table: tasks
cannot run the statement: NOT NULL constraint failed: notes.title
```

## Several connections, threads and tasks

The tasks of a server each take their connection, and SQLite keeps their work apart with the locks of the file, the same that keep apart two programs on the same database: many can read together, and one at a time writes.

- A statement that finds the file locked waits for it, and tries again, for the timeout of the database: 5 seconds by default, `data.setTimeout(milliseconds)` to change it for the connections made from then on. After that it is an `Error`, `database is locked`.
- A transaction that will write is best started with `c.exec("BEGIN IMMEDIATE")` in place of `begin()`: it takes the right to write at once, and waits for it, instead of finding it taken at its first `UPDATE`, when two transactions that have both read can no longer both go on.
- Keep the transactions short: while one writes, the others wait.

```cromo
import std.db
import std.thread

i32 main() {
    db.Database data = db.Database(":memory:")
    if (true) {
        db.Connection c = data.connect()
        c.exec("CREATE TABLE hits (task INTEGER, n INTEGER)")
    }
    thread.Group tasks
    for (i64 t = 0; t < 8; t++) {
        tasks.start([data, t]() {
            db.Connection c = data.connect()
            db.Statement insert = c.prepare("INSERT INTO hits (task, n) VALUES (?, ?)")
            for (i64 n = 0; n < 25; n++) {
                insert.setInt(1, t)
                insert.setInt(2, n)
                insert.exec()
            }
        })
    }
    tasks.wait()
    db.Connection c = data.connect()
    db.Statement count = c.prepare("SELECT count(*) FROM hits")
    count.next()
    println("{} rows", count.getInt(0))
    ret 0
}
```

```output
200 rows
```

## Copying a database

`c.backup(file)` copies the whole database into another file, also while the program and the others use it: the copy is a database as it was at one moment.

```cromo ignore
db.Connection c = data.connect()
c.backup("notes-backup.db")
```

## What this SQLite has, and what it does not

SQLite is compiled into the runtime of Cromo without the C library, so the executables stay without dependencies, also on Linux. Some of its parts are left out:

- **The connections work together.** Two tasks, each with its connection, are in SQLite at the same instant, on the same database or on two: they wait for each other only on the locks of the files. A connection runs one statement at a time.
- **The write-ahead log is there, and off by default,** as in SQLite. `c.exec("PRAGMA journal_mode = WAL")`, once, turns it on for the database, which keeps it: then who reads does not wait for who writes, and a commit writes less. A server with many readers wants it. The database then has two more files next to it, with `-wal` and `-shm` at the end of its name, and must be on a disk of the computer, not of the network.
- **Times in UTC.** The functions of SQLite for the dates have no `localtime`.
- **No extensions loaded from files.**
- **Foreign keys** are checked only after `c.exec("PRAGMA foreign_keys = ON")`, on each connection, as SQLite does by default.

## PostgreSQL

The same module talks to a server of PostgreSQL: the text that opens the database is an address in place of the name of a file, and the rest of the program is the same. The client is written in Cromo, so the program has no library to install for it.

```cromo ignore
db.Database data = db.Database("postgresql://notes:secret@db.example.com:5432/notes?sslmode=require")
db.Connection c = data.connect()
c.exec("CREATE TABLE IF NOT EXISTS notes (id SERIAL PRIMARY KEY, title TEXT, done BOOLEAN)")
db.Statement insert = c.prepare("INSERT INTO notes (title, done) VALUES (?, ?)")
insert.setText(1, "Buy the bread")
insert.setBool(2, false)
insert.exec()
```

The address is `postgresql://user:password@host:port/name`. Without a port it is 5432, without a host this computer, and without a name the database has the name of the user. A character that an address cannot have in the user or in the password is written with its code, as `%40` for `@` and `%3F` for `?`.

- **The login.** The password is checked with SCRAM-SHA-256, the way of PostgreSQL since its version 14: it never goes to the server, and the server must prove that it knows it too. The servers that still ask for MD5, or for the password as it is, are answered as well.
- **TLS.** With `?sslmode=require` at the end of the address the connection is encrypted, and the certificate of the server must come from an authority that the system trusts and name the host, as for `net.connectTls`: a database in the cloud wants it. Without it, or with `?sslmode=disable`, the connection is not encrypted, which is right on the same computer or on a network of one's own.
- **The parameters** are `?` as with SQLite, and std.db gives them to the server as `$1`, `$2`. The values go as texts that the server reads as the type that the statement wants, so `setBool` works with a column `BOOLEAN` and with an integer one. A `?` inside a text, a name between quotes or a comment stays as it is; the operators of PostgreSQL that have a `?`, as those of `jsonb`, are written `??`.
- **The rows** of a statement all arrive at its first `next()`, and are then read one at a time: a statement with millions of rows wants a `LIMIT`.
- **The errors** are an `Error` with the message of the server and, between parentheses at its end, its code of five characters: `23505` is a key that is there already, `42P01` a table that is missing. The message is in the language of the server.
- **The connections** given back are kept and given again, as with SQLite; one that ends inside a transaction rolls it back. `data.setTimeout(milliseconds)` is how long a statement waits for a lock that another holds (`lock_timeout`), for the connections opened from then on.
- `hasTable` and `hasColumn` look in the schemas of the search path. `backup` is of SQLite only: a database of PostgreSQL is copied with the tools of its server, and the method throws an `Error`.

What differs between the two databases is their SQL: the types of the columns, `SERIAL` or `INTEGER PRIMARY KEY` for a key that counts by itself, the functions. A program that must run on both keeps to what they share, or has the few statements that differ in two forms.

## Summary

- `db.Database(text)` opens a database: a file of SQLite, made if it is missing, `":memory:"`, or an address of PostgreSQL, `postgresql://user:password@host/name`. It is a handle that the threads and the tasks capture.
- `data.connect()` gives a `db.Connection`, which goes back to the database when its variable ends.
- `c.exec(sql)` runs statements without parameters and gives the rows changed; `c.prepare(sql)` gives a `db.Statement` with a `?` for each parameter.
- The parameters are set from 1 with `setInt`, `setFloat`, `setText`, `setBytes`, `setBool` and `setNull`; the columns are read from 0 with `getInt`, `getFloat`, `getText`, `getBytes`, `getBool` and `isNull`.
- `exec()` runs a statement and makes it ready to run again; `next()` gives its rows one at a time.
- `begin()`, `commit()` and `rollback()` are a transaction; a connection that ends inside one rolls it back.
- An error of the database is an `Error` with its message; a wrong use of a statement is a panic.
- A statement that finds the file locked waits up to the timeout of the database.
