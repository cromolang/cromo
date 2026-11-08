# Concetti comuni

Questo capitolo presenta ciò che ogni linguaggio ha: variabili e costanti, tipi, funzioni, commenti e le istruzioni che scelgono e ripetono. Un programmatore C o C++ ne riconoscerà la maggior parte; le differenze sono poche, ma contano. I numeri, con i loro operatori e le loro conversioni, hanno il capitolo successivo, [Numeri e operatori](operators.md).

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

Il valore si dà sempre con `=`. La forma del C++ con le parentesi, `string name("Cromo")`, non è Cromo, e una struct si crea come gli altri valori: `Counter c = Counter(5)`.

```cromo error
i32 main() {
    string name("Cromo")
    ret 0
}
```

```output
error: expected a newline at the end of the statement (one statement per line), found '('
```

`=` dà anche un nuovo valore a una variabile, e `+=`, `-=` e gli altri assegnamenti composti lo modificano; `i++` e `i--` aggiungono e tolgono uno. Sono istruzioni su una riga a sé, mai parti di un'espressione: [Numeri e operatori](operators.md) li elenca tutti.

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

### Un nome, una variabile

Una variabile vive fino alla fine del blocco, tra graffe, in cui è dichiarata. Dentro quel blocco il suo nome non si può dichiarare di nuovo, nemmeno in un blocco interno: non c'è shadowing, il nome indica sempre la stessa variabile. Lo stesso vale per i parametri di una funzione.

```cromo error
i32 main() {
    i32 count = 1
    for (i32 i = 0; i < 3; i++) {
        i32 count = i * 2
        println("{}", count)
    }
    ret 0
}
```

```output
error: a variable named 'count' is already visible here (declared at line 2)
```

Finito il suo blocco, un nome torna libero: due cicli uno dopo l'altro possono dichiarare entrambi `i32 i`, e due blocchi affiancati possono dichiarare lo stesso nome. Dentro un metodo, inoltre, una variabile non può avere il nome di un campo della struct, come mostra [Struct](structs.md).

## Costanti

Le variabili possono cambiare. `const` ne rende una immutabile: deve ricevere il suo valore nel punto in cui viene dichiarata, e quel valore non cambia più. Il valore può venire da qualsiasi espressione, anche da una calcolata mentre il programma è in esecuzione, come `final` in Java: una `const` non è necessariamente nota al compilatore.

```cromo
i64 square(i64 x) {
    ret x * x
}

i32 main() {
    i64 side = 12
    const i64 area = square(side)
    const list<string> days = ["Mon", "Tue", "Wed"]
    println("{} {} {}", area, days.len(), days[1])
    ret 0
}
```

```output
144 3 Tue
```

`const` è profondo: una lista o una stringa `const` non si può modificare in nessuna sua parte, quindi se ne possono chiamare solo i metodi che non la modificano, e non si può passare con `ref` a una funzione che la modifica.

```cromo error
i32 main() {
    const f64 PI = 3.14159
    const list<i32> primes = [2, 3, 5]
    PI = 3.0
    primes.add(7)
    ret 0
}
```

```output
error: 'PI' is const and cannot be modified
error: cannot call 'add', which is not const
```

Anche una `const` senza valore è un errore: `const i32 n` da sola non ha nulla da conservare. Una `const` si può comunque cedere con `move`, perché il suo valore non cambia, passa soltanto a un altro proprietario: lo spiega [Possesso](ownership.md).

### Costanti di un modulo

Fuori dalle funzioni, un modulo può dichiarare anche delle costanti, come limiti e tabelle di numeri. Contengono numeri, `bool` e `char`, o array di dimensione fissa di questi. Il loro valore è un letterale, con `-` davanti a un numero negativo, e per un array una lista di letterali tra parentesi quadre, uno per ogni elemento. Le vede solo il loro modulo.

```cromo
const i64 minute = 60
const i64 below = -5
const u8[4] masks = [1, 2, 4, 8]

i32 main() {
    println("{} {} {}", 3 * minute, below, masks[3])
    ret 0
}
```

