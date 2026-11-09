# Enums and switch

An enum is a type whose value is one of a list of cases. In Cromo a case can carry data, so an enum can say not only *which* case a value is, but also *what* comes with it. Together with `switch`, which the compiler checks against every case, enums are the safest way to represent alternatives. `switch` also chooses among integers, characters and strings, as in C, without the traps of C.

## Cases without data

```cromo
enum Direction {
    North
    East
    South
    West
}

Direction turnRight(Direction d) {
    switch (d) {
        case North {
            ret Direction.East
        }
        case East {
            ret Direction.South
        }
        case South {
            ret Direction.West
        }
        case West {
            ret Direction.North
        }
    }
}

i32 main() {
    Direction d = Direction.North
    d = turnRight(turnRight(d))
    println("{:?} {}", d, d == Direction.South)
    Direction first
    println("{:?}", first)
    ret 0
}
```

```output
South true
North
```

The cases are written one per line, and an enum has at least one. A value is written with the name of the enum, `Direction.North`: two enums can have cases with the same name, and they are never confused. Inside a `switch` on the enum the type is known, so the case alone is enough; elsewhere `North` alone is an unknown name.

The default value of an enum is its first case: `first` above is `North`.

`==` and `!=` compare two values of an enum whose cases have no data. `<` and `>` do not work on enums; to order them, compare their values, `i32(a) < i32(b)`, described in the next section.

`{:?}` prints the name of the case. `{}` needs a method `string text() const`, as for structs: the section on methods below shows one.

## The values of the cases

Each case of an enum without data has a value, an `i32`: its position, from 0, or the one written after `=`. `i32(...)` reads it, and `Color(n)` turns a number back into a case:

```cromo
enum Color {
    Red = 1
    Green = 2
    Blue = 4
}

enum Level {
    Low
    Medium
    High
}

i32 main() {
    Color c = Color.Green
    println("{} {:?}", i32(c), Color(4))
    println("{} {}", i64(Level.High), u8(Level.Medium))
    ret 0
}
```

```output
2 Blue
2 1
```

- The value of a case converts to any integer type, as `i64(...)` and `u8(...)`, with the rules of the explicit conversions between integers. It is never converted by itself: `i32 x = Color.Green` is an error.
- `Color(n)` takes any integer, and is a panic, *the value is not a case of Color*, when no case has the value `n`.
- The values are integer literals that fit in an `i32`, also negative, and all different. Either every case has one, or none.

```cromo error
enum Color {
    Red = 1
    Green
    Blue = 1
}

i32 main() {
    ret 0
}
```

```output
error: give a value to every case of Color, or to none
error: the value 1 is already the one of Red
```

The values are useful where a number comes from outside, as a field of a file format or a constant of a C library ([Calling C](c-interop.md)). Only the cases of an enum without data can have a value.

## Cases with data

A case can carry values, written as the parameters of a function:

```cromo
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
    Empty
}

f64 area(Shape shape) {
    switch (shape) {
        case Circle(f64 r) {
            ret 3.14159 * r * r
        }
        case Rect(f64 w, f64 h) {
            ret w * h
        }
        case Empty {
            ret 0.0
        }
    }
}

i32 main() {
    list<Shape> shapes = [Shape.Circle(1.0), Shape.Rect(2.0, 3.0), Shape.Empty]
    for (Shape s in shapes) {
        println("{:>12} {:.2f}", format("{:?}", s), area(s))
    }
    Shape none
    println("{:?}", none)
    ret 0
}
```

```output
 Circle(1.0) 3.14
Rect(2.0, 3.0) 6.00
       Empty 0.00
Circle(0.0)
```

A case with data is made like a call, `Shape.Circle(1.0)`, with all its data; `Shape.Circle` alone is an error. The default value is still the first case, with the default values of its data: `Circle(0.0)`. `{:?}` prints the case with its data.

## switch on an enum

On an enum, `switch` has a branch for each case, `case Name { ... }`, with mandatory braces and no colon. The value of the `switch` is written in parentheses, and can be any expression: a variable, an element of a list, the result of a call.

### Naming the data of a case

In `case Circle(f64 r)` the data of the case get a name, read-only and without a copy, as the element of a `for`. Each name is written with the type of the data, exactly, and a case names all its data or none: without the parentheses the data are ignored.

```cromo error
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
}

f64 measure(Shape s) {
    switch (s) {
        case Circle(f32 r) {
            ret f64(r)
        }
        case Rect(f64 w) {
            ret w
        }
    }
}

i32 main() {
    ret 0
}
```

```output
error: the data 'radius' of Circle is f64: write f64 r
error: case 'Rect' has 2 data, but 1 name was given
```

### Every case is covered

