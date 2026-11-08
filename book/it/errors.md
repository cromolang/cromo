# Errori

Cromo distingue due tipi di errori:

- **Errori che un programma si aspetta**, come un file che non esiste o una connessione che cade: sono **eccezioni**, che un chiamante può catturare e gestire. Un'eccezione che nessuno cattura ferma il programma con codice di uscita 1.
- **Errori di programmazione**, come un indice fuori da una lista o l'overflow di un intero: sono **panic**, che fermano il programma con codice di uscita 101, perché un programma che ha violato le proprie regole non può sapere che cosa fare dopo.

Entrambi stampano le chiamate che hanno portato fin lì, uno *stack trace*, anche nelle build fatte con `--release`.

## Lanciare e catturare eccezioni

```cromo
void checkAge(i64 age) {
    if (age < 0) {
        throw Error(format("invalid age {}", age))
    }
}

i32 main() {
    try {
        checkAge(30)
        println("30 is fine")
        checkAge(-3)
        println("not reached")
    } catch (Exception e) {
        println("error: {}", e.message())
    }
    ret 0
}
```

```output
30 is fine
error: invalid age -3
```

`throw` interrompe la funzione e risale attraverso i chiamanti fino a un `try` il cui `catch` accetta l'eccezione. `Error` è un'eccezione già pronta con un messaggio; `catch (Exception e)` accetta qualsiasi eccezione, e `e.message()` ne restituisce il testo.

Le eccezioni non si dichiarano nella firma di una funzione: qualsiasi chiamata può lanciarne una, anche la chiamata di una lambda. Come `ret`, `throw` termina un percorso della funzione, quindi una funzione che restituisce un valore può terminare con un `throw` invece che con un `ret`.

La libreria segnala i suoi errori nello stesso modo, per lo più con un `Error` e un messaggio:

```cromo
import std.fs

i32 main() {
    try {
        string text = fs.readText("settings.txt")
        println("{}", text)
    } catch (Exception e) {
        println("using the defaults: {}", e.message())
    }
    ret 0
}
```

```output
using the defaults: cannot read 'settings.txt': no such file or folder
```

## Tipi di eccezione

Un'eccezione implementa l'interfaccia `Exception`, che ha un solo metodo, `string message() const`. Un tipo dedicato permette a un chiamante di riconoscere l'errore e di leggerne i campi:

```cromo
struct NotFound : Exception {
    string name

    NotFound(move string what) {
        name = move what
    }

    string message() const {
        ret format("{} was not found", name)
    }
}

i64 position(list<string> items, string wanted) {
    for (i64 i = 0; i < items.len(); i++) {
        if (items[i] == wanted) {
            ret i
        }
    }
    throw NotFound(wanted.copy())
}

i32 main() {
    list<string> names = ["Ada", "Alan"]
    try {
        println("{}", position(names, "Alan"))
        println("{}", position(names, "Grace"))
    } catch (NotFound e) {
        println("missing: {}", e.name)
    } catch (Exception e) {
        println("other error: {}", e.message())
    }
    ret 0
}
```

```output
1
missing: Grace
```

I blocchi `catch` vengono provati in ordine; il primo che accetta il tipo dell'eccezione la gestisce.

Ciò che si lancia è un valore nuovo, come `NotFound(...)` qui sopra, oppure una variabile: `throw e` lancia una copia, `throw move e` il valore stesso. Si può lanciare solo un tipo che implementa `Exception`:

```cromo error
i32 main() {
    throw "something went wrong"
}
```

```output
error: only a struct that implements Exception can be thrown, found string
```

Anche un enum può implementare `Exception` ([Enum e switch](enums.md)) ed essere lanciato: ogni caso è un tipo di errore, con i suoi dati, e il `catch` li distingue con uno `switch`:

```cromo
enum ParseError : Exception {
    Empty
    BadChar(char c)

    string message() const {
        switch {
            case Empty {
                ret "the text is empty"
            }
            case BadChar(char c) {
                ret format("unexpected '{:c}'", c)
            }
        }
    }
}

i64 digits(string text) {
    if (text.len() == 0) {
        throw ParseError.Empty
    }
    i64 n = 0
    for (i64 i = 0; i < text.len(); i++) {
        char c = text[i]
        if (c < '0' || c > '9') {
            throw ParseError.BadChar(c)
        }
        n = n * 10 + i64(c - '0')
    }
    ret n
}

i32 main() {
    list<string> inputs = ["42", "", "4x"]
    for (string s in inputs) {
        try {
            println("{}", digits(s))
        } catch (ParseError e) {
            switch (e) {
                case Empty {
                    println("nothing to read")
                }
                case BadChar {
                    println("{}", e.message())
                }
            }
        }
    }
    ret 0
}
```

