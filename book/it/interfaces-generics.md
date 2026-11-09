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

// ...
#
# struct Square : Shape {
#     f64 side
#
#     Square(f64 s) {
#         side = s
#     }
#
#     f64 area() const {
#         ret side * side
#     }
#
#     string name() const {
#         ret "square"
#     }
# }

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

### Che cosa contiene un'interfaccia

Un'interfaccia contiene solo le firme dei metodi: il tipo che restituiscono, il loro nome, i loro parametri con il modo in cui si passano, e `const` quando il metodo non modifica il valore. Non ha campi, né corpi, né costruttori, e non estende un'altra interfaccia. Il compilatore rifiuta ciascuna di queste cose:

```cromo error
interface Shape {
    f64 size
    f64 area() const
}

interface Named {
    string name() const {
        ret ""
    }
}

interface Solid : Shape {
    f64 volume() const
}

i32 main() {
    ret 0
}
```

```output
error: an interface contains only methods
error: the methods of an interface have no body
error: an interface cannot extend another interface
```

Il codice condiviso da più tipi va in una funzione che prende l'interfaccia, come `describe` qui sopra; un tipo che ha bisogno di più interfacce le implementa tutte, come si vede più avanti.

I metodi di un'interfaccia sono pubblici quando lo è l'interfaccia. In un altro modulo, un metodo che implementa un'interfaccia non ha bisogno di un `public` suo, perché si può chiamare comunque attraverso l'interfaccia: [Moduli e progetti](modules.md) spiega `public`.

### Implementare esattamente un'interfaccia

Una struct che indica un'interfaccia deve scriverne ogni metodo con la stessa firma: lo stesso tipo restituito, gli stessi tipi e modi di passaggio dei parametri, e `const` dove l'interfaccia lo ha. Possono cambiare solo i nomi dei parametri. Il compilatore dice quale metodo manca o è diverso, e come va scritto:

```cromo error
interface Shape {
    f64 area() const
    void scale(f64 factor)
}

struct Square : Shape {
    f64 side

    f64 area() {
        ret side * side
    }
}

i32 main() {
    ret 0
}
```

```output
error: 'area' does not match the interface Shape: it must be 'f64 area() const'
error: Square does not implement 'scale' of Shape: add 'void scale(f64 factor)'
```

La struct può avere altri metodi e campi, che l'interfaccia non vede. Una struct implementa un'interfaccia solo quando la indica: una struct con i metodi giusti, ma senza `: Shape`, non è una `Shape`, e passarla come tale è l'errore `Square does not implement Shape`. Dopo i due punti si possono scrivere solo interfacce: `struct Big : Square` è l'errore `'Square' is not an interface: a struct can only implement interfaces`.

### Più interfacce

Un tipo che implementa più interfacce le elenca separate da virgole. Anche un enum implementa delle interfacce, con i suoi metodi dopo i casi, come mostra [Enum e switch](enums.md):

```cromo
interface Shape {
    f64 area() const
}

interface Named {
    string name() const
}

struct Circle : Shape, Named {
    f64 radius

    f64 area() const {
        ret 3.14159 * radius * radius
    }

    string name() const {
        ret "circle"
    }
}

enum Tile : Shape, Named {
    Empty
    Square(f64 side)

    f64 area() const {
        switch {
            case Empty {
                ret 0.0
            }
            case Square(f64 side) {
                ret side * side
            }
        }
    }

    string name() const {
        ret "tile"
    }
}

void describe(Named n, Shape s) {
    println("{}: {:.2f}", n.name(), s.area())
}

i32 main() {
    Circle c
    c.radius = 2.0
    describe(c, c)
    Tile t = Tile.Square(3.0)
    describe(t, t)
    ret 0
}
```

```output
circle: 12.57
tile: 9.00
```

### Un'interfaccia come parametro

Un parametro che è un'interfaccia è un parametro come gli altri ([Possesso](ownership.md)): senza parola è in sola lettura, e su di esso si possono chiamare solo i metodi `const` dell'interfaccia. Con `ref` la funzione può chiamare i metodi che modificano il valore, e anche il chiamante scrive `ref`:

```cromo
interface Shape {
    f64 area() const
    void scale(f64 factor)
}

struct Square : Shape {
    f64 side

    f64 area() const {
        ret side * side
    }

    void scale(f64 factor) {
        side *= factor
    }
}

void enlarge(ref Shape s) {
    s.scale(2.0)
}

void show(Shape s) {
    println("area {:.2f}", s.area())
}

i32 main() {
    Square sq
    sq.side = 1.5
    enlarge(ref sq)
    show(sq)
    println("side {:.2f}", sq.side)
    ret 0
}
```