On an enum, `switch` must handle every case, or have a `default`, which takes the cases not listed. Forgetting one is a compile error, not a bug found later:

```cromo error
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
}

f64 area(Shape s) {
    switch (s) {
        case Circle(f64 r) {
            ret 3.14159 * r * r
        }
    }
}

i32 main() {
    ret 0
}
```

```output
error: 'switch' does not cover Rect of Shape: add the cases, or 'default'
```

When a case is added to an enum, the compiler lists every `switch` that must learn about it, unless it has a `default`. So `default` is convenient, but it gives up that help: on an enum that will grow, prefer to list the cases. A case written twice in the same `switch` is an error.

### Several cases in one branch

A `switch` has no fall-through: each branch runs its own block and the `switch` ends. Several cases can share a branch, separated by commas, when the branch does not name their data:

```cromo
enum Direction {
    North
    East
    South
    West
}

enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
    Empty
}

i32 main() {
    list<Direction> moves = [Direction.North, Direction.East, Direction.South]
    for (Direction d in moves) {
        switch (d) {
            case North, South {
                println("vertical")
            }
            default {
                println("horizontal")
            }
        }
    }
    Shape s = Shape.Rect(2.0, 3.0)
    switch (s) {
        case Circle, Rect {
            println("a shape with a size")
        }
        case Empty {
        }
    }
    ret 0
}
```

```output
vertical
horizontal
vertical
a shape with a size
```

`case Circle(f64 r), Rect { ... }` is an error: in a branch with several cases there are no data to name. An empty branch, as `case Empty { }`, does nothing.

### Modifying the data of a case

With `ref`, a case gives its data for modification, when the value of the `switch` can be modified: a local variable, a `ref` parameter, an element of a list.

```cromo
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
}

void grow(ref Shape s) {
    switch (s) {
        case Circle(ref f64 r) {
            r *= 2.0
        }
        case Rect(ref f64 w, ref f64 h) {
            w *= 2.0
            h *= 2.0
        }
    }
}

i32 main() {
    Shape s = Shape.Circle(1.5)
    grow(ref s)
    list<Shape> shapes = [Shape.Rect(1.0, 2.0)]
    grow(ref shapes[0])
    println("{:?} {:?}", s, shapes)
    ret 0
}
```

```output
Circle(3.0) [Rect(2.0, 4.0)]
```

A name without `ref` is read-only, and on a read parameter `ref` is not allowed:

```cromo error
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
}

void grow(Shape s) {
    switch (s) {
        case Circle(ref f64 r) {
            r *= 2.0
        }
        case Rect(f64 w, f64 h) {
            w = 3.0
        }
    }
}

i32 main() {
    ret 0
}
```

```output
error: cannot modify the data with 'ref': parameter 's' cannot be modified: parameters are read-only
error: 'w' is data of the case, read-only: write 'ref f64 w' in the case to modify it
```

### The value of a switch while a branch uses its data

While a branch has names for the data of the case, the value of the `switch` cannot be modified, except through the names given with `ref`: the names point inside the value, and giving it another case would leave them pointing at nothing. It is the same rule as for a list during a `for` over it.

```cromo error
enum Shape {
    Circle(f64 radius)
    Rect(f64 width, f64 height)
}

i32 main() {
    Shape s = Shape.Circle(1.0)
    switch (s) {
        case Circle(f64 r) {
            s = Shape.Rect(r, r)
        }
        case Rect {
        }
    }
    ret 0
}
```

```output
error: 's' cannot be modified while a 'switch' uses the data of its case
```

A branch that does not name the data can give the value a new case:

```cromo
enum Light {
    Off
    On(i32 level)
}

void toggle(ref Light l) {
    switch (l) {
        case Off {
            l = Light.On(100)
        }
        case On {
            l = Light.Off
        }
    }
}

i32 main() {
    Light l
    toggle(ref l)
    println("{:?}", l)
    toggle(ref l)
    println("{:?}", l)
    ret 0
}
```

```output
On(100)
Off
```

## switch on integers, characters and strings

Besides enums, `switch` works on the integer types, `char` included, and on `string`. The cases are literals, and a case can list several:

```cromo
string kind(char c) {
    switch (c) {
        case 'a', 'e', 'i', 'o', 'u' {
            ret "vowel"
        }
        case ' ', '\n' {
            ret "space"
        }
        default {
            ret "other"
        }
    }
}

void answer(string reply) {
    switch (reply) {
        case "yes", "y" {
            println("{}: confirmed", reply)
        }
        case "no" {
            println("{}: cancelled", reply)
        }
    }
}

i32 main() {
    println("{} {} {}", kind('e'), kind(' '), kind('x'))
    answer("y")
    answer("maybe")
    answer("no")
    i64 code = -1
    switch (code) {
        case -1 {
            println("not found")
        }
        case 0x10 {
            println("sixteen")
        }
    }
    ret 0
}
```

