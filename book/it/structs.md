# Struct

Una struct raggruppa sotto un unico nome dei valori correlati, come in C. In Cromo può avere anche un costruttore, dei metodi e un distruttore, come una classe del C++, ma non c'è ereditarietà: ciò che tipi diversi hanno in comune si esprime con le interfacce, in [Interfacce e generici](interfaces-generics.md).

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

Un campo si dichiara come una variabile: il tipo, poi il nome. I campi stanno in memoria nell'ordine in cui sono scritti, con l'allineamento del C, quindi una struct fatta di numeri ha la stessa disposizione in memoria della stessa struct in C ([Chiamare il C](c-interop.md)).

In un programma di più moduli, una struct, i suoi campi, il suo costruttore e i suoi metodi sono privati del loro modulo, a meno che non siano marcati `public`: lo spiega [Moduli e progetti](modules.md).

## Valori predefiniti

`Rectangle r` dichiara un rettangolo con il suo **valore predefinito**: ogni campo ha il valore predefinito del suo tipo, qui 0. Un campo può invece dichiarare un proprio valore iniziale:

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

Il valore iniziale può essere un'espressione qualsiasi, anche la chiamata di una funzione o di un costruttore, e viene calcolato ogni volta che si crea un valore predefinito della struct: per una variabile dichiarata senza valore, prima che venga eseguito un costruttore, per `Settings()`.

```cromo
struct Size {
    i64 width
    i64 height

    Size(i64 w, i64 h) {
        width = w
        height = h
    }
}

i64 kib(i64 n) {
    ret n * 1024
}

struct Window {
    string title = "untitled"
    Size size = Size(800, 600)
    i64 buffer = kib(64)
}

i32 main() {
    Window w
    println("{} {}x{} {}", w.title, w.size.width, w.size.height, w.buffer)
    ret 0
}
```

```output
untitled 800x600 65536
```

### L'ordine dei valori iniziali

Il valore predefinito di una struct si crea in due passi: prima i valori predefiniti delle struct contenute nei suoi campi, nell'ordine dei campi, poi i suoi valori iniziali, sempre nell'ordine dei campi:

```cromo
string label(string what) {
    println("  computing {}", what)
    ret what
}

struct Inner {
    string a = label("inner.a")
    string b = label("inner.b")
}

struct Outer {
    string first = label("outer.first")
    Inner inner
    string last = label("outer.last")
}

i32 main() {
    println("one Outer:")
    Outer o
    ret 0
}
```

```output
one Outer:
  computing inner.a
  computing inner.b
  computing outer.first
  computing outer.last
```

Un valore iniziale non può leggere gli altri campi, che potrebbero non avere ancora il loro valore:

```cromo error
struct Box {
    i64 width = 4
    i64 twice = width * 2
}

i32 main() {
    ret 0
}
```

```output
error: the initial value of a field cannot read the field 'width'
```

E non può lanciare un'eccezione: il valore predefinito si crea in molti punti, una dichiarazione, un costruttore, una nuova chiave di una mappa, e nessuno di questi se ne aspetta una. Una funzione che può lanciare un'eccezione si chiama invece nel costruttore.

```cromo error
i64 parsePort(string text) {
    if (text == "") {
        throw Error("no port")
    }
    ret 8080
}

struct Config {
    i64 port = parsePort("8080")
}

i32 main() {
    ret 0
}
```

```output
error: the initial value of a field cannot throw an exception: it is computed wherever the default value is made
```

### Progettare il valore predefinito

Come in Go, il valore predefinito di una struct si usa spesso: per una variabile dichiarata senza valore, per i campi che un costruttore non assegna e per un campo il cui valore è stato spostato altrove ([Possesso](ownership.md)). Una struct va progettata in modo che il suo valore predefinito abbia senso, e il suo distruttore deve saperlo gestire.

Per una struct che detiene un handle di sistema, come il descrittore di un file, il valore predefinito significa "niente di aperto", e il distruttore rilascia solo ciò che è stato aperto. Qui `fd` rappresenta quell'handle:

```cromo
struct Handle {
    i32 fd = -1

    ~Handle() {
        if (fd >= 0) {
            println("close {}", fd)
        }
    }
}

struct Server {
    string name
    Handle listener
}

i32 main() {
    Handle unused
    Server s
    s.listener.fd = 4
    Handle taken = move s.listener
    println("left in the server: {}", s.listener.fd)
    ret 0
}
```