```output
180 -5 8
```

Il valore di una costante di un modulo non può essere un'espressione, nemmeno di altre costanti. Non esistono variabili globali: una dichiarazione fuori dalle funzioni senza `const` è un errore.

```cromo error
const i64 minute = 60
const i64 hour = 60 * minute
i64 counter = 0

i32 main() {
    ret 0
}
```

```output
error: the value of a constant of a module is a number, a character, true or false
error: a variable outside the functions is only for the modules with 'import unsafe'
```

Il secondo messaggio parla del dialetto con cui la libreria standard dialoga con il sistema, [Il dialetto unsafe](unsafe.md). In un programma normale, un valore condiviso da più funzioni si passa come parametro, o si tiene in una struct.

## Tipi

I tipi integrati nel linguaggio sono pochi:

| Tipo | Valori |
|---|---|
| `i8` `i16` `i32` `i64` | interi con segno da 8, 16, 32 e 64 bit |
| `u8` `u16` `u32` `u64` | interi senza segno da 8, 16, 32 e 64 bit |
| `f32` `f64` | numeri in virgola mobile dello standard IEEE 754 |
| `bool` | `true` o `false` |
| `char` | un byte, un altro nome di `u8` |
| `string` | testo in UTF-8 |

Ogni tipo ha la stessa dimensione su tutti i sistemi, e non ci sono interi a 128 bit. I numeri, i loro letterali, i loro operatori e le loro conversioni sono in [Numeri e operatori](operators.md). Liste, mappe e array di dimensione fissa sono in [Liste, mappe e testo](collections.md); struct ed enum, i tipi che un programma definisce, hanno capitoli propri.

### Booleani

Un `bool` vale `true` o `false`. I confronti ne producono uno, e `&&`, `||` e `!` li combinano:

```cromo
i32 main() {
    i32 age = 20
    bool adult = age >= 18
    bool teen = age >= 13 && age <= 19
    println("{} {} {}", adult, teen, !adult)
    ret 0
}
```

```output
true false false
```

Un `bool` non è un numero: non si converte in 0 e 1, e un numero non è vero o falso. Le condizioni di `if` e `for` vogliono un `bool`.

### Caratteri

`char` è un byte: è un altro nome di `u8`, un byte di una stringa UTF-8. Un letterale carattere si scrive tra apici singoli e deve essere ASCII: `'a'`, `'7'`, `' '`. Le sequenze di escape sono `'\n'` (a capo), `'\t'` (tabulazione), `'\r'` (ritorno carrello), `'\0'` (zero), `'\\'`, `'\''` e `'\x41'`, un byte in esadecimale.

Poiché un `char` è un numero, l'aritmetica dei numeri funziona anche su di esso. Stampato con `{}` è un numero; `{:c}` stampa il carattere:

```cromo
i32 main() {
    char letter = 'c'
    char upper = letter - 'a' + 'A'
    bool isLetter = letter >= 'a' && letter <= 'z'
    println("{} {:c} {:c} {}", letter, letter, upper, isLetter)
    ret 0
}
```

```output
99 c C true
```

Un carattere fuori dall'ASCII occupa più di un byte in UTF-8, quindi non è un `char`, e il compilatore dice cosa scrivere al suo posto:

```cromo error
i32 main() {
    char e = 'é'
    ret 0
}
```

```output
error: the character 'é' does not fit in one byte: use the string "é" or the code point 0xE9
```

### Stringhe

`string` è testo in UTF-8. `+` unisce due stringhe. La lunghezza è in byte, e `s[i]` è un byte, un `char`:

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

`s[i]` si può leggere ma non scrivere: per cambiare una stringa se ne costruisce una nuova.

```cromo error
i32 main() {
    string city = "Torino"
    city[0] = 't'
    ret 0
}
```

```output
error: the bytes of a string are read-only: build a new string
```

[Liste, mappe e testo](collections.md) mostra le funzioni sul testo: cercare, dividere, sostituire, e i byte delle lettere fuori dall'ASCII.