```output
vowel space other
y: confirmed
no: cancelled
not found
```

- On numbers and strings `default` is optional: without it, a value that matches no case does nothing, as in C. Here `answer("maybe")` prints nothing.
- The cases are literals of the type of the value: numbers, also negative or in hexadecimal, characters (`'a'` is a number, so it is a case also on an `i64`) and strings. A variable or a constant is not a case; for those, use `if`.
- The same value cannot appear twice, and a value that does not fit in the type is an error.
- Other types, as `f64` and `bool`, cannot be the value of a `switch`.

```cromo error
i32 main() {
    u8 b = 7
    i32 limit = 5
    switch (b) {
        case 1, 2 {
        }
        case 2 {
        }
        case 300 {
        }
        case limit {
        }
    }
    f64 x = 1.0
    switch (x) {
        case 1 {
        }
    }
    ret 0
}
```

```output
error: this value is already in this 'switch'
error: value 300 is out of range for u8 (0 .. 255)
error: the cases of a switch on a u8 are literals
error: 'switch' works on enums, integers and strings, found f64
```

## break and continue in a switch

A branch ends by itself, so `break` and `continue` cannot be written in it, also inside an `if` of the branch. In C a `break` closes a case; in Cromo it would leave the loop around the `switch`, and the compiler stops the C habit before it becomes a bug:

```cromo error
i32 main() {
    for (i32 i = 0; i < 3; i++) {
        switch (i) {
            case 1 {
                break
            }
            case 2 {
                continue
            }
        }
    }
    ret 0
}
```

```output
error: 'break' cannot be used in a 'switch': a branch ends by itself
error: 'continue' cannot be used in a 'switch': a branch ends by itself
```

A loop written inside a branch can use `break` and `continue` for itself. To leave the loop around the `switch`, the branch sets a variable, and the loop looks at it after the `switch`:

```cromo
i32 main() {
    list<string> commands = ["add", "add", "skip", "quit", "add"]
    i64 total = 0
    bool done = false
    for (string c in commands) {
        switch (c) {
            case "add" {
                total++
            }
            case "quit" {
                done = true
            }
        }
        if (done) {
            break
        }
    }
    println("total {}", total)
    ret 0
}
```

```output
total 2
```

`ret` can leave a branch, as in `turnRight` above, and so can `throw`.

## A switch that ends a function

A function that returns a value must end with `ret` on every path. A `switch` ends the paths when all its branches end them and it covers every value: on an enum, with all the cases or `default`; on numbers and strings, only with `default`.

```cromo error
string name(i32 n) {
    switch (n) {
        case 1 {
            ret "one"
        }
        case 2 {
            ret "two"
        }
    }
}

i32 main() {
    ret 0
}
```

```output
error: function 'name' must end with 'ret' on every path: it returns string
```

Add a `default`, or, if another value means that the program is wrong, a `panic` after the `switch` ([Errors](errors.md)).

## Methods

An enum can have methods, written after the cases, as a struct; `const` methods do not modify the value. Inside a method, `switch` without a value works on the enum itself:

```cromo
enum Token {
    Number(i64 value)
    Word(string text)
    End

    string describe() const {
        switch {
            case Number(i64 n) {
                ret format("the number {}", n)
            }
            case Word(string w) {
                ret format("the word '{}'", w)
            }
            case End {
                ret "the end"
            }
        }
    }

    void double() {
        switch {
            case Number(ref i64 n) {
                n *= 2
            }
            default {
            }
        }
    }
}

i32 main() {
    list<Token> tokens = [Token.Number(21), Token.Word("hello"), Token.End]
    tokens[0].double()
    for (Token t in tokens) {
        println("{}", t.describe())
    }
    ret 0
}
```

```output
the number 42
the word 'hello'
the end
```

Inside a method the value itself has no name, as in the methods of a struct: there is no `this`, and the whole value cannot be passed to a function or replaced with another case. A method changes the data of its case through `ref`, as `double` does; to change the case, write a function with a `ref` parameter, as `toggle` above.

A method `string text() const` lets `{}` print the enum, while `{:?}` keeps printing the name of the case ([Formatting text](formatting.md)):

```cromo
enum Level {
    Low
    High

    string text() const {
        switch {
            case Low {
                ret "low"
            }
            case High {
                ret "high"
            }
        }
    }
}

i32 main() {
    Level l = Level.High
    println("level {} ({:?})", l, l)
    ret 0
}
```

```output
level high (High)
```

An enum has no constructor and no destructor written by hand: its values are made by its cases, and its data are destroyed by themselves.

