# Formattare il testo

Cinque funzioni integrate scrivono dei valori come testo: `print`, `println`, `eprint`, `eprintln` e `format`. Ricevono una stringa di formato con dei segnaposto, `{}`, e i valori da metterci dentro, con la sintassi di `std::format` del C++ e di `format` di Python. Il compilatore legge la stringa di formato mentre compila il programma: confronta i segnaposto con i valori e trasforma la chiamata nel codice che scrive ogni pezzo, così durante l'esecuzione non c'è nulla da analizzare.

## Stampare

```cromo
i32 main() {
    string name = "Anna"
    i64 age = 30
    print("Hello, ")
    println("{}!", name)
    println("{} is {} years old", name, age)
    string line = format("{}:{}", name, age)
    println("{} has {} bytes", line, line.len())
    println("")
    println("the end")
    ret 0
}
```

```output
Hello, Anna!
Anna is 30 years old
Anna:30 has 7 bytes

the end
```

- `print` scrive il testo così com'è; `println` aggiunge un a capo alla fine. `println("")` scrive una riga vuota.
- `format` scrive in una nuova `string`, che restituisce, invece che sullo schermo.
- `eprint` ed `eprintln` scrivono sullo standard error, il flusso degli avvisi e degli errori, che resta sullo schermo quando lo standard output va in un file o in una pipe:

```cromo
i32 main() {
    i64 missing = 3
    println("checking the files")
    eprintln("warning: {} files are missing", missing)
    ret 0
}
```

```output
checking the files
warning: 3 files are missing
```

Ogni chiamata scrive tutto il suo testo in una volta: quando più thread stampano, le loro righe non si mescolano.

### Il compilatore controlla il formato

La stringa di formato è un letterale, scritto nella chiamata. Il compilatore ne conta i segnaposto, controlla che ogni valore venga usato e che ciascuno si possa stampare nel modo chiesto dal suo segnaposto. Un testo contenuto in una variabile si stampa con un segnaposto, `println("{}", text)`: così una `{` dentro il testo non viene mai scambiata per un segnaposto.

Un letterale numerico tra i valori ha bisogno del suo tipo, perché nient'altro dice quale sia: `println("{}", i64(5))`, oppure una variabile.

```cromo error
i32 main() {
    string message = "50% done"
    println(message)
    println("{} of {}", message)
    println("{}", message, message)
    println("{}", 5)
    ret 0
}
```

```output
error: the first argument of println must be a string literal with the format
error: the format string needs at least 2 arguments, but 1 were given
error: this argument is not used by the format string
error: the type of this literal cannot be deduced: write it explicitly, for example i32(5)
```

## Segnaposto

`{}` prende il valore successivo. `{0}`, `{1}` e così via prendono un valore in base alla sua posizione, a partire da 0, così un valore si può usare più di una volta e in qualsiasi ordine. `{{` e `}}` scrivono una graffa:

```cromo
i32 main() {
    println("{} and {}", "first", "second")
    println("{0} and {1}, then {0} again", "first", "second")
    println("{1} before {0}", "a", "b")
    println("{{ and }} are braces; {} is a value", "{}")
    ret 0
}
```

```output
first and second
first and second, then first again
b before a
{ and } are braces; {} is a value
```

Un formato usa o `{}` o i numeri, non entrambi. Una graffa da sola è un errore, come lo è un segnaposto con qualcosa di diverso da un numero prima dei `:`:

```cromo error
i32 main() {
    i64 n = 5
    println("{} {1}", n, n)
    println("a } b", n)
    println("a { b", n)
    println("{n}", n)
    ret 0
}
```

```output
error: cannot mix automatic '{}' and numbered '{0}' placeholders
error: single '}' in the format string: write '}}' for a brace
error: '{' without a matching '}' in the format string: write '{{' for a brace
error: invalid placeholder '{n}'
```

## La specifica

Dopo la posizione, un `:` apre la specifica del valore, che dice come scriverlo. Le sue parti sono tutte facoltative, ma vengono in quest'ordine, dove le parentesi quadre segnano ciò che si può omettere:

```text
{position:[[fill]align][sign][#][0][width][.precision][type]}
```