```output
42
nothing to read
unexpected 'x'
```

## Le regole di try e catch

- Un `try` ha almeno un `catch`, oppure un `finally`, oppure entrambi.
- Un `catch` prende un tipo che implementa `Exception`, oppure `Exception` stessa, che accetta ogni eccezione.
- Un `catch` che non potrebbe mai essere eseguito è un errore: uno dopo `catch (Exception e)`, o un secondo per lo stesso tipo.
- L'eccezione di un `catch` è in sola lettura, come un parametro in lettura: i suoi campi non si possono modificare, e si possono chiamare solo i suoi metodi `const`.

```cromo error
i32 main() {
    try {
        println("working")
    }
    ret 0
}
```

```output
error: 'try' needs at least one 'catch' or a 'finally'
```

```cromo error
struct Failure : Exception {
    i64 tries

    string message() const {
        ret "failure"
    }
}

struct Point {
    i64 x
}

i32 main() {
    try {
        throw Failure()
    } catch (Failure e) {
        e.tries++
    } catch (Point p) {
    }
    try {
        throw Failure()
    } catch (Exception e) {
        println("{}", e.message())
    } catch (Failure e) {
        println("never")
    }
    ret 0
}
```

```output
error: 'e' is the exception of a 'catch', which is read-only
error: a 'catch' takes a struct that implements Exception, or Exception itself, found Point
error: this 'catch' is never reached: a previous one already catches every exception
```

Un'eccezione che nessun `catch` di un `try` accetta, o che viene lanciata dentro un `catch`, passa al `try` che lo racchiude, dopo il `finally` del proprio `try`:

```cromo
void step(i64 n) {
    if (n == 2) {
        throw Error("step 2 failed")
    }
}

i32 main() {
    try {
        try {
            step(2)
        } catch (Exception e) {
            println("inner: {}", e.message())
            throw Error(format("while working: {}", e.message()))
        } finally {
            println("inner finally")
        }
    } catch (Exception e) {
        println("outer: {}", e.message())
    }
    ret 0
}
```

```output
inner: step 2 failed
inner finally
outer: while working: step 2 failed
```

## finally

`finally` viene eseguito in qualunque modo finisca il `try`: normalmente, con `ret`, con `break` o `continue`, oppure con un'eccezione, catturata o no:

```cromo
i32 main() {
    for (i32 i = 0; i < 2; i++) {
        try {
            if (i == 1) {
                throw Error("second round")
            }
            println("round {}", i)
        } catch (Exception e) {
            println("caught: {}", e.message())
        } finally {
            println("end of round {}", i)
        }
    }
    ret 0
}
```

```output
round 0
end of round 0
caught: second round
end of round 1
```

Con `ret`, prima si calcola il valore, poi viene eseguito `finally`, poi la funzione restituisce il valore. Con un'eccezione che il `try` non cattura, `finally` viene eseguito e l'eccezione prosegue:

```cromo
i64 value() {
    println("computing the value")
    ret 7
}

i64 compute() {
    try {
        ret value()
    } finally {
        println("finally")
    }
}

void fail() {
    try {
        throw Error("lost connection")
    } finally {
        println("cleanup")
    }
}

i32 main() {
    println("got {}", compute())
    try {
        fail()
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
computing the value
finally
got 7
cleanup
caught: lost connection
```

Un `finally` termina sempre normalmente: `ret`, `break` e `continue` non possono uscirne, perché farebbero sparire in silenzio l'eccezione o il valore in uscita. Un ciclo scritto dentro un `finally` può comunque usare `break` e `continue` per sé.

```cromo error
i64 compute() {
    try {
        ret 1
    } finally {
        ret 2
    }
}

i32 main() {
    for (i32 i = 0; i < 3; i++) {
        try {
            println("{}", i)
        } finally {
            break
        }
    }
    ret 0
}
```

```output
error: 'ret' cannot leave a 'finally' block
error: 'break' cannot leave a 'finally' block
```

## Eccezioni e distruttori

Mentre un'eccezione risale attraverso le funzioni, ogni oggetto degli ambiti che abbandona viene distrutto, come alla fine di un blocco normale. È questo che rende affidabile il RAII: una risorsa detenuta da un oggetto viene rilasciata anche quando qualcosa va storto.

```cromo
struct Resource {
    string name

    Resource(move string label) {
        name = move label
        println("acquire {}", name)
    }

    ~Resource() {
        println("release {}", name)
    }
}

void work() {
    Resource a = Resource("a")
    Resource b = Resource("b")
    throw Error("failure in work")
}

i32 main() {
    try {
        work()
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
acquire a
acquire b
release b
release a
caught: failure in work
```

