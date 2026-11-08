# Ownership

Ownership is how Cromo manages memory and every other resource without a garbage collector and without pointers. The rules are few, the compiler checks them, and once learned they make the cost of every line visible.

The examples of this chapter use structs with a constructor and a destructor, to show when values are made and when they end. [Structs](structs.md), the next chapter, explains them in full; here it is enough to know that the destructor, `~Name()`, runs when the value ends.

## Every value has one owner

A value belongs to the variable, the field or the element of a list that holds it: its **owner**. When the owner ends, the value ends with it. For a local variable, that happens at the end of the block where it is declared, in the reverse order of the declarations.

A struct can say what to do when it ends, with a **destructor**: a method named `~` followed by the name of the struct. The program below uses one to show when each value ends:

```cromo
struct Log {
    string name

    Log(move string label) {
        name = move label
        println("open {}", name)
    }

    ~Log() {
        println("close {}", name)
    }
}

void inner() {
    Log b = Log("b")
    println("inner")
}

i32 main() {
    Log a = Log("a")
    inner()
    Log d = Log("d")
    println("end of main")
    ret 0
}
```

```output
open a
open b
inner
close b
open d
end of main
close d
close a
```

`b` ends when `inner` returns; `d` and `a` at the end of `main`, the last declared first. This is RAII, as in C++: a struct that owns a file closes it in its destructor, one that owns a connection closes the connection, and the code that uses them cannot forget it, even when an exception goes through. A `string` and a `list` free their memory in the same way.

## When a value ends

The rules are those of C++:

- A local variable ends at the end of the block where it is declared. The variables of the same block end in the reverse order of their declarations.
- A **temporary**, a value without a name such as the result of a call, ends at the end of the statement that contains it.
- `ret`, `break` and `continue` end everything that is alive in the blocks they leave, and so does an exception that goes through them.
- The fields of a struct end with it, after its destructor, the last declared first.
- An element of a list ends when it is removed, when a new value is assigned to it, or when the list ends.
- Assigning a new value to a variable ends the value that it held.
- A value that has been moved ends with its new owner, and only there.

A temporary lives until the end of its statement, long enough to be used in it. Each iteration of a loop is a block of its own, which `continue` and `break` leave. `Log` is the struct of the first example, with a method `label()` that gives its name:

```cromo
# struct Log {
#     string name
#
#     Log(move string label) {
#         name = move label
#         println("open {}", name)
#     }
#
#     ~Log() {
#         println("close {}", name)
#     }
#
#     string label() const {
#         ret name
#     }
# }
#
i32 main() {
    println("got {}", Log("temp").label())
    println("next statement")
    for (i32 i = 0; i < 3; i++) {
        Log a = Log(format("loop {}", i))
        if (i == 1) {
            continue
        }
        if (i == 2) {
            break
        }
        println("end of iteration {}", i)
    }
    println("after the loop")
    ret 0
}
```

```output
open temp
got temp
close temp
next statement
open loop 0
end of iteration 0
close loop 0
open loop 1
close loop 1
open loop 2
close loop 2
after the loop
```

A `ret` from deep inside a function ends the values of every block it leaves, the innermost first, and an exception does the same on its way to the `catch`:

```cromo
# struct Log {
#     string name
#
#     Log(move string label) {
#         name = move label
#         println("open {}", name)
#     }
#
#     ~Log() {
#         println("close {}", name)
#     }
# }
#
i64 find(list<i32> values, i32 wanted) {
    Log outer = Log("outer")
    for (i64 i = 0; i < values.len(); i++) {
        Log inner = Log("inner")
        if (values[i] == wanted) {
            ret i
        }
    }
    ret -1
}

void fail() {
    Log a = Log("fail")
    throw Error("broken")
}

i32 main() {
    println("found at {}", find([4, 7], 4))
    try {
        fail()
    } catch (Error e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
open outer
open inner
close inner
close outer
found at 0
open fail
close fail
caught: broken
```

The elements of a list are values like the others. Removing one ends it; assigning a new value to an element, or to a variable, ends the old value; and when the list ends, its elements end in their order, the first one first:

```cromo
struct Log {
    string name

    Log(move string label) {
        name = move label
    }

    ~Log() {
        println("close {}", name)
    }
}

i32 main() {
    list<Log> logs
    logs.add(Log("one"))
    logs.add(Log("two"))
    logs.add(Log("three"))
    logs.remove(1)
    println("removed")
    Log x = Log("x")
    x = Log("y")
    println("replaced")
    logs[0] = Log("uno")
    println("end")
    ret 0
}
```

