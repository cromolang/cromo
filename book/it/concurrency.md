# Thread, task e canali

Cromo può eseguire codice contemporaneamente in due modi: con i **thread** del sistema e con i **task** leggeri, molti dei quali condividono pochi thread. Entrambi si trovano nel modulo `std.thread`, con la stessa interfaccia, e il compilatore controlla ciò che condividono: due thread non scrivono mai gli stessi dati nello stesso momento.

Il modulo ha sette tipi:

| Tipo | Che cos'è |
|---|---|
| `thread.Thread` | un thread del sistema, creato da una lambda |
| `thread.Task` | un task leggero, con l'interfaccia di `Thread` |
| `thread.Group` | task avviati uno per uno, anche in un ciclo, e attesi insieme |
| `thread.Mutex<T>` | dati che più thread modificano, uno alla volta |
| `thread.Shared<T>` | un valore con il suo lock, raggiunto attraverso un handle che si copia liberamente |
| `thread.Atomic<T>` | un intero o un `bool` che i thread modificano senza lock |
| `thread.Channel<T>` | valori passati da un thread o da un task a un altro, in ordine |

Le regole dei thread valgono anche per i task: la sezione sui task dice solo ciò che cambia.

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

Creare il thread non esegue nulla: la lambda viene eseguita a partire da `start()`, in un nuovo thread del sistema, mentre `main` prosegue. `join()` ne attende la fine.

### Che cosa cattura la lambda di un thread

La lambda di un thread cattura con le parole di [Funzioni anonime](lambdas.md):

- **Un nome** legge la variabile. Un valore semplice, come un numero (vedi [Possesso](ownership.md)), viene copiato quando si crea la lambda, così il blocco può poi modificare la propria variabile; ogni altro valore viene letto dove si trova, come `values` qui sopra.
- **`ref`** modifica la variabile, come `low` qui sopra.
- **`move`** prende il valore: il thread lo possiede. Un thread esegue la sua lambda una volta sola, quindi, a differenza delle altre lambda, può modificare ciò che ha preso.

Qui `limit` viene copiato quando si crea la lambda, quindi `main` modifica liberamente la propria variabile, e `names` viene spostata nel thread, che aggiunge un nome:

```cromo
import std.thread
import std.time

i32 main() {
    i64 limit = 1
    list<string> names = ["Ada", "Grace"]
    thread.Thread t = thread.Thread([limit, move names]() {
        time.msleep(50)
        names.add("Linus")
        println("the thread sees limit {} and {:?}", limit, names)
    })
    limit = 2
    t.start()
    limit = 3
    t.join()
    println("main has limit {}", limit)
    ret 0
}
```

```output
the thread sees limit 1 and ["Ada", "Grace", "Linus"]
main has limit 3
```

Un thread non può catturare un parametro `func` che non sia `const`, nemmeno attraverso una lambda dentro la propria: una funzione del genere potrebbe essere una lambda che modifica ciò che cattura, mentre un altro thread la legge. Il compilatore chiede di dichiarare il parametro `const func`, una funzione che non modifica nulla e può essere eseguita in più thread contemporaneamente:

```cromo error
import std.thread

void inBackground(func() work) {
    thread.Thread t = thread.Thread([work]() { work() })
    t.start()
    t.join()
}

i32 main() {
    inBackground([]() { println("working") })
    ret 0
}
```

```output
error: 'work' is a parameter func that is not const: a thread or a task cannot call it, as it may modify what it captures; declare it 'const func'
```

Con `const func` il thread può chiamarla. Funziona anche un parametro `move func()`: è la funzione stessa, consegnata alla funzione chiamata, quindi il thread può prenderla con `move`, e nessun altro ce l'ha:

```cromo
import std.thread

void inBackground(const func() work) {
    thread.Thread t = thread.Thread([work]() { work() })
    t.start()
    t.join()
}

thread.Thread later(move func() work) {
    ret thread.Thread([move work]() { work() })
}

i32 main() {
    inBackground([]() { println("working") })
    i64 n = 3
    thread.Thread t = later([n]() { println("n is {}", n) })
    t.start()
    t.join()
    ret 0
}
```

```output
working
n is 3
```

### I thread di un blocco

Un thread la cui lambda cattura per indirizzo, con `ref` o leggendo un valore che non è semplice, è un **thread del suo blocco**: usa variabili del blocco, quindi deve finire prima di loro. Il compilatore lo controlla con queste regole:

- Si dichiara come una variabile il cui valore è `thread.Thread(...)`, con la lambda scritta lì: `thread.Thread t = thread.Thread([ref v]() { ... })`.
- `t.start()` e `t.join()` si scrivono direttamente nel blocco che dichiara `t`, non dentro un `if` o un ciclo: così il compilatore sa dove il thread è in esecuzione.
- Mentre è in esecuzione, da `start()` a `join()`, il blocco non può modificare ciò che il thread legge, e non può usare affatto ciò che il thread ha con `ref`. Più thread possono leggere la stessa variabile; uno che l'ha con `ref` esclude gli altri, e avviarne un secondo è un errore.
- Se `join()` non viene chiamato, il thread viene atteso alla fine del suo blocco.
- Non si può spostare, assegnare, restituire, passare con `ref` né catturare.

