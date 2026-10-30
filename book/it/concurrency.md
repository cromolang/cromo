# Thread, task e canali

Cromo può eseguire codice contemporaneamente in due modi: con i **thread** del sistema e con i **task** leggeri, molti dei quali condividono pochi thread. Entrambi si trovano nel modulo `std.thread`, con la stessa interfaccia, e il compilatore controlla ciò che condividono: due thread non scrivono mai gli stessi dati nello stesso momento.

## Thread

Un `thread.Thread` si crea da una lambda, si avvia con `start()` e si attende con `join()`:

```cromo
import std.thread

i64 sum(list<i64> values, i64 from, i64 to) {
    i64 total = 0
    for (i64 i = from; i < to; i++) {
        total += values[i]
    }
    ret total
}

i32 main() {
    list<i64> values
    for (i64 i = 1; i <= 1000000; i++) {
        values.add(i)
    }
    i64 low = 0
    thread.Thread worker = thread.Thread([values, ref low]() {
        low = sum(values, 0, 500000)
    })
    worker.start()
    i64 high = sum(values, 500000, 1000000)
    worker.join()
    println("{}", low + high)
    ret 0
}
```

```output
500000500000
```

La lambda legge `values` e modifica `low`, entrambe variabili di `main`. Finché il thread è in esecuzione, il compilatore non permette a `main` di modificare ciò che il thread legge, né di toccare ciò che modifica: qui `main` si limita a leggere `values`, e questo è consentito. Un thread che cattura per indirizzo appartiene al suo blocco: se `join()` non viene chiamato, il blocco lo attende alla propria fine, così le variabili che il thread usa vivono sempre più a lungo di lui.

Un thread che cattura solo con `move` possiede tutto ciò che usa, ed è libero: si può tenere in una lista e può sopravvivere al blocco che lo ha avviato.

Un'eccezione che termina un thread viene rilanciata da `join()`, nel thread che attende.

## Mutex

Quando più thread devono modificare gli stessi dati, i dati vanno messi in un `thread.Mutex<T>`. Si possono raggiungere solo dentro `lock`, che li passa con `ref` a un thread alla volta:

```cromo
import std.thread

i32 main() {
    thread.Mutex<i64> counter
    thread.Thread a = thread.Thread([counter]() {
        for (i32 i = 0; i < 1000; i++) {
            counter.lock([](ref i64 n) { n++ })
        }
    })
    thread.Thread b = thread.Thread([counter]() {
        for (i32 i = 0; i < 1000; i++) {
            counter.lock([](ref i64 n) { n += 2 })
        }
    })
    a.start()
    b.start()
    a.join()
    b.join()
    counter.lock([](ref i64 n) { println("{}", n) })
    ret 0
}
```

```output
3000
```

Non c'è modo di raggiungere l'`i64` senza il lock, e quindi non c'è modo di dimenticarselo. Il `lock` viene rilasciato alla fine della lambda, anche quando la attraversa un'eccezione.

## Task

Un `thread.Task` ha l'interfaccia e le regole di un `thread.Thread`, ma non è un thread del sistema: il runtime esegue molti task su pochi thread, uno per processore. Quando un task aspetta la rete, `time.sleep`, un mutex o `join()`, il suo thread nel frattempo esegue altri task. Il codice è lo stesso che si scrive con i thread: niente `async`, niente `await`, e nessuna funzione di un colore diverso.

Un task costa pochi kilobyte: un programma ne può avere centinaia di migliaia. `thread.Group` ne avvia molti e li attende tutti:

```cromo
import std.thread
import std.time

i32 main() {
    time.Instant start = time.now()
    thread.Atomic<i64> done
    thread.Group jobs
    for (i64 id = 0; id < 10000; id++) {
        jobs.start([done]() {
            time.msleep(100)
            done.add(1)
        })
    }
    jobs.wait()
    println("{} tasks waited 100 ms each", done.load())
    println("in less than a second: {}", start.elapsed().milliseconds() < 1000)
    ret 0
}
```

```output
10000 tasks waited 100 ms each
in less than a second: true
```

Diecimila thread del sistema richiederebbero gigabyte di stack; diecimila task richiedono qualche decina di megabyte.

- I task di un gruppo possono leggere le variabili dichiarate prima del gruppo, che restano costanti fino alla fine del suo blocco, e prenderne altre con `move`; non possono catturare con `ref`.
- `wait()` attende tutti i task, e rilancia la prima eccezione sollevata da un task. Alla fine del suo blocco, un gruppo attende da solo i propri task.
- Un task che chiama una funzione che blocca il suo thread, come la lettura di un file o una funzione C, tiene occupato quel thread; dopo circa un millisecondo il runtime assegna agli altri task un altro thread.

Usa i task per il lavoro che passa il tempo ad aspettare, come i server e i client di rete; usa i thread per i calcoli lunghi e per il codice C che blocca.

## Canali

Un `thread.Channel<T>` passa valori da un task o da un thread a un altro, in ordine:

```cromo
import std.thread

i32 main() {
    thread.Channel<string> lines = thread.Channel<string>(16)
    thread.Atomic<i64> letters
    thread.Group readers
    for (i32 r = 0; r < 4; r++) {
        readers.start([lines, letters]() {
            for (string line in lines) {
                letters.add(line.len())
            }
        })
    }
    list<string> words = ["channels", "pass", "values", "between", "tasks"]
    for (string word in words) {
        lines.send(word.copy())
    }
    lines.close()
    readers.wait()
    println("{} letters", letters.load())
    ret 0
}
```

```output
30 letters
```

- `thread.Channel<T>(n)` contiene fino a `n` valori: `send` attende finché il canale è pieno, `receive` finché è vuoto, e un task che attende lascia il suo thread agli altri. Con capacità 0, chi invia attende finché un ricevente non ha preso il valore.
- `close()` chiude il canale: i valori già inviati si possono ancora ricevere, dopodiché `receive()` lancia un'eccezione e `for (T v in channel)` termina.
- `next(ref v)` riceve senza eccezione: restituisce `false` quando il canale è chiuso e vuoto.

## Atomici

`thread.Atomic<T>` contiene un intero o un `bool` che più thread modificano senza lock: `load`, `store`, `add`, `exchange` e `compareExchange`. Le operazioni sono sequenzialmente consistenti, il modello su cui è più semplice ragionare. Un atomico non si può copiare, e i task e i thread che lo usano lo catturano in lettura, come `done` e `letters` qui sopra.

## Riepilogo

- `thread.Thread` è un thread del sistema; `thread.Task` è un task leggero con la stessa interfaccia.
- Il compilatore controlla ciò che thread e task catturano: i dati condivisi che cambiano vanno in un `thread.Mutex`.
- `thread.Group` avvia molti task e li attende; un task che attende lascia il suo thread agli altri.
- `thread.Channel` passa valori tra i task; `thread.Atomic` conta senza lock.