```output
close two
removed
close x
replaced
close one
end
close y
close uno
close three
```

At the end of `main`, `x` ends before `logs`, because it was declared after it.

A destructor is never called by hand: the compiler calls it, exactly once for each value.

## Simple values are copied

Numbers, `bool` and the structs made only of them, without a destructor of their own, are **simple** types: assigning, passing and returning them copies their bytes, as in C.

```cromo
struct Point {
    f64 x
    f64 y
}

i32 main() {
    Point p
    p.x = 1.5
    Point q = p
    q.x = 9.0
    println("{} {}", p.x, q.x)
    ret 0
}
```

```output
1.5 9
```

## Values that own resources: copied or moved

A `string`, a `list`, a `map` and the structs that contain them, or that have a destructor, own something: memory on the heap, a file, a connection. As in C++, assigning one to another variable copies it, and both variables stay usable:

```cromo
i32 main() {
    string first = "hello"
    string second = first
    second += "!"
    println("{} {}", first, second)
    ret 0
}
```

```output
hello hello!
```

The copy is made by the method `copy()`, which can also be written, `first.copy()`, to make it visible. A copy duplicates what the value owns, so it costs; when the old variable is no longer needed, `move` gives the value to the new owner without copying anything, and the old variable can no longer be used:

```cromo error
i32 main() {
    string first = "hello"
    string second = move first
    println("{}", first)
    ret 0
}
```

```output
error: 'first' has been moved and can no longer be used
```

### Where a value is copied

A value that owns resources is copied wherever a variable or a field gives it without `move`. In the program below, `Tracked` is a struct that prints a line each time it is copied (how a struct writes its own `copy()` is shown in [Structs](structs.md)):

```cromo
# struct Tracked {
#     string name
#
#     ~Tracked() {
#     }
#
#     Tracked copy() const {
#         println("  copy of {}", name)
#         Tracked other
#         other.name = name
#         ret other
#     }
# }
#
struct Holder {
    Tracked item

    Tracked get() const {
        ret item
    }
}

Tracked make(string name) {
    Tracked t
    t.name = name
    ret t
}

void keep(move Tracked t) {
    println("  keeping {}", t.name)
}

i32 main() {
    println("a variable:")
    Tracked a = make("a")
    println("assigned:")
    Tracked b = a
    println("moved:")
    Tracked c = move a
    println("to a move parameter:")
    keep(b)
    keep(move b)
    println("a temporary:")
    keep(make("t"))
    println("a field returned:")
    Holder h
    h.item = move c
    Tracked d = h.get()
    println("in a list:")
    list<Tracked> all
    all.add(d)
    all.add(move d)
    ret 0
}
```

```output
a variable:
assigned:
  copy of a
moved:
to a move parameter:
  copy of a
  keeping a
  keeping a
a temporary:
  keeping t
a field returned:
  copy of a
in a list:
  copy of a
```

A value is **copied**:

- in an initialization or an assignment from a variable or a field: `Tracked b = a`, `b = a`;
- when a variable is given to a `move` parameter without `move`, as `keep(b)` or `all.add(d)`: the function receives its own copy, and the variable keeps its value;
- by `ret` of a field or of a read parameter, which belong to someone else.

A value is **not copied**:

- when it is given with `move`;
- when it is a temporary, as the result of `make("t")`;
- by `ret` of a local variable, as in `make`;
- when it is passed to a read or a `ref` parameter: the function works on the original.

### Values that cannot be copied

A struct with a destructor of its own is not copied, unless it writes a `copy()` method: the compiler cannot know how to duplicate what the destructor releases, as a file. Such a value can only be moved:

```cromo error
struct Handle {
    i32 fd = -1

    ~Handle() {
        println("closing {}", fd)
    }
}

i32 main() {
    Handle a
    Handle b = a
    ret 0
}
```

```output
error: values of type Handle cannot be copied
write 'move a'
```

A struct that contains a field that cannot be copied cannot be copied either.

### Temporaries and returned values

A value without a name, as the result of a call or a literal, is moved without writing `move`. So is a local variable returned with `ret`:

```cromo
list<string> names() {
    list<string> result = ["Ada", "Grace"]
    ret result
}

i32 main() {
    list<string> all = names()
    all.add("Alan")
    println("{}", all.len())
    ret 0
}
```

```output
3
```

Nothing is copied here: the list built in `names` becomes `all`. Writing `move` before a new value is an error, since it would say nothing:

```cromo error
string greeting() {
    ret "hello"
}

i32 main() {
    string s = move greeting()
    ret 0
}
```

```output
error: 'move' is not needed before a new object: it is moved anyway
```

### Moving a simple value

