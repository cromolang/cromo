# Concetti comuni

Questo capitolo presenta ciò che ogni linguaggio ha: variabili, tipi, funzioni, commenti e controllo del flusso. Un programmatore C o C++ ne riconoscerà la maggior parte; le differenze sono poche, ma contano.

## Variabili

Una variabile si dichiara con il tipo prima del nome, sempre:

```cromo
i32 main() {
    i32 apples = 5
    i64 total = apples + 10
    string name = "Cromo"
    bool ready = true
    println("{} {} {} {}", apples, total, name, ready)
    ret 0
}
```

```output
5 15 Cromo true
```

Non c'è inferenza di tipo: il tipo si scrive anche quando il valore basterebbe a dedurlo. Il codice è più lungo di qualche carattere e più facile da leggere, perché hai sempre davanti il tipo di ogni variabile.

### Ogni variabile ha un valore

Una variabile dichiarata senza valore prende il **valore predefinito** del suo tipo: 0 per i numeri, `false` per `bool`, la stringa vuota e la lista vuota.

```cromo
i32 main() {
    i32 count
    f64 average
    string label
    list<i32> numbers
    println("[{}] [{}] [{}] [{}]", count, average, label, numbers.len())
    ret 0
}
```

```output
[0] [0] [] [0]
```

In Cromo non esistono variabili non inizializzate, e quindi nemmeno il comportamento indefinito che ne deriva.

### Costanti

Le variabili possono cambiare. `const` ne rende una immutabile:

```cromo error
i32 main() {
    const f64 PI = 3.14159
    PI = 3.0
    ret 0
}
```

```output
error: 'PI' is const
```

Una variabile `const` deve ricevere il suo valore nel punto in cui viene dichiarata.

## Tipi

### Interi

| Tipo | Dimensione | Intervallo |
|---|---|---|
| `i8`, `u8` | 8 bit | da −128 a 127, da 0 a 255 |
| `i16`, `u16` | 16 bit | da −32 768 a 32 767, da 0 a 65 535 |
| `i32`, `u32` | 32 bit | circa ±2,1 miliardi, da 0 a 4,2 miliardi |
| `i64`, `u64` | 64 bit | circa ±9,2 × 10¹⁸, da 0 a 1,8 × 10¹⁹ |

Ogni tipo ha la stessa dimensione su tutti i sistemi: non c'è un `int` la cui dimensione dipende dalla macchina, né un `size_t`. Lunghezze e posizioni sono `i64`.

I letterali interi si possono scrivere in decimale, esadecimale e binario, con `_` per separare le cifre: `1_000_000`, `0xFF`, `0b1010`.

### Overflow

Un'operazione tra interi il cui risultato non sta nel suo tipo è un errore. In una build di debug, quella predefinita, il programma si ferma:

```cromo file=overflow.cro
i32 main() {
    i8 small = 120
    for (i32 i = 0; i < 10; i++) {
        small += 1
    }
    println("{}", small)
    ret 0
}
```

```output
panic: integer overflow at overflow.cro:4:9
    at main (overflow.cro:4)
```

Un *panic* ferma il programma, stampa dove è avvenuto e le chiamate che hanno portato fin lì, e restituisce il codice di uscita 101. In una build di release, con `--release`, gli interi invece ricominciano dall'estremo opposto, come in C: il controllo ha un piccolo costo, e un programma già testato può farne a meno. La divisione per zero è sempre un panic.

### Virgola mobile, booleani e caratteri

- `f32` e `f64` sono i numeri in virgola mobile dello standard IEEE 754: `3.14`, `1e-9`.
- `bool` vale `true` o `false`.
- `char` è un byte, sinonimo di `u8`. Un letterale carattere si scrive tra apici singoli e deve essere ASCII: `'a'`, `'\n'`, `'\x41'`. Stampato con `{}`, un `char` appare come numero.

### Stringhe

`string` è testo in UTF-8. La sua lunghezza è in byte, e `s[i]` è un byte:

```cromo
i32 main() {
    string city = "Torino"
    string greeting = "Hello, " + city + "!"
    println("{} has {} bytes, the first is {}", greeting, greeting.len(), greeting[0])
    ret 0
}
```

```output
Hello, Torino! has 14 bytes, the first is 72
```

Il capitolo 7 mostra le funzioni sul testo: cercare, dividere, sostituire.

### Conversioni

Un numero viene convertito implicitamente solo quando non si può perdere informazione: un `i32` diventa da solo un `i64` o un `f64`. In tutti gli altri casi la conversione si scrive come una chiamata con il nome del tipo:

```cromo
i32 main() {
    i32 x = 7
    i64 big = x
    f64 exact = f64(x) / 2.0
    i32 back = i32(exact)
    i32 wide = 300
    u8 byte = u8(wide)
    println("{} {} {} {}", big, exact, back, byte)

    i32 parsed = i32("42") + 1
    println("{}", parsed)
    ret 0
}
```

```output
7 3.5 3 44
43
```

`i32(exact)` scarta la parte frazionaria; `u8(wide)` tiene gli 8 bit più bassi. `i32("42")` legge un numero da un testo, e lancia un'eccezione se il testo non è un numero.

### I letterali prendono il tipo dal contesto

