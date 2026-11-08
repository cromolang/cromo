# Appendice A: Parole chiave, operatori e attributi

Questa appendice elenca le parole, i simboli e i nomi integrati di Cromo, ciascuno con il capitolo che lo spiega.

## Parole chiave

Queste parole non si possono usare come nomi:

| Parola chiave | Uso | Capitolo |
|---|---|---|
| `break` | esce da un ciclo | [Concetti comuni](common-concepts.md) |
| `case` | un ramo di uno `switch` | [Enum e switch](enums.md) |
| `catch` | gestisce le eccezioni di un `try` | [Errori](errors.md) |
| `const` | una variabile che non cambia; una costante di un modulo; un metodo che non modifica il suo oggetto; un parametro `move const`; una `const func` | [Concetti comuni](common-concepts.md), [Struct](structs.md), [Possesso](ownership.md), [Funzioni anonime](lambdas.md) |
| `continue` | passa al giro successivo di un ciclo | [Concetti comuni](common-concepts.md) |
| `default` | il ramo di uno `switch` per i valori non elencati | [Enum e switch](enums.md) |
| `else` | l'alternativa di un `if` | [Concetti comuni](common-concepts.md) |
| `enum` | dichiara un enum | [Enum e switch](enums.md) |
| `extern` | dichiara funzioni C, con `import unsafe` | [Chiamare il C](c-interop.md) |
| `false`, `true` | i valori di `bool` | [Concetti comuni](common-concepts.md) |
| `finally` | viene eseguito a ogni uscita da un `try` | [Errori](errors.md) |
| `for` | il ciclo, in tutte le sue forme | [Concetti comuni](common-concepts.md) |
| `func` | il tipo di una funzione | [Funzioni anonime](lambdas.md) |
| `if` | una condizione | [Concetti comuni](common-concepts.md) |
| `import` | importa un modulo | [Moduli e progetti](modules.md) |
| `in` | in un `for` sugli elementi di una lista, di un array, di una mappa o di un canale | [Liste, mappe e testo](collections.md) |
| `interface` | dichiara un'interfaccia | [Interfacce e generici](interfaces-generics.md) |
| `move` | sposta un valore; un parametro o una cattura che prende il possesso | [Possesso](ownership.md) |
| `public` | rende una dichiarazione o un membro visibile fuori dal suo modulo | [Moduli e progetti](modules.md) |
| `ref` | un parametro o una cattura che modifica; l'elemento di un `for`, o i dati di un `case`, che si possono modificare | [Possesso](ownership.md) |
| `ret` | ritorna da una funzione | [Concetti comuni](common-concepts.md) |
| `struct` | dichiara una struct | [Struct](structs.md) |
| `switch` | sceglie tra più case | [Enum e switch](enums.md) |
| `throw` | lancia un'eccezione | [Errori](errors.md) |
| `try` | il blocco di cui si gestiscono le eccezioni | [Errori](errors.md) |
| `unsafe` | `import unsafe`: il dialetto delle funzioni C e della memoria grezza | [Il dialetto unsafe](unsafe.md) |
| `void` | nessun risultato | [Concetti comuni](common-concepts.md) |

Non esistono `while`, `do`, `goto`, `class`, `new`, `delete`, `null` né `this`. Le funzioni integrate, come `println`, e i tipi integrati, come `i64`, sono nomi, non parole chiave; tuttavia un programma non può dichiarare una funzione, una struct o un'interfaccia con il nome di una funzione integrata o di un tipo dei numeri, di `bool` o di `char`.

## Simboli

| Simbolo | Uso |
|---|---|
| `{ }` | i blocchi, e i corpi di funzioni, struct, enum, interfacce e blocchi `extern` |
| `( )` | parametri e argomenti; le condizioni di `if`, `for` e `switch`; il raggruppamento nelle espressioni |
| `[ ]` | un indice, `v[i]`; un letterale lista, `[1, 2, 3]`; le catture di una lambda, `[ref total]`; la dimensione di un array, `i32[4]` |
| `[[ ]]` | un attributo, `[[test]]` |
| `< >` | i parametri e gli argomenti di tipo dei generici, `list<T>`, `max<T : Ordered>` |
| `:` | le interfacce di una struct o di un enum, `struct Circle : Shape`; un vincolo, `T : Key`; l'espressione condizionale |
| `+` | tra i vincoli, `T : Key + Debug`, oltre all'addizione |
| `.` | un campo o un metodo, `p.x`; un nome di un modulo, `json.decode`; un case di un enum, `Color.Red` |
| `,` | separa parametri, argomenti, elementi e case |
| `;` | solo in `for (init; condition; step)`: un'istruzione finisce alla fine della sua riga |
| `~` | il distruttore, `~File()` |
| `//`, `/* */` | commenti; le righe `//` subito sopra una dichiarazione la documentano |

