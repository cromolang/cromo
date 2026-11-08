# Liste, mappe e testo

Il nucleo della libreria, che ogni programma vede senza importarlo, contiene i contenitori di cui quasi ogni programma ha bisogno: `list`, gli array di dimensione fissa, `map` e `string`. Ognuno possiede i propri elementi e li libera quando finisce. Alla fine del capitolo, i vettori SIMD lavorano su più numeri insieme.

## Liste

`list<T>` è una sequenza di valori di tipo `T` che cresce quando serve, come `std::vector` in C++. Gli elementi stanno uno dopo l'altro sullo heap:

```cromo
i32 main() {
    list<i64> numbers = [5, 3, 8]
    numbers.add(13)
    numbers.insert(0, 1)
    i64 removed = numbers.remove(2)
    println("{:?}, removed {}, length {}", numbers, removed, numbers.len())

    numbers[0] = 10
    for (ref i64 n in numbers) {
        n *= 2
    }
    println("{:?}", numbers)
    ret 0
}
```

```output
[1, 5, 8, 13], removed 3, length 4
[20, 10, 16, 26]
```

Una lista dichiarata senza valore è vuota, come `list<i64>()`: ogni variabile ha un valore.

### Letterali di lista

`[a, b, c]` crea una lista. Il tipo dei suoi elementi è quello che il punto in cui compare si aspetta: il tipo dichiarato della variabile, il tipo del parametro, il tipo che la funzione restituisce. Dove non ci si aspetta nulla, è il tipo del primo elemento:

```cromo
f64 average(list<f64> values) {
    f64 sum = 0.0
    for (f64 v in values) {
        sum += v
    }
    ret sum / f64(values.len())
}

i32 main() {
    list<string> names = ["anna", "bruno"]
    list<f64> empty = []
    list<i64> primes = [
        2,
        3,
        5,
        7,
    ]
    println("{:?} {} {:?}", names, empty.len(), primes)
    println("{}", average([1, 2.5, 3]))
    println("{:?}", [i64(10), 20, 30])
    ret 0
}
```

```output
["anna", "bruno"] 0 [2, 3, 5, 7]
2.1666666666666665
[10, 20, 30]
```

- In `average([1, 2.5, 3])` il parametro dice che gli elementi sono `f64`, quindi anche `1` e `3` sono `f64`.
- Un letterale può stare su più righe, ed è ammessa una virgola dopo l'ultimo elemento.
- Gli elementi vengono dati alla lista come ad `add()`: una stringa o un altro valore che possiede memoria viene copiato, a meno che non sia scritto `move name`. Le regole delle copie e degli spostamenti sono in [Possesso](ownership.md).

Dove nulla indica il tipo, una lista di letterali numerici non compila, come non compila un `5` da solo; e nemmeno un `[]` vuoto. Un elemento non si può scrivere con `ref`: la lista tiene i propri elementi, non fa riferimento a variabili.

```cromo error
i32 main() {
    i64 x = 1
    println("{:?}", [1, 2])
    println("{:?}", [])
    list<i64> v = [ref x, 2]
    ret 0
}
```

```output
error: the type of the elements cannot be deduced: write the type of the list, as list<i64> values = [1, 2], or of the first element, as [i64(1), 2]
error: the type of an empty list cannot be deduced: write it, as list<i64> values = []
error: an element of a list is given to it, not passed with 'ref'
```

### Metodi ed elementi

| Metodo | Che cosa fa |
|---|---|
| `len()` | il numero di elementi, un `i64` |
| `add(x)` | aggiunge `x` in fondo |
| `insert(i, x)` | mette `x` alla posizione `i`, da 0 a `len()`; gli elementi da lì in poi avanzano di un posto |
| `remove(i)` | toglie l'elemento in `i` e lo restituisce; gli elementi successivi arretrano di un posto |
| `clear()` | toglie tutti gli elementi |
| `reserve(n)` | fa spazio per `n` elementi in tutto, così aggiungerli non sposta di nuovo la lista in memoria |
| `copy()` | una nuova lista con una copia di ogni elemento |
| `map<U>(f)` | una nuova lista con ciò che `f` restituisce per ogni elemento |
| `filter(f)` | una nuova lista con una copia degli elementi per cui `f` restituisce `true` |