```output
left in the server: -1
close 4
```

`unused` e il campo rimasto in `s` hanno `fd` a -1, e i loro distruttori non fanno nulla; solo `taken` chiude il suo handle. Un campo spostato riceve il valore predefinito del suo tipo, qui un `Handle` con `fd` a -1, che comprende i valori iniziali scritti dentro `Handle`. Un valore iniziale scritto sul campo di `Server`, come `Handle listener = open(5)`, non verrebbe calcolato di nuovo: uno spostamento non calcola nulla e non alloca nulla.

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

Un costruttore parte dal valore predefinito: quando il suo corpo viene eseguito, ogni campo ha già il suo valore iniziale, e i campi che il costruttore non assegna lo conservano. Una variabile dichiarata senza valore riceve il valore predefinito anche quando la struct ha un costruttore: il costruttore viene eseguito solo quando lo si chiama.

```cromo
struct Counter {
    i64 value = 10
    i64 step = 1

    Counter(i64 start) {
        println("in the constructor: value {} step {}", value, step)
        value = start
    }
}

i32 main() {
    Counter a
    Counter b = Counter(3)
    println("{} {} / {} {}", a.value, a.step, b.value, b.step)
    ret 0
}
```

```output
in the constructor: value 10 step 1
10 1 / 3 1
```

Una struct senza costruttore si può comunque creare come valore, con `Rectangle()`: è il suo valore predefinito. Senza un costruttore non esiste un'inizializzazione per posizione come `Rectangle(3.0, 2.0)`: i campi si assegnano dopo, oppure si dà alla struct un costruttore.

```cromo error
struct Point {
    f64 x
    f64 y
}

i32 main() {
    Point p = Point(1.0, 2.0)
    ret 0
}
```

```output
error: Point has no constructor: Point() gives its default value
```

Una struct ha al massimo un costruttore: il valore predefinito copre già il caso senza argomenti.

```cromo error
struct Range {
    i64 low
    i64 high

    Range(i64 end) {
        high = end
    }

    Range(i64 start, i64 end) {
        low = start
        high = end
    }
}

i32 main() {
    ret 0
}
```

```output
error: a struct has only one constructor
```

Un parametro non può avere il nome di un campo, e nemmeno una variabile locale di un costruttore o di un metodo, così un nome dentro la struct indica sempre una cosa sola: `width = w`, mai `width = width`.

```cromo error
struct Rectangle {
    f64 width
    f64 height

    Rectangle(f64 width, f64 height) {
    }
}

i32 main() {
    ret 0
}
```

```output
error: 'width' is a field of Rectangle: choose another name
```

### Un costruttore che fallisce

Quando un costruttore non riesce a fare il suo lavoro, per esempio perché un file non si apre, lancia un'eccezione ([Errori](errors.md)). L'oggetto allora non esiste: la variabile non viene creata, i campi, che avevano già i loro valori, vengono distrutti, e il distruttore non viene eseguito, perché l'oggetto non è mai stato completo.

```cromo
struct Part {
    string name

    ~Part() {
        println("part {} destroyed", name)
    }
}

struct Device {
    Part motor
    i32 port = -1

    Device(i32 p) {
        motor.name = "motor"
        if (p < 0) {
            throw Error("no such port")
        }
        port = p
    }

    ~Device() {
        println("device on port {} closed", port)
    }
}

i32 main() {
    try {
        Device d = Device(-5)
        println("not reached")
    } catch (Error e) {
        println("failed: {}", e.message())
    }
    Device ok = Device(2)
    println("port {}", ok.port)
    ret 0
}
```

```output
part motor destroyed
failed: no such port
port 2
device on port 2 closed
part motor destroyed
```

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
    println("{}", Rectangle(2.0, 5.0).area())
    ret 0
}
```

```output
area 6, square true
a contains b: false
10
```

Un metodo si può chiamare anche su un temporaneo, come `Rectangle(2.0, 5.0).area()`. I parametri di un metodo seguono le regole di tutti i parametri ([Possesso](ownership.md)): `other` viene letto, senza copia.

### Le chiamate dentro i metodi

Dentro un metodo, una chiamata `f()` indica il metodo `f` della stessa struct, se esiste; altrimenti indica una funzione libera. Vince il nome più vicino al codice, come per i campi e come in C++:

```cromo
void report() {
    println("free function")
}