```cromo error
import std.thread

i32 main(list<string> args) {
    list<i64> values = [1, 2, 3]
    i64 total = 0
    thread.Thread a = thread.Thread([values, ref total]() {
        for (i64 v in values) {
            total += v
        }
    })
    thread.Thread b = thread.Thread([ref total]() { total = 0 })
    a.start()
    values.add(4)
    println("{}", total)
    b.start()
    if (args.len() > 1) {
        a.join()
    }
    ret 0
}
```

```output
error: 'values' cannot be modified while the thread 'a' runs
error: 'total' is used by the thread 'a' until a.join()
error: the thread 'b' cannot start: 'total' is used by the thread 'a' until a.join()
error: join() of a thread of a block is written directly in the block that declares 'a'
```

Prima di `start()` e dopo `join()` le variabili tornano al blocco: `values.add(4)` prima di `a.start()`, o dopo `a.join()`, è consentito.

Un thread di un blocco resta dove è dichiarato:

```cromo error
import std.thread

thread.Thread counter(thread.Mutex<i64> total) {
    ret thread.Thread([total]() { total.lock([](ref i64 n) { n++ }) })
}

i32 main() {
    i64 x = 0
    thread.Thread t = thread.Thread([ref x]() { x = 1 })
    list<thread.Thread> kept
    kept.add(move t)
    ret 0
}
```

```output
error: a thread whose lambda captures by address belongs to its block: declare it, as thread.Thread t = thread.Thread(...)
error: 't' is a thread of its block, which captures by address: it cannot be moved, assigned, returned, passed with 'ref' or captured
```

Qui `count` arriva alla sua fine mentre il suo thread è ancora in esecuzione, e non chiama `join()`: la fine del blocco attende il thread, quindi `total` è già impostato quando `count` ritorna:

```cromo
import std.thread
import std.time

void count(ref i64 total) {
    thread.Thread t = thread.Thread([ref total]() {
        time.msleep(50)
        total = 5
        println("the thread ends")
    })
    t.start()
    println("count ends its work")
}

i32 main() {
    i64 total = 0
    count(ref total)
    println("total {}", total)
    ret 0
}
```

```output
count ends its work
the thread ends
total 5
```

### Thread liberi

Un thread che cattura solo con `move`, o valori semplici, possiede tutto ciò che usa: è **libero**. Si può restituire, spostare, tenere in una lista, e può sopravvivere al blocco che lo ha creato. Un thread libero che deve restituire un risultato usa un `thread.Shared`, che è un valore semplice, come si vede più avanti:

```cromo
import std.thread

// A thread that adds the numbers of 'part' to 'total'.
thread.Thread adder(move list<i64> part, thread.Shared<i64> total) {
    ret thread.Thread([move part, total]() {
        i64 sum = 0
        for (i64 v in part) {
            sum += v
        }
        total.lock([sum](ref i64 t) { t += sum })
    })
}

i32 main() {
    thread.Shared<i64> total = thread.Shared<i64>(0)
    list<thread.Thread> workers
    for (i64 w = 0; w < 4; w++) {
        list<i64> part
        for (i64 i = w * 250 + 1; i <= (w + 1) * 250; i++) {
            part.add(i)
        }
        workers.add(adder(move part, total))
    }
    for (ref thread.Thread w in workers) {
        w.start()
    }
    for (ref thread.Thread w in workers) {
        w.join()
    }
    total.lock([](ref i64 t) { println("{}", t) })
    ret 0
}
```

```output
500500
```

Il compilatore non segue un thread libero: i suoi `start()` e `join()` possono stare ovunque, anche in un ciclo, come qui.

### Thread staccati

Un thread libero si può anche **staccare**: dopo `start()`, `detach()` lo lascia proseguire da solo, come `std::thread::detach` in C++, un `JoinHandle` che Rust lascia cadere, o una goroutine di Go. La sua variabile può finire subito: il suo distruttore non lo attende più, nessuno lo attende con `join()`, e nemmeno la fine di `main` lo aspetta. È il modo di avviare un lavoro che nessuno deve aspettare, come il lavoro lungo che avvia un handler di una finestra ([Finestre e app](gui.md)).