```output
area 9.00
side 3.00
```

Chiamare `s.scale(2.0)` in `show`, il cui parametro è in sola lettura, è l'errore `cannot call 'scale', which is not const`.

### Valori di un'interfaccia

Possono essere di un'interfaccia una variabile, un campo, un valore restituito, un parametro `move`, gli elementi di una lista e i valori di una mappa. Una struct diventa il valore di un'interfaccia spostandosi sullo heap, insieme alla sua tabella dei metodi; quando il valore finisce, la struct viene distrutta:

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

// ...
#
# struct Square : Shape {
#     f64 side
#
#     f64 area() const {
#         ret side * side
#     }
# }

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

Qui `small` e `big` vengono copiate nella lista, come si copia una struct data senza `move`; una struct che non si può copiare, come una che ha un suo distruttore, si dà con `move`.

Negli altri posti funziona allo stesso modo: il valore di un'interfaccia si passa con `move` e si restituisce con `ret`, come una struct che possiede delle risorse:

```cromo
interface Shape {
    f64 area() const
}

struct Circle : Shape {
    f64 radius

    Circle(f64 r) {
        radius = r
    }

    f64 area() const {
        ret 3.14159 * radius * radius
    }
}

// ...
#
# struct Square : Shape {
#     f64 side
#
#     Square(f64 s) {
#         side = s
#     }
#
#     f64 area() const {
#         ret side * side
#     }
# }

struct Drawing {
    string title
    Shape main
}

Shape larger(move Shape a, move Shape b) {
    if (a.area() >= b.area()) {
        ret move a
    }
    ret move b
}

i32 main() {
    Shape first = Circle(1.0)
    Shape second = Square(2.0)
    Drawing plan
    plan.title = "plan"
    plan.main = larger(move first, move second)

    map<string, Shape> parts
    parts["door"] = Square(1.5)
    println("{:.2f} {:.2f}", plan.main.area(), parts["door"].area())
    ret 0
}
```

```output
4.00 2.25
```

- Un valore di un'interfaccia non viene mai copiato, solo spostato: per copiarlo servirebbe il suo tipo, che non è noto. `Shape t = s` è l'errore `values of type Shape cannot be copied: write 'move s'`.
- Su di esso si possono chiamare solo i metodi dell'interfaccia: i suoi campi non sono raggiungibili (`a Shape has no fields`), e non c'è modo di risalire al tipo della struct. Quando serve il tipo, il lavoro lo fa un generico o un enum.
- Il suo valore predefinito è vuoto: una variabile `Shape` dichiarata senza valore non contiene nessuna struct, e lo stesso vale per un campo di tipo `Shape` dopo che `move` ne ha portato via la struct. Chiamare un metodo su un valore vuoto è un panic, `the value of Shape is empty: it was moved, or never given`.

## Generici

Una funzione generica ha dei parametri di tipo, scritti tra `<` e `>` dopo il suo nome. Ogni parametro di tipo può essere vincolato da un'interfaccia, che dice che cosa la funzione può farci:

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

- `T : Comparable` dice che `T` può essere qualsiasi tipo che implementa `Comparable`: dentro `largest`, un `T` ha il metodo `less`.
- Gli argomenti di tipo vengono dedotti dagli argomenti della chiamata: `largest(versions)` è `largest<Version>`. Quando non si possono dedurre, si scrivono: `largest<Version>(versions)`.
- Il compilatore crea una versione della funzione per ogni tipo con cui viene usata, come i template del C++: le chiamate sono dirette, e veloci quanto con una funzione scritta apposta per quel tipo.

### Self

In un'interfaccia, `Self` è il tipo che la implementa: `less` confronta una `Version` con un'altra `Version`, e `Version` scrive il suo metodo con il proprio nome, `bool less(Version other) const`. `Self` si può scrivere solo nei metodi di un'interfaccia.

Un'interfaccia che usa `Self` può essere solo un vincolo, non il tipo di un parametro o di un valore: attraverso un `Comparable` e nient'altro, il tipo di `other` non sarebbe noto.

```cromo error
interface Comparable {
    bool less(Self other) const
}

void show(Comparable c) {
}

i32 main() {
    ret 0
}
```