| Parte | Significato |
|---|---|
| fill | il carattere che riempie la larghezza, uno spazio di default; si scrive solo prima di un allineamento |
| align | `<` a sinistra, `^` al centro, `>` a destra |
| sign | `+` scrive il segno anche prima dei numeri positivi; uno spazio scrive uno spazio prima di essi, `{: }`; `-` scrive solo il meno dei negativi, il default |
| `#` | il prefisso della base: `0x`, `0X`, `0b`, `0` |
| `0` | riempie un numero di zeri dopo il segno, fino alla larghezza |
| width | il numero minimo di caratteri, al massimo 1000 |
| .precision | per i float le cifre, per il testo il numero massimo di caratteri; al massimo 500 |
| type | una lettera, come `x` per l'esadecimale o `f` per un float in virgola fissa, oppure `?` per la forma per il debug |

### Larghezza, riempimento e allineamento

La larghezza è un minimo: un valore più corto viene riempito fino a raggiungerla, uno più lungo viene scritto per intero. Di default i numeri vanno a destra, il testo e i `bool` a sinistra:

```cromo
i32 main() {
    i64 n = 42
    string word = "città"
    println("[{:8}] [{:8}] [{:8}]", n, word, true)
    println("[{:<8}] [{:^8}] [{:>8}]", n, word, true)
    println("[{:*^9}] [{:-<8}] [{:.>8}] [{:2}]", "mid", "left", n, "longer")
    ret 0
}
```

```output
[      42] [città   ] [true    ]
[42      ] [ città  ] [    true]
[***mid***] [left----] [......42] [longer]
```

- La larghezza conta i caratteri, non i byte: `città` ha sei byte, ma occupa cinque posti.
- Con `^`, uno spazio che non si divide in due va a destra.
- Il riempimento è un solo carattere ASCII, diverso da `{` e `}`, e si scrive solo prima di un allineamento: `{:*^9}`, mai `{:*9}`.

### Segno e zeri

```cromo
i32 main() {
    i64 n = 42
    i64 m = -42
    f64 x = 3.14159
    println("[{:+}] [{:+}] [{: }] [{: }] [{:-}]", n, m, n, m, n)
    println("[{:06}] [{:06}] [{:+08.2f}] [{:<06}]", n, m, x, n)
    ret 0
}
```

```output
[+42] [-42] [ 42] [-42] [42]
[000042] [-00042] [+0003.14] [42    ]
```

Gli zeri vanno dopo il segno e dopo il prefisso di `#`, e contano nella larghezza. Con un allineamento, lo `0` non ha effetto: decidono il riempimento e l'allineamento.

### Precisione

Per un float, la precisione è il numero di cifre: dopo il punto decimale con `f` ed `e`, in tutto con `g` o senza una lettera del tipo. Per una stringa, è il numero massimo di caratteri, e il resto viene tagliato:

```cromo
i32 main() {
    f64 x = 3.14159
    f64 y = 1234.5
    println("[{:.2f}] [{:.0f}] [{:.3e}] [{:.3g}] [{:.3}]", x, x, y, y, y)
    println("[{:.3}] [{:>6.2}]", "abcdef", "città")
    ret 0
}
```

```output
[3.14] [3] [1.234e+03] [1.23e+03] [1.23e+03]
[abc] [    ci]
```

L'ultima cifra è arrotondata al valore più vicino, a partire dal valore esatto che il float contiene: `2.675` è memorizzato come 2.67499999..., quindi `{:.2f}` scrive `2.67`. Una precisione è un errore per un intero e per un `bool`.

### Lettere del tipo

| Lettera | Per | Scrive |
|---|---|---|
| `d` | interi | in decimale, il default |
| `x`, `X` | interi | in esadecimale, con le lettere minuscole o maiuscole; `#` aggiunge `0x` o `0X` |
| `b` | interi | in binario; `#` aggiunge `0b` |
| `o` | interi | in ottale; `#` aggiunge `0` |
| `c` | `u8` (`char`) | il carattere del byte |
| `f` | float | in virgola fissa, con sei decimali di default |
| `e` | float | con l'esponente, `6.666667e-01`, con sei decimali di default |
| `g` | float | la più breve tra `f` ed `e`, con sei cifre di default |
| `s` | stringhe, `bool`, tipi con `text()` | il testo, il default |
| `?` | ogni valore tranne funzioni e interfacce | la forma per il debug |