`move` on a simple value copies it, and the variable stays usable: nobody gives up anything. For the same reason, `move` can be left out when a simple value is given to a `move` parameter:

```cromo
void show(move i32 n) {
    println("{}", n)
}

i32 main() {
    i32 n = 5
    show(move n)
    show(n)
    i32 m = move n
    println("{} {}", n, m)
    ret 0
}
```

```output
5
5
5 5
```

So a function with a `move` parameter works in the same way for an `i32` and for a `string`, which matters for generics ([Interfaces and generics](interfaces-generics.md)).

## After a move

A variable that has been moved can no longer be used. The compiler follows every path of the function: a variable moved only in one branch of an `if` cannot be used after the `if` either.

```cromo error
i32 main(list<string> args) {
    string name = "Ada"
    list<string> names
    if (args.len() > 1) {
        names.add(move name)
    }
    println("{}", name)
    ret 0
}
```

```output
error: 'name' may have been moved, on another path or in a previous iteration of a loop
```

Assigning a new value makes the variable usable again:

```cromo
i32 main() {
    list<string> lines
    string current = "first"
    lines.add(move current)
    current = "second"
    lines.add(move current)
    println("{:?}", lines)
    ret 0
}
```

```output
["first", "second"]
```

In a loop, a variable declared outside the loop and moved inside it must receive a new value before the next iteration, since the next iteration would use it again:

```cromo error
i32 main() {
    list<string> lines
    string current = "line"
    for (i32 i = 0; i < 3; i++) {
        lines.add(move current)
    }
    ret 0
}
```

```output
error: 'current' may have been moved, on another path or in a previous iteration of a loop
```

Writing `current = ""` after the `add`, at the end of the body, makes the loop correct.

A value moved on one path only is destroyed by whoever owns it at the end: the function that took it, or the variable, if it was not moved. It is never destroyed twice, and never forgotten:

```cromo
# struct Log {
#     string name
#
#     Log(move string label) {
#         name = move label
#     }
#
#     ~Log() {
#         println("close {}", name)
#     }
# }
#
void consume(move Log log) {
    println("consume {}", log.name)
}

void maybe(bool give) {
    Log log = Log(format("give={}", give))
    if (give) {
        consume(move log)
    }
    println("end of maybe")
}

i32 main() {
    maybe(true)
    maybe(false)
    ret 0
}
```

```output
consume give=true
close give=true
end of maybe
end of maybe
close give=false
```

### Moving a field, an element or a `ref` parameter

A whole local variable cannot be used after a move. Moving a field, an element of a list or a `ref` parameter instead takes the value and leaves the **default value of its type** in its place, so the struct, the list and the caller's variable stay usable:

```cromo
struct Settings {
    string host = "localhost"
    list<string> paths = ["/", "/health"]
}

void take(ref string s) {
    string old = move s
    println("took [{}]", old)
}

i32 main() {
    Settings s
    list<string> paths = move s.paths
    string host = move s.host
    println("{:?} [{}] {:?} [{}]", paths, host, s.paths, s.host)
    Settings t
    take(ref t.host)
    println("[{}]", t.host)
    list<string> words = ["a", "b"]
    string w = move words[0]
    println("{:?} {}", words, w)
    ret 0
}
```

```output
["/", "/health"] [localhost] [] []
took [localhost]
[]
["", "b"] a
```

The field left behind has the default of its type, an empty string or an empty list, not the initial value that the struct declares for it: a move computes nothing and allocates nothing. A struct must therefore accept the default value of its fields, and its destructor must handle it; [Structs](structs.md) shows how.

### Where `move` can be written

`move` is written before a variable, a field or an element, and only where a value changes owner:

- in an initialization, `string t = move s`;
- in an assignment, `t = move s`;
- as the argument of a `move` parameter, `names.add(move s)`;
- after `ret`, `ret move s.text`.

Anywhere else it is an error, because nothing would take the value:

```cromo error
i32 main() {
    string s = "hello"
    println("{}", move s)
    ret 0
}
```

```output
error: 'move' is written only in an initialization, an assignment, the argument of a 'move' parameter or after 'ret'
```

What belongs to someone else cannot be moved: a read parameter, a field of a read parameter, the element of a `for` that reads a list. A read parameter is copied with `ret` or with an assignment, never moved:

```cromo error
void keep(string s) {
    list<string> all
    all.add(move s)
}

i32 main() {
    keep("a")
    ret 0
}
```

```output
error: parameter 's' is read-only: it belongs to the caller and cannot be moved
```

## Passing values to functions