## Operatori

Dal più forte al più debole, come in [Numeri e operatori](operators.md):

| Operatori | Significato | Raggruppamento |
|---|---|---|
| `.` `()` `[]` | membro, chiamata, indice | da sinistra a destra |
| `!` `-` | not, inversione dei bit, negazione | prima del valore |
| `*` `/` `%` | moltiplicazione, divisione, resto | da sinistra a destra |
| `+` `-` | addizione, sottrazione; `+` unisce anche le stringhe | da sinistra a destra |
| `<<` `>>` | shift | da sinistra a destra |
| `&` | and bit a bit | da sinistra a destra |
| `^` | or esclusivo bit a bit | da sinistra a destra |
| `\|` | or bit a bit | da sinistra a destra |
| `<` `<=` `>` `>=` | confronti | da sinistra a destra |
| `==` `!=` | uguale, diverso | da sinistra a destra |
| `&&` | and logico, in cortocircuito | da sinistra a destra |
| `\|\|` | or logico, in cortocircuito | da sinistra a destra |
| `? :` | condizionale: `c ? a : b` | da destra a sinistra |

- Gli operatori bit a bit legano più dei confronti, a differenza del C: `flags & 4 == 4` è `(flags & 4) == 4`. Come in C, `==` e `!=` legano meno di `<` e degli altri.
- `c ? a : b` è il più debole: `a || b ? x : y` è `(a || b) ? x : y`, e `a ? b : c ? d : e` è `a ? b : (c ? d : e)`. La condizione è un `bool`, e viene calcolato solo il ramo scelto.
- `!` su un `bool` è il not logico, su un intero l'inversione dei suoi bit; non esiste l'operatore `~`.
- `%` funziona solo sugli interi; i confronti non si concatenano, `a < b < c` è un errore.
- Nelle build di debug un overflow e uno shift oltre la larghezza del tipo sono panic, e una divisione intera per zero in ogni build; vedi [Numeri e operatori](operators.md).
- `ref x` e `move x` prima di un argomento dicono come viene passato: non sono operatori, vedi [Possesso](ownership.md).

Gli assegnamenti sono istruzioni, mai parti di un'espressione, quindi `if (a = b)` non è Cromo:

| Istruzione | Significato |
|---|---|
| `x = v` | assegna |
| `x += v`, `-=`, `*=`, `/=`, `%=` | `x = x + v`, e così via |
| `x &= v`, `\|=`, `^=`, `<<=`, `>>=` | lo stesso con gli operatori bit a bit |
| `x++`, `x--` | aggiunge o toglie 1 |

## Letterali

| Letterale | Esempi | Capitolo |
|---|---|---|
| intero | `42`, `1_000_000`, `0xFF`, `0b1010` | [Numeri e operatori](operators.md) |
| virgola mobile | `3.14`, `1e-9`, `2.5e3` | [Numeri e operatori](operators.md) |
| carattere, un byte | `'a'`, `'\n'`, `'\t'`, `'\r'`, `'\0'`, `'\\'`, `'\''`, `'\x41'` | [Concetti comuni](common-concepts.md) |
| stringa, UTF-8 | `"text"`, con le sequenze di escape dei caratteri, `\"` e `\u{e9}` | [Concetti comuni](common-concepts.md) |
| `bool` | `true`, `false` | [Concetti comuni](common-concepts.md) |
| lista | `[1, 2, 3]`, `[]` | [Liste, mappe e testo](collections.md) |
| array di una costante di un modulo | `[0xFF, 0xFF00]` | [Concetti comuni](common-concepts.md) |

Un letterale intero prende il tipo richiesto dal contesto, e deve starci dentro. Non esistono letterali per le mappe.

## Tipi integrati