```cromo
i32 main() {
    i64 n = 1234567
    println("[{:d}] [{:x}] [{:#x}] [{:X}] [{:#X}]", n, n, n, n, n)
    println("[{:b}] [{:o}] [{:#o}] [{:#010x}]", n, n, n, n)
    u8 letter = 'A'
    println("[{}] [{:c}] [{:>3c}] [{:#04x}]", letter, letter, letter, letter)
    f64 ratio = 2.0 / 3.0
    println("[{:f}] [{:e}] [{:g}] [{:8.2f}]", ratio, ratio, ratio, ratio)
    i32 m = -42
    println("[{:x}] [{:x}] [{:b}]", m, u32(m), u8(m))
    ret 0
}
```

```output
[1234567] [12d687] [0x12d687] [12D687] [0X12D687]
[100101101011010000111] [4553207] [04553207] [0x0012d687]
[65] [A] [  A] [0x41]
[0.666667] [6.666667e-01] [0.666667] [    0.67]
[-2a] [ffffffd6] [11010110]
```

Un numero negativo mantiene il segno in ogni base, come in `std::format`: `-42` è `-2a`. Per vederne i bit, convertilo nel tipo senza segno della stessa dimensione, come `u32(m)`, che conserva i bit.

Una lettera che non si addice al tipo del valore è un errore, e lo sono anche le parti che un tipo non ha:

```cromo error
i32 main() {
    i64 n = 5
    f64 x = 1.5
    string s = "hi"
    i32 code = 65
    println("{:f}", n)
    println("{:x}", x)
    println("{:.2}", n)
    println("{:+}", s)
    println("{:c}", code)
    println("{:q}", n)
    ret 0
}
```

```output
error: 'f' is not a format for integers
error: 'x' is not a format for floating-point numbers
error: a precision is not allowed for integers
error: a string only allows fill, alignment, width and precision
error: 'c' prints a character: the value must be a char (u8)
error: invalid format specification 'q'
```

Non esistono le maiuscole `E` e `G`.

## Come si stampa ogni tipo con {}

- Un intero si scrive in decimale. Un `char` è un `u8`, quindi `{}` ne scrive il numero, e `{:c}` il carattere.
- Un `bool` è `true` o `false`.
- Una `string` si scrive così com'è.
- Un float si scrive nella forma più breve che, riletta, dà lo stesso numero: `0.1`, non `0.1000000000000000055`. Un float con un valore intero non ha il punto, `100`, e la forma con l'esponente si usa quando è più breve, `1e+20`.

```cromo
i32 main() {
    f64 a = 0.1
    f64 b = 100.0
    f64 c = 100000.0
    f64 d = 1e20
    f64 e = 0.1 + 0.2
    f64 tiny = 1.5e-7
    println("{} {} {} {} {} {}", a, b, c, d, e, tiny)
    f32 single = 0.1
    println("{} {:.10f}", single, single)
    f64 zero = 0.0
    println("{} {} {} {}", 1.0 / zero, -1.0 / zero, zero / zero, -zero)
    ret 0
}
```

```output
0.1 100 1e+05 1e+20 0.30000000000000004 1.5e-07
0.1 0.1000000015
inf -inf nan -0
```

Un `f32` si scrive nella forma più breve per un `f32`: `0.1`, anche se il numero che contiene è 0.1000000015. `0.1 + 0.2` non è esattamente `0.3`, e la forma più breve lo dice. Una divisione per zero tra float dà un infinito o un NaN, scritti `inf`, `-inf` e `nan`.

Per vedere sempre i decimali, indica una precisione, come `{:.2f}`; per vedere sempre che il numero è un float, usa `{:?}`, che scrive `100.0`.

## Stampare i propri tipi: text()

Una struct o un enum si stampa con `{}` quando ha un metodo `string text() const`. Il testo che restituisce viene poi formattato come una stringa: gli si applicano la larghezza, l'allineamento e la precisione.