A parameter is passed in one of three ways, and **the call repeats the word of the declaration**, so that reading a call is enough to know what happens to its arguments:

| Mode | Declaration | Call | The function |
|---|---|---|---|
| read | `list<i32> xs` | `f(numbers)` | reads the value; the caller keeps it |
| modify | `ref list<i32> xs` | `f(ref numbers)` | modifies the caller's value |
| take | `move list<i32> xs` | `f(move numbers)` | becomes the owner; the caller loses it |

### Read

A parameter without a word is read-only. The function cannot modify it, and it can call on it only the `const` methods:

```cromo error
void addOne(list<i32> numbers) {
    numbers.add(1)
}

i32 main() {
    list<i32> values
    addOne(values)
    ret 0
}
```

```output
error: cannot call 'add', which is not const
parameter 'numbers' cannot be modified: parameters are read-only
```

A read parameter is not copied: a large value is passed by its address, a small one, up to 16 bytes, by copy, because that is faster. The compiler chooses, and it can, because the value cannot change during the call. In C++ terms, a read parameter is a `const T&` that you never have to write; for this reason `const` is not written on a read parameter, and `ref const` does not exist.

A read parameter belongs to the caller: to keep its value, the function copies it, with an assignment or with `ret`.

### Modify: `ref`

With `ref`, the function works on the caller's value. The call writes `ref` too:

```cromo
void addOne(ref list<i32> numbers) {
    numbers.add(1)
}

i32 main() {
    list<i32> values
    addOne(ref values)
    addOne(ref values)
    println("{}", values.len())
    ret 0
}
```

```output
2
```

Forgetting the `ref` in the call is an error, not a silent copy:

```cromo error
void addOne(ref list<i32> numbers) {
    numbers.add(1)
}

i32 main() {
    list<i32> values
    addOne(values)
    ret 0
}
```

```output
error: parameter 'numbers' is 'ref': write 'ref' before the argument
```

`ref` is also how a function gives more than one result:

```cromo
bool find(list<string> items, string wanted, ref i64 position) {
    for (i64 i = 0; i < items.len(); i++) {
        if (items[i] == wanted) {
            position = i
            ret true
        }
    }
    ret false
}

i32 main() {
    list<string> planets = ["Mercury", "Venus", "Earth"]
    i64 at
    if (find(planets, "Earth", ref at)) {
        println("found at {}", at)
    }
    ret 0
}
```

```output
found at 2
```

The argument of a `ref` parameter must be something that the function can modify in place:

- a variable, a field or an element of a list, as `ref at`, `ref p.x`, `ref items[0]`; a temporary or an expression cannot be passed with `ref`;
- that can be modified: not a `const` variable and not a read parameter;
- with exactly the type of the parameter. An `i32` is not passed to a `ref i64`, because the function would write eight bytes into four:

```cromo error
void next(ref i64 n) {
    n++
}

i32 main() {
    i32 small = 1
    next(ref small)
    ret 0
}
```

```output
error: parameter 'n' is 'ref i64': the argument must have exactly that type, found i32
```

### Take: `move`

A `move` parameter receives the ownership of the value. The function can keep it, for example in a list, without copying it:

```cromo
struct Library {
    list<string> titles

    void add(move string title) {
        titles.add(move title)
    }
}

i32 main() {
    Library library
    string book = "The C Programming Language"
    library.add(move book)
    library.add("Structure and Interpretation of Computer Programs")
    println("{} books", library.titles.len())
    ret 0
}
```

```output
2 books
```

The second call passes a temporary, which needs no `move`. A variable passed without `move` is copied, as shown above, and the function receives the copy: `library.add(book)` would leave `book` usable.

Inside the function, a `move` parameter is a local variable like the others: it can be modified, moved, and it ends at the end of the function, unless it was moved.

### `move const`

A `move` parameter can also be `const`, with the mode first: the function becomes the owner of the value but does not modify it. It can still move it, as any `const` variable:

```cromo
struct Registry {
    list<string> names

    void keep(move const string name) {
        println("keeping {} ({} letters)", name, name.len())
        names.add(move name)
    }
}

i32 main() {
    Registry r
    string a = "Ada"
    r.keep(move a)
    r.keep("Grace")
    println("{:?}", r.names)
    ret 0
}
```

```output
keeping Ada (3 letters)
keeping Grace (5 letters)
["Ada", "Grace"]
```

On the other parameters `const` makes no sense: a read parameter is already read-only, and `ref` exists to modify.

```cromo error
void show(const string name) {
    println("{}", name)
}

i32 main() {
    show("x")
    ret 0
}
```

```output
error: only a 'move' parameter can be const, written 'move const'
```

