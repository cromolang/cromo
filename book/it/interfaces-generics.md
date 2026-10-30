# Interfacce e generici

Spesso tipi diversi condividono un comportamento: un cerchio e un quadrato hanno entrambi un'area. In Cromo il comportamento è descritto da un'**interfaccia**, e il codice che funziona con qualsiasi tipo che lo possiede si scrive una volta sola: come funzione che prende l'interfaccia, risolta mentre il programma è in esecuzione, oppure come **generico**, risolto dal compilatore. Non c'è ereditarietà tra struct.

## Interfacce

Un'interfaccia elenca dei metodi senza corpo. Una struct la implementa indicandone il nome dopo i due punti e scrivendo quei metodi:

```cromo
interface Shape {
    f64 area() const
    string name() const
}

struct Circle : Shape {
    f64 radius

    Circle(f64 r) {
        radius = r
    }

    f64 area() const {
        ret 3.14159 * radius * radius
    }

    string name() const {
        ret "circle"
    }
}

struct Square : Shape {
    f64 side

    Square(f64 s) {
        side = s
    }

    f64 area() const {
        ret side * side
    }

    string name() const {
        ret "square"
    }
}

void describe(Shape s) {
    println("{} with area {:.2f}", s.name(), s.area())
}

i32 main() {
    Circle c = Circle(1.0)
    describe(c)
    describe(Square(2.0))
    ret 0
}
```

```output
circle with area 3.14
square with area 4.00
```

Un parametro di tipo `Shape` accetta qualsiasi struct che implementa `Shape`, senza copiarla e senza allocare nulla: la struct resta dov'è, e la chiamata passa per una tabella dei suoi metodi, come una chiamata virtuale in C++.

### Valori di un'interfaccia

Una variabile, un campo o una lista possono contenere valori di un'interfaccia. Una struct lo diventa spostandosi sullo heap, insieme alla sua tabella dei metodi; quando il valore finisce, la struct viene distrutta:

```cromo
interface Shape {
    f64 area() const
}

struct Circle : Shape {
    f64 radius

    f64 area() const {
        ret 3.14159 * radius * radius
    }
}

struct Square : Shape {
    f64 side

    f64 area() const {
        ret side * side
    }
}

i32 main() {
    Circle small
    small.radius = 1.0
    Square big
    big.side = 3.0
    list<Shape> shapes = [small, big]
    f64 total = 0.0
    for (Shape s in shapes) {
        total += s.area()
    }
    println("{:.2f}", total)
    ret 0
}
```

```output
12.14
```

Qui `small` e `big` sono valori semplici, quindi la lista ne riceve delle copie; una struct che possiede delle risorse andrebbe aggiunta con `move`. Un valore di un'interfaccia non viene mai copiato, solo spostato, e su di esso si possono chiamare solo i metodi dell'interfaccia. Il suo valore predefinito è vuoto, e chiamare un metodo su un valore vuoto è un panic.

## Generici

Una funzione generica ha dei parametri di tipo, scritti tra `<` e `>` dopo il suo nome. Ogni parametro di tipo è vincolato da un'interfaccia, che dice che cosa la funzione può farci:

```cromo
interface Comparable {
    bool less(Self other) const
}

struct Version : Comparable {
    i32 major
    i32 minor

    Version(i32 a, i32 b) {
        major = a
        minor = b
    }

    bool less(Version other) const {
        if (major != other.major) {
            ret major < other.major
        }
        ret minor < other.minor
    }
}

T largest<T : Comparable>(list<T> items) {
    T best = items[0]
    for (T item in items) {
        if (best.less(item)) {
            best = item
        }
    }
    ret best
}

i32 main() {
    list<Version> versions = [Version(1, 2), Version(2, 0), Version(1, 9)]
    Version newest = largest(versions)
    println("{}.{}", newest.major, newest.minor)
    ret 0
}
```

```output
2.0
```

- `T : Comparable` dice che `T` può essere qualsiasi tipo che implementa `Comparable`. In un'interfaccia, `Self` è il tipo che la implementa: `less` confronta una `Version` con un'altra `Version`.
- Gli argomenti di tipo vengono dedotti dagli argomenti della chiamata: `largest(versions)` è `largest<Version>`. Quando non si possono dedurre, si scrivono esplicitamente: `largest<Version>(versions)`.
- Un generico viene controllato una volta sola, dove è scritto: su un `T` si possono chiamare solo i metodi dei suoi vincoli, e gli errori parlano del generico, non dei suoi usi.
- Il compilatore crea una versione della funzione per ogni tipo con cui viene usata, come i template del C++: le chiamate sono dirette, e veloci quanto con una funzione scritta apposta per quel tipo.

Un'interfaccia che usa `Self`, come `Comparable`, può essere solo un vincolo, non il tipo di un parametro o di un valore.

### Interfaccia o generico?

| | `void f(Shape s)` | `void f<T : Shape>(T s)` |
|---|---|---|
| La chiamata | attraverso la tabella dei metodi | diretta, spesso inline |
| Il codice | una sola funzione | una versione per ogni tipo |
| Tipi diversi in una lista | sì, `list<Shape>` | no, una `list<T>` ha un solo tipo |

Usa un'interfaccia per le collezioni di tipi diversi e per il codice che non ha bisogno della massima velocità; usa un generico per gli algoritmi che lavorano su un tipo alla volta.

## Struct generiche

Anche una struct può avere dei parametri di tipo, e lo stesso vale per i suoi metodi:

```cromo
struct Stack<T> {
    list<T> items

    void push(move T value) {
        items.add(move value)
    }

    T pop() {
        ret items.remove(items.len() - 1)
    }

    bool empty() const {
        ret items.len() == 0
    }
}

struct Pair<A, B> {
    A first
    B second
}

i32 main() {
    Stack<string> stack
    stack.push("first")
    stack.push("second")
    println("{} {}", stack.pop(), stack.empty())

    Pair<string, i64> entry
    entry.first = "answer"
    entry.second = 42
    println("{:?}", entry)
    ret 0
}
```

```output
second false
Pair { first: "answer", second: 42 }
```

Una struct generica si scrive sempre con i suoi argomenti di tipo: `Stack<string>`, `Pair<string, i64>`. I contenitori della libreria, `list<T>` e `map<K, V>`, sono struct generiche scritte in Cromo allo stesso modo.

## Riepilogo

- Un'interfaccia elenca dei metodi; una struct la implementa con `struct Name : Interface`.
- Un parametro di tipo interfaccia accetta qualsiasi struct che la implementa, senza copiarla.
- I valori di un'interfaccia, come quelli di una `list<Shape>`, possiedono la loro struct sullo heap.
- I generici hanno parametri di tipo vincolati da interfacce, vengono controllati una volta sola e compilati per ogni tipo.
- Non c'è ereditarietà: un comportamento condiviso è un'interfaccia, dei dati condivisi sono un campo.
