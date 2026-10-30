# Liste, mappe e testo

Il nucleo della libreria, che ogni programma vede senza importarlo, contiene i contenitori di cui quasi ogni programma ha bisogno: `list`, gli array di dimensione fissa, `map` e `string`. Ognuno possiede i propri elementi e li libera quando finisce.

## Liste

`list<T>` è una sequenza di valori di tipo `T` che cresce quando serve, come `std::vector` in C++:

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

- `[a, b, c]` crea una lista; una lista vuota è `list<i64>()`, o semplicemente una variabile senza valore.
- `add`, `insert(i, x)`, `remove(i)`, `clear()` e `reserve(n)` modificano la lista; `len()` ne restituisce la lunghezza.
- `numbers[i]` è l'elemento stesso: si può leggere, assegnare, passare con `ref` e spostare. Un indice fuori dalla lista è un panic.
- `for (ref i64 n in numbers)` visita gli elementi e può modificarli; senza `ref` sono in sola lettura.

Mentre un `for` visita una lista, la lista non può cambiare lunghezza: aggiungere un elemento nel ciclo sposterebbe gli elementi in memoria sotto i piedi del ciclo. Il compilatore lo rifiuta.

### map e filter

`map` crea una nuova lista trasformando ogni elemento, e `filter` tiene gli elementi che superano un test. Entrambi prendono una funzione, qui una funzione anonima, spiegata nel capitolo 10:

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

## Array di dimensione fissa

Un array ha una dimensione fissata nel suo tipo, e vive dove si trova la sua variabile, senza memoria sullo heap:

```cromo
i32 main() {
    i32[4] scores
    scores[1] = 7
    f64[2][3] grid
    grid[1][2] = 0.5
    println("{:?} has {} elements", scores, scores.len())
    println("{:?}", grid)
    ret 0
}
```

```output
[0, 7, 0, 0] has 4 elements
[[0.0, 0.0, 0.0], [0.0, 0.0, 0.5]]
```

`f64[2][3]` sono due array di tre elementi. Un array è un valore: viene copiato quando i suoi elementi sono semplici, spostato altrimenti.

## Mappe

`map<K, V>` associa chiavi a valori, con una tabella hash:

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

- `m[k] = v`, `m[k] += v` e `m[k]++` aggiungono una chiave mancante, con il valore predefinito, prima di modificarne il valore, come `operator[]` del C++. Leggere una chiave mancante in qualsiasi altro modo è un panic: chiedi prima con `contains`.
- `for (K key, V value in m)` visita la mappa senza un ordine preciso; con `ref V value` può modificare i valori.
- Le chiavi possono essere numeri, `bool`, `string` e le struct che implementano l'interfaccia `Key`, con `u64 hash() const` e `bool equals(Self other) const`.

## Stringhe

Una `string` è testo in UTF-8. Possiede i propri byte, e si sposta o si copia come una lista:

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

Le funzioni sul testo sono metodi di `string`: `find`, `findFrom`, `findLast`, `contains`, `startsWith`, `endsWith`, `split`, `trim`, `trimStart`, `trimEnd`, `replace`, `upper`, `lower`, `repeat`, più la funzione `join(parts, separator)`. Le posizioni sono in byte, e un testo non trovato dà -1. `upper` e `lower` cambiano solo le lettere ASCII.

### Stringhe e byte

La lunghezza di una stringa è in byte, e una lettera fuori dall'ASCII ne occupa più di uno:

```cromo
i32 main() {
    string word = "città"
    list<u8> bytes = word.bytes()
    println("{} bytes, the first three are '{}'", word.len(), word.slice(0, 3))
    string again = string(bytes)
    println("{} {}", again, again == word)
    ret 0
}
```

```output
6 bytes, the first three are 'cit'
città true
```

`slice(from, to)` prende i byte da `from` a `to` escluso. `string(bytes)` controlla che i byte siano UTF-8 valido, e altrimenti lancia un'eccezione.

Le stringhe si confrontano con `==`, `!=`, `<`, `<=`, `>`, `>=`, byte per byte, e si concatenano con `+` e `+=`.

## Formattazione

`println`, `print`, `eprintln` (sullo standard error) e `format` (che restituisce una `string`) condividono il formato di `std::format` del C++ e di Python. Ogni `{}` prende il valore successivo; dentro le graffe, dopo `:`, una specifica dice come scriverlo:

```cromo
i32 main() {
    f64 ratio = 2.0 / 3.0
    i64 n = 1234567
    println("[{:.3f}] [{:8.2f}] [{:e}]", ratio, ratio, ratio)
    println("[{:<6}] [{:^7}] [{:>6}]", "ab", "mid", "r")
    println("[{:+}] [{:09}] [{:x}] [{:#x}] [{:b}]", n, n, n, n, n)
    println("{0} and {1}, then {0} again", "first", "second")
    println("{{ and }} are braces")
    ret 0
}
```

```output
[0.667] [    0.67] [6.666667e-01]
[ab    ] [  mid  ] [     r]
[+1234567] [001234567] [12d687] [0x12d687] [100101101011010000111]
first and second, then first again
{ and } are braces
```

| Specifica | Significato |
|---|---|
| `<` `^` `>` | allinea a sinistra, al centro, a destra, nella larghezza che segue |
| un numero | la larghezza minima; se preceduto da `0`, riempita di zeri |
| `.n` | le cifre decimali, o la lunghezza massima di un testo |
| `+` | il segno anche per i numeri positivi |
| `x` `X` `b` `o` | esadecimale, binario, ottale; `#` aggiunge il prefisso |
| `f` `e` `g` | virgola fissa, notazione esponenziale, la più breve delle due |
| `?` | la forma per il debug, per qualsiasi valore |

La stringa di formato deve essere un letterale: il compilatore controlla il numero e i tipi dei valori in base a essa.

## Riepilogo

- `list<T>` cresce quando serve; `[a, b]` ne crea una; `for` la visita, con `ref` per modificarne gli elementi.
- `T[n]` è un array di dimensione fissa, senza heap.
- `map<K, V>` è una tabella hash; `m[k] += v` aggiunge una chiave mancante; l'ordine non è definito.
- `string` è UTF-8, misurata in byte, con metodi per cercare, dividere e sostituire.
- `format` e `println` usano il formato di `std::format`, controllato dal compilatore.