## References live only during a call

`ref` exists only for parameters. There are no references in variables, in fields or in returned values: a reference cannot outlive the object it refers to, because it ends with the call. This single rule replaces the lifetimes and the borrow checker of other languages.

```cromo error
struct Cursor {
    ref string text
}

i32 main() {
    ret 0
}
```

```output
error: expected a type, found 'ref'
```

When a program needs to point to another object, as in a graph, it keeps an index into a list instead of a pointer.

## Exclusivity

During a call, an object passed with `ref` cannot be reached through any other argument of the same call. Otherwise a function could see a value change under its feet:

```cromo error
void swap(ref i32 a, ref i32 b) {
    i32 t = a
    a = b
    b = t
}

i32 main() {
    i32 x = 1
    swap(ref x, ref x)
    ret 0
}
```

```output
error: 'x' overlaps 'x' in the same call, and the call can modify both
```

Two arguments overlap when they are the same object, or when one is a part of the other: `ref p` and `ref p.x`, a list and one of its elements. When the compiler cannot know, as in `swap(ref v[i], ref v[j])`, the program checks it while it runs, and a call with `i` equal to `j` stops with a panic: the same element is passed twice to a call that modifies it.

The object of a method that is not `const` counts as an argument passed with `ref`, because the method can modify it. A read argument that overlaps it is accepted when it is a simple value: it is copied, and the method sees the value it had at the moment of the call.

```cromo
struct Counter {
    i64 value
    i64 step = 2

    void add(i64 amount) {
        value += amount
        step = 100
    }
}

i32 main() {
    Counter c
    c.add(c.step)
    println("{} {}", c.value, c.step)
    ret 0
}
```

```output
2 100
```

A value that owns resources is not copied to make the call work: the overlap is an error.

```cromo error
struct Box {
    string label = "box"

    void rename(string name) {
        label = name + "!"
    }
}

i32 main() {
    Box b
    b.rename(b.label)
    ret 0
}
```

```output
error: 'b.label' overlaps 'b' in the same call, which the call can modify
```

Passing a copy, `b.rename(b.label.copy())`, says that the copy is wanted, and compiles.

The same rule holds for a `for` that goes over a list: while it runs, the list cannot be modified, not even through another name:

```cromo error
i32 main() {
    list<i64> numbers = [1, 2, 3]
    for (i64 n in numbers) {
        numbers.add(n)
    }
    ret 0
}
```

```output
error: 'numbers' cannot be modified while a 'for' goes over the elements of 'numbers'
```

Exclusivity also allows the compiler to tell LLVM that two `ref` parameters never overlap, which makes the code faster, as `restrict` in C.

## const

A `const` variable cannot be modified, and `const` is deep: the strings and lists inside a `const` value are `const` too, and only their `const` methods can be called. A `const` variable cannot be passed with `ref`, but it can be copied and it can be moved: moving does not change the value, it only gives it to another owner.

```cromo
i32 main() {
    const list<string> fixed = ["a", "b"]
    list<string> more = fixed
    more.add("c")
    list<string> taken = move fixed
    println("{:?} {:?}", more, taken)
    ret 0
}
```

```output
["a", "b", "c"] ["a", "b"]
```

```cromo error
i32 main() {
    const list<string> fixed = ["a", "b"]
    fixed.add("c")
    ret 0
}
```

```output
error: cannot call 'add', which is not const
'fixed' is const and cannot be modified
```

A method marked `const` does not modify its object, as [Structs](structs.md) shows: only `const` methods can be called on a read parameter, on a `const` variable and on the element of a `for` that reads a list.

## Summary

- Every value has one owner, and ends with it: at the end of its block, at the end of the statement for a temporary, when `ret`, `break`, `continue` or an exception leave its block. Destructors release the resources, exactly once.
- Simple values are copied. Values that own resources are copied with `copy()` where they are given without `move`; `move` gives them to another owner without copying, and temporaries and `ret` of a local variable move by themselves.
- A struct with a destructor and no `copy()` cannot be copied, only moved.
- A moved variable cannot be used, on any path, until it receives a new value. A moved field, element or `ref` parameter is left with the default value of its type.
- `move` is written only in an initialization, an assignment, the argument of a `move` parameter and after `ret`; a read parameter cannot be moved.
- A parameter is read, modified with `ref`, or taken with `move`, and the call says which; `move const` takes a value without modifying it.
- `ref` is written on a variable, a field or an element that can be modified, with exactly the type of the parameter.
- References exist only during a call: no lifetimes to write, no dangling pointers.
- Two arguments of a call that modifies them are never the same object, nor one part of the other.
