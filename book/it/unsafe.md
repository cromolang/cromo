# Il dialetto unsafe

Cromo controlla che un programma non legga mai memoria che non possiede: non ci sono puntatori, ogni indice viene controllato, e il compilatore segue ogni valore dalla sua creazione alla sua fine. Una parte del codice, però, deve lavorare al di sotto di quelle regole: il codice che gestisce la memoria a mano, come fa una lista con i suoi elementi, e il codice che parla con il C. Per questo codice Cromo ha un dialetto, che un modulo attiva con `import unsafe`.

La libreria standard è scritta in questo dialetto: `list`, `string` e `map` sono struct di Cromo che gestiscono la propria memoria con le funzioni di questo capitolo, dietro un'interfaccia sicura, e lo stesso vale per gran parte del runtime, come lo heap e i thread. Un programma ne ha bisogno di rado, se non per chiamare il C, come in [Chiamare il C](c-interop.md). Questo capitolo mostra che cosa permette il dialetto, così che tu possa leggere la libreria e scrivere le poche righe che ne hanno bisogno.

## import unsafe

`import unsafe` si scrive insieme agli altri import, in cima a un file, e vale per quel file: in un modulo fatto di più file, come una cartella o i file di un sistema come `clock_windows.cro`, ogni file che usa il dialetto lo scrive. Permette:

| Che cosa | Dove si vede |
|---|---|
| i blocchi `extern` di funzioni C | [Chiamare il C](c-interop.md) |
| il tipo `addr`, un indirizzo di memoria | questo capitolo |
| le funzioni integrate della memoria grezza, come `alloc` e `free` | questo capitolo |
| `s.cstring()` e `string(data, length)`, le stringhe del C | [Chiamare il C](c-interop.md) |
| le variabili di modulo, fuori dalle funzioni | questo capitolo |
| gli attributi `[[export("name")]]` e `[[cold]]` | questo capitolo |

Tutto il resto mantiene i suoi controlli: il possesso, gli indici delle liste, l'overflow degli interi. Ciò che il dialetto aggiunge è ciò che il compilatore non può controllare, e questo è responsabilità dell'autore del modulo.

Senza l'import, queste cose non esistono:

```cromo error
i32 main() {
    addr place
    i64 size = sizeOf<i64>()
    ret 0
}
```

```output
error: 'addr' belongs to the unsafe dialect: the module must write 'import unsafe'
error: 'sizeOf' belongs to the unsafe dialect: the module must write 'import unsafe'
```

Gli altri moduli usano solo ciò che il modulo rende pubblico, e il compilatore controlla che questa interfaccia sia sicura: una funzione, un campo o un metodo pubblico che usa `addr` è l'errore `function 'block' is public, but its type addr is only for the modules with 'import unsafe': offer a safe type instead`. Tenere `addr` e la memoria grezza del modulo fuori dalla sua interfaccia pubblica è la regola che fa restare il dialetto dentro il suo modulo, e una ricerca di `import unsafe` trova allora ogni punto in cui la sicurezza del programma dipende dalla cura del suo autore.

## addr

`addr` è un indirizzo di memoria, senza tipo: dice dove, non che cosa. Il suo valore predefinito è l'indirizzo nullo, e due indirizzi si confrontano con `==` e `!=`:

```cromo
import unsafe

i32 main() {
    addr none
    addr block = alloc(64)
    println("{} {}", none == addr(0), block != none)
    i64 number = i64(block)
    println("aligned to 16: {}", number % 16 == 0)
    println("{:#x}", u64(addr(4096)))
    free(block)
    ret 0
}
```

```output
true true
aligned to 16: true
0x1000
```

- `addr(n)` crea un indirizzo da un `i64` o da un `u64`, e `addr(0)` è l'indirizzo nullo.
- `i64(p)` e `u64(p)` danno il numero di un indirizzo, per gli allineamenti e per i valori che il sistema fornisce come numeri.
- Nessun altro tipo si converte in `addr` o da `addr`; non c'è aritmetica sugli indirizzi, a parte `offset`, mostrata più avanti; un indirizzo non si può stampare, se non come numero.