## Funzioni

Una funzione dichiara il tipo del suo risultato prima del nome, e il tipo di ogni parametro prima del parametro. `ret` restituisce il valore; una funzione che non restituisce nulla è `void`, e può terminare senza `ret`.

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

L'ordine delle funzioni in un file non conta: `main` può venire per prima e chiamare funzioni scritte dopo. Due funzioni non possono avere lo stesso nome, nemmeno con parametri diversi, e i parametri non hanno valori predefiniti.

Un parametro è in sola lettura: la funzione legge il valore del chiamante e non può cambiarlo. Una funzione che modifica un argomento lo dichiara con `ref`, e una che lo prende per sé con `move`, come spiega [Possesso](ownership.md).

```cromo error
void reset(i32 n) {
    n = 0
}

i32 main() {
    reset(5)
    ret 0
}
```

```output
error: parameter 'n' cannot be modified: parameters are read-only
```

Una funzione ha un solo risultato, e non esiste `null`. Quando potrebbe non avere nulla da dare, restituisce un valore convenzionale, come -1 per una posizione non trovata, lancia un'eccezione, oppure restituisce un `bool` e scrive il risultato in un parametro passato con `ref`.

### Ogni percorso finisce con ret

Una funzione che restituisce un valore deve finire, su ogni percorso, con un'istruzione dopo la quale non si prosegue: `ret`, `throw`, che spiega [Errori](errors.md), una chiamata di `panic`, un `if` con un `else` i cui rami finiscono tutti così, un `for` infinito senza `break`, oppure uno `switch` i cui case finiscono tutti così e coprono ogni valore. Altrimenti il programma non compila:

```cromo error
i32 sign(i64 n) {
    if (n > 0) {
        ret 1
    } else if (n < 0) {
        ret -1
    }
}

i32 main() {
    ret sign(5)
}
```

```output
error: function 'sign' must end with 'ret' on every path: it returns i32
```

Quando `n` è 0, `sign` arriva alla fine senza un valore. Un `else { ret 0 }` risolve. Le due funzioni qui sotto finiscono negli altri modi:

```cromo
// The first power of two that is not smaller than n.
i64 nextPower(i64 n) {
    i64 p = 1
    for {
        if (p >= n) {
            ret p
        }
        p *= 2
    }
}

i64 firstNegative(list<i64> values) {
    for (i64 v in values) {
        if (v < 0) {
            ret v
        }
    }
    throw Error("no negative value")
}

i32 main() {
    println("{} {}", nextPower(100), firstNegative([3, -4, 5]))
    ret 0
}
```

```output
128 -4
```

Il codice che viene dopo un `ret`, `break`, `continue`, `throw` o `panic` nello stesso blocco non può mai essere eseguito: il compilatore lo accetta, con l'avviso `unreachable code`.

## Commenti

```cromo ignore
// a comment until the end of the line
/* a comment
   on several lines */
```

I commenti `//` subito sopra una dichiarazione, senza righe vuote in mezzo, sono la sua documentazione: VS Code li mostra sotto il mouse, nel completamento e mentre si scrivono gli argomenti di una chiamata, come fa per la libreria standard. Un commento alla fine della riga di un campo documenta il campo. Il testo è Markdown, con il codice dentro una frase tra apici singoli, `'r'`, e un blocco di codice tra due righe `'''`:

```cromo ignore
// The area of a circle of radius 'r'.
//
// '''
// f64 a = area(2.0)
// '''
f64 area(f64 r) {
    ret 3.14159 * r * r
}

struct Point {
    f64 x  // from the left
    f64 y  // from the top
}
```

## Istruzioni e righe

Un'istruzione finisce alla fine della sua riga, senza punto e virgola. Prosegue sulla riga successiva quando la riga termina con un operatore tra due valori, come `+` o `&&`, con una virgola, una parentesi aperta, `?` o `:`, oppure finché una parentesi tonda o quadra è ancora aperta:

```cromo
i32 main() {
    i64 total = 1 +
        2 +
        3
    list<i64> values = [
        10,
        20
    ]
    println("{} {}",
        total, values.len())
    ret 0
}
```