`numbers[i]` è l'elemento stesso: si può leggere, assegnare, passare con `ref` e spostare con `move numbers[i]`, che lascia al suo posto il valore predefinito del tipo:

```cromo
i32 main() {
    list<string> names = ["anna", "bruno", "carla"]
    string first = move names[0]
    list<string> copied = names
    copied.add("dario")
    println("{} {:?} {:?}", first, names, copied)
    ret 0
}
```

```output
anna ["", "bruno", "carla"] ["", "bruno", "carla", "dario"]
```

`copied = names` copia la lista, con tutte le sue stringhe: le due liste sono indipendenti. `move` dà una lista a un'altra variabile senza copiarla.

Un indice fuori dalla lista, in `numbers[i]` come in `remove(i)` e `insert(i, x)`, è un panic, che ferma il programma:

```cromo file=scores.cro
i32 main() {
    list<i64> scores = [7, 9, 4]
    println("{}", scores[3])
    ret 0
}
```

```output
panic: index out of bounds: the index is 3 but the length is 3 at scores.cro:3:25
    at main (scores.cro:3)
```

Due liste non si possono confrontare con `==`: confronta i loro elementi in un ciclo.

### for su una lista

`for (T x in v)` visita gli elementi nel loro ordine. Senza `ref`, `x` è in sola lettura; con `ref`, il corpo può modificare ogni elemento attraverso `x`. Il tipo di `x` va scritto, e deve essere esattamente il tipo degli elementi:

```cromo error
i32 main() {
    list<i32> ages = [30, 40]
    for (i64 a in ages) {
        println("{}", a)
    }
    for (i32 a in ages) {
        a += 1
    }
    ret 0
}
```

```output
error: the elements of this list are i32: write for (i32 a in ...)
error: 'a' is an element of a read-only 'for': write 'for (ref i32 a in ...)' to modify it
```

Mentre un `for` visita una lista, il corpo può leggere la lista, ma non può modificarla se non attraverso `x`: nessun metodo che la modifica, nessun assegnamento, nessun `ref` o `move` della lista o dei suoi altri elementi. Aggiungere un elemento potrebbe spostare in memoria tutti gli elementi sotto i piedi del ciclo, e `ages[0] = 1` potrebbe essere lo stesso elemento di `x`:

```cromo error
i32 main() {
    list<i64> ages = [30, 40]
    for (ref i64 a in ages) {
        ages.add(a)
        ages[0] = 1
    }
    ret 0
}
```

```output
error: 'ages' cannot be modified while a 'for' goes over the elements of 'ages'
error: 'ages[0]' cannot be modified while a 'for' goes over the elements of 'ages'
```

Per cambiare la lunghezza di una lista mentre la si percorre, usa un ciclo con un indice, che rilegge la lunghezza a ogni passo:

```cromo
i32 main() {
    list<i64> numbers = [1, 2, 3, 4, 5, 6]
    i64 i = 0
    for (i < numbers.len()) {
        if (numbers[i] % 2 == 0) {
            numbers.remove(i)
        } else {
            i++
        }
    }
    println("{:?}", numbers)
    ret 0
}
```

```output
[1, 3, 5]
```

### map e filter

`map` crea una nuova lista a partire da ogni elemento, e `filter` tiene gli elementi che superano un test. Entrambi prendono una funzione, qui una funzione anonima, spiegata in [Funzioni anonime](lambdas.md):

```cromo
i32 main() {
    list<i64> numbers = [1, 2, 3, 4, 5, 6]
    list<i64> even = numbers.filter([](i64 n) bool { ret n % 2 == 0 })
    list<string> labels = even.map<string>([](i64 n) string { ret format("#{}", n) })
    println("{:?}", labels)
    ret 0
}
```

```output
["#2", "#4", "#6"]
```

La lista di partenza non cambia.

### Ordinare