void log(string message) {
    println("log: {}", message)
}

struct Task {
    string name

    void report() const {
        println("task {}", name)
    }

    void run() const {
        report()
        log("running")
    }
}

i32 main() {
    Task t
    t.name = "build"
    t.run()
    report()
    ret 0
}
```

```output
task build
log: running
free function
```

In `run`, `report()` è il metodo; `log` non è un metodo di `Task`, quindi è la funzione libera. Fuori dalla struct, `report()` è la funzione libera.

### Metodi const

`const` dopo i parametri, come in C++, dice che il metodo non modifica l'oggetto. Solo i metodi `const` si possono chiamare su un valore che non può essere modificato: un parametro in lettura, una variabile `const`, l'elemento di un `for` che legge una lista.

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

Il compilatore controlla il corpo di un metodo `const`: non può assegnare i campi, né chiamare i metodi che non sono `const`.

```cromo error
struct Counter {
    i64 value

    i64 next() const {
        value++
        ret value
    }
}

i32 main() {
    ret 0
}
```

```output
error: method 'next' is const and cannot modify the fields
```

Senza puntatori, `const` è profondo: un metodo `const` non può modificare nemmeno le stringhe e le liste contenute nell'oggetto.

## Distruttori

Il distruttore, `~Name()`, viene eseguito quando l'oggetto finisce: alla fine del suo ambito, quando viene rimosso da una lista, quando finisce la struct che lo contiene ([Possesso](ownership.md) elenca ogni caso). Rilascia ciò che l'oggetto detiene.

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

Dopo il distruttore, i campi vengono distrutti da soli, a partire dall'ultimo dichiarato, quindi un distruttore si occupa solo di ciò che i campi non sanno rilasciare da soli: un handle di sistema, un file temporaneo, un messaggio da stampare. Anche una struct senza distruttore distrugge i suoi campi.

```cromo
struct Part {
    string name

    ~Part() {
        println("  part {} ends", name)
    }
}

struct Machine {
    Part engine
    Part wheels

    Machine() {
        engine.name = "engine"
        wheels.name = "wheels"
    }

    ~Machine() {
        println("machine ends")
    }
}

i32 main() {
    Machine m = Machine()
    println("end of main")
    ret 0
}
```

```output
end of main
machine ends
  part wheels ends
  part engine ends
```

Un distruttore non ha parametri né tipo, e non si chiama mai a mano: lo chiama il compilatore, una volta per ogni oggetto. Non può nemmeno lasciar uscire un'eccezione, perché può essere eseguito mentre un'altra eccezione sta risalendo: un errore al suo interno si cattura al suo interno.

```cromo error
struct Log {
    string path

    ~Log() {
        throw Error("cannot flush")
    }
}

i32 main() {
    ret 0
}
```

```output
error: a destructor cannot let an exception out: catch it inside
```

## Copiare una struct

Il modo in cui si copia una struct dipende da ciò che contiene:

- Una struct fatta solo di numeri, `bool` e altre struct di questo genere, senza distruttore, è un tipo **semplice**: si copia byte per byte, come in C.
- Una struct con stringhe, liste, mappe o altre struct che possiedono risorse, e senza un distruttore proprio, si copia con un `copy()` che scrive il compilatore: copia ogni campo.
- Una struct con un distruttore, o con un campo che non si può copiare, non si copia: il compilatore non può sapere come duplicare ciò che il distruttore rilascia. Viene solo spostata, a meno che non scriva un proprio `copy()`.

[Possesso](ownership.md) spiega dove avvengono le copie.

### `copy()` scritto a mano

Una struct che il compilatore non sa copiare, perché ha un distruttore o un campo che non si può copiare, si può comunque copiare se scrive il metodo `copy()`, esattamente con questa firma: il tipo della struct, `copy()`, nessun parametro, `const`. Il metodo viene allora chiamato ovunque la struct venga copiata, come per una `string`:

```cromo
struct Ticket {
    string owner
    i64 number

    Ticket(move string name, i64 n) {
        owner = move name
        number = n
        println("issue ticket {} to {}", number, owner)
    }

    ~Ticket() {
        println("cancel ticket {}", number)
    }