### Un costruttore che lancia un'eccezione

Un costruttore può lanciare un'eccezione, quando non riesce a creare un oggetto valido. I campi già creati vengono distrutti, ma il distruttore dell'oggetto non viene eseguito, perché l'oggetto non è mai stato completo; la variabile che si stava inizializzando non viene mai creata:

```cromo
struct Engine {
    ~Engine() {
        println("engine destroyed")
    }
}

struct Machine {
    Engine engine

    Machine(bool broken) {
        println("building")
        if (broken) {
            throw Error("broken")
        }
    }

    ~Machine() {
        println("machine destroyed")
    }
}

i32 main() {
    try {
        Machine m = Machine(true)
        println("not reached")
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
building
engine destroyed
caught: broken
```

## Che cosa non può lanciare eccezioni

Alcune parti del codice vengono eseguite dove nessuno potrebbe catturare un'eccezione, quindi il compilatore non lascia che un'eccezione ne esca:

- un **distruttore** e un blocco **`finally`**: vengono eseguiti quando un'eccezione potrebbe già stare risalendo, e una seconda non avrebbe dove andare;
- un **`copy()`** e un **`message()`** scritti a mano: il compilatore li chiama in punti suoi, come le copie che fa e la segnalazione di un'eccezione non catturata;
- il **valore iniziale di un campo**, come `i64 port = parse(text)`: viene calcolato ovunque si crei il valore predefinito della struct, come in una dichiarazione senza valore o in un nuovo valore di una mappa.

Lì una chiamata che può lanciare un'eccezione è un errore, a meno che un `try` che la racchiude non catturi ogni eccezione:

```cromo error
void risky() {
    throw Error("boom")
}

struct Holder {
    i64 x

    ~Holder() {
        risky()
    }
}

struct Report : Exception {
    string message() const {
        risky()
        ret "report"
    }
}

i32 main() {
    try {
        println("working")
    } finally {
        risky()
    }
    ret 0
}
```

```output
error: a destructor cannot let an exception out: catch it inside
error: message() cannot let an exception out: catch it inside
error: a 'finally' block cannot let an exception out: catch it inside
```

```cromo
void risky() {
    throw Error("boom")
}

struct Holder {
    i64 x

    ~Holder() {
        try {
            risky()
        } catch (Exception e) {
            println("in the destructor: {}", e.message())
        }
    }
}

i32 main() {
    Holder h
    ret 0
}
```

```output
in the destructor: boom
```

## Rilanciare un'eccezione

`throw` da solo, dentro un `catch`, rilancia la stessa eccezione, per esempio dopo averla scritta in un log:

```cromo
void load() {
    throw Error("disk full")
}

i32 main() {
    try {
        try {
            load()
        } catch (Exception e) {
            println("load failed, passing it on")
            throw
        }
    } catch (Exception e) {
        println("main: {}", e.message())
    }
    ret 0
}
```

```output
load failed, passing it on
main: disk full
```

Fuori da un `catch` non c'è nessuna eccezione da rilanciare, e `throw` da solo è un errore. L'eccezione rilanciata conserva lo stack trace del suo primo `throw`, così la traccia mostra ancora dove è avvenuto l'errore, non dove è passato:

```cromo file=again.cro
void load() {
    throw Error("disk full")
}

void retry() {
    try {
        load()
    } catch (Exception e) {
        println("load failed: {}", e.message())
        throw
    }
}

i32 main() {
    retry()
    ret 0
}
```

```output
load failed: disk full
uncaught exception: disk full
    at load (again.cro:2)
    at retry (again.cro:7)
    at main (again.cro:15)
```

Una nuova eccezione lanciata nel `catch`, come `throw Error(...)`, comincia una nuova traccia, dalla riga di quel `throw`.

## Eccezioni non catturate

Un'eccezione che esce da `main` ferma il programma con codice di uscita 1, e stampa `uncaught exception:`, il suo messaggio e le chiamate dal `throw` fino a `main`:

```cromo file=deep.cro
void deep(i64 n) {
    if (n == 0) {
        throw Error("too deep")
    }
    deep(n - 1)
    println("back from {}", n - 1)
}

i32 main() {
    deep(3)
    ret 0
}
```

```output
uncaught exception: too deep
    at deep (deep.cro:3)
    at deep (deep.cro:5)
    ... the same 2 more times
    at main (deep.cro:10)
```