`sort(ref items)` ordina una lista a partire dall'elemento più piccolo; `sortBy(ref items, less)` la ordina con una funzione che dice se il suo primo valore viene prima del secondo. Entrambe sono stabili: gli elementi uguali mantengono il loro ordine, come qui `kiwi` e `pear`, che hanno la stessa lunghezza:

```cromo
i32 main() {
    list<string> words = ["pear", "fig", "banana", "kiwi"]
    sort(ref words)
    println("{}", join(words, " "))
    sortBy(ref words, [](string a, string b) bool { ret a.len() < b.len() })
    println("{}", join(words, " "))
    ret 0
}
```

```output
banana fig kiwi pear
fig kiwi pear banana
```

`sort` funziona sulle liste i cui elementi implementano l'interfaccia `Ordered` del nucleo, con un solo metodo:

```cromo ignore
interface Ordered {
    i32 compare(Self other) const
}
```

`a.compare(b)` è negativo quando `a` viene prima di `b`, zero quando sono uguali, positivo quando `a` viene dopo. I numeri e `string` la implementano da sé; le stringhe sono ordinate byte per byte. Una struct la implementa quando dichiara `: Ordered` e scrive `compare`:

```cromo
struct Version : Ordered {
    i32 major
    i32 minor

    Version(i32 a, i32 b) {
        major = a
        minor = b
    }

    i32 compare(Version other) const {
        if (major != other.major) {
            ret major.compare(other.major)
        }
        ret minor.compare(other.minor)
    }
}

i32 main() {
    list<Version> versions = [Version(1, 10), Version(0, 9), Version(1, 2)]
    sort(ref versions)
    list<string> names
    for (Version v in versions) {
        names.add(format("{}.{}", v.major, v.minor))
    }
    println("{}", join(names, " "))
    println("{} {}", i64(3).compare(5), "b".compare("a"))
    ret 0
}
```

```output
0.9 1.2 1.10
-1 1
```

Le versioni vengono confrontate come numeri, quindi `1.10` viene dopo `1.2`, cosa che un ordinamento dei loro testi non farebbe.

Una struct senza `: Ordered` non viene ordinata da `sort`, anche se ha un metodo `compare`:

```cromo error
struct Point {
    i64 x
    i64 y
}

i32 main() {
    list<Point> points
    sort(ref points)
    ret 0
}
```

```output
error: Point does not implement Ordered, required by T of 'sort'
```

Per una lista del genere, o per un altro ordine, usa `sortBy`, come `sortBy(ref points, [](Point a, Point b) bool { ret a.x < b.x })`.

- Tra due float, `compare` restituisce 0 quando uno dei due è un NaN: un NaN è "uguale" a ogni numero, quindi una lista con dei NaN non ha un ordine ben definito. Toglili prima di ordinare.
- Se `less` lancia un'eccezione, `sortBy` la lascia arrivare al chiamante, e la lista mantiene tutti i suoi elementi, forse in un altro ordine.
- L'ordinamento è un merge sort: richiede O(n log n) confronti, anche nel caso peggiore.

## Array di dimensione fissa

Un array ha la sua dimensione nel tipo, e vive dove si trova la sua variabile, senza memoria sullo heap:

```cromo
i32 main() {
    i32[4] scores
    scores[1] = 7
    f64[2][3] grid
    grid[1][2] = 0.5
    println("{:?} has {} elements", scores, scores.len())
    for (f64[3] row in grid) {
        println("{:?}", row)
    }
    ret 0
}
```

```output
[0, 7, 0, 0] has 4 elements
[0.0, 0.0, 0.0]
[0.0, 0.0, 0.5]
```

- Ogni elemento parte con il valore predefinito del suo tipo. Non c'è un letterale di array per una variabile locale: gli elementi si assegnano uno per uno. Le costanti di un modulo possono essere array con un letterale, come mostra [Concetti comuni](common-concepts.md).
- La dimensione è un letterale intero, almeno 1. Un array occupa al massimo 2 GB.
- `f64[2][3]` sono due array di tre elementi, come in C: `grid[i][j]` ha `i < 2` e `j < 3`.
- `a.len()` è la dimensione, e `for (T x in a)` e `for (ref T x in a)` visitano gli elementi come per una lista, con le stesse regole.
- Un indice fuori dall'array è un panic, come per una lista; quando l'indice è una costante, è un errore del compilatore.
- Gli array non si confrontano con `==`, e si stampano solo con `{:?}`.