```cromo error
import unsafe

i32 main() {
    addr a = alloc(16)
    addr b = a + 8
    u32 low = u32(a)
    println("{}", a < b)
    ret 0
}
```

```output
error: a number cannot be combined with a value of type addr
error: cannot convert addr to u32: an addr converts to i64 or u64, and back
error: '<' cannot compare addr values
```

Un `addr` può contenere qualsiasi numero: il compilatore non controlla mai che punti a memoria del programma. Leggere o scrivere attraverso un indirizzo sbagliato è un comportamento indefinito, come in C.

## La memoria: alloc, resize e free

Lo heap si usa con tre funzioni integrate:

| Funzione | Che cosa fa |
|---|---|
| `addr alloc(i64 bytes)` | alloca `bytes` byte, allineati a 16; il loro contenuto non è impostato |
| `addr resize(addr p, i64 bytes)` | dà un blocco di `bytes` byte con il contenuto di `p`, fino alla dimensione minore; `p` non è più valido |
| `void free(addr p)` | restituisce il blocco; `free` dell'indirizzo nullo non fa nulla |

`resize` dell'indirizzo nullo è un `alloc`, quindi un blocco può partire nullo e crescere. Quando la memoria finisce, o la dimensione è negativa, il programma si ferma con `panic: out of memory` e il codice di uscita 101: un'allocazione non restituisce mai l'indirizzo nullo.

Altre due funzioni misurano e spostano:

| Funzione | Che cosa fa |
|---|---|
| `i64 sizeOf<T>()` | i byte che occupa un valore di tipo `T`, come campo o come elemento |
| `addr offset(addr p, i64 bytes)` | l'indirizzo `bytes` byte dopo `p`, o prima quando è negativo |

```cromo
import unsafe

struct Point {
    i32 x
    i32 y
}

i32 main() {
    println("{} {} {} {}", sizeOf<i64>(), sizeOf<Point>(), sizeOf<bool>(), sizeOf<string>())
    addr block = alloc(4 * sizeOf<i64>())
    for (i64 i = 0; i < 4; i++) {
        put<i64>(offset(block, i * 8), (i + 1) * 10)
    }
    block = resize(block, 8 * sizeOf<i64>())
    println("{}", copyAt<i64>(offset(block, 24)))
    free(block)
    ret 0
}
```

```output
8 8 1 24
40
```

Una `string` occupa 24 byte perché è una struct della libreria: l'indirizzo dei suoi byte, la sua lunghezza e la sua capacità. I byte stanno sullo heap, e la stringa li libera nel suo distruttore.

Il compilatore non segue i blocchi: liberare un blocco due volte, usarlo dopo `free` o dimenticare di liberarlo sono errori che non trova. La risposta abituale è quella del C++: il blocco appartiene a una struct, il cui distruttore lo libera.

## I valori in memoria: put, take, copyAt e destroy

Quattro funzioni leggono e scrivono un valore di un tipo `T` a un indirizzo. Conoscono il tipo, quindi fanno con quel valore ciò che fa Cromo: lo spostano, lo copiano con `copy()` o lo distruggono.

| Funzione | Che cosa fa |
|---|---|
| `void put<T>(addr p, move T value)` | sposta il valore nella memoria a `p`, senza distruggere ciò che c'era |
| `T take<T>(addr p)` | sposta il valore fuori dalla memoria a `p`, che poi non contiene nulla |
| `T copyAt<T>(addr p)` | dà una copia del valore a `p`, che resta lì |
| `void destroy<T>(addr p)` | distrugge il valore a `p`, che poi non contiene nulla; la memoria resta allocata |