```cromo
struct Money {
    i64 cents

    string text() const {
        ret format("{}.{:02}", cents / 100, cents % 100)
    }
}

enum Level {
    Low
    High

    string text() const {
        switch {
            case Low {
                ret "low"
            }
            case High {
                ret "HIGH"
            }
        }
    }
}

i32 main() {
    Money price
    price.cents = 1250
    println("[{}] [{:>10}] [{:*<10}] [{:.4}]", price, price, price, price)
    Level level = Level.High
    println("[{}] [{:^8}] [{:?}]", level, level, level)
    ret 0
}
```

```output
[12.50] [     12.50] [12.50*****] [12.5]
[HIGH] [  HIGH  ] [High]
```

- `text()` non ha parametri, è `const` e restituisce una `string`. Un metodo `text` di un'altra forma non conta.
- Deve essere visibile dove il valore viene stampato: se lo si stampa in un altro modulo, il metodo è `public string text() const`, altrimenti il compilatore dice `method 'text' of Money is private to its module`.
- La lettera del tipo, se c'è, è `s`, come per una stringa.
- `text()` serve solo a `{}`: `{:?}` scrive comunque la forma per il debug, come `High` qui sopra.

## La forma per il debug: {:?}

`{:?}` scrive qualsiasi valore in una forma pensata per il programmatore: per i log, per i test, per dare un'occhiata a un valore mentre si scrive un programma. Il compilatore la genera per ogni tipo, senza che si debba scrivere nulla:

```cromo
struct Point {
    i64 x
    i64 y
}

struct Box<T> {
    T item
}

struct Marker {
}

enum Shape {
    Circle(f64 radius)
    Rect(f64 w, f64 h)
    Empty
}

i32 main() {
    Point p
    p.x = 1
    p.y = 2
    Box<i64> b
    b.item = 7
    Marker m
    println("{:?} {:?} {:?}", p, b, m)
    println("{:?} {:?} {:?}", Shape.Circle(2.5), Shape.Rect(1.0, 2.0), Shape.Empty)

    list<Point> points = [p, p]
    i32[3] counts
    map<string, f64> prices
    prices["fig"] = 3.0
    println("{:?} {:?} {:?}", points, counts, prices)

    f64 whole = 9.0
    u8 letter = 'A'
    println("{:?} {:?} {:?} {:?}", whole, letter, true, f32x4(1.5))
    println("{:?}", "say \"hi\"\n\tcittà")
    ret 0
}
```

```output
Point { x: 1, y: 2 } Box { item: 7 } Marker
Circle(2.5) Rect(1.0, 2.0) Empty
[Point { x: 1, y: 2 }, Point { x: 1, y: 2 }] [0, 0, 0] {"fig": 3.0}
9.0 65 true [1.5, 1.5, 1.5, 1.5]
"say \"hi\"\n\tcittà"
```

| Valore | Forma per il debug |
|---|---|
| intero, `char` | come con `{}`: `65` |
| float | come con `{}`, con `.0` aggiunto quando quella forma non ha né un punto né un esponente: `9.0`, `0.1`, `1e+20` |
| `bool` | `true`, `false` |
| `string` | tra virgolette, con le sequenze di escape di Cromo: `\"`, `\\`, `\n`, `\r`, `\t`, e `\x01` per gli altri byte di controllo |
| lista, array, vettore | `[a, b, c]` |
| mappa | `{key: value, ...}`, nell'ordine della mappa |
| struct | `Name { field: value, ... }`, con tutti i suoi campi, anche quelli privati; `Name` se non ha campi; una struct generica senza i suoi argomenti di tipo, `Box { item: 7 }` |
| enum | il case, con i suoi valori in ordine: `Circle(2.5)`, `Empty` |

Le parti si stampano con la loro forma per il debug, anche quando hanno `text()`, quindi una stringa dentro una lista o una struct sta tra virgolette. Il nome di una struct o di un enum è quello della sua dichiarazione, senza il modulo.

La forma per il debug è una stringa, e le si applica la specifica di una stringa: larghezza, riempimento e allineamento, e una precisione che la taglia.

```cromo
struct Point {
    i64 x
    i64 y
}

i32 main() {
    Point p
    list<i64> v = [1, 2, 3]
    println("[{:>22?}] [{:*<12?}] [{:.5?}]", p, v, v)
    ret 0
}
```