Un array è un valore, come una struct: può essere un campo, un parametro, un valore restituito. Viene copiato quando i suoi elementi sono valori semplici, come i numeri; un array di stringhe o di altri valori che possiedono memoria viene spostato, e non ha `copy()`:

```cromo error
i32 main() {
    i32[4] a
    a[4] = 1
    string[2] names
    string[2] other = names
    ret 0
}
```

```output
error: the index 4 is outside the array, which has 4 elements
error: values of type string[2] cannot be copied
```

Un array locale sta sullo stack del thread, che è di 1 MB su Windows e di 8 MB su Linux: un array più grande ferma il programma con `panic: stack overflow`. Una tabella grande va in una `list`, sullo heap.

## Mappe

`map<K, V>` associa chiavi a valori, come `std::unordered_map` in C++: è una tabella hash.

```cromo
i32 main() {
    map<string, i32> stock
    stock["apples"] = 10
    stock["pears"] = 4
    stock["apples"] -= 3
    stock["plums"] += 6

    println("{} apples, plums known: {}", stock["apples"], stock.contains("plums"))
    bool removed = stock.remove("pears")
    println("removed: {}, {} kinds left", removed, stock.len())

    i32 total = 0
    for (string fruit, i32 count in stock) {
        total += count
    }
    println("{} fruits", total)
    ret 0
}
```

```output
7 apples, plums known: true
removed: true, 2 kinds left
13 fruits
```

- `m[k] = v`, `m[k] += v` e `m[k]++` aggiungono una chiave mancante, con il valore predefinito, prima di modificarne il valore, come `operator[]` del C++.
- Qualsiasi altro uso di una chiave mancante è un panic: leggere `m[k]`, ma anche `m[k].add(x)` su una mappa di liste, o `m[a][b] = v` su una mappa di mappe quando `a` manca. Chiedi prima con `contains`, oppure assegna prima la chiave: `groups["fruit"] = []`.
- `remove(k)` restituisce `true` se la chiave c'era; `len()` è il numero di chiavi; `copy()` copia la mappa, e `b = a` fa lo stesso.
- Una chiave aggiunta alla mappa è una copia del valore dato.

`for (K key, V value in m)` visita la mappa senza un ordine preciso: non è l'ordine in cui le chiavi sono state aggiunte, può cambiare quando si aggiungono o si tolgono chiavi, e un programma non deve farci affidamento. Con `ref V value` il ciclo può modificare i valori; le chiavi sono sempre in sola lettura, perché cambiare una chiave la metterebbe nel posto sbagliato della tabella. Come per una lista, la mappa non può cambiare mentre un `for` la visita:

```cromo
i32 main() {
    map<string, i32> stock
    stock["apples"] = 10
    stock["pears"] = 4
    map<string, i32> before = stock
    for (string fruit, ref i32 count in stock) {
        count *= 2
    }
    println("{} {} {}", stock["apples"], stock["pears"], before["apples"])
    ret 0
}
```

```output
20 8 10
```

```cromo error
i32 main() {
    map<string, i32> stock
    for (ref string fruit, i32 count in stock) {
    }
    for (string fruit, i32 count in stock) {
        stock.remove(fruit)
    }
    ret 0
}
```

```output
error: the keys of a map are read-only: only the value can be 'ref'
error: 'stock' cannot be modified while a 'for' goes over the elements of 'stock'
```

`{:?}` stampa una mappa come `{"apples": 20, "pears": 8}`, nel suo ordine.

### Chiavi

Le chiavi possono essere numeri, `bool`, `string` e le struct che implementano l'interfaccia `Key` del nucleo:

```cromo ignore
interface Key {
    u64 hash() const
    bool equals(Self other) const
}
```

Due chiavi uguali devono avere lo stesso hash. I numeri, `bool` e `string` hanno `hash()` ed `equals()` da sé, quindi una struct può costruire il suo hash da quelli dei suoi campi:

```cromo
struct Cell : Key {
    i32 row
    i32 column

    u64 hash() const {
        ret row.hash() ^ column.hash() << 1
    }

    bool equals(Cell other) const {
        ret row == other.row && column == other.column
    }
}

i32 main() {
    map<Cell, string> board
    Cell c
    c.row = 1
    c.column = 2
    board[c] = "rook"

    Cell same
    same.row = 1
    same.column = 2
    println("{} {}", board.contains(same), board[same])
    ret 0
}
```

```output
true rook
```

Una struct senza `: Key` non può essere una chiave: `map<Point, i32>` dà `error: Point does not implement Key, required by K of map`.

Con i float come chiavi, `-0.0` e `0.0` sono la stessa chiave, e un NaN non viene mai trovato, perché non è uguale a sé stesso: ogni `m[nan] = v` aggiunge una nuova chiave.

`Key` nomina `Self`, il tipo che la implementa, quindi è solo un vincolo di un generico, mai il tipo di un parametro o di una variabile: `void f(Key k)` è un errore. Un generico che crea una mappa del suo `T` lo richiede con `T : Key`:

```cromo
i64 distinct<T : Key>(list<T> items) {
    map<T, bool> seen
    for (T item in items) {
        seen[item] = true
    }
    ret seen.len()
}

i32 main() {
    println("{} {}", distinct<i64>([1, 2, 2, 3]), distinct<string>(["a", "b", "a"]))
    ret 0
}
```

```output
3 2
```

[Interfacce e generici](interfaces-generics.md) spiega i vincoli.

## Stringhe

Una `string` è testo in UTF-8, come `std::string` in C++. Possiede i propri byte, e si copia o si sposta come una lista:

```cromo
i32 main() {
    string text = "  Hello, World  "
    string clean = text.trim()
    println("[{}]", clean)
    println("{} {} {}", clean.find("World"), clean.contains("lo"), clean.startsWith("Hello"))
    println("{} / {}", clean.upper(), clean.replace("World", "Cromo"))
    println("{:?}", "a,b,,c".split(","))
    println("{}", join(["x", "y", "z"], " - "))
    println("{}", "ab".repeat(3))
    ret 0
}
```

```output
[Hello, World]
7 true true
HELLO, WORLD / Hello, Cromo
["a", "b", "", "c"]
x - y - z
ababab
```

Le funzioni sul testo sono metodi di `string`. I metodi di questa tabella non modificano la stringa: ognuno restituisce un nuovo valore. Posizioni e lunghezze sono in byte, come `i64`.

| Metodo | Che cosa restituisce |
|---|---|
| `len()` | la lunghezza in byte |
| `find(part)` | la posizione del primo `part`, o -1 |
| `findFrom(part, from)` | la posizione del primo `part` in `from` o dopo, o -1 |
| `findLast(part)` | la posizione dell'ultimo `part`, o -1 |
| `contains(part)`, `startsWith(part)`, `endsWith(part)` | se `part` è nel testo, al suo inizio, alla sua fine |
| `split(separator)` | le parti tra i separatori, come `list<string>` |
| `replace(old, new)` | una copia in cui ogni `old` diventa `new` |
| `trim()`, `trimStart()`, `trimEnd()` | una copia senza gli spazi alle due estremità, all'inizio, alla fine |
| `upper()`, `lower()` | una copia con le lettere A-Z e a-z cambiate |
| `repeat(n)` | il testo ripetuto `n` volte |
| `slice(from, to)` | una copia dei byte da `from` a `to` escluso |
| `bytes()` | una copia dei byte, come `list<u8>` |
| `copy()` | una copia della stringa |

`join(parts, separator)` è una funzione, non un metodo: mette una dopo l'altra le stringhe di una `list<string>`, con il separatore tra l'una e l'altra.

### Cercare

```cromo
i32 main() {
    string line = "one two one two"
    println("{} {} {}", line.find("two"), line.findFrom("two", 5), line.findLast("one"))
    println("{} {}", line.find("six"), line.contains("six"))
    println("{} {} {}", line.find(""), line.findFrom("", 3), line.findLast(""))
    ret 0
}
```