```output
error: Comparable uses Self, so it can only be a constraint: <T : Comparable>
```

Un'interfaccia non ha parametri di tipo suoi: `interface Container<T>` è l'errore `an interface has no type parameters: its methods use Self`. Ciò che cambia con il tipo si scrive con `Self`, oppure il generico è una struct.

### Vincoli

Un parametro di tipo può non avere vincoli, averne uno, cioè un'interfaccia, o averne più d'uno uniti da `+`; e una funzione può avere più parametri di tipo, separati da virgole, come `apply<T, U>` più avanti:

```cromo
interface Shape {
    f64 area() const
}

interface Named {
    string name() const
}

// ...
#
# struct Circle : Shape, Named {
#     f64 radius
#
#     f64 area() const {
#         ret 3.14159 * radius * radius
#     }
#
#     string name() const {
#         ret "circle"
#     }
# }

void report<T : Shape + Named>(T item) {
    println("{}: {:.2f}", item.name(), item.area())
}

T first<T>(list<T> items) {
    ret items[0]
}

i32 main() {
    Circle c
    c.radius = 1.0
    report(c)
    list<string> words = ["one", "two"]
    println("{}", first(words))
    ret 0
}
```

```output
circle: 3.14
one
```

Un vincolo deve essere un'interfaccia, e il tipo di ogni chiamata deve implementarla; il compilatore controlla entrambe le cose:

```cromo error
struct Point {
    i64 x
    i64 y
}

void wrong<T : Point>(T value) {
}

T largest<T : Ordered>(list<T> items) {
    ret items[0]
}

i32 main() {
    list<Point> points
    Point p = largest(points)
    ret 0
}
```

```output
error: a constraint must be an interface, found 'Point'
error: Point does not implement Ordered, required by T of 'largest'
```

`Ordered` è un'interfaccia della libreria, descritta più avanti in una sezione apposita.

### Che cosa può fare un generico con T

Di un `T` un generico non sa nulla, a parte i suoi vincoli. Con un valore di tipo `T` può:

- dichiararlo senza valore, il che dà il valore predefinito del tipo: `0`, `""`, una lista vuota;
- copiarlo, con `=` o `ret`, e con `x.copy()`, che ogni tipo copiabile possiede, numeri compresi;
- spostarlo con `move`, e passarlo a parametri di tipo `T` e ad altri generici;
- chiamare i metodi dei suoi vincoli.

```cromo
void swap<T>(ref T a, ref T b) {
    T keep = move a
    a = move b
    b = move keep
}

T orDefault<T>(list<T> items, i64 index) {
    T empty
    if (index < items.len()) {
        ret items[index]
    }
    ret empty
}

list<T> repeat<T>(T value, i64 times) {
    list<T> out
    for (i64 i = 0; i < times; i++) {
        out.add(value.copy())
    }
    ret out
}

i32 main() {
    string a = "left"
    string b = "right"
    swap(ref a, ref b)
    println("{} {}", a, b)
    list<i64> numbers = [10, 20]
    println("{} {}", orDefault(numbers, 1), orDefault(numbers, 5))
    println("{:?}", repeat(i64(7), 3))
    ret 0
}
```

```output
right left
20 0
[7, 7, 7]
```

Gli operatori, i confronti, le conversioni e la stampa con `{}` non funzionano su un `T`, perché nessuna interfaccia li fornisce: un generico che confronta chiede `Ordered` e chiama `compare`, uno che stampa chiede `Debug` e stampa con `{:?}`.

```cromo error
T sum<T>(list<T> items) {
    T total
    for (T x in items) {
        total = total + x
    }
    ret total
}

bool same<T>(T a, T b) {
    ret a == b
}

void show<T>(T value) {
    println("{}", value)
}

i32 main() {
    ret 0
}
```

```output
error: '+' does not work on values of type T
error: '==' does not work on values of type T
error: a T cannot be printed with {}: print its form of debug with {:?}, with the constraint Debug
```

Questi generici non vengono mai chiamati, eppure gli errori arrivano lo stesso: il perché è spiegato più avanti, nella sezione su quando viene controllato un generico.

### Argomenti di tipo: dedotti o scritti

Gli argomenti di tipo di una chiamata vengono dedotti dai tipi dei suoi argomenti, come in C++. Quando non si possono dedurre, si scrivono tra `<` e `>` dopo il nome:

- Un letterale, come `5`, `1.5` o `[1, 2]`, non ha un tipo suo: prende il tipo che gli altri argomenti danno a `T`. Quando tutti gli argomenti di un `T` sono letterali, o non ce ne sono, `T` si scrive: `pick<i64>(true, 1, 2)`, `zero<string>()`.
- Una lambda non aiuta: il tipo restituito da `[](i64 n) string { ... }` non serve a dedurre `U` in `apply`.
- Un `T` è un solo tipo: `pick(true, small, big)` con un `i32` e un `i64` è un errore, anche se uno potrebbe diventare l'altro.

```cromo
T pick<T>(bool first, T a, T b) {
    if (first) {
        ret a
    }
    ret b
}

T zero<T>() {
    T value
    ret value
}

U apply<T, U>(T value, func(T) U f) {
    ret f(value)
}

i32 main() {
    i64 count = 4
    println("{}", pick(false, 5, count))
    println("{}", pick<f64>(true, 1.5, 2.5))
    println("{:?}", zero<string>())
    println("{}", apply<i64, string>(count, [](i64 n) string { ret format("<{}>", n) }))
    ret 0
}
```

```output
4
1.5
""
<4>
```

Senza gli argomenti di tipo queste chiamate non compilano, e il compilatore dice che cosa scrivere:

```cromo error
T pick<T>(bool first, T a, T b) {
    if (first) {
        ret a
    }
    ret b
}

U apply<T, U>(T value, func(T) U f) {
    ret f(value)
}

i32 main() {
    i32 small = 1
    i64 big = 2
    i64 a = pick(true, 1, 2)
    i64 b = pick(true, small, big)
    string c = apply(big, [](i64 n) string { ret "x" })
    ret 0
}
```

```output
error: cannot deduce T in this call of 'pick': write the type arguments, pick<...>(...)
error: T is i32 for a previous argument, but i64 here: write the type arguments, pick<...>(...)
error: cannot deduce U in this call of 'apply': write the type arguments, apply<...>(...)
```

Una funzione che non è generica non prende argomenti di tipo: `zero<i32>()` con una semplice `zero()` è l'errore `'zero' is not generic`.

### Quando viene controllato un generico

Il compilatore controlla il corpo di un generico una volta sola, dove è scritto, sapendo di `T` solo ciò che i vincoli promettono. Così gli errori di un generico vengono trovati anche se nessuno lo chiama, indicano il generico stesso, e usarlo con un tipo nuovo non ne fa scoprire altri al suo interno.

Due cose dipendono dal tipo stesso, e vengono controllate per ogni tipo con cui il generico viene usato.

**Le copie di T.** Un generico che copia un `T`, come `largest` con `T best = items[0]`, o `pick` con il `ret a` di un parametro in lettura, funziona con ogni tipo che si può copiare. Se lo si usa con un tipo che non si può copiare, come una struct con un suo distruttore, l'errore è alla riga del generico che copia, e indica l'uso che ha richiesto la copia:

```cromo error
struct File {
    i32 handle

    ~File() {
    }
}

T pick<T>(bool first, T a, T b) {
    if (first) {
        ret a
    }
    ret b
}

i32 main() {
    File a
    File b
    File c = pick(true, a, b)
    ret 0
}
```

```output
error: values of type File cannot be copied
(in pick<File>, used at line 18)
```

Un generico che prende i suoi valori con `move` e li restituisce con `ret move` non copia nulla, e funziona con ogni tipo: `T pick<T>(bool first, move T a, move T b)`.

**I metodi di una struct generica.** Per ogni tipo, un metodo di una struct generica viene controllato, e compilato, solo se il programma lo usa. Così una `list<File>` funziona, benché `File` non si possa copiare, finché il programma non chiama `copy()` della lista, l'unico metodo che copia gli elementi:

```cromo
struct File {
    string name

    File(string n) {
        name = n
    }

    ~File() {
        println("close {}", name)
    }
}

i32 main() {
    list<File> files
    files.add(File("a.txt"))
    files.add(File("b.txt"))
    println("{} files", files.len())
    ret 0
}
```

```output
2 files
close a.txt
close b.txt
```

Aggiungere `list<File> other = files.copy()` a `main` dà l'errore `values of type File cannot be copied`, alla riga della libreria che copia, con `(in list<File>, used at line ...)` che indica la chiamata.

### Interfaccia o generico?