| Tipo | Significato | Capitolo |
|---|---|---|
| `i8`, `i16`, `i32`, `i64` | interi con segno | [Numeri e operatori](operators.md) |
| `u8`, `u16`, `u32`, `u64` | interi senza segno | [Numeri e operatori](operators.md) |
| `f32`, `f64` | numeri in virgola mobile | [Numeri e operatori](operators.md) |
| `bool` | `true` o `false` | [Concetti comuni](common-concepts.md) |
| `char` | un byte, lo stesso tipo di `u8` | [Concetti comuni](common-concepts.md) |
| `string` | testo in UTF-8 | [Liste, mappe e testo](collections.md) |
| `list<T>` | una lista che cresce | [Liste, mappe e testo](collections.md) |
| `map<K, V>` | una tabella hash | [Liste, mappe e testo](collections.md) |
| `T[n]`, `T[n][m]` | array di dimensione fissa | [Liste, mappe e testo](collections.md) |
| `i32x4`, `f32x8`, ... | vettori SIMD, di 128 o 256 bit | [Liste, mappe e testo](collections.md) |
| `func(A, B) R`, `const func(A) R` | funzioni, con i modi dei loro parametri, come `func(ref list<i64>) bool` | [Funzioni anonime](lambdas.md) |
| `addr` | un indirizzo di memoria, solo con `import unsafe` | [Il dialetto unsafe](unsafe.md) |

`string`, `list` e `map` sono struct del nucleo della libreria, che ogni modulo vede senza importarlo. Le dimensioni dei numeri sono le stesse su tutti i sistemi; non esistono `int`, `usize` né interi a 128 bit.

## Funzioni integrate

| Funzione | Cosa fa | Capitolo |
|---|---|---|
| `print(format, ...)`, `println(format, ...)` | scrivono sullo standard output, `println` con un a capo | [Formattare il testo](formatting.md) |
| `eprint(format, ...)`, `eprintln(format, ...)` | scrivono sullo standard error | [Formattare il testo](formatting.md) |
| `format(format, ...)` | il testo che `print` scriverebbe, come `string` | [Formattare il testo](formatting.md) |
| `panic(reason)` | ferma il programma, con il codice di uscita 101 | [Errori](errors.md) |
| `assert(condition)` | un panic quando la condizione è falsa, anche con `--release` | [Errori](errors.md), [Test](tests.md) |
| `wrapAdd(a, b)`, `wrapSub(a, b)`, `wrapMul(a, b)` | aritmetica degli interi che ricomincia dall'estremo opposto, senza il controllo di overflow | [Numeri e operatori](operators.md) |
| `countOnes(x)`, `trailingZeros(x)`, `leadingZeros(x)` | contano i bit di un intero, come `i32` | [Numeri e operatori](operators.md) |
| `rotateLeft(x, n)`, `rotateRight(x, n)` | ruotano i bit di un intero | [Numeri e operatori](operators.md) |
| `embed(path)` | i byte di un file, letti dal compilatore e messi nel programma, come `list<u8>` | [Finestre e app](gui.md) |

I nomi dei tipi convertono, e così due metodi dei numeri in virgola mobile:

| Chiamata | Cosa fa | Capitolo |
|---|---|---|
| `i64(x)`, `f64(x)`, ... | converte un numero; `i64("42")` ne legge uno da un testo | [Numeri e operatori](operators.md) |
| `string(bytes)` | una stringa da una `list<u8>` di UTF-8 | [Liste, mappe e testo](collections.md) |
| `Color(n)`, `i32(color)` | il case di un enum con quel valore, e viceversa | [Enum e switch](enums.md) |
| `T()` | il valore predefinito di `T`, o il suo costruttore | [Struct](structs.md) |
| `x.bits()`, `f64.fromBits(b)`, `f32.fromBits(b)` | i bit di un numero in virgola mobile, e viceversa | [Numeri e operatori](operators.md) |

Il dialetto di `import unsafe` ha le sue: `alloc`, `resize`, `free`, `sizeOf`, `offset`, `take`, `put`, `destroy`, `copyAt`, `copyMemory`, `compareMemory`, `checkIndex`, `variableAddress`, `addr(n)`, `s.cstring()` e `string(data, length)`; vedi [Il dialetto unsafe](unsafe.md).

## Il nucleo della libreria

Ogni modulo vede questi nomi senza importarli; un nome dichiarato dal modulo ha la precedenza.

| Nome | Cos'è | Capitolo |
|---|---|---|
| `sort(ref items)` | ordina una lista di valori `Ordered` | [Liste, mappe e testo](collections.md) |
| `sortBy(ref items, less)` | ordina una lista con una funzione che confronta | [Liste, mappe e testo](collections.md) |
| `join(parts, separator)` | unisce una lista di stringhe | [Liste, mappe e testo](collections.md) |
| `debugText(value)` | il testo che stampa `{:?}` | [Formattare il testo](formatting.md) |
| `Exception` | l'interfaccia di ciò che si può lanciare: `string message() const` | [Errori](errors.md) |
| `Error` | un'eccezione già pronta, con il suo messaggio | [Errori](errors.md) |
| `Key` | l'interfaccia delle chiavi di una mappa: `hash()` ed `equals()` | [Liste, mappe e testo](collections.md) |
| `Ordered` | l'interfaccia dei valori che `sort` ordina: `compare()` | [Interfacce e generici](interfaces-generics.md) |
| `Debug` | il vincolo di un `T` stampato con `{:?}` | [Formattare il testo](formatting.md) |