```output
4 12 8
-1 false
0 3 15
```

- Un testo che non c'è dà -1, non un errore: una ricerca che non trova nulla è un risultato normale.
- Un `part` vuoto si trova ovunque: `find("")` è 0, `findFrom("", from)` è `from`, `findLast("")` è `len()`; `contains("")`, `startsWith("")` ed `endsWith("")` sono `true`.
- `findFrom` accetta `from` tra 0 e `len()`; fuori da questi limiti è un panic.

### Dividere, sostituire e togliere gli spazi

```cromo
i32 main() {
    println("{:?} {:?} {:?}", "abc".split(","), "".split(","), ",a,".split(","))
    println("{:?}", "a, b, c".split(", "))
    println("[{}] [{}]", "aaa".replace("aa", "b"), "ab".repeat(0))
    println("{:?}", " \t\n x y \r\n".trim())
    println("{:?} {:?}", "  x  ".trimStart(), "  x  ".trimEnd())
    println("{} {}", "Città".upper(), "ÀB".lower())
    ret 0
}
```

```output
["abc"] [""] ["", "a", ""]
["a", "b", "c"]
[ba] []
"x y"
"x  " "  x"
CITTà Àb
```

- `split` restituisce una parte in più dei separatori: due separatori vicini danno una parte vuota, e un testo senza il separatore dà una sola parte con tutto il testo, anche quando il testo è vuoto. Il separatore può essere più lungo di un byte.
- `replace` procede da sinistra, senza sovrapposizioni: `"aaa".replace("aa", "b")` è `"ba"`.
- `repeat(0)` è la stringa vuota.
- `trim` toglie gli spazi dell'ASCII: lo spazio, il tab, il line feed, il tab verticale, il form feed e il carriage return. Gli spazi di Unicode, come lo spazio non separabile, restano.
- `upper` e `lower` cambiano solo le lettere dell'ASCII: `à` resta `à`.
- Un separatore vuoto in `split`, un `old` vuoto in `replace` e un numero negativo in `repeat` sono un panic: non hanno un risultato sensato.

### Costruire stringhe

`+` unisce due stringhe in una nuova, e `+=` o `append` aggiunge in fondo a una stringa. `reserve(n)` fa spazio per `n` byte, così una stringa costruita un pezzo alla volta non viene spostata in memoria a ogni pezzo. Un numero diventa testo con `format`, descritto in [Formattare il testo](formatting.md), e un testo diventa un numero con una conversione, come `i64("42")`, che lancia un'eccezione quando il testo non è un numero:

```cromo
i32 main() {
    string path = "/home"
    path += "/anna"
    path.append("/notes")
    string line
    line.reserve(64)
    for (i64 i = 1; i <= 3; i++) {
        line += format("{}:{} ", i, i * i)
    }
    i64 n = i64("42") + 1
    println("{} [{}] {}", path, line.trimEnd(), n)
    println("{} {} {}", "abc" < "abd", "ab" < "abc", "Z" < "a")
    ret 0
}
```

```output
/home/anna/notes [1:1 2:4 3:9] 43
true true true
```

Le stringhe si confrontano con `==`, `!=`, `<`, `<=`, `>`, `>=`, byte per byte: in UTF-8 è l'ordine dei code point, in cui ogni lettera maiuscola dell'ASCII viene prima di ogni minuscola. Una stringa non si unisce a un numero con `+`: scrivi `format("{}{}", text, n)`.

### Stringhe e byte

La lunghezza di una stringa è in byte, e una lettera fuori dall'ASCII ne occupa più di uno. `s[i]` è il byte alla posizione `i`, un `u8`:

```cromo
i32 main() {
    string word = "città"
    println("{} bytes; the first is {}, the letter {:c}", word.len(), word[0], word[0])
    println("'{}'", word.slice(0, 3))
    list<u8> bytes = word.bytes()
    string again = string(bytes)
    println("{} {} {}", bytes.len(), again, again == word)
    ret 0
}
```

```output
6 bytes; the first is 99, the letter c
'cit'
6 città true
```

