# Enum e switch

Un enum è un tipo il cui valore è uno tra i casi di un elenco. In Cromo un caso può portare con sé dei dati, quindi un enum può dire non solo *quale* caso è un valore, ma anche *che cosa* lo accompagna. Insieme a `switch`, che il compilatore controlla rispetto a ogni caso, gli enum sono il modo più sicuro di rappresentare delle alternative.

## Casi senza dati

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

Un valore si scrive con il nome dell'enum, `Direction.North`; dentro uno `switch` sull'enum basta il nome del caso. Il valore predefinito di un enum è il suo primo caso.

I casi di un enum senza dati possono avere un valore, che `i32(...)` legge e `Color(n)` riconverte in un caso:

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

`Color(3)` provocherebbe un panic, perché nessun caso ha il valore 3.

## Casi con dati

Un caso può portare con sé dei valori, scritti come i parametri di una funzione:

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

In `case Circle(f64 r)` i dati del caso ricevono un nome, in sola lettura e senza copia. Per ignorare i dati di un caso basta omettere le parentesi.

## switch copre ogni caso

Su un enum, `switch` deve gestire ogni caso, oppure avere un `default`. Dimenticarne uno è un errore di compilazione, non un bug da scoprire più tardi:

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

Quando si aggiunge un caso a un enum, il compilatore elenca tutti gli `switch` che devono tenerne conto.

Uno `switch` non ha fall-through: ogni case esegue il proprio blocco e lo `switch` termina. Più casi senza dati possono condividere un blocco: `case North, South { ... }`. Un `break` dentro un case è un errore, perché in C uscirebbe dallo `switch`, mentre in Cromo uscirebbe dal ciclo che lo racchiude.

## Modificare i dati di un caso

Con `ref`, i dati di un caso si possono modificare:

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

## Metodi ed enum generici

Un enum può avere dei metodi, come una struct. Dentro un metodo, `switch` senza valore lavora sull'enum stesso:

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

Un enum può avere anche dei parametri di tipo, come le struct generiche del capitolo 9. Questo contiene un risultato oppure il motivo di un fallimento:

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

## Riepilogo

- Un enum elenca dei casi; un caso può portare con sé dei dati, scritti come parametri.
- `switch` su un enum deve coprire ogni caso o avere `default`, e il compilatore lo controlla.
- `case Circle(f64 r)` dà un nome ai dati di un caso; con `ref` si possono modificare.
- Gli enum hanno metodi e parametri di tipo, come le struct; il valore predefinito è il primo caso.