`put` e `take` trattano la memoria come vuota: `put` non distrugge un valore precedente, e dopo `take` il valore appartiene alla variabile che lo ha ricevuto, quindi la memoria non va distrutta di nuovo. Un valore dato a `put` senza `move` viene copiato, come per ogni parametro `move`, e un valore che non si può copiare è allora un errore, come lo è per `copyAt`. Questa struct dice quando viene copiata e distrutta:

```cromo
import unsafe

struct Noisy {
    string name

    ~Noisy() {
        println("destroy {}", name)
    }

    Noisy copy() const {
        println("copy {}", name)
        Noisy other
        other.name = name + "'"
        ret other
    }
}

i32 main() {
    addr slot = alloc(sizeOf<Noisy>())
    Noisy a
    a.name = "a"
    put<Noisy>(slot, move a)
    Noisy b = copyAt<Noisy>(slot)
    Noisy c = take<Noisy>(slot)
    println("{} {}", b.name, c.name)
    put<Noisy>(slot, move c)
    destroy<Noisy>(slot)
    free(slot)
    println("end of main")
    ret 0
}
```

```output
copy a
a' a
destroy a
end of main
destroy a'
```

`copyAt` ha chiamato `copy()`, `take` ha spostato fuori il valore senza copiarlo, e `destroy` ha eseguito il distruttore del valore rimesso nella memoria. Per i numeri e gli altri tipi semplici, queste funzioni si limitano a leggere e scrivere byte, e `destroy` non fa nulla.

Il tipo non viene confrontato con la memoria: `take<string>` di un blocco che contiene un `i64` legge spazzatura, come farebbe un cast del C.

## I byte: copyMemory e compareMemory

| Funzione | Che cosa fa |
|---|---|
| `void copyMemory(addr dest, addr source, i64 bytes)` | copia i byte; le due aree possono sovrapporsi |
| `i32 compareMemory(addr a, addr b, i64 bytes)` | confronta i byte: negativo, zero o positivo, come `memcmp` del C |

```cromo
import unsafe

i32 main() {
    string first = "hello"
    string second = "help!"
    addr a = alloc(5)
    addr b = alloc(5)
    copyMemory(a, first.cstring(), 5)
    copyMemory(b, second.cstring(), 5)
    println("{} {}", compareMemory(a, b, 3) == 0, compareMemory(a, b, 4) < 0)
    copyMemory(offset(a, 1), a, 4)
    println("{}", string(a, 5))
    free(a)
    free(b)
    ret 0
}
```

```output
true true
hhell
```

Spostano byte, non valori: un valore che possiede risorse, come una stringa, si sposta con loro solo quando il vecchio posto viene poi trattato come vuoto, come fa `list` quando inserisce un elemento nel mezzo.

## checkIndex

`checkIndex(index, length)` ferma il programma con il panic di un indice fuori da una lista quando `index` non è compreso tra 0 e `length - 1`. È il controllo che fa `v[i]`, per le struct che gestiscono a mano i loro elementi, come lo stack qui sotto.

## Come si scrive una lista

Insieme, queste funzioni bastano per scrivere un contenitore. Questo stack tiene i suoi elementi uno dopo l'altro sullo heap, come fa `list`:

```cromo file=stack.cro
import unsafe

// A stack of values on the heap, written as list is.
struct Stack<T> {
    addr data
    i64 count
    i64 capacity

    ~Stack() {
        for (i64 i = 0; i < count; i++) {
            destroy<T>(at(i))
        }
        free(data)
    }

    // The place of the element i.
    addr at(i64 i) const {
        ret offset(data, i * sizeOf<T>())
    }

    void push(move T value) {
        if (count == capacity) {
            capacity = capacity == 0 ? 4 : capacity * 2
            data = resize(data, capacity * sizeOf<T>())
        }
        put<T>(at(count), move value)
        count++
    }

    T pop() {
        checkIndex(count - 1, count)
        count--
        ret take<T>(at(count))
    }

    T top() const {
        checkIndex(count - 1, count)
        ret copyAt<T>(at(count - 1))
    }

    i64 len() const {
        ret count
    }
}

i32 main() {
    Stack<string> words
    words.push("one")
    words.push("two")
    words.push("three")
    println("{} on top, {} in all", words.top(), words.len())
    string last = words.pop()
    println("popped {}, {} left", last, words.len())
    Stack<i64> numbers
    numbers.pop()
    ret 0
}
```

