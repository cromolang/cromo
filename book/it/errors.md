# Errori

Cromo distingue due tipi di errori:

- **Errori che un programma si aspetta**, come un file che non esiste o una connessione che cade: sono **eccezioni**, che un chiamante può catturare e gestire.
- **Errori di programmazione**, come un indice fuori da una lista o l'overflow di un intero: sono **panic**, che fermano il programma, perché un programma che ha violato le proprie regole non può sapere che cosa fare dopo.

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

`throw` interrompe la funzione e risale attraverso i chiamanti fino a un `try` il cui `catch` accetta l'eccezione. `Error` è un'eccezione già pronta con un messaggio; `catch (Exception e)` accetta qualsiasi eccezione, e `e.message()` ne restituisce il testo. Le eccezioni non si dichiarano nella firma di una funzione.

## Tipi di eccezione

Un'eccezione è una struct che implementa l'interfaccia `Exception`, che ha un solo metodo, `string message() const`. Un tipo dedicato permette a un chiamante di riconoscere l'errore e di leggerne i campi:

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

I blocchi `catch` vengono provati in ordine; il primo che accetta il tipo dell'eccezione la gestisce. L'eccezione di un `catch` è in sola lettura.

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

Un distruttore, così come un `finally`, non può lasciar uscire un'eccezione: il compilatore richiede che la catturi al proprio interno.

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

## Eccezioni non catturate

Un'eccezione che esce da `main` ferma il programma con codice di uscita 1, e stampa il messaggio e le chiamate dal `throw` fino a `main`:

```cromo file=deep.cro
void deep(i64 n) {
    if (n == 0) {
        throw Error("too deep")
    }
    deep(n - 1)
}

i32 main() {
    deep(3)
    ret 0
}
```

```output
uncaught exception: too deep
    at deep (deep.cro:3)
    at main (deep.cro:9)
```

La traccia viene stampata anche nelle build di release, con i nomi, i file e le righe del sorgente. Per farlo, all'eseguibile non servono le informazioni di debug: il compilatore vi scrive dentro una tabella compatta del codice.

## Panic

Un panic è un errore di programmazione: un indice fuori dai limiti, un overflow, una divisione per zero, una chiave mancante in una mappa, uno stack overflow. Non è un'eccezione e non si può catturare: il programma si ferma con codice di uscita 101 e stampa le chiamate che lo hanno portato fin lì.

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

`panic("reason")` ferma il programma di proposito, quando raggiunge uno stato che dovrebbe essere impossibile.

### assert

`assert(condition)` provoca un panic quando la sua condizione è falsa. Il messaggio ripete la condizione così come è scritta e, per un confronto tra numeri, i valori dei due lati:

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

`assert` funziona in ogni build, anche con `--release`. È anche lo strumento dei test del capitolo 12.

## Come scegliere

- Se il chiamante può fare qualcosa per rimediare all'errore, come chiedere un altro nome di file, lancia un'**eccezione**.
- Se l'errore significa che il programma è sbagliato, come un indice calcolato male, è un **panic**: correggi il programma.
- Se l'assenza di un risultato è normale, come una parola non trovata in un testo, restituisci un valore convenzionale o un `bool`, come fa `find` con -1.

## Riepilogo

- `throw`, `try`, `catch` e `finally` funzionano come in C++, Java e C#; le eccezioni implementano `Exception`.
- I distruttori vengono eseguiti mentre un'eccezione risale, quindi le risorse vengono sempre rilasciate.
- Un'eccezione non catturata, un panic e uno stack overflow stampano le chiamate che hanno portato fin lì, anche in release.
- I panic servono per gli errori di programmazione; `assert` controlla ciò che deve essere vero.
