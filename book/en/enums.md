# Enums and switch

An enum is a type whose value is one of a list of cases. In Cromo a case can carry data, so an enum can say not only *which* case a value is, but also *what* comes with it. Together with `switch`, which the compiler checks against every case, enums are the safest way to represent alternatives.

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
    ret 0
}
```

```output
South true
```

A value is written with the name of the enum, `Direction.North`; inside a `switch` on the enum, the case is enough. The default value of an enum is its first case.

The cases of an enum without data can have a value, which `i32(...)` reads and `Color(n)` turns back into a case:

```cromo
enum Color {
    Red = 1
    Green = 2
    Blue = 4
}

i32 main() {
    Color c = Color.Green
    println("{} {:?}", i32(c), Color(4))
    ret 0
}
```

```output
2 Blue
```

`Color(3)` would be a panic, since no case has the value 3.

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
    ret 0
}
```

```output
 Circle(1.0) 3.14
Rect(2.0, 3.0) 6.00
       Empty 0.00
```

In `case Circle(f64 r)` the data of the case get a name, read-only and without a copy. A case can ignore its data by leaving out the parentheses.

## switch covers every case

On an enum, `switch` must handle every case, or have a `default`. Forgetting one is a compile error, not a bug found later:

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

When a case is added to an enum, the compiler lists every `switch` that must learn about it.

A `switch` has no fall-through: each case runs its own block and the `switch` ends. Several cases without data can share a block: `case North, South { ... }`. A `break` inside a case is an error, because in C it would leave the `switch`, while in Cromo it would leave the loop around it.

## Modifying the data of a case

With `ref`, a case gives its data for modification:

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
    println("{:?}", s)
    ret 0
}
```

```output
Circle(3.0)
```

## Methods, and generic enums

An enum can have methods, as a struct. Inside a method, `switch` without a value works on the enum itself:

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
}

i32 main() {
    list<Token> tokens = [Token.Number(42), Token.Word("hello"), Token.End]
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

An enum can also have type parameters, as the generic structs of chapter 9. This one holds either a result or the reason of a failure:

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

## Summary

- An enum lists cases; a case can carry data, written as parameters.
- `switch` on an enum must cover every case or have `default`, and the compiler checks it.
- `case Circle(f64 r)` names the data of a case; with `ref` they can be modified.
- Enums have methods and type parameters, as structs; the default value is the first case.