```output
three on top, 3 in all
popped three, 2 left
panic: index out of bounds: the index is -1 but the length is 0 at stack.cro:31:9
    at Stack<i64>.pop (stack.cro:31)
    at main (stack.cro:55)
```

- Il valore predefinito della struct è uno stack vuoto con un `data` nullo, che `resize` trasforma in un blocco al primo `push`.
- Il distruttore distrugge gli elementi ancora presenti, poi libera il blocco.
- `Stack` ha un distruttore e nessun `copy()`, quindi non si può copiare, solo spostare: due stack non liberano mai lo stesso blocco. Un contenitore che si può copiare scrive `copy()`, che crea un nuovo blocco e copia ogni elemento con `copyAt`, come fa `list`.

La struct si può usare in sicurezza da altri moduli: i suoi campi sono privati, e i suoi metodi pubblici, quando ne ha, controllano gli indici. Questa è la forma di tutto il codice scritto nel dialetto: poche righe che il compilatore non può controllare, dentro una struct che il resto del programma usa con tutti i controlli.

## Le variabili di modulo

Un file con `import unsafe` può dichiarare variabili fuori dalle funzioni: lo stato dell'intero programma, come lo heap del runtime e la coda dei suoi task. Il codice normale non ne ha, quindi non ha né stato globale né data race tra thread.

Si scrivono come le variabili locali, al livello delle dichiarazioni, e ogni funzione del modulo le legge e le modifica:

```cromo
import unsafe

i64 calls
i64[4] slots
bool ready = true
i64 start = -5

void count() {
    calls++
}

i32 main() {
    count()
    count()
    println("{} {} {}", calls, ready, start)
    slots[2] = 42
    addr place = variableAddress(slots[2])
    println("{} {}", copyAt<i64>(place), place == offset(variableAddress(slots), 16))
    ret 0
}
```

```output
2 true -5
42 true
```

Le regole:

- Una variabile di modulo è privata del suo modulo, che la vede in tutti i suoi file, anche in quelli senza `import unsafe`. `public` e gli attributi sono errori.
- Contiene numeri, `bool`, `char`, `addr`, e array di dimensione fissa e struct fatti di questi, i cui campi non hanno valori iniziali.
- Il suo valore all'avvio è il suo valore predefinito, oppure un letterale: un numero, preceduto da `-` se negativo, un carattere, `true` o `false`. Il valore è scritto nell'eseguibile: nessun codice gira prima di `main` e nessuno dopo, quindi una variabile di modulo non ha distruttore, e un tipo che ha bisogno di codice per essere creato o distrutto, come `string` o `list`, è un errore.
- Una variabile locale o un parametro non può avere il suo nome: `a variable named 'count' is already visible here`, come per le altre variabili.
- Nulla protegge una variabile di modulo dai thread: due thread che la scrivono nello stesso momento sono una data race, che il compilatore non vede. Un programma che condivide dati tra thread usa i tipi di `std.thread`, come `thread.Mutex<T>` e `thread.Atomic<T>`, nel codice normale: vedi [Thread, task e canali](concurrency.md).

```cromo error
import unsafe

string name
i64 limit = 2 * 512

i32 main() {
    ret 0
}
```

```output
error: a module variable holds numbers, bool, addr, or arrays and structs of them whose fields have no initial values, not string
error: the initial value of a module variable is a number, a character, true or false
```

```cromo error
import unsafe

public i64 total

i32 main() {
    ret 0
}
```

```output
error: a module variable has no attributes and is not public: only its module sees it
```