```output
6 2
```

L'operatore va alla fine della riga, non all'inizio della successiva: lì, la prima riga è già un'istruzione completa.

```cromo error
i32 main() {
    i64 total = 1
        + 2
    ret 0
}
```

```output
error: expected an expression, found '+'
```

C'è un'istruzione per riga, e `;` compare solo nell'intestazione del `for` classico. Il compilatore accetta anche la graffa aperta sulla riga successiva, ed `else` sulla riga dopo la graffa chiusa, ma lo stile di Cromo li tiene sulla stessa riga, e `cromo fmt` ce li sposta: lo spiega [Strumenti: formattazione, editor e debugger](tools.md).

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

Le parentesi e le graffe sono sempre obbligatorie, anche intorno a una sola istruzione, e la condizione deve essere un `bool`: `if (count)` è un errore, `if (count != 0)` è corretto.

```cromo error
i32 main() {
    i32 count = 3
    if (count) {
        println("some")
    }
    ret 0
}
```

```output
error: the condition must be a bool, found i32
```

### Scegliere un valore

`c ? a : b` vale `a` quando `c` è vera, `b` altrimenti, come in C. Viene calcolato solo il valore scelto, e i due hanno lo stesso tipo:

```cromo
i32 main() {
    i64 n = 7
    string kind = n % 2 == 0 ? "even" : "odd"
    println("{} is {}", n, kind)
    ret 0
}
```

```output
7 is odd
```

[Numeri e operatori](operators.md#lespressione-condizionale) ne dà la precedenza e le altre regole.

## Cicli

Cromo ha un solo ciclo, `for`, in quattro forme:

```cromo
i32 main() {
    // The classic form of C.
    for (i32 i = 0; i < 3; i++) {
        print("{} ", i)
    }
    println("")

    // Only a condition, where C has while.
    i32 n = 1
    for (n < 100) {
        n *= 3
    }
    println("{}", n)

    // Without anything, until a break.
    for {
        n -= 50
        if (n < 0) {
            break
        }
    }
    println("{}", n)

    // Over the elements of a list.
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

`break` esce dal ciclo e `continue` passa al giro successivo; fuori da un `for` sono errori. Non esistono `while`, `do` né `goto`.

`break` e `continue` non hanno etichette: agiscono sul ciclo più interno. Per uscire da più cicli in una volta, mettili in una funzione ed esci da questa con `ret`:

```cromo
// The position of the first value that appears twice, or -1.
i64 firstRepeated(list<i32> values) {
    for (i64 i = 0; i < values.len(); i++) {
        for (i64 j = 0; j < i; j++) {
            if (values[i] == values[j]) {
                ret i
            }
        }
    }
    ret -1
}

i32 main() {
    println("{} {}", firstRepeated([4, 7, 1, 7, 4]), firstRepeated([1, 2]))
    ret 0
}
```

```output
3 -1
```

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

`switch` dà il meglio di sé con gli enum, come mostra [Enum e switch](enums.md): lì il compilatore controlla che ogni caso sia gestito.

## Riepilogo

- Il tipo viene prima del nome, il valore dopo `=`; ogni variabile ha un valore, e un nome non si dichiara mai due volte nei blocchi in cui è visibile.
- `const` rende una variabile immutabile, in profondità; le costanti di un modulo contengono numeri, `bool` e `char`, scritti come letterali.
- I tipi integrati sono i numeri, `bool`, `char` (un byte) e `string`; i byte di una stringa sono in sola lettura.
- Le funzioni restituiscono con `ret` su ogni percorso; i parametri sono in sola lettura; non esistono `null` né overloading.
- Un'istruzione finisce alla fine della sua riga, e prosegue dopo un operatore, una virgola o una parentesi aperta.
- `if` vuole parentesi, graffe e un `bool`; `c ? a : b` sceglie un valore; `for` è l'unico ciclo; `break` e `continue` non hanno etichette; `switch` non ha fall-through.