```cromo
import std.thread
import std.time

i64 count(thread.Shared<i64> done) {
    i64 seen = 0
    done.lock([ref seen](ref i64 n) { seen = n })
    ret seen
}

i32 main() {
    thread.Shared<i64> done = thread.Shared<i64>(0)
    if (true) {
        thread.Thread worker = thread.Thread([done]() {
            time.msleep(100)
            done.lock([](ref i64 n) { n = 42 })
        })
        worker.start()
        worker.detach()
    }
    println("the block did not wait: {}", count(done))
    for (i32 i = 0; i < 100 && count(done) == 0; i++) {
        time.msleep(10)
    }
    println("the thread has finished: {}", count(done))
    ret 0
}
```

```output
the block did not wait: 0
the thread has finished: 42
```

- Si può staccare solo un thread libero: un thread di un blocco usa le variabili del suo blocco, e `detach()` su di esso è un errore del compilatore, `the thread 'worker' captures by address, and cannot be detached: it uses the variables of its block`.
- Un'eccezione che termina un thread staccato ferma il programma, come una che nessun `join()` raccoglie.
- I risultati tornano attraverso ciò che il thread condivide: uno `thread.Shared`, un canale che possiede con `move`, un atomico dentro uno `Shared`, o i componenti di una finestra.
- Anche `thread.Task` ha `detach()`, con le stesse regole.

### start(), join() e detach()

- `start()` due volte è un panic, e lo sono anche `join()` prima di `start()` e `join()` due volte. Per un thread di un blocco, un secondo `start()` nel suo blocco è già un errore del compilatore, anche dopo il suo `join()`.
- `detach()` prima di `start()` è un panic, e lo sono anche `join()`, `start()` e `detach()` dopo di esso.
- Un thread che non viene mai avviato non fa nulla, e nulla lo attende.
- Un thread avviato, né atteso con `join()` né staccato, viene atteso quando finisce la sua variabile: alla fine del suo blocco, o quando finisce la lista che lo contiene.

I panic dicono `the thread is already started`, `the thread is not started: call start() first`, `the thread is already joined` e `the thread is already joined or detached`.

### Eccezioni e panic in un thread

Un'eccezione che termina la lambda di un thread viene rilanciata da `join()`, nel thread che attende:

```cromo
import std.thread

i32 main() {
    thread.Thread t = thread.Thread([]() { throw Error("no data") })
    t.start()
    try {
        t.join()
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
caught: no data
```

Un'eccezione che nessun `join()` raccoglie ferma il programma quando il thread viene atteso, come un'eccezione che esce da `main` ([Errori](errors.md)), con codice di uscita 1:

```cromo file=lost.cro
import std.thread

void work() {
    throw Error("no data")
}

i32 main() {
    thread.Thread t = thread.Thread([]() { work() })
    t.start()
    println("main goes on")
    ret 0
}
```

```output
main goes on
uncaught exception in a thread: no data
    at work (lost.cro:4)
    at main.lambda (lost.cro:8)
```

Un panic in un thread qualsiasi ferma subito l'intero programma, con codice di uscita 101: `join()` non lo cattura, e si fermano anche gli altri thread.

```cromo file=late.cro
import std.thread

i32 main() {
    list<i64> values = [1, 2]
    thread.Thread t = thread.Thread([values]() { println("{}", values[5]) })
    t.start()
    t.join()
    println("not printed")
    ret 0
}
```

```output
panic: index out of bounds: the index is 5 but the length is 2 at late.cro:5:70
    at main.lambda (late.cro:5)
```

### Lo stack di un thread

Un thread ha lo stack del thread principale: 1 MB su Windows, 8 MB su Linux, con una pagina di guardia alla fine. Una ricorsione che lo supera è un panic, `stack overflow`, come in `main`. La memoria di uno stack viene presa quando viene usata.

### Stampare da più thread

Ogni `print` e `println` scrive tutto il suo testo in una volta: due thread non mescolano mai i pezzi delle loro righe. L'ordine delle righe di thread diversi cambia da un'esecuzione all'altra. `format` scrive nella propria stringa, e non aspetta gli altri. Un panic attende la fine di una stampa in corso, poi ferma il programma.

```cromo nocheck
import std.thread

void steps(string name) {
    for (i32 step = 1; step <= 3; step++) {
        println("{}: step {} of 3", name, step)
    }
}

i32 main() {
    thread.Thread a = thread.Thread([]() { steps("first") })
    thread.Thread b = thread.Thread([]() { steps("second") })
    a.start()
    b.start()
    steps("main")
    a.join()
    b.join()
    ret 0
}
```

Un'esecuzione ha stampato:

```output
main: step 1 of 3
main: step 2 of 3
first: step 1 of 3
first: step 2 of 3
first: step 3 of 3
second: step 1 of 3
main: step 3 of 3
second: step 2 of 3
second: step 3 of 3
```

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

Non c'è modo di raggiungere l'`i64` senza il lock, e quindi non c'è modo di dimenticarselo. Entrambi i thread catturano `counter` leggendolo: `lock` non modifica il mutex, ne presta i dati.