- `s[i]` è in sola lettura: una stringa si cambia costruendone una nuova. Un indice fuori dalla stringa è un panic.
- `for` non percorre una stringa: un ciclo sui suoi byte usa un indice, `for (i64 i = 0; i < s.len(); i++)`.
- `slice(from, to)` prende i byte da `from` a `to` escluso. Posizioni fuori dalla stringa, `from` dopo `to`, o una porzione che taglia in due un carattere UTF-8 sono un panic: `"città".slice(0, 5)` ferma il programma, perché `à` occupa i byte 4 e 5.
- `bytes()` restituisce una copia dei byte. `string(bytes)` ne ricava di nuovo una stringa, dopo aver controllato che siano UTF-8 valido; altrimenti lancia un `Error` il cui messaggio dice dove i byte smettono di essere UTF-8, come `the bytes are not valid UTF-8: the character at 4`.

```cromo error
i32 main() {
    string s = "abc"
    s[0] = 'x'
    ret 0
}
```

```output
error: the bytes of a string are read-only: build a new string
```

## Vettori

Un vettore SIMD contiene un numero fisso di numeri, su cui il processore lavora insieme: `f32x8` contiene otto `f32`, e `i16x16` sedici `i16`. I suoi operatori agiscono su tutti i numeri contemporaneamente, spesso con una sola istruzione AVX2, SSE o NEON:

```cromo
i32 main() {
    list<f32> prices = [1.5, 2.0, 3.5, 4.0, 0.5, 6.0, 7.5, 8.0]
    f32x8 all = f32x8.load(prices, 0)  // eight numbers in one value
    f32x8 taxed = all * 1.2            // eight multiplications in one instruction
    i32x8 expensive = taxed > 5.0      // a mask: -1 where true, 0 where false
    f32x8 capped = expensive.select(f32x8(5.0), taxed)
    println("{:.2f} {:.2f} {}", taxed.sum(), capped.sum(), expensive.any())
    ret 0
}
```

```output
39.60 28.80 true
```

Il nome di un tipo è il tipo dei numeri, `x` e quanti sono, per 128 o 256 bit:

| Numeri | 128 bit | 256 bit |
|---|---|---|
| `i8`, `u8` | `i8x16`, `u8x16` | `i8x32`, `u8x32` |
| `i16`, `u16` | `i16x8`, `u16x8` | `i16x16`, `u16x16` |
| `i32`, `u32` | `i32x4`, `u32x4` | `i32x8`, `u32x8` |
| `i64`, `u64` | `i64x2`, `u64x2` | `i64x4`, `u64x4` |
| `f32` | `f32x4` | `f32x8` |
| `f64` | `f64x2` | `f64x4` |

Un vettore è un valore semplice, come un numero: viene copiato, e può stare in una lista, un array, una struct o una mappa. Il suo valore predefinito ha 0 in ogni numero.

```cromo
i32 main() {
    i32[4] values
    values[0] = 5
    values[1] = -6
    values[2] = 7
    values[3] = -8
    i32x4 a = i32x4.load(values, 0)
    i32x4 ones = i32x4(1)
    a[1] = 100
    println("{:?} {} {}", a * 2 + ones, a[1], a.len())
    println("{:?} {:?}", a.abs(), a.max(ones))
    println("{} {} {}", a.sum(), a.smallest(), a.largest())

    i32x4 positive = a > 0
    println("{:?} {} {}", positive, positive.all(), positive.any())
    println("{:?}", positive.select(a, ones))

    f32x4 big = f32x4(1e20)
    println("{:?} {:?}", f32x4(a), i32x4(big))

    list<i32> out = [0, 0, 0, 0, 0, 0]
    a.store(ref out, 2)
    println("{:?}", out)
    ret 0
}
```

```output
[11, 201, 15, -15] 100 4
[5, 100, 7, 8] [5, 100, 7, 1]
104 -8 100
[-1, -1, -1, 0] false true
[5, 100, 7, 1]
[5.0, 100.0, 7.0, -8.0] [2147483647, 2147483647, 2147483647, 2147483647]
[0, 0, 5, 100, 7, -8]
```

