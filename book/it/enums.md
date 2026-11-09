# Enum e switch

Un enum è un tipo il cui valore è uno tra i casi di un elenco. In Cromo un caso può portare con sé dei dati, quindi un enum può dire non solo *quale* caso è un valore, ma anche *che cosa* lo accompagna. Insieme a `switch`, che il compilatore controlla rispetto a ogni caso, gli enum sono il modo più sicuro di rappresentare delle alternative. `switch` sceglie anche tra interi, caratteri e stringhe, come in C, ma senza le trappole del C.

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
    Direction first
    println("{:?}", first)
    ret 0
}
```

```output
South true
North
```

I casi si scrivono uno per riga, e un enum ne ha almeno uno. Un valore si scrive con il nome dell'enum, `Direction.North`: due enum possono avere casi con lo stesso nome, e non si confondono mai. Dentro uno `switch` sull'enum il tipo è noto, quindi basta il nome del caso; altrove `North` da solo è un nome sconosciuto.

Il valore predefinito di un enum è il suo primo caso: `first`, qui sopra, è `North`.

`==` e `!=` confrontano due valori di un enum i cui casi non hanno dati. `<` e `>` non funzionano sugli enum; per ordinarli, confronta i loro valori, `i32(a) < i32(b)`, descritti nella prossima sezione.

`{:?}` stampa il nome del caso. `{}` richiede un metodo `string text() const`, come per le struct: la sezione sui metodi, più avanti, ne mostra uno.

## I valori dei casi

Ogni caso di un enum senza dati ha un valore, un `i32`: la sua posizione, a partire da 0, oppure quello scritto dopo `=`. `i32(...)` lo legge, e `Color(n)` riconverte un numero in un caso:

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

- Il valore di un caso si converte in qualsiasi tipo intero, come `i64(...)` e `u8(...)`, con le regole delle conversioni esplicite tra interi. Non viene mai convertito da solo: `i32 x = Color.Green` è un errore.
- `Color(n)` accetta qualsiasi intero, ed è un panic, *the value is not a case of Color*, quando nessun caso ha il valore `n`.
- I valori sono letterali interi che stanno in un `i32`, anche negativi, e tutti diversi. O ogni caso ha un valore, o nessuno.

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

I valori sono utili dove un numero arriva dall'esterno, come un campo di un formato di file o una costante di una libreria C ([Chiamare il C](c-interop.md)). Solo i casi di un enum senza dati possono avere un valore.

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

Un caso con dati si crea come una chiamata, `Shape.Circle(1.0)`, con tutti i suoi dati; `Shape.Circle` da solo è un errore. Il valore predefinito è sempre il primo caso, con i valori predefiniti dei suoi dati: `Circle(0.0)`. `{:?}` stampa il caso con i suoi dati.

## switch su un enum

Su un enum, `switch` ha un ramo per ogni caso, `case Name { ... }`, con le graffe obbligatorie e senza i due punti. Il valore dello `switch` si scrive tra parentesi, e può essere un'espressione qualsiasi: una variabile, un elemento di una lista, il risultato di una chiamata.

### Dare un nome ai dati di un caso

In `case Circle(f64 r)` i dati del caso ricevono un nome, in sola lettura e senza copia, come l'elemento di un `for`. Ogni nome si scrive con il tipo del dato, esattamente quello, e un caso dà un nome a tutti i suoi dati o a nessuno: senza le parentesi i dati vengono ignorati.

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

### Ogni caso è coperto

Su un enum, `switch` deve gestire ogni caso, oppure avere un `default`, che prende i casi non elencati. Dimenticarne uno è un errore di compilazione, non un bug da scoprire più tardi:

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

Quando si aggiunge un caso a un enum, il compilatore elenca tutti gli `switch` che devono tenerne conto, a meno che non abbiano un `default`. Quindi `default` è comodo, ma rinuncia a questo aiuto: su un enum che crescerà, è meglio elencare i casi. Un caso scritto due volte nello stesso `switch` è un errore.

### Più casi in un ramo

Uno `switch` non ha fall-through: ogni ramo esegue il proprio blocco e lo `switch` termina. Più casi possono condividere un ramo, separati da virgole, quando il ramo non dà un nome ai loro dati:

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

`case Circle(f64 r), Rect { ... }` è un errore: in un ramo con più casi non ci sono dati a cui dare un nome. Un ramo vuoto, come `case Empty { }`, non fa nulla.

### Modificare i dati di un caso

Con `ref`, un caso dà i suoi dati da modificare, quando il valore dello `switch` si può modificare: una variabile locale, un parametro `ref`, un elemento di una lista.

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

Un nome senza `ref` è in sola lettura, e su un parametro in lettura `ref` non è permesso:

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

### Il valore di uno switch mentre un ramo usa i suoi dati

Finché un ramo ha dei nomi per i dati del caso, il valore dello `switch` non si può modificare, se non attraverso i nomi dati con `ref`: i nomi puntano dentro il valore, e dargli un altro caso li lascerebbe puntare al nulla. È la stessa regola che vale per una lista durante un `for` su di essa.

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

Un ramo che non dà un nome ai dati può dare al valore un nuovo caso:

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

## switch su interi, caratteri e stringhe

Oltre che sugli enum, `switch` funziona sui tipi interi, `char` compreso, e su `string`. I casi sono letterali, e un caso può elencarne più di uno:

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

- Su numeri e stringhe `default` è facoltativo: senza, un valore che non corrisponde a nessun caso non fa nulla, come in C. Qui `answer("maybe")` non stampa nulla.
- I casi sono letterali del tipo del valore: numeri, anche negativi o in esadecimale, caratteri (`'a'` è un numero, quindi è un caso anche su un `i64`) e stringhe. Una variabile o una costante non è un caso; per quelle, usa `if`.
- Lo stesso valore non può comparire due volte, e un valore che non sta nel tipo è un errore.
- Altri tipi, come `f64` e `bool`, non possono essere il valore di uno `switch`.

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

## break e continue in uno switch

Un ramo termina da solo, quindi `break` e `continue` non si possono scrivere al suo interno, nemmeno dentro un `if` del ramo. In C un `break` chiude un case; in Cromo uscirebbe dal ciclo che racchiude lo `switch`, e il compilatore ferma l'abitudine del C prima che diventi un bug:

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

Un ciclo scritto dentro un ramo può usare `break` e `continue` per sé. Per uscire dal ciclo che racchiude lo `switch`, il ramo imposta una variabile, e il ciclo la controlla dopo lo `switch`:

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

`ret` può uscire da un ramo, come in `turnRight` qui sopra, e così anche `throw`.

## Uno switch che termina una funzione

Una funzione che restituisce un valore deve terminare con `ret` su ogni percorso. Uno `switch` termina i percorsi quando tutti i suoi rami li terminano e copre ogni valore: su un enum, con tutti i casi o con `default`; su numeri e stringhe, solo con `default`.

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

Aggiungi un `default` oppure, se un altro valore significa che il programma è sbagliato, un `panic` dopo lo `switch` ([Errori](errors.md)).

## Metodi

Un enum può avere dei metodi, scritti dopo i casi, come una struct; i metodi `const` non modificano il valore. Dentro un metodo, `switch` senza valore lavora sull'enum stesso:

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

Dentro un metodo il valore stesso non ha nome, come nei metodi di una struct: non esiste `this`, e il valore intero non si può passare a una funzione né sostituire con un altro caso. Un metodo cambia i dati del suo caso attraverso `ref`, come fa `double`; per cambiare il caso, scrivi una funzione con un parametro `ref`, come `toggle` qui sopra.

Un metodo `string text() const` permette a `{}` di stampare l'enum, mentre `{:?}` continua a stampare il nome del caso ([Formattare il testo](formatting.md)):

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

Un enum non ha un costruttore né un distruttore scritti a mano: i suoi valori si creano con i suoi casi, e i suoi dati vengono distrutti da soli.

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

## Interfacce

Un enum può implementare delle interfacce, come una struct, nominandole dopo i due punti ([Interfacce e generici](interfaces-generics.md)):

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

Un enum che implementa `Exception` si può lanciare, con un caso per ogni tipo di errore ([Errori](errors.md)).

## I dati di un caso e il possesso

I dati di un caso si passano come gli argomenti di un parametro `move` ([Possesso](ownership.md)): un valore nuovo; una variabile, che viene copiata; oppure una variabile con `move`, che cede il suo valore senza copia. L'enum diventa il proprietario dei suoi dati:

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

`name` è stata copiata e resta utilizzabile; `greeting` è stata spostata e non si può più usare.

Un enum i cui casi possiedono risorse, come una `string` o una struct con un distruttore, distrugge i dati del suo caso corrente quando viene distrutto, e quando riceve un altro caso:

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

Assegnare un enum lo copia, come una struct: il suo `copy()` è generato dal compilatore, e copia i dati del suo caso. Quando i dati di uno dei suoi casi non si possono copiare, come `Resource` qui sopra, che ha un distruttore e nessun `copy()`, nemmeno l'enum si può copiare, e viene solo spostato:

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

La strada giusta è `Slot t = move s`.

## Enum generici

Un enum può avere anche dei parametri di tipo, come le struct generiche di [Interfacce e generici](interfaces-generics.md). Questo contiene un risultato oppure il motivo di un fallimento:

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

Un valore si crea scrivendo gli argomenti di tipo, `Outcome<i64>.Done(5)`: `Outcome.Done(5)` è un errore, anche dove il tipo è noto.

## Riepilogo

- Un enum elenca dei casi, uno per riga; un valore si scrive `Enum.Case`, e il valore predefinito è il primo caso, con i valori predefiniti dei suoi dati.
- I casi senza dati hanno un valore `i32`, la loro posizione o quello scritto con `=`: `i32(c)` lo legge, `Color(n)` restituisce il caso, oppure provoca un panic.
- `==` e `!=` confrontano gli enum senza dati; i casi con dati si distinguono con `switch`.
- Un caso può portare con sé dei dati, scritti come parametri e passati come argomenti `move`: copiati, oppure spostati con `move`. L'enum li possiede, e li distrugge quando viene distrutto o cambia caso.
- `switch` su un enum deve coprire ogni caso o avere `default`; non c'è fall-through, e più casi possono condividere un ramo.
- `case Circle(f64 r)` dà un nome ai dati, in sola lettura; con `ref` si possono modificare. Finché un ramo dà un nome ai dati, il valore dello `switch` non si può modificare in altri modi.
- `switch` funziona anche su interi, caratteri e stringhe, con letterali come casi; lì `default` è facoltativo.
- `break` e `continue` non si possono scrivere in un ramo di uno `switch`.
- Gli enum hanno metodi, `switch` senza valore al loro interno, interfacce e parametri di tipo; non hanno un costruttore né un distruttore scritti a mano.