Un'eccezione che termina un thread viene rilanciata da `join()`, nel thread che lo aspetta ([Thread, task e canali](concurrency.md)).

## Panic

Un panic è un errore di programmazione: un indice fuori dai limiti, un overflow, una divisione intera per zero, una chiave mancante in una mappa, uno stack overflow. Non è un'eccezione e non si può catturare: il programma si ferma con codice di uscita 101 e stampa le chiamate che lo hanno portato fin lì.

La prima riga è `panic:`, il motivo e il punto del sorgente, `file:line:column`; segue lo stack trace. I distruttori e i blocchi `finally` non vengono eseguiti.

```cromo file=bounds.cro
i64 third(list<i64> values) {
    ret values[2]
}

i32 main() {
    list<i64> short = [1, 2]
    println("{}", third(short))
    ret 0
}
```

```output
panic: index out of bounds: the index is 2 but the length is 2 at bounds.cro:2:15
    at third (bounds.cro:2)
    at main (bounds.cro:7)
```

Questi sono i panic del linguaggio; i capitoli della libreria indicano i panic delle loro funzioni.

| Panic | Quando |
|---|---|
| un indice fuori dai limiti, in una lista, una stringa o un array | sempre |
| una divisione intera o un `%` per zero | sempre |
| l'overflow di un intero | senza `--release`; con `--release`, il valore ricomincia dall'estremo opposto |
| un valore in virgola mobile convertito in un intero che non può contenerlo, oppure `nan` | senza `--release`; con `--release`, il valore più vicino |
| la lettura di una chiave che non è in una mappa, `m[key]` | sempre |
| `Color(n)` quando nessun caso dell'enum ha il valore `n` | sempre |
| la chiamata di un metodo su un valore vuoto di un'interfaccia | sempre |
| lo stesso elemento passato due volte con `ref`, come `swap(ref v[i], ref v[j])` con `i == j` | sempre |
| uno stack overflow, `panic: stack overflow` | sempre |
| un `assert` falso, e `panic(...)` | sempre |

Una divisione per zero tra numeri in virgola mobile non è un panic: dà `inf`, `-inf` o `nan`, come stabilisce lo standard IEEE 754 ([Numeri e operatori](operators.md)).

Un panic dentro una funzione della libreria, come `remove` con un indice fuori dalla lista, indica il punto della chiamata nel tuo programma, e la traccia mostra la funzione della libreria come prima riga:

```cromo file=remove.cro
i32 main() {
    list<i64> values = [1, 2]
    values.remove(5)
    ret 0
}
```

```output
panic: index out of bounds: the index is 5 but the length is 2 at remove.cro:3:12
    at list<i64>.remove (cromo:core.cro:81)
    at main (remove.cro:3)
```

### panic

`panic("reason")` ferma il programma di proposito, quando raggiunge uno stato che dovrebbe essere impossibile. Come `ret` e `throw`, termina un percorso della funzione:

```cromo file=codes.cro
string symbol(i32 code) {
    switch (code) {
        case 1 {
            ret "hearts"
        }
        case 2 {
            ret "spades"
        }
    }
    panic(format("unknown code {}", code))
}

i32 main() {
    println("{}", symbol(1))
    println("{}", symbol(9))
    ret 0
}
```

```output
hearts
panic: unknown code 9 at codes.cro:10:5
    at symbol (codes.cro:10)
    at main (codes.cro:15)
```

### assert

`assert(condition)` provoca un panic quando la sua condizione è falsa. Il messaggio ripete la condizione così come è scritta e, per un confronto tra numeri, i valori dei suoi due lati:

```cromo file=check.cro
i32 main() {
    i64 x = 3
    assert(x * 2 == 7)
    ret 0
}
```

```output
panic: assertion failed: x * 2 == 7 (x * 2 is 6) at check.cro:3:5
    at main (check.cro:3)
```

`assert` funziona in ogni build, anche con `--release`. È anche lo strumento dei [Test](tests.md).

## Lo stack trace

Un panic, un'eccezione che nessuno cattura e uno stack overflow stampano le chiamate che hanno portato fin lì, una riga per ciascuna, dalla più interna fino a `main`:

```
    at function (file:line)
```

La traccia viene stampata anche nelle build fatte con `--release`, con i nomi, i file e le righe del sorgente. Per farlo, all'eseguibile non servono le informazioni di debug: il compilatore vi scrive dentro una tabella compatta del codice.

### I nomi

Le funzioni compaiono con i nomi che hanno nel sorgente:

```cromo file=names.cro
struct Account {
    i64 balance

    Account(i64 start) {
        balance = start
        check()
    }

    void check() const {
        assert(balance >= 0)
    }
}

i32 main() {
    func(i64) Account open = [](i64 amount) Account {
        ret Account(amount)
    }
    Account a = open(-5)
    ret 0
}
```