- `T(x)` mette il numero `x` in ogni numero del vettore. `T(v)` converte uno per uno i numeri di un vettore che ne ha altrettanti: da un float a un intero il risultato è saturato, senza panic, quindi `1e20` diventa il più grande `i32`.
- `T.load(values, at)` legge i numeri dalla posizione `at` di una `list` o di un array degli stessi numeri, e `v.store(ref values, at)` ve li scrive. `v[i]` legge e scrive un numero, e `len()` dice quanti sono. Un indice fuori, anche in `load` e `store`, è un panic.
- `+`, `-` e `*` agiscono numero per numero, come `/` per i float, e `-v` cambia segno ai numeri con segno e ai float; `&`, `|`, `^` e `!` agiscono sui bit degli interi, e `<<` e `>>` li spostano di una quantità controllata come per un singolo intero. Un numero da una parte va in ogni numero, come `a * 2`. Gli interi ricominciano dall'estremo opposto, senza i controlli di overflow.
- `/` tra vettori di interi è un errore del compilatore, perché i processori non hanno la divisione dei vettori di interi; `%` non funziona su nessun vettore.
- Un confronto dà una maschera, un vettore di interi con segno larghi quanto i numeri, con -1 dove il confronto è vero e 0 dove è falso: `f32x8 > f32x8` dà un `i32x8`. Una maschera ha `all()`, `any()` e `select(a, b)`, che prende i numeri di `a` dove è vera e quelli di `b` dove è falsa.
- `sum()`, `smallest()` e `largest()` restituiscono un solo numero; la somma dei float viene fatta nell'ordine che il SIMD preferisce, che può differire nelle ultime cifre da quello di un ciclo. `abs()` funziona sui numeri con segno e sui float, `min(w)` e `max(w)` numero per numero, e `low()` e `high()` restituiscono le due metà di un vettore di 256 bit.
- Un vettore si stampa con `{:?}`, come un array; `{}` non lo stampa.

```cromo error
i32 main() {
    i32x4 a = i32x4(6)
    i32x4 half = a / i32x4(2)
    println("{}", a)
    ret 0
}
```

```output
error: '/' works on vectors of floats only: the processors have no division of integer vectors
error: a i32x4 cannot be printed with {}: print its form of debug with {:?}
```

Di default un programma usa le istruzioni che ogni processore x86-64 ha, SSE2, e un vettore di 256 bit diventa due da 128; `cromo build --cpu x86-64-v3` compila l'intero programma per i processori con AVX2. Su ARM64 i vettori usano NEON.

## Formattazione

`println`, `print` e `format` scrivono numeri, stringhe e gli altri valori con una stringa di formato, come fanno `std::format` del C++ e Python: `{:>8.2f}` scrive un float con due decimali, allineato a destra in otto caratteri. [Formattare il testo](formatting.md) è il riferimento completo.

## Riepilogo

- `list<T>` cresce quando serve; `[a, b]` ne crea una, con il tipo che il punto in cui compare si aspetta; `v[i]` è l'elemento stesso, controllato rispetto alla lunghezza.
- `for (T x in v)` legge gli elementi, `for (ref T x in v)` li modifica; mentre il ciclo è in esecuzione, la lista non cambia in nessun altro modo.
- `sort` ordina le liste di valori `Ordered`, con `compare`; `sortBy` prende una funzione; entrambe sono stabili.
- `T[n]` è un array di dimensione fissa, senza heap, copiato quando i suoi elementi sono semplici.
- `map<K, V>` è una tabella hash; `m[k] += v` aggiunge una chiave mancante, e qualsiasi altra lettura di una chiave mancante è un panic; l'ordine non è definito. Una struct è una chiave con `Key`, `hash` ed `equals`.
- `string` è UTF-8, misurata in byte; i suoi metodi cercano, dividono, sostituiscono e tolgono gli spazi, e restituiscono sempre una nuova stringa. `s[i]` è un byte in sola lettura.
- `string(bytes)` controlla l'UTF-8; `slice` non può tagliare un carattere.
- `f32x8`, `i16x16` e gli altri vettori lavorano su molti numeri insieme, con il SIMD.