Un letterale non ha un tipo suo: prende quello richiesto dal punto in cui si trova. In `i64 total = apples + 10`, `10` è un `i32` come `apples`. Dove nulla indica il tipo, il compilatore chiede di specificarlo:

```cromo error
i32 main() {
    println("{}", 10 / 3)
    ret 0
}
```

```output
error: the type of this expression cannot be deduced
```

Basta scrivere `i32(10) / 3`, o usare una variabile, per risolvere.

## Operatori

Gli operatori sono quelli del C, con una differenza nella precedenza: gli operatori bit a bit legano più dei confronti, quindi `flags & MASK == 0` significa `(flags & MASK) == 0`, come ci si aspetterebbe. Dal più forte:

| Operatori | Significato |
|---|---|
| `*` `/` `%` | moltiplicazione, divisione, resto |
| `+` `-` | addizione, sottrazione |
| `<<` `>>` | shift |
| `&` | and bit a bit |
| `^` | or esclusivo bit a bit |
| `\|` | or bit a bit |
| `==` `!=` `<` `<=` `>` `>=` | confronti |
| `&&` | and logico |
| `\|\|` | or logico |

`!` nega un `bool` e inverte i bit di un intero. `i++` e `i--` esistono, ma solo come istruzioni a sé stanti, mai dentro un'espressione: `a[i++]` non è Cromo.

## Funzioni

```cromo
i64 square(i64 x) {
    ret x * x
}

void greet(string name, i32 times) {
    for (i32 i = 0; i < times; i++) {
        println("Hello, {}!", name)
    }
}

i32 main() {
    println("{}", square(12))
    greet("Ada", 2)
    ret 0
}
```

```output
144
Hello, Ada!
Hello, Ada!
```

Una funzione che restituisce un valore deve arrivare a un `ret` su ogni percorso, altrimenti il programma non compila. Una funzione `void` può terminare senza `ret`. L'ordine delle funzioni in un file non conta: `main` può venire per prima e chiamare funzioni scritte dopo.

Una funzione ha un solo risultato. Quando potrebbe non avere nulla da dare, restituisce un valore convenzionale, lancia un'eccezione, oppure restituisce un `bool` e scrive il risultato in un parametro passato con `ref`, come spiega il capitolo 4. Non esiste `null`.

## Commenti

```cromo ignore
// un commento fino alla fine della riga
/* un commento
   su più righe */
```

## Istruzioni e righe

Un'istruzione finisce alla fine della sua riga, senza punto e virgola. Prosegue sulla riga successiva quando la riga termina con un operatore, una virgola o una parentesi aperta:

```cromo
i32 main() {
    i64 total = 1 +
        2 +
        3
    println("{}",
        total)
    ret 0
}
```

```output
6
```

C'è un'istruzione per riga.

## Condizioni

```cromo
i32 main() {
    i32 temperature = 23
    if (temperature > 30) {
        println("hot")
    } else if (temperature < 10) {
        println("cold")
    } else {
        println("mild")
    }
    ret 0
}
```

```output
mild
```

Le graffe sono sempre obbligatorie, e la condizione deve essere un `bool`: `if (count)` è un errore, `if (count != 0)` è corretto.

## Cicli

Cromo ha un solo ciclo, `for`, in quattro forme:

```cromo
i32 main() {
    // La forma classica del C.
    for (i32 i = 0; i < 3; i++) {
        print("{} ", i)
    }
    println("")

    // Solo una condizione, dove il C usa while.
    i32 n = 1
    for (n < 100) {
        n *= 3
    }
    println("{}", n)

    // Senza niente, fino a un break.
    for {
        n -= 50
        if (n < 0) {
            break
        }
    }
    println("{}", n)

    // Sugli elementi di una lista.
    list<string> names = ["Ada", "Alan", "Grace"]
    for (string name in names) {
        if (name == "Alan") {
            continue
        }
        println("{}", name)
    }
    ret 0
}
```

```output
0 1 2 
243
-7
Ada
Grace
```

`break` esce dal ciclo e `continue` passa al giro successivo. Non esistono `while`, `do` né `goto`.

## switch

`switch` sceglie tra interi o stringhe. Non c'è fall-through: ogni `case` ha il suo blocco, e un `case` può elencare più valori. `default` prende i valori non elencati:

```cromo
string describe(i32 status) {
    switch (status) {
        case 200 {
            ret "ok"
        }
        case 301, 302 {
            ret "moved"
        }
        case 404 {
            ret "not found"
        }
        default {
            ret "other"
        }
    }
}

i32 main() {
    println("{}, {}, {}", describe(200), describe(302), describe(500))
    ret 0
}
```

```output
ok, moved, other
```

`switch` dà il meglio di sé con gli enum, come mostra il capitolo 6: lì il compilatore controlla che ogni caso sia gestito.

## Riepilogo

- Il tipo viene prima del nome; ogni variabile ha un valore; `const` la rende immutabile.
- Gli interi hanno una dimensione fissa; l'overflow e la divisione per zero fermano il programma.
- Le conversioni che possono perdere informazione vanno scritte: `i32(x)`.
- Le funzioni restituiscono il risultato con `ret`; non esiste `null`.
- `if` vuole le graffe e un `bool`; `for` è l'unico ciclo; `switch` non ha fall-through.