- Un mutex parte sbloccato, con il valore predefinito dei suoi dati: `thread.Mutex<i64> counter` è pronto, a 0. Non ha un costruttore: per partire da un altro valore, lo si imposta con un primo `lock`.
- `lock` viene rilasciato alla fine della sua lambda, anche quando la attraversa un'eccezione: le modifiche fatte prima dell'eccezione restano nei dati, e l'eccezione prosegue.
- `lock` non è ricorsivo: bloccare di nuovo lo stesso mutex dentro la sua lambda attende per sempre.
- Chi trova il mutex bloccato attende: nel frattempo un task lascia il suo thread agli altri. Un mutex non è equo: chi attende da più tempo non è sempre il prossimo ad averlo.
- Un mutex non si può copiare: la copia sarebbe un secondo lock, che gli altri thread non vedono. Si può spostare mentre nessun thread lo usa.

Quando la lambda di `lock` lancia un'eccezione, ciò che ha modificato prima resta nel mutex, che viene sbloccato:

```cromo
import std.thread

i32 main() {
    thread.Mutex<list<string>> names
    names.lock([](ref list<string> v) { v.add("Ada") })
    try {
        names.lock([](ref list<string> v) {
            v.add("Grace")
            throw Error("stop")
        })
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    names.lock([](ref list<string> v) { println("{:?}", v) })
    ret 0
}
```

```output
caught: stop
["Ada", "Grace"]
```

Copiare un mutex è un errore, e lo è anche spostarlo mentre un thread lo usa:

```cromo error
import std.thread

i32 main() {
    thread.Mutex<i64> counter
    thread.Mutex<i64> copy = counter
    thread.Thread a = thread.Thread([counter]() { counter.lock([](ref i64 n) { n++ }) })
    a.start()
    thread.Mutex<i64> moved = move counter
    a.join()
    ret 0
}
```

```output
error: values of type Mutex<i64> cannot be copied
error: 'counter' cannot be modified while the thread 'a' runs
```

## Shared

Un `Mutex` resta dove è dichiarato: una lambda che viene tenuta, come l'handler di una finestra, non può catturarlo, e i thread liberi non possono condividerlo. `thread.Shared<T>` è un handle a un valore con il suo lock, tenuto fino alla fine del programma: un valore semplice, che si copia liberamente, e ogni copia raggiunge lo stesso valore con `lock`:

```cromo
import std.thread

i32 main() {
    thread.Shared<list<string>> log = thread.Shared<list<string>>(list<string>())
    list<func(string)> handlers
    handlers.add([log](string line) { log.lock([line](ref list<string> v) { v.add(line) }) })
    handlers[0]("opened")
    handlers[0]("saved")
    log.lock([](ref list<string> v) { println("{}", join(v, ", ")) })
    ret 0
}
```

```output
opened, saved
```

Gli handler delle finestre tengono così il loro stato, come mostra [Finestre e app](gui.md); thread e task catturano uno `Shared` come qualunque altro valore semplice, e un thread che cattura solo handle `Shared` e valori semplici è libero.

- `thread.Shared<T>(value)` sposta il valore in una memoria propria, che resta fino alla fine del programma: il distruttore del valore non viene mai eseguito.
- `lock` funziona come quello di `Mutex`: uno alla volta, rilasciato anche quando lo attraversa un'eccezione; bloccarlo di nuovo dentro la sua lambda attende per sempre.
- Uno `Shared` dichiarato senza valore non ne ha nessuno: `lock` su di esso è un panic, `this thread.Shared has no value: make it with thread.Shared<T>(value)`.

Qui due handle raggiungono lo stesso `Log`, il cui distruttore non viene mai eseguito:

```cromo
import std.thread

struct Log {
    list<string> lines

    ~Log() {
        println("the log ends")
    }
}

i32 main() {
    thread.Shared<Log> log = thread.Shared<Log>(Log())
    thread.Shared<Log> other = log
    other.lock([](ref Log l) { l.lines.add("opened") })
    log.lock([](ref Log l) { println("{} line", l.lines.len()) })
    ret 0
}
```

```output
1 line
```

Usa un `Mutex` per i dati di un blocco, come i contatori di un calcolo, e uno `Shared` per uno stato che vive quanto il programma, come quello di una finestra: poiché il suo valore non viene mai liberato, un programma che creasse un nuovo `Shared` in un ciclo li terrebbe tutti.

## Atomici

`thread.Atomic<T>` contiene un intero o un `bool` che più thread modificano senza lock. Ogni operazione avviene per intero, e tutte in un unico ordine che ogni thread vede: la consistenza sequenziale del C++, il modello su cui è più semplice ragionare.

| Metodo | Che cosa fa | Restituisce |
|---|---|---|
| `load()` | legge il valore | il valore |
| `store(v)` | scrive `v` | nulla |
| `add(n)` | aggiunge `n`, solo per gli interi | il valore precedente |
| `exchange(v)` | scrive `v` | il valore precedente |
| `compareExchange(expected, desired)` | scrive `desired` solo se il valore è `expected`, in un solo passo | `true` se ha scritto |
| `loadRelaxed()`, `storeRelaxed(v)`, `addRelaxed(n)` | come `load`, `store` e `add`, senza un ordine tra i thread | come quelli |