| | `void f(Shape s)` | `void f<T : Shape>(T s)` |
|---|---|---|
| La chiamata | attraverso la tabella dei metodi | diretta, spesso inline |
| Il codice | una sola funzione | una versione per ogni tipo |
| Tipi diversi in una lista | sì, `list<Shape>` | no, una `list<T>` ha un solo tipo |
| Un'interfaccia con `Self` | no | sì |

Usa un'interfaccia per le collezioni di tipi diversi e per il codice che non ha bisogno della massima velocità; usa un generico per gli algoritmi che lavorano su un tipo alla volta.

## Le interfacce della libreria

La libreria dichiara alcune interfacce, che le sue funzioni usano come vincoli: `json.Data` sta nel modulo `std.json`, le altre nel core, che ogni programma vede. I tipi integrati le implementano da soli, e una struct le implementa come qualsiasi altra interfaccia:

| Interfaccia | I suoi metodi | Implementata da | Richiesta da | Capitolo |
|---|---|---|---|---|
| `Ordered` | `i32 compare(Self other) const` | i numeri e `string`, da soli | `sort`, un generico che confronta | [Liste, mappe e testo](collections.md) |
| `Key` | `u64 hash() const`, `bool equals(Self other) const` | i numeri, `bool` e `string`, da soli | le chiavi di una `map` | [Liste, mappe e testo](collections.md) |
| `Debug` | nessuno | ogni tipo tranne le funzioni e le interfacce, da solo | `{:?}` di un `T` | [Formattare il testo](formatting.md) |
| `Exception` | `string message() const` | `Error`, e le eccezioni del programma | `throw` e `catch` | [Errori](errors.md) |
| `json.Data` | nessuno | i tipi che JSON può contenere, da soli | `json.encode` e `json.decode` di un `T` | [JSON](json.md) |

`compare` dà un numero negativo quando il valore viene prima di `other`, zero quando sono uguali, e un numero positivo quando viene dopo; tra due numeri in virgola mobile di cui uno è un NaN dà zero. Due valori che sono `equals` devono avere lo stesso `hash`.

```cromo
struct Version : Ordered, Key {
    i32 major
    i32 minor

    Version(i32 a, i32 b) {
        major = a
        minor = b
    }

    i32 compare(Version other) const {
        if (major != other.major) {
            ret major - other.major
        }
        ret minor - other.minor
    }

    u64 hash() const {
        ret major.hash() ^ minor.hash() << 1
    }

    bool equals(Version other) const {
        ret major == other.major && minor == other.minor
    }
}

T largest<T : Ordered>(list<T> items) {
    T best = items[0]
    for (T item in items) {
        if (item.compare(best) > 0) {
            best = item
        }
    }
    ret best
}

void show<T : Debug>(string label, T value) {
    println("{}: {:?}", label, value)
}

i32 main() {
    list<Version> versions = [Version(2, 0), Version(1, 9), Version(1, 2)]
    sort(ref versions)
    show("sorted", versions)

    map<Version, string> names
    names[Version(1, 9)] = "stable"
    println("{}", names[Version(1, 9)])

    list<i64> numbers = [3, 9, 4]
    list<string> fruits = ["pear", "apple", "plum"]
    println("{} {}", largest(numbers), largest(fruits))
    show("newest", largest(versions))
    ret 0
}
```

```output
sorted: [Version { major: 1, minor: 2 }, Version { major: 1, minor: 9 }, Version { major: 2, minor: 0 }]
stable
9 plum
newest: Version { major: 2, minor: 0 }
```

`Debug` e `json.Data` non hanno metodi, e un tipo non li indica: il compilatore sa quali tipi hanno una forma di debug, e quali può convertire in JSON. Esistono per i generici: un `T` si stampa con `{:?}` solo quando il generico chiede `T : Debug`, e si converte in JSON solo con `T : json.Data`; altrimenti il compilatore dice `T can be printed with {:?} only with a constraint: write <T : Debug>`.

`Exception` non ha `Self`, quindi è anche un tipo, come in `catch (Exception e)`.

## Struct generiche

Anche una struct può avere dei parametri di tipo:

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

Una struct generica si scrive sempre con i suoi argomenti di tipo, anche quando se ne chiama il costruttore: `Stack<string>`, `Pair<string, i64>("answer", 42)`. Non vengono dedotti come quelli di una funzione: `Pair("answer", 42)` è l'errore `Pair is generic: write its type arguments, Pair<...>(...)`, e `Pair<string>` l'errore `'Pair' takes 2 type arguments, but 1 were given`.