## Nomi con un significato

| Nome | Significato | Capitolo |
|---|---|---|
| `main` | dove comincia il programma: `i32 main()` o `void main()`, con `list<string> args` o senza | [Concetti comuni](common-concepts.md), [File, processi e tempo](system.md) |
| `Name(...)`, `~Name()` | il costruttore e il distruttore della struct `Name` | [Struct](structs.md) |
| `T copy() const` | la copia di un valore, fatta dove lo si passa senza `move` | [Struct](structs.md) |
| `string text() const` | il testo che stampa `{}` | [Formattare il testo](formatting.md) |
| `Self` | in un'interfaccia, il tipo che la implementa | [Interfacce e generici](interfaces-generics.md) |

## Attributi

Gli attributi si scrivono come in C++11, `[[name]]` o `[[name(arguments)]]`, prima di ciò a cui si applicano, sulla riga precedente o all'inizio della stessa riga. Ogni attributo ha le sue parentesi, e un attributo che il compilatore non conosce è un errore.

| Attributo | Prima di | Significato | Capitolo |
|---|---|---|---|
| `[[test]]` | una funzione `void name()` | un test, che `cromo test` esegue | [Test](tests.md) |
| `[[json("name")]]` | un campo, o un case di un enum | il suo nome nel JSON | [JSON](json.md) |
| `[[json(skip)]]` | un campo | escluso dal JSON | [JSON](json.md) |
| `[[window]]` | `main` | su Windows, un eseguibile senza console | [Finestre e app](gui.md) |
| `[[android("org.example.app", "Name")]]` | `main` | il package e il nome dell'app per Android | [Finestre e app](gui.md) |
| `[[export("name")]]` | una funzione, con `import unsafe` | il nome e la convenzione di chiamata del C | [Il dialetto unsafe](unsafe.md) |
| `[[cold]]` | una funzione, con `import unsafe` | una funzione che viene eseguita di rado | [Il dialetto unsafe](unsafe.md) |

```cromo
import std.json

struct User : json.Data {
    [[json("user_id")]]
    i64 id
    [[json(skip)]] string password
    string name
}

i32 main() {
    User u
    u.id = 7
    u.name = "Ada"
    u.password = "secret"
    println("{}", json.encode(u))
    ret 0
}
```

```output
{"user_id":7,"name":"Ada"}
```

Gli attributi vanno solo prima delle funzioni, dei campi e dei case degli enum; prima di una struct o di un metodo sono errori. Una funzione ha `[[test]]` una volta sola; `[[window]]` e `[[android(...)]]` vanno solo prima di `main`, e `[[export]]` e `[[cold]]` solo nei file con `import unsafe`:

```cromo error
[[inline]]
i32 twice(i32 x) {
    ret x * 2
}

struct Point {
    [[name("x")]] f64 x
}

[[window]]
void helper() {
}

i32 main() {
    ret 0
}
```

```output
error: unknown attribute 'name': for now there is only json
error: unknown attribute 'inline': a function has only [[test]]
error: [[window]] is only for main of the program, whose executable opens no console
```

## Riepilogo

- 28 parole chiave; `println`, `i64` e gli altri nomi integrati non sono parole chiave, ma un programma non può dichiararli di nuovo.
- Gli operatori hanno l'ordine del C, con quelli bit a bit sopra i confronti e `c ? a : b` il più debole, che raggruppa da destra; gli assegnamenti, `++` e `--` sono istruzioni.
- Le funzioni integrate stampano, fermano il programma, fanno l'aritmetica che ricomincia dall'estremo opposto, contano e ruotano i bit, e incorporano file; i nomi dei tipi convertono; il nucleo aggiunge `sort`, `sortBy`, `join`, `Exception`, `Error`, `Key`, `Ordered` e `Debug`.
- Sette attributi: `[[test]]`, `[[json(...)]]`, `[[window]]`, `[[android(...)]]`, e `[[export(...)]]` e `[[cold]]` del dialetto unsafe.