Un atomico dichiarato senza valore parte da 0 o da `false`; `thread.Atomic<i64>(5)` parte da 5. `add` ricomincia dall'estremo opposto quando il risultato non ci sta, in ogni build, senza panic. A differenza del C++, `compareExchange` non scrive in `expected` il valore che ha trovato: il programma chiama di nuovo `load()`.

```cromo
import std.thread

i32 main() {
    thread.Atomic<i64> count = thread.Atomic<i64>(5)
    thread.Atomic<bool> ready
    println("{} {}", count.load(), ready.load())
    println("add gives {}, now {}", count.add(10), count.load())
    println("exchange gives {}, now {}", count.exchange(100), count.load())
    println("{}", count.compareExchange(5, 7))
    println("{}, now {}", count.compareExchange(100, 7), count.load())
    println("{}", ready.exchange(true))
    thread.Atomic<i32> big = thread.Atomic<i32>(2147483647)
    big.add(1)
    println("{}", big.load())
    ret 0
}
```

```output
5 false
add gives 5, now 15
exchange gives 15, now 100
false
true, now 7
false
-2147483648
```

`compareExchange` in un ciclo modifica un valore secondo una regola propria, come tenere il più grande visto finora: se un altro thread ha cambiato il valore tra `load()` e `compareExchange`, il ciclo lo rilegge.

```cromo
import std.thread

// Raises 'best' to 'value', if it is larger, without a lock.
void raise(thread.Atomic<i64> best, i64 value) {
    for {
        i64 seen = best.load()
        if (value <= seen || best.compareExchange(seen, value)) {
            ret
        }
    }
}

i32 main() {
    list<i64> values = [12, 7, 41, 3, 29, 41, 18, 5]
    thread.Atomic<i64> best
    thread.Thread a = thread.Thread([values, best]() {
        for (i64 i = 0; i < 4; i++) {
            raise(best, values[i])
        }
    })
    thread.Thread b = thread.Thread([values, best]() {
        for (i64 i = 4; i < 8; i++) {
            raise(best, values[i])
        }
    })
    a.start()
    b.start()
    a.join()
    b.join()
    println("{}", best.load())
    ret 0
}
```

```output
41
```

`loadRelaxed`, `storeRelaxed` e `addRelaxed` sono più veloci dove conta solo il valore, come un contatore che molti thread aumentano: il valore non è mai scritto a metà, ma non danno un ordine a ciò che i thread fanno intorno, quindi un altro thread potrebbe non vedere ancora ciò che questo ha scritto prima.

Un atomico non si può copiare, come un mutex; i thread e i task che lo usano lo catturano leggendolo, e una funzione lo prende come parametro in lettura, come `raise` qui sopra. Un tipo che non sia un intero o un `bool` è un errore dove è scritto, e `add` di un `Atomic<bool>` è un errore nella chiamata:

```cromo error
import std.thread

i32 main() {
    thread.Atomic<f64> speed
    thread.Atomic<bool> flag
    flag.add(true)
    thread.Atomic<i64> a
    thread.Atomic<i64> b = a
    ret 0
}
```

```output
error: thread.Atomic holds an integer or a bool, not f64
error: add() is for an atomic integer, not for Atomic<bool>
error: values of type Atomic<i64> cannot be copied
```

## Task

Un `thread.Task` ha l'interfaccia e le regole di un `thread.Thread`, ma non è un thread del sistema: il runtime esegue molti task su pochi thread, i suoi **worker**, uno per processore. Quando un task attende, la rete, `time.sleep`, un mutex, un canale o `join()`, nel frattempo il suo worker esegue altri task. Il codice è lo stesso che si scrive con i thread: niente `async`, niente `await`, e nessuna funzione di un colore diverso.

```cromo file=task.cro
import std.thread

i32 main() {
    list<i64> values = [1, 2, 3]
    i64 total = 0
    thread.Task t = thread.Task([values, ref total]() {
        for (i64 v in values) {
            total += v
        }
    })
    t.start()
    t.join()
    println("{}", total)
    thread.Task failing = thread.Task([]() { throw Error("bad input") })
    failing.start()
    ret 0
}
```

```output
6
uncaught exception in a task: bad input
    at main.lambda (task.cro:14)
```

Un task cattura, si avvia e si attende come un thread, con gli stessi errori e panic; i messaggi dicono "task": `the task 'b' cannot start: 'x' is used by the task 'a' until a.join()`.

### Quanto costa un task