```output
[  Point { x: 0, y: 0 }] [[1, 2, 3]***] [[1, 2]
```

### La forma per il debug nei generici

Un generico stampa il suo `T` con `{:?}` solo se lo chiede, con il vincolo `T : Debug` del nucleo: ogni tipo implementa `Debug` da sé, tranne le funzioni e le interfacce. `debugText(x)` dà lo stesso testo come `string`:

```cromo
struct Point {
    i64 x
    i64 y
}

void show<T : Debug>(string label, T value) {
    println("{}: {:?}", label, value)
}

i32 main() {
    Point p
    show("point", p)
    show<list<f64>>("list", [1.0, 2.5])
    string d = debugText(p)
    println("{} bytes", d.len())
    ret 0
}
```

```output
point: Point { x: 0, y: 0 }
list: [1.0, 2.5]
20 bytes
```

Le funzioni e le interfacce non hanno una forma per il debug, e non ce l'hanno nemmeno le struct e le liste che le contengono. Il messaggio prosegue indicando la parte che non ce l'ha, qui `Handler.run has type func(i64) i64`:

```cromo error
struct Handler {
    string name
    func(i64) i64 run
}

void show<T>(T value) {
    println("{:?}", value)
}

i32 main() {
    Handler h
    println("{:?}", h)
    ret 0
}
```

```output
error: values of type Handler cannot be printed with {:?}
error: T can be printed with {:?} only with a constraint: write <T : Debug>
```

## Cosa {} non stampa

`{}` stampa i numeri, `bool`, `string`, e le struct e gli enum con `text()`. Gli altri valori non hanno un testo proprio, e il compilatore dice cosa usare al suo posto:

- una struct o un enum senza `text()`, una `list` e una `map`: il messaggio propone `text()` e `{:?}`;
- un array, un vettore, una funzione, un'interfaccia e il `T` di un generico: il messaggio propone `{:?}`, che funziona per gli array, i vettori e un `T : Debug`.

```cromo error
struct Point {
    i64 x
    i64 y
}

i32 main() {
    Point p
    list<i64> v = [1, 2]
    i32[3] a
    println("{}", p)
    println("{}", v)
    println("{}", a)
    ret 0
}
```

```output
error: a Point cannot be printed with {}: give it a method string text() const
error: a list<i64> cannot be printed with {}: give it a method string text() const
error: a i32[3] cannot be printed with {}: print its form of debug with {:?}
```

Un valore stampato sia con `{}` sia con `{:?}` si passa due volte, una per ogni segnaposto, come in `println("{} {:?}", p, p)`: il compilatore trasforma ogni argomento in un solo testo, quello di `text()` oppure quello della forma per il debug.

```cromo error
struct Money {
    i64 cents

    string text() const {
        ret format("{}", cents)
    }
}

i32 main() {
    Money price
    println("{0} {0:?}", price)
    ret 0
}
```

```output
error: this argument is printed both with {} and with {:?}: pass it twice
```

## Riepilogo

- `print` e `println` scrivono sullo standard output, `eprint` ed `eprintln` sullo standard error, `format` in una nuova `string`.
- La stringa di formato è un letterale, controllato dal compilatore: il numero dei valori, i loro tipi e la specifica di ogni segnaposto.
- `{}` prende il valore successivo, `{0}` un valore in base alla posizione; `{{` e `}}` sono graffe.
- Dopo `:` vengono riempimento e allineamento, segno, `#`, `0`, larghezza, precisione e una lettera del tipo: `{:*>10}`, `{:+08.2f}`, `{:#x}`.
- Larghezza e precisione contano i caratteri, non i byte.
- Gli interi si stampano in decimale, i `char` come numeri salvo con `{:c}`, i float nella forma più breve che, riletta, dà lo stesso numero.
- Una struct o un enum con `string text() const` si stampa con `{}`, e la specifica si applica al suo testo.
- `{:?}` stampa ogni valore tranne funzioni e interfacce in una forma per il debug, con nomi, virgolette e sequenze di escape; accetta anche larghezza e allineamento.
- Un valore stampato sia con `{}` sia con `{:?}` si passa due volte.