```output
panic: assertion failed: balance >= 0 (balance is -5) at names.cro:10:9
    at Account.check (names.cro:10)
    at Account.Account (names.cro:6)
    at main.lambda (names.cro:16)
    at main (names.cro:18)
```

| Nel sorgente | In una traccia |
|---|---|
| una funzione | `main`, `deep` |
| un metodo | `Account.check` |
| un costruttore e un distruttore | `Account.Account`, `Account.~Account` |
| una funzione generica, un metodo di un tipo generico | `first<i64>`, `list<i64>.remove` |
| una lambda, con il nome della funzione che la contiene | `main.lambda` |
| una funzione di un altro modulo del programma | `geometry.area` |
| una funzione della libreria, con il nome completo del suo modulo | `std.fs.readText` |

Le funzioni del file passato a `cromo` compaiono senza il nome del loro modulo.

### I file

Il file passato a `cromo` compare così come è stato passato, come `names.cro` qui sopra. Gli altri file del programma, come i moduli che importa, compaiono con il loro percorso completo. I file della libreria compaiono come `cromo:` seguito dalla loro posizione nella libreria, senza la cartella in cui è installato `cromo`: `cromo:core.cro`, `cromo:std/fs.cro`. Qui, su Windows, l'errore viene dalla parte di `std.fs` scritta per Windows:

```cromo nocheck file=settings.cro
import std.fs

i32 main() {
    string text = fs.readText("settings.txt")
    ret 0
}
```

```output
uncaught exception: cannot read 'settings.txt': no such file or folder
    at std.fs.loadFile (cromo:std/fs_windows.cro:41)
    at std.fs.readText (cromo:std/fs.cro:69)
    at main (settings.cro:4)
```

### Ricorsione, codice ottimizzato e tracce lunghe

- La stessa funzione, chiamata dalla stessa riga molte volte di seguito, come in una ricorsione, compare una volta sola, seguita da `... the same N more times`, come in `deep.cro` qui sopra.
- Il compilatore ottimizza per default, e mette le funzioni piccole dentro le funzioni che le chiamano: la traccia le mostra comunque, con le loro righe. Una funzione che chiama se stessa come ultimo passo può diventare un ciclo: allora le sue chiamate lasciano una sola riga.
- Le funzioni del runtime e del C vengono omesse.
- Una traccia ha al massimo 63 chiamate; una più lunga termina con `...`. Il caso tipico è uno stack overflow:

```cromo nocheck file=overflow.cro
i64 depth(i64 n) {
    ret depth(n + 1) + 1
}

i32 main() {
    println("{}", depth(0))
    ret 0
}
```

```output
panic: stack overflow
    at depth (overflow.cro:2)
    ... the same 58 more times
    ...
```

Per avere più di una traccia, come i valori delle variabili, si può eseguire il programma in un debugger: vedi [Strumenti: formattazione, editor e debugger](tools.md).

## Come scegliere

- Se il chiamante può fare qualcosa per rimediare all'errore, come chiedere un altro nome di file, lancia un'**eccezione**.
- Se l'errore significa che il programma è sbagliato, come un indice calcolato male, è un **panic**: correggi il programma.
- Se l'assenza di un risultato è normale, come una parola non trovata in un testo, restituisci un valore convenzionale o un `bool`, come fa `find` con -1.

## Riepilogo

- `throw`, `try`, `catch` e `finally` funzionano come in C++, Java e C#; ciò che si lancia implementa `Exception`, e spesso è l'`Error` già pronto.
- I blocchi `catch` vengono provati in ordine; uno che non potrebbe mai essere eseguito è un errore. L'eccezione di un `catch` è in sola lettura, e `throw` da solo la rilancia, con la sua prima traccia.
- `finally` viene eseguito a ogni uscita dal `try`, e non se ne può uscire con `ret`, `break` o `continue`.
- I distruttori vengono eseguiti mentre un'eccezione risale, quindi le risorse vengono sempre rilasciate. Un distruttore, un `finally`, `copy()`, `message()` e il valore iniziale di un campo non possono lasciar uscire un'eccezione.
- Un'eccezione non catturata ferma il programma con codice di uscita 1; un panic, per un errore di programmazione, con 101, e non si può catturare.
- Entrambi stampano uno stack trace, anche con `--release`, con i nomi del sorgente; i file della libreria compaiono come `cromo:core.cro`, `cromo:std/fs.cro`.
- `panic` e `assert` fermano il programma di proposito.