Senza `import unsafe`, una variabile fuori dalle funzioni è un errore. Le costanti di un modulo, `const i64 limit = 1024`, non contengono stato e sono ammesse in ogni modulo: vedi [Concetti comuni](common-concepts.md).

**variableAddress.** `variableAddress(x)` dà l'`addr` di una variabile di modulo, o di un suo campo o di un suo elemento, per gli atomici e per le funzioni C che conservano un indirizzo. Resta lo stesso per tutto il programma. Una variabile locale non ne ha uno, perché il suo posto finisce con il suo blocco:

```cromo error
import unsafe

i32 main() {
    i64 local = 5
    addr place = variableAddress(local)
    ret 0
}
```

```output
error: variableAddress() takes a module variable, or a field or an element of one
```

## [[export("name")]]

Una funzione di un modulo con `import unsafe` si può esportare con un nome fisso e la convenzione di chiamata del C: il codice C collegato al programma, come una libreria statica di un blocco `extern`, la chiama allora con quel nome. Il runtime di Cromo lo usa per le funzioni che vengono chiamate dal codice generato e dal C del runtime, come `cromo_alloc`.

```cromo
import unsafe

[[export("example_twice")]]
i64 twice(i64 x) {
    ret x * 2
}

[[export("example_positive")]]
bool positive(i32 x) {
    ret x > 0
}

i32 main() {
    println("{} {}", twice(21), positive(-3))
    ret 0
}
```

```output
42 false
```

Il C la dichiara come `long long example_twice(long long x);`, mentre Cromo continua a chiamarla con il suo nome, `twice`. Un esempio con una libreria C è in [Chiamare il C](c-interop.md). Le regole:

- Il nome è fatto di lettere, cifre e `_`, e non inizia con una cifra. È un simbolo dell'intero programma, come una funzione del C, quindi due funzioni esportate non possono avere lo stesso nome, e i nomi del runtime, che iniziano con `cromo_`, come `cromo_alloc`, fanno segnalare al linker un simbolo duplicato.
- I parametri sono numeri, `bool` o `addr`, passati per valore; il risultato è uno di questi, oppure `void`.
- Non può essere generica, e non può lasciar uscire un'eccezione, perché il C non ha eccezioni: le cattura al suo interno.
- Non ha altri attributi, a parte `[[cold]]`.

```cromo error
import unsafe

[[export("count_words")]]
i64 countWords(string text) {
    ret text.len()
}

[[export("check_value")]]
i64 check(i64 x) {
    if (x < 0) {
        throw Error("negative")
    }
    ret x
}

[[export("9lives")]]
void lives() {
}

i32 main() {
    ret 0
}
```

```output
error: the parameters of an exported function are numbers, bool or addr, passed by value
error: the exported function 'check' can throw, and C has no exceptions: catch them inside it
error: [[export]] takes the name of the function in C, as [[export("cromo_alloc")]]
```

## [[cold]]

`[[cold]]` prima di una funzione dice che viene eseguita di rado, come `#[cold]` di Rust e `__attribute__((cold))` di GCC. Il compilatore la tiene fuori dalle funzioni che la chiamano, e le organizza in favore dell'altro percorso, il che rende più breve il percorso comune. Il runtime lo usa per i percorsi lenti, come quello in cui lo heap chiede al sistema altra memoria.

```cromo
import unsafe

i64 slowCalls

[[cold]]
i64 slow(i64 x) {
    slowCalls++
    ret x * 2
}

i64 fast(i64 x) {
    if (x % 50 == 49) {
        ret slow(x) - x
    }
    ret x
}

i32 main() {
    i64 sum = 0
    for (i64 i = 0; i < 100; i++) {
        sum += fast(i)
    }
    println("{} after {} slow calls", sum, slowCalls)
    ret 0
}
```

```output
4950 after 2 slow calls
```

La funzione si comporta come qualsiasi altra: `[[cold]]` cambia solo la velocità del codice che la circonda. Non prende argomenti, si scrive una volta sola, può stare insieme a `[[export]]`, ed è solo per i moduli con `import unsafe`:

```cromo error
[[cold]]
void rarely() {
}

i32 main() {
    rarely()
    ret 0
}
```

```output
error: [[cold]] is only for the modules with 'import unsafe'
```

## I nomi del dialetto

La libreria standard usa qualche altra funzione integrata del dialetto, per il proprio lavoro: i thread e i task, gli atomici, gli argomenti del programma, l'ambiente e le chiamate di sistema di Linux. Servono solo ai suoi moduli e possono cambiare, quindi non sono descritte qui.

In un file con `import unsafe` tutti questi nomi sono funzioni integrate:

- `alloc`, `resize`, `free`, `sizeOf`, `offset`, `take`, `put`, `destroy`, `copyAt`, `copyMemory`, `compareMemory`, `checkIndex` e `variableAddress`, di questo capitolo;
- `argumentCount`, `argumentLength`, `argumentData`, `environment`, `exitProcess`, `systemCall`, `decimalToFloat`, `hashBytes`, `equalAt`, `jsonWrite`, `jsonRead`, `debugWrite`, `selfAddress`, `threadStart`, `threadWait`, `threadRethrow`, `threadUncaught`, `threadFree`, `taskStart`, `groupStart`, `mutexLock`, `mutexUnlock`, `atomicLoad`, `atomicStore`, `atomicAdd`, `atomicExchange`, `atomicCompareExchange`, `atomicLoadRelaxed`, `atomicStoreRelaxed` e `atomicAddRelaxed`, della libreria.

Una funzione del modulo, o una funzione C, con uno di questi nomi non verrebbe mai chiamata in quel file, perciò è un errore dove viene dichiarata: dai alle tue funzioni altri nomi. Nei file senza `import unsafe` questi nomi sono liberi.

## Che cosa il compilatore non controlla più

Il dialetto, con le funzioni C che chiama, è l'unico posto in cui un programma Cromo può avere un comportamento indefinito. Questi errori compilano, e il programma può andare in crash o, peggio, proseguire con dati sbagliati:

- leggere o scrivere attraverso un indirizzo nullo, liberato o fuori dal suo blocco;
- liberare un blocco due volte, o mai;
- `take` di un posto che non contiene nulla, `put` sopra un valore che poi non viene mai distrutto, un tipo `T` diverso da quello presente in memoria;
- l'`addr` di `cstring()` conservato dopo la sua istruzione;
- una variabile di modulo scritta da due thread nello stesso momento.

Per questo il codice nel dialetto resta breve, e viene incapsulato al più presto in una struct con un distruttore e un'interfaccia sicura. Il resto del programma mantiene così tutte le garanzie di Cromo.

## Riepilogo

- `import unsafe` attiva il dialetto nel file che lo scrive: blocchi `extern`, `addr`, memoria grezza, variabili di modulo, `[[export]]` e `[[cold]]`. Gli altri moduli usano la sua interfaccia pubblica.
- `addr` è un indirizzo senza tipo: nullo per impostazione predefinita, si confronta con `==` e `!=`, e si converte solo in `i64` e `u64` e da questi.
- `alloc`, `resize` e `free` gestiscono lo heap; `sizeOf<T>()` e `offset` misurano e spostano; `put<T>`, `take<T>`, `copyAt<T>` e `destroy<T>` spostano, copiano e distruggono valori in memoria; `copyMemory` e `compareMemory` lavorano sui byte; `checkIndex` controlla un indice.
- Le variabili di modulo contengono numeri, `bool`, `addr`, e array e struct fatti di questi, con un letterale o il valore predefinito, senza codice prima di `main`; `variableAddress(x)` ne dà l'indirizzo.
- `[[export("name")]]` dà a una funzione il nome e la convenzione del C; `[[cold]]` segna una funzione che viene eseguita di rado.
- La libreria standard è scritta in questo dialetto; un programma lo usa per chiamare il C, e lo tiene piccolo e incapsulato.