I suoi parametri di tipo possono avere dei vincoli, e la struct può implementare delle interfacce, come qualsiasi altra struct:

```cromo
interface Sized {
    i64 size() const
}

struct Sorted<T : Ordered> : Sized {
    list<T> items

    void insert(T value) {
        i64 i = 0
        for (i < items.len() && items[i].compare(value) < 0) {
            i++
        }
        items.insert(i, value)
    }

    i64 size() const {
        ret items.len()
    }
}

void report(Sized s) {
    println("{} elements", s.size())
}

i32 main() {
    Sorted<string> names
    names.insert("Carla")
    names.insert("Anna")
    names.insert("Bruno")
    println("{:?}", names.items)
    report(names)
    ret 0
}
```

```output
["Anna", "Bruno", "Carla"]
3 elements
```

I contenitori della libreria, `list<T>` e `map<K, V>`, sono struct generiche scritte in Cromo allo stesso modo. Anche un enum può essere generico, come `enum Result<T, E>` in [Enum e switch](enums.md).

### Metodi con parametri di tipo propri

Un metodo può avere dei parametri di tipo suoi, scritti dopo il nome come per una funzione, `R name<U>(...)`: il metodo di `list<T>` che crea una lista di un altro tipo è `list<U> map<U>(func(T) U f)`. I loro argomenti di tipo vengono dedotti dagli argomenti della chiamata, oppure si scrivono dopo il nome del metodo:

```cromo
struct Pair<A> {
    A first
    A second

    Pair<B> convert<B>(func(A) B f) const {
        Pair<B> result
        result.first = f(first)
        result.second = f(second)
        ret result
    }
}

struct Log {
    list<string> lines

    void add<T : Debug>(string label, T value) {
        lines.add(format("{} = {:?}", label, value))
    }
}

i32 main() {
    Pair<i64> size
    size.first = 3
    size.second = 4
    Pair<string> text = size.convert<string>([](i64 n) string { ret format("{} cm", n) })

    Log log
    log.add("size", size)
    log.add("text", text)
    list<i64> numbers = [1, 2]
    log.add("labels", numbers.map<string>([](i64 n) string { ret format("#{}", n) }))
    for (string line in log.lines) {
        println("{}", line)
    }
    ret 0
}
```

```output
size = Pair { first: 3, second: 4 }
text = Pair { first: "3 cm", second: "4 cm" }
labels = ["#1", "#2"]
```

- `log.add("size", size)` deduce `T` da `size`. `convert` e `map` prendono solo una lambda, che non aiuta a dedurre, quindi il loro argomento di tipo si scrive.
- I nomi dei parametri di tipo di un metodo non possono essere quelli della struct: in `struct Box<T>`, un metodo `T get<T>()` è l'errore `'T' is already a type parameter of Box`.
- Li hanno solo i metodi: non un costruttore, e nemmeno un metodo di un'interfaccia, la cui tabella ha una voce per ogni metodo e non può contenerne una per ogni tipo.

## Riepilogo

- Un'interfaccia elenca le firme di alcuni metodi: niente campi, corpi o costruttori, e nessuna interfaccia ne estende un'altra.
- Una struct o un enum implementa delle interfacce indicandole, `struct Circle : Shape, Named`, con ogni metodo scritto esattamente come lo ha l'interfaccia.
- Un parametro che è un'interfaccia accetta qualsiasi tipo che la implementa, senza copiarlo; con `ref` può chiamare i metodi che modificano.
- I valori di un'interfaccia, come quelli di una `list<Shape>`, possiedono la loro struct sullo heap; vengono spostati, mai copiati, e all'inizio sono vuoti.
- `Self` è il tipo che implementa l'interfaccia; un'interfaccia che lo usa è solo un vincolo.
- I generici hanno parametri di tipo, vincolati da interfacce unite con `+`; con un `T` possono solo dichiararlo, copiarlo, spostarlo e chiamare i metodi dei vincoli.
- Gli argomenti di tipo vengono dedotti dagli argomenti, ma non dai soli letterali né dalle lambda; altrimenti si scrivono.
- Un generico viene controllato una volta sola, dove è scritto; solo le copie di `T`, e i metodi usati di una struct generica, vengono controllati per ogni tipo.
- La libreria ha `Ordered`, `Key`, `Debug`, `Exception` e `json.Data`, che i tipi integrati implementano da soli.
- Non c'è ereditarietà: un comportamento condiviso è un'interfaccia, dei dati condivisi sono un campo.
