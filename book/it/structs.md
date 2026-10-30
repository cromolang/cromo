# Struct

Una struct raggruppa sotto un unico nome dei valori correlati, come in C. In Cromo può avere anche un costruttore, dei metodi e un distruttore, come una classe del C++, ma non c'è ereditarietà: ciò che tipi diversi hanno in comune si esprime con le interfacce, nel capitolo 9.

## Definire una struct

```cromo
struct Rectangle {
    f64 width
    f64 height
}

i32 main() {
    Rectangle r
    r.width = 3.0
    r.height = 2.0
    println("{} x {}", r.width, r.height)
    ret 0
}
```

```output
3 x 2
```

`Rectangle r` dichiara un rettangolo con il suo valore predefinito: ogni campo ha il valore predefinito del suo tipo, qui 0. Un campo può dichiarare un proprio valore iniziale:

```cromo
struct Settings {
    string host = "localhost"
    i32 port = 8080
    list<string> paths = ["/", "/health"]
    bool verbose
}

i32 main() {
    Settings s
    println("{}:{} {} {}", s.host, s.port, s.paths.len(), s.verbose)
    ret 0
}
```

```output
localhost:8080 2 false
```

Il valore iniziale può essere un'espressione qualsiasi, e viene calcolato ogni volta che si crea un `Settings` predefinito.

## Costruttori

Un costruttore si scrive come in C++, con il nome della struct e senza tipo. Assegna i campi, che si usano con il loro nome: non esiste `this`.

```cromo
struct Rectangle {
    f64 width
    f64 height

    Rectangle(f64 w, f64 h) {
        width = w
        height = h
    }
}

i32 main() {
    Rectangle r = Rectangle(3.0, 2.0)
    println("{} x {}", r.width, r.height)
    ret 0
}
```

```output
3 x 2
```

Una struct ha al massimo un costruttore. Un parametro non può avere il nome di un campo, così un nome dentro la struct indica sempre una cosa sola: `width = w`, mai `width = width`.

Quando un costruttore non riesce a fare il suo lavoro, per esempio perché un file non si apre, lancia un'eccezione: l'oggetto allora non esiste, e i campi già costruiti vengono distrutti.

Una struct senza costruttore si può comunque creare come valore, con `Rectangle()`: è il suo valore predefinito. Una variabile dichiarata senza valore riceve sempre il valore predefinito, anche quando la struct ha un costruttore: il costruttore viene eseguito solo quando lo si chiama.

## Metodi

Le funzioni scritte dentro la struct sono i suoi metodi. Vedono i campi per nome e possono chiamare gli altri metodi per nome:

```cromo
struct Rectangle {
    f64 width
    f64 height

    Rectangle(f64 w, f64 h) {
        width = w
        height = h
    }

    f64 area() const {
        ret width * height
    }

    bool isSquare() const {
        ret width == height
    }

    void scale(f64 factor) {
        width *= factor
        height *= factor
    }

    bool contains(Rectangle other) const {
        ret width >= other.width && height >= other.height
    }
}

i32 main() {
    Rectangle a = Rectangle(3.0, 2.0)
    Rectangle b = Rectangle(1.0, 1.0)
    println("area {}, square {}", a.area(), b.isSquare())
    b.scale(4.0)
    println("a contains b: {}", a.contains(b))
    ret 0
}
```

```output
area 6, square true
a contains b: false
```

### Metodi const

`const` dopo i parametri, come in C++, dice che il metodo non modifica l'oggetto. Solo i metodi `const` si possono chiamare su un valore che non può essere modificato: un parametro in lettura, una variabile `const`, l'elemento di un `for` su una lista.

```cromo error
struct Counter {
    i64 value

    void increment() {
        value++
    }
}

void show(Counter c) {
    c.increment()
}

i32 main() {
    Counter c
    show(c)
    ret 0
}
```

```output
error: cannot call 'increment', which is not const
```

Senza puntatori, `const` è profondo: un metodo `const` non può modificare nemmeno le stringhe e le liste contenute nell'oggetto.

## Distruttori

Il distruttore, `~Name()`, viene eseguito quando l'oggetto finisce: alla fine del suo blocco, quando viene rimosso da una lista, quando finisce la struct che lo contiene. Rilascia ciò che l'oggetto detiene. Dopo il distruttore, i campi vengono distrutti da soli, a partire dall'ultimo dichiarato, quindi un distruttore si occupa solo di ciò che i campi non sanno rilasciare da soli: un handle di sistema, un file temporaneo, un messaggio da stampare.

```cromo
struct Timer {
    string label

    Timer(move string name) {
        label = move name
        println("{}: start", label)
    }

    ~Timer() {
        println("{}: end", label)
    }
}

void work() {
    Timer t = Timer("work")
    println("working")
}

i32 main() {
    work()
    Timer outer = Timer("main")
    ret 0
}
```

```output
work: start
working
work: end
main: start
main: end
```

Un distruttore non si chiama mai a mano, e non può lasciar uscire un'eccezione: il compilatore lo controlla.

Una struct con un distruttore non è un tipo semplice: viene spostata, e copiata solo se scrive un proprio metodo `copy()`.

## Stampare una struct

`{:?}` stampa qualsiasi valore in una forma pensata per il debug, con i nomi dei campi. Per stampare una struct con `{}`, dalle un metodo `string text() const`:

```cromo
struct Point {
    i64 x
    i64 y

    string text() const {
        ret format("({}, {})", x, y)
    }
}

i32 main() {
    Point p
    p.x = 1
    p.y = 2
    println("{} {:?}", p, p)
    ret 0
}
```

```output
(1, 2) Point { x: 1, y: 2 }
```

`format` funziona come `println`, ma restituisce il testo invece di stamparlo.

## Struct dentro altre struct

Una struct può contenere altre struct, liste di struct, mappe: tutto ciò che contiene viene distrutto insieme a lei. Non può contenere direttamente se stessa, perché avrebbe una dimensione infinita, ma può farlo attraverso una lista:

```cromo
struct Folder {
    string name
    list<Folder> children

    i64 count() const {
        i64 total = 1
        for (Folder child in children) {
            total += child.count()
        }
        ret total
    }
}

i32 main() {
    Folder root
    root.name = "/"
    Folder docs
    docs.name = "docs"
    docs.children.add(Folder())
    root.children.add(move docs)
    root.children.add(Folder())
    println("{} folders", root.count())
    ret 0
}
```

```output
4 folders
```

## Riepilogo

- Una struct ha dei campi, al massimo un costruttore, dei metodi e un distruttore; al suo interno, i campi si usano per nome.
- Ogni campo ha un valore predefinito, oppure il valore iniziale che dichiara.
- I metodi `const` non modificano l'oggetto, e sono gli unici che si possono chiamare su un valore in sola lettura.
- Il distruttore rilascia ciò che l'oggetto detiene, in automatico, al momento giusto.
- `text()` permette di stampare una struct con `{}`; `{:?}` stampa qualsiasi valore per il debug.