- Un task ha uno stack di 1 MB, riservato con una pagina di guardia, e la memoria viene presa pagina per pagina quando viene usata: un task che attende usa pochi KB. Una ricorsione che ha bisogno di più di 1 MB di stack va in overflow in un task, anche su Linux, dove un thread ha 8 MB.
- Lo stack di un task che finisce viene tenuto, con la sua memoria, per il task successivo, fino a 1024 stack; gli altri restituiscono la loro memoria al sistema.
- Su Windows un programma può avere più di centomila task contemporaneamente: 100 000 task che attendono usano circa mezzo gigabyte.
- Su Linux ogni stack è, per il kernel, due aree di memoria, la pagina di guardia e lo stack, e il kernel limita le aree di un processo con `vm.max_map_count`, di solito 65530: possono quindi vivere contemporaneamente circa 32 000 task, tanti quanti i thread di un programma C. Oltre il limite, avviare un task è un panic: `cannot start a task: the system has no resources for it`. Alcuni sistemi impostano un limite più alto, e l'amministratore può alzarlo.

Un thread del sistema costa di più: uno stack riservato per ciascuno, 1 MB su Windows e 8 MB su Linux, e il lavoro del sistema per avviarlo e per passare da un thread all'altro.

### I worker

- C'è un worker per ogni processore logico: tutti i gruppi di processori su Windows, i processori che il processo può usare su Linux. Il loro numero non si può cambiare.
- Un task che chiama una funzione che blocca il suo thread, come la lettura di un file, una funzione C o la ricerca del nome di un host su Windows, tiene occupato il suo worker. Ogni millisecondo il runtime controlla i worker: mentre ci sono task in attesa di essere eseguiti e nessun worker è libero, un worker che ha eseguito lo stesso task per l'intero millisecondo se lo tiene, e un nuovo worker prende il suo posto per gli altri. Così qualunque libreria C funziona dentro un task, e un calcolo lungo non ferma gli altri task. I worker aggiunti sono al massimo 256 oltre i processori.
- `main` non è un task: è il primo thread del sistema. Quando attende, per `join()`, un canale o un mutex, attende come un thread, e i task proseguono nei worker. Avvia i task come fa qualunque thread.
- Un task che trova un mutex bloccato gira a vuoto per un attimo, poi si sospende e lascia il suo worker agli altri; un thread dorme.

## Gruppi

`thread.Group` avvia molti task e li attende tutti. `g.start(lambda)` avvia subito un task, anche in un ciclo; `g.wait()` attende i task avviati fino a quel momento:

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

Diecimila thread del sistema riserverebbero 10 GB di stack su Windows e 80 GB su Linux; diecimila task che attendono usano qualche decina di megabyte.

I task di un gruppo vengono eseguiti contemporaneamente, anche tra loro, quindi condividono solo ciò che nessuno modifica:

- Possono leggere le variabili dichiarate prima del gruppo. Le variabili che leggono restano costanti dalla dichiarazione del gruppo, non dal suo primo `start()`, fino alla fine del suo blocco, anche dopo `wait()`.
- Prendono altri valori con `move`; i valori semplici, come `id` in un ciclo, vengono copiati, ovunque siano dichiarati.
- Non possono catturare con `ref`: i dati che i task modificano vanno in un `Mutex`, uno `Shared` o un `Atomic` dichiarato prima del gruppo, come `done` qui sopra.
- Un gruppo non si può spostare, assegnare, restituire, passare con `ref` né catturare.

```cromo error
import std.thread

void take(ref thread.Group g) {
}

i32 main() {
    list<i64> before = [1, 2]
    i64 count = 0
    thread.Group jobs
    before.add(3)
    list<i64> after = [3]
    jobs.start([before]() { println("{}", before.len()) })
    jobs.start([after]() { println("{}", after.len()) })
    jobs.start([ref count]() { count++ })
    jobs.wait()
    before.add(4)
    take(ref jobs)
    ret 0
}
```

```output
error: 'before' cannot be modified after the group 'jobs', whose tasks read it
error: the tasks of the group 'jobs' read 'after', declared after the group: declare it before, or take it with 'move'
error: the tasks of a group run at the same time: they cannot capture 'count' with 'ref'; read it, or take it with 'move'
error: 'before' cannot be modified while the tasks of the group 'jobs' read it
error: 'jobs' is a group of tasks, which read the variables of its block: it cannot be moved, assigned, returned, passed with 'ref' or captured
```

Le eccezioni dei task:

- `wait()` attende tutti i task avviati fino a quel momento, poi rilancia la prima eccezione di uno di loro; le eccezioni degli altri vanno perse.
- Dopo `wait()` il gruppo può avviare altri task, e attenderli di nuovo.
- Alla fine del suo blocco un gruppo attende da solo i propri task. Un'eccezione che nessun `wait()` raccoglie ferma il programma, come quella di un thread mai atteso con `join()`.

Qui lo stesso gruppo attende tre volte; la seconda eccezione va persa, e l'ultima, che nessun `wait()` raccoglie, ferma il programma:

```cromo file=group.cro
import std.thread
import std.time

i32 main() {
    thread.Atomic<i64> done
    thread.Group jobs
    jobs.start([done]() { done.add(1) })
    jobs.wait()
    jobs.start([done]() { done.add(10) })
    jobs.wait()
    println("done: {}", done.load())
    jobs.start([]() { throw Error("first") })
    jobs.start([]() {
        time.msleep(100)
        throw Error("second")
    })
    try {
        jobs.wait()
    } catch (Exception e) {
        println("caught {}", e.message())
    }
    jobs.wait()
    println("the second is lost")
    jobs.start([]() { throw Error("never taken") })
    ret 0
}
```

```output
done: 11
caught first
the second is lost
uncaught exception in a task: never taken
    at main.lambda (group.cro:24)
```

## Canali

Un `thread.Channel<T>` passa valori da un task o da un thread a un altro, nell'ordine in cui sono stati inviati:

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

Più task e thread possono inviare allo stesso canale e ricevere da esso: ogni valore va a un solo ricevente. I task e i thread catturano un canale leggendolo, e una funzione lo prende come parametro in lettura: `send`, `receive` e `close` non hanno bisogno di `ref`. Un canale non si può copiare.

### La capacità

`thread.Channel<T>(n)` contiene fino a `n` valori: `send` attende finché il canale è pieno, `receive` finché è vuoto, e un task che attende lascia il suo worker agli altri. Un canale dichiarato senza valore ha capacità 0: chi invia attende finché un ricevente non ha preso il valore. Una capacità negativa è un panic.

```cromo
import std.thread
import std.time

// Sends 1, 2, 3 and 4, and receives them 100 ms later.
void run(thread.Channel<i64> channel) {
    thread.Atomic<i64> sent
    thread.Task producer = thread.Task([channel, sent]() {
        for (i64 i = 1; i <= 4; i++) {
            channel.send(i)
            sent.add(1)
        }
        channel.close()
    })
    producer.start()
    time.msleep(100)
    println("sent before the first receive: {}", sent.load())
    i64 sum = 0
    for (i64 v in channel) {
        sum += v
    }
    producer.join()
    println("received: {}", sum)
}

i32 main() {
    run(thread.Channel<i64>(2))
    thread.Channel<i64> handoff
    run(handoff)
    ret 0
}
```

```output
sent before the first receive: 2
received: 10
sent before the first receive: 0
received: 10
```

### Inviare, ricevere e chiudere

- `send(move v)` sposta il valore nel canale. Lancia un'eccezione se il canale è chiuso, e il valore viene distrutto.
- `receive()` restituisce il valore successivo. Una volta che il canale è chiuso e vuoto, lancia un'eccezione.
- `next(ref v)` riceve senza l'eccezione: mette il valore successivo in `v` e restituisce `true`, oppure restituisce `false`, lasciando `v` invariato, una volta che il canale è chiuso e vuoto.
- `close()` chiude il canale: i valori già inviati si possono ancora ricevere. Un secondo `close()` non fa nulla, quindi due task che finiscono il loro lavoro possono chiudere entrambi lo stesso canale.
- I valori ancora presenti in un canale quando questo finisce vengono distrutti insieme a lui.

```cromo
import std.thread

i32 main() {
    thread.Channel<i64> numbers = thread.Channel<i64>(4)
    numbers.send(1)
    numbers.send(2)
    numbers.close()
    numbers.close()
    try {
        numbers.send(3)
    } catch (Exception e) {
        println("send after close() throws")
    }
    println("{}", numbers.receive())
    i64 v = 0
    println("{} {}", numbers.next(ref v), v)
    println("{} {}", numbers.next(ref v), v)
    try {
        numbers.receive()
    } catch (Exception e) {
        println("receive from a closed, empty channel throws")
    }
    ret 0
}
```

```output
send after close() throws
1
true 2
false 2
receive from a closed, empty channel throws
```

Quando il canale viene chiuso, chi invia può essere in attesa. Con capacità 0 attende finché un ricevente non ha il suo valore: se il canale viene chiuso prima, `send()` ritorna normalmente, e il valore si può ancora ricevere. Chi invia ed è in attesa su un canale pieno riceve un'eccezione, come un `send()` dopo `close()`:

```cromo
import std.thread
import std.time

i32 main() {
    thread.Channel<string> handoff
    thread.Channel<string> full = thread.Channel<string>(1)
    thread.Task a = thread.Task([handoff]() {
        handoff.send("kept")
        println("send() to handoff returned")
    })
    thread.Task b = thread.Task([full]() {
        full.send("first")
        try {
            full.send("second")
        } catch (Exception e) {
            println("send() to full threw")
        }
    })
    a.start()
    b.start()
    time.msleep(100)
    handoff.close()
    a.join()
    full.close()
    b.join()
    for (string s in handoff) {
        println("handoff still gives {}", s)
    }
    for (string s in full) {
        println("full still gives {}", s)
    }
    ret 0
}
```