```cromo error
enum Level {
    Low
    High

    Level() {
    }
}

i32 main() {
    ret 0
}
```

```output
error: an enum has no constructor or destructor
```

## Interfaces

An enum can implement interfaces, as a struct, by naming them after a colon ([Interfaces and generics](interfaces-generics.md)):

```cromo
interface Named {
    string name() const
}

enum Animal : Named {
    Dog
    Cat(string nickname)

    string name() const {
        switch {
            case Dog {
                ret "a dog"
            }
            case Cat(string n) {
                ret format("the cat {}", n)
            }
        }
    }
}

void introduce(Named x) {
    println("this is {}", x.name())
}

i32 main() {
    introduce(Animal.Dog)
    introduce(Animal.Cat("Tom"))
    ret 0
}
```

```output
this is a dog
this is the cat Tom
```

An enum that implements `Exception` can be thrown, with a case for each kind of error ([Errors](errors.md)).

## The data of a case and ownership

The data of a case are given as the arguments of a `move` parameter ([Ownership](ownership.md)): a new value; a variable, which is copied; or a variable with `move`, which gives its value without a copy. The enum becomes the owner of its data:

```cromo
enum Message {
    Text(string body)
    Quit
}

i32 main() {
    string name = "Ada"
    Message a = Message.Text(name)
    string greeting = "hello"
    Message b = Message.Text(move greeting)
    println("{:?} {:?} {}", a, b, name)
    ret 0
}
```

```output
Text("Ada") Text("hello") Ada
```

`name` was copied and stays usable; `greeting` was moved and can no longer be used.

An enum whose cases own resources, as a `string` or a struct with a destructor, destroys the data of its current case when it is destroyed, and when it is given another case:

```cromo
struct Resource {
    string name

    Resource(move string label) {
        name = move label
        println("open {}", name)
    }

    ~Resource() {
        println("close {}", name)
    }
}

enum Slot {
    Free
    Held(Resource r)
}

i32 main() {
    Slot s = Slot.Held(Resource("a"))
    s = Slot.Free
    s = Slot.Held(Resource("b"))
    println("end of main")
    ret 0
}
```

```output
open a
close a
open b
end of main
close b
```

Assigning an enum copies it, as a struct: its `copy()` is generated by the compiler, and copies the data of its case. When the data of one of its cases cannot be copied, as `Resource` above, which has a destructor and no `copy()`, the enum cannot be copied either, and is only moved:

```cromo error
struct Resource {
    string name

    ~Resource() {
    }
}

enum Slot {
    Free
    Held(Resource r)
}

i32 main() {
    Slot s = Slot.Held(Resource())
    Slot t = s
    ret 0
}
```

```output
error: values of type Slot cannot be copied
```

`Slot t = move s` is the way.

## Generic enums

An enum can also have type parameters, as the generic structs of [Interfaces and generics](interfaces-generics.md). This one holds either a result or the reason of a failure:

```cromo
enum Outcome<T> {
    Done(T value)
    Failed(string reason)
}

Outcome<i64> half(i64 n) {
    if (n % 2 != 0) {
        ret Outcome<i64>.Failed(format("{} is odd", n))
    }
    ret Outcome<i64>.Done(n / 2)
}

i32 main() {
    list<i64> inputs = [10, 7]
    for (i64 n in inputs) {
        switch (half(n)) {
            case Done(i64 v) {
                println("half of {} is {}", n, v)
            }
            case Failed(string why) {
                println("cannot: {}", why)
            }
        }
    }
    ret 0
}
```

```output
half of 10 is 5
cannot: 7 is odd
```

A value is made with the type arguments written, `Outcome<i64>.Done(5)`: `Outcome.Done(5)` is an error, also where the type is known.

## Summary

- An enum lists cases, one per line; a value is written `Enum.Case`, and the default value is the first case, with the default values of its data.
- The cases without data have an `i32` value, their position or the one written with `=`: `i32(c)` reads it, `Color(n)` gives the case back, or panics.
- `==` and `!=` compare the enums without data; the cases with data are told apart with `switch`.
- A case can carry data, written as parameters and given as `move` arguments: copied, or moved with `move`. The enum owns them, and destroys them when it is destroyed or changes case.
- `switch` on an enum must cover every case or have `default`; there is no fall-through, and several cases can share a branch.
- `case Circle(f64 r)` names the data, read-only; with `ref` they can be modified. While a branch names the data, the value of the `switch` cannot be modified in other ways.
- `switch` also works on integers, characters and strings, with literals as cases; there `default` is optional.
- `break` and `continue` cannot be written in a branch of a `switch`.
- Enums have methods, `switch` without a value inside them, interfaces and type parameters; they have no constructor or destructor written by hand.