    Ticket copy() const {
        ret Ticket(owner, number + 100)
    }
}

i32 main() {
    Ticket a = Ticket("Ada", 1)
    Ticket b = a
    println("{} has ticket {}, and {}", b.owner, a.number, b.number)
    ret 0
}
```

```output
issue ticket 1 to Ada
issue ticket 101 to Ada
Ada has ticket 1, and 101
cancel ticket 101
cancel ticket 1
```

La copia di un biglietto è un nuovo biglietto, con un numero tutto suo, e ognuno viene annullato dal proprio distruttore.

Il compilatore controlla la firma:

```cromo error
struct Ticket {
    i64 number

    ~Ticket() {
        println("cancel ticket {}", number)
    }

    Ticket copy() {
        ret Ticket()
    }
}

i32 main() {
    ret 0
}
```

```output
error: copy() must be written 'Ticket copy() const'
```

Una struct semplice non può scrivere `copy()`: si copia sempre byte per byte, e un `copy()` scritto a mano potrebbe fare altro.

```cromo error
struct Point {
    i64 x
    i64 y

    Point copy() const {
        Point p
        ret p
    }
}

i32 main() {
    ret 0
}
```

```output
error: Point is copied implicitly: its copy() is generated by the compiler
```

Come un distruttore, un `copy()` scritto a mano non può lasciar uscire un'eccezione: il compilatore lo chiama in punti in cui nessuno potrebbe catturarla.

## Confrontare le struct

`==` e `!=` non funzionano ancora sulle struct:

```cromo error
struct Point {
    i64 x
    i64 y
}

i32 main() {
    Point a
    Point b
    println("{}", a == b)
    ret 0
}
```

```output
error: '==' does not work on structs yet
```

Finché non funzioneranno, una struct confronta i suoi campi in un metodo. Il nome `equals` è quello che usa una chiave di una mappa ([Liste, mappe e testo](collections.md)):

```cromo
struct Point {
    i64 x
    i64 y

    bool equals(Point other) const {
        ret x == other.x && y == other.y
    }
}

i32 main() {
    Point a
    Point b
    b.x = 1
    println("{} {}", a.equals(b), a.equals(a))
    ret 0
}
```

```output
false true
```

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

`format` funziona come `println`, ma restituisce il testo invece di stamparlo. Senza `text()`, `{}` su una struct è un errore, che propone entrambe le strade. Le larghezze, l'allineamento e le altre opzioni di `{}` si applicano al testo restituito da `text()`: le spiega [Formattare il testo](formatting.md), insieme alle forme di `{:?}`.

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

```cromo error
struct Node {
    i64 value
    Node next
}

i32 main() {
    ret 0
}
```

```output
error: struct 'Node' contains itself through field 'next': use a list instead
```

## Riepilogo

- Una struct ha dei campi, al massimo un costruttore, dei metodi e un distruttore; al suo interno, i campi e i metodi si usano per nome, e non esiste `this`.
- Ogni campo ha il valore predefinito del suo tipo, oppure il valore iniziale che dichiara: un'espressione qualsiasi che non legge gli altri campi e non lancia eccezioni. Prima si creano le struct contenute, poi i valori iniziali, nell'ordine dei campi.
- Il valore predefinito deve avere senso, e il distruttore deve saperlo gestire: si usa per le dichiarazioni senza valore, per i campi che un costruttore non assegna e per i campi spostati.
- Un costruttore parte dal valore predefinito e viene eseguito solo quando lo si chiama; se lancia un'eccezione, l'oggetto non esiste e i suoi campi vengono distrutti.
- Dentro un metodo, `f()` è un metodo della struct, se esiste, altrimenti una funzione libera.
- I metodi `const` non modificano l'oggetto, e sono gli unici che si possono chiamare su un valore in sola lettura.
- Il distruttore rilascia ciò che l'oggetto detiene, in automatico, poi finiscono i campi, a partire dall'ultimo. Non ha parametri e non può lanciare eccezioni.
- Le struct semplici si copiano byte per byte, le altre con un `copy()` generato; una struct con un distruttore, o con un campo che non si può copiare, si copia solo se scrive `T copy() const`.
- `==` non funziona ancora sulle struct: scrivi un metodo `equals`.
- `text()` stampa una struct con `{}`; `{:?}` stampa qualsiasi valore per il debug.