```output
send() to handoff returned
send() to full threw
handoff still gives kept
full still gives first
```

### for su un canale

`for (T v in channel)` riceve finché il canale non è chiuso e vuoto, poi termina: è il modo abituale di leggere un canale. A differenza degli elementi di una lista, `v` è una variabile del corpo, che possiede il valore ricevuto: il corpo può modificarlo, o spostarlo, per esempio in un altro canale.

```cromo
import std.thread

i32 main() {
    thread.Channel<string> words = thread.Channel<string>(8)
    thread.Channel<string> loud = thread.Channel<string>(8)
    words.send("tasks")
    words.send("threads")
    words.close()
    for (string w in words) {
        w = w.upper()
        loud.send(move w)
    }
    loud.close()
    for (string w in loud) {
        println("{}", w)
    }
    ret 0
}
```

```output
TASKS
THREADS
```

Il canale di un `for` è una variabile o un campo, letto a ogni giro; `ref` è un errore, perché non c'è nulla da modificare sul posto:

```cromo error
import std.thread

thread.Channel<i64> numbers() {
    thread.Channel<i64> c = thread.Channel<i64>(1)
    c.close()
    ret c
}

i32 main() {
    thread.Channel<string> words = thread.Channel<string>(8)
    words.close()
    for (ref string w in words) {
        w = "x"
    }
    for (i64 n in numbers()) {
        println("{}", n)
    }
    thread.Channel<string> copy = words
    ret 0
}
```

```output
error: the values of a channel belong to the loop that receives them: write for (string w in ...)
error: a 'for' receives from a channel in a variable or in a field
error: values of type Channel<string> cannot be copied
```

## Dormire e cedere il turno

`time.sleep(seconds)`, `time.msleep(milliseconds)` e `time.usleep(microseconds)`, del modulo `std.time` ([File, processi e tempo](system.md)), fanno una pausa: un thread dorme, e un task attende mentre il suo worker esegue gli altri task. Un tempo di 0 o meno ritorna subito, anche in un task: non cede il worker agli altri. Non c'è un `thread.yield()`: un task lascia il suo worker quando attende, e dei task che non attendono mai si occupa il runtime, come spiega la sezione sui worker qui sopra.

## Thread o task

Thread e task hanno la stessa interfaccia, quindi passare dagli uni agli altri significa cambiare `Thread` in `Task`. La scelta:

- **Task** per il lavoro che attende, come i server e i client di rete, gli stadi di una pipeline collegati da canali e il lavoro lungo che una finestra avvia; e ogni volta che le unità di lavoro sono molte: migliaia di task costano poco, e solo i task hanno un `thread.Group`.
- **Thread** per pochi calcoli lunghi, per il codice C che blocca a lungo il suo thread, poiché ogni chiamata del genere tiene occupato un worker e i worker aggiunti sono al massimo 256, e per una ricorsione che ha bisogno di più dell'1 MB di stack di un task.

## Riepilogo

- `thread.Thread` è un thread del sistema; `thread.Task` un task leggero con la stessa interfaccia. Entrambi si creano da una lambda, si avviano con `start()` e si attendono con `join()`.
- La lambda legge, modifica con `ref` o prende con `move`; non può catturare un parametro `func` che non sia `const`.
- Un thread che cattura per indirizzo appartiene al suo blocco: `start()` e `join()` si scrivono in quel blocco, fino a `join()` il blocco non può modificare ciò che il thread legge né usare ciò che modifica, e la fine del blocco lo attende. Un thread che possiede ciò che usa è libero.
- `start()` o `join()` due volte, e `join()` prima di `start()`, sono panic. `join()` rilancia l'eccezione del thread; un'eccezione che nessuno raccoglie ferma il programma, e un panic in un thread qualsiasi lo ferma subito.
- I dati che i thread modificano vanno in un `thread.Mutex`; `thread.Shared` è un handle a un valore con il suo lock, che possono tenere anche gli handler delle finestre e i thread liberi.
- `thread.Atomic` contiene un intero o un `bool`; `add` ed `exchange` restituiscono il valore precedente, `compareExchange` se ha scritto.
- Un task costa pochi KB; su Linux ne possono vivere contemporaneamente circa 32 000. `main` è un thread del sistema.
- `thread.Group` avvia molti task e li attende; i suoi task leggono le variabili dichiarate prima del gruppo, che restano costanti fino alla fine del suo blocco.
- `thread.Channel` passa valori in ordine, con una capacità, 0 per impostazione predefinita; `close()` lo chiude, e `for (T v in channel)` riceve fino ad allora.
- Ogni `print` e `println` scrive la sua riga intera, anche tra thread. Una pausa di 0 ritorna subito, e non c'è un `thread.yield()`.
