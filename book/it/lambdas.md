# Funzioni anonime

Una funzione anonima, o *lambda*, è una funzione scritta nel punto in cui serve, come argomento o come valore. Può usare le variabili che ha intorno, e il modo in cui le prende segue le regole spiegate in [Possesso](ownership.md).

## Scrivere una lambda

```cromo
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

i32 main() {
    list<i64> values = [3, 8, 12, 5, 20]
    i64 limit = 6
    i64 big = count(values, [limit](i64 x) bool { ret x > limit })
    println("{} values are above {}", big, limit)
    ret 0
}
```

```output
3 values are above 6
```

La lambda è `[limit](i64 x) bool { ret x > limit }`:

- tra parentesi quadre, le variabili che **cattura**, qui `limit`; una lambda che non cattura nulla comincia con `[]`;
- i suoi parametri, come quelli di una funzione;
- il tipo che restituisce, dopo i parametri; se manca, la lambda non restituisce nulla;
- il suo corpo, dove `ret` esce dalla lambda, non dalla funzione che la contiene.

Un parametro di tipo funzione si chiama proprio come una funzione: `keep(v)`.

Una funzione dichiarata con un nome non è un valore: `count(values, isEven)` è l'errore `'isEven' is a function: call it with '()'`. Al suo posto va una lambda che la chiama: `count(values, [](i64 x) bool { ret isEven(x) })`.

## Il tipo di una funzione

`func(i64) bool` è il tipo di una funzione che riceve un `i64` e restituisce un `bool`: la parola chiave `func`, i tipi dei parametri e, dopo di essi, il tipo restituito. Un tipo funzione senza tipo restituito è una funzione che non restituisce nulla: `func()`, `func(string)`.

I parametri mantengono i loro modi, come in una funzione: `func(ref i64)` è una funzione che può modificare il suo argomento, `func(move string)` una che prende il suo argomento, e la chiamata scrive `ref` e `move` come per qualsiasi funzione:

```cromo
void update(ref list<i64> values, func(ref i64) change) {
    for (ref i64 v in values) {
        change(ref v)
    }
}

void give(func(move string) take) {
    string text = "a new string"
    take(move text)
}

i32 main() {
    list<i64> values = [1, 2, 3]
    update(ref values, [](ref i64 v) { v *= 10 })
    println("{:?}", values)

    give([](move string s) { println("took {}", s) })
    ret 0
}
```

```output
[10, 20, 30]
took a new string
```

Una lambda è accettata solo dove il tipo è lo stesso, con gli stessi tipi e modi dei parametri e lo stesso tipo restituito:

```cromo error
void update(ref list<i64> values, func(ref i64) change) {
    for (ref i64 v in values) {
        change(ref v)
    }
}

i32 main() {
    list<i64> values = [1, 2, 3]
    update(ref values, [](i64 v) {
        println("{}", v)
    })
    ret 0
}
```

```output
error: a value of type func(i64) cannot be used as func(ref i64)
```

Un tipo funzione può usare i parametri di tipo di un generico, come `func(T) U` in `U apply<T, U>(T value, func(T) U f)`: lo mostra [Interfacce e generici](interfaces-generics.md).

## Catture

Una lambda dichiara come usa ogni variabile che cattura, con le stesse parole che si usano per i parametri:

| Cattura | La lambda |
|---|---|
| `[limit]` | legge la variabile |
| `[ref total]` | modifica la variabile |
| `[move name]` | prende il valore, che la variabile perde |

### Leggere

Un nome da solo legge la variabile. Un valore di tipo semplice, un numero, un `bool` o una struct fatta solo di questi, viene copiato quando la lambda viene creata, quindi le modifiche successive alla variabile non raggiungono la lambda:

```cromo
i32 main() {
    i64 limit = 1
    func(i64) bool above = [limit](i64 x) bool { ret x > limit }
    limit = 100
    println("{} {}", above(5), limit)
    ret 0
}
```

```output
true 100
```

Qualsiasi altro valore, come una `string` o una lista, viene letto dove si trova, per indirizzo, senza copiarlo. Una lambda del genere dipende dalla variabile, e può essere passata solo come argomento, come spiega più avanti la sezione sulle lambda come valori.

### Modificare con ref

Con `ref`, la lambda modifica la variabile stessa:

```cromo
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

i32 main() {
    list<i64> values = [3, 8, 12, 5, 20]
    i64 total = 0
    count(values, [ref total](i64 x) bool {
        total += x
        ret true
    })
    println("total {}", total)
    ret 0
}
```

```output
total 48
```

Un parametro si cattura con `ref` solo se è un parametro `ref`: un parametro in lettura non può essere modificato, né dalla funzione né dalle sue lambda, e `[ref total]` su un parametro del genere è l'errore `cannot capture 'total' with 'ref': parameter 'total' cannot be modified`.

### Prendere con move

Con `move`, la lambda prende il valore e ne diventa proprietaria: dopo, la variabile non si può più usare, come dopo qualsiasi `move`. Solo una variabile locale o un parametro `move` possono cedere il loro valore; un parametro in lettura appartiene al chiamante, e `[move text]` su un parametro del genere è l'errore `cannot capture 'text' with 'move': only a local variable or a 'move' parameter can give its value`. Per un tipo semplice, `move` è una copia, e la variabile resta utilizzabile.

```cromo
func() greeter(move string name) {
    ret [move name]() { println("hello {}", name) }
}

i32 main() {
    func() hello = greeter("Anna")
    hello()
    hello()
    ret 0
}
```

```output
hello Anna
hello Anna
```

### Le regole delle catture

- Una lambda usa solo le variabili esterne che cattura: qualsiasi altra è un errore, che chiede di scriverla tra le `[]`.
- Un nome si cattura una volta sola: `[limit, ref limit]` è l'errore `'limit' is already captured`.
- Un campo non si può catturare: dentro un metodo, `[count]` su un campo è un errore; il metodo lo copia prima in una variabile, `i64 c = count`, e cattura `c`. Una struct locale si cattura intera, `[p]`, e la lambda legge `p.x`.
- Ciò che si cattura senza `ref` è in sola lettura, anche ciò che la lambda ha preso con `move`: la lambda può essere eseguita di nuovo, e deve ritrovare le sue catture com'erano. Solo la lambda di un thread o di un task può modificare ciò che ha preso, come spiega la fine del capitolo.
- Per lo stesso motivo, un valore catturato non si può spostare fuori dalla lambda: `ret name` ne dà una copia, `ret move name` è un errore.
- `break` e `continue` non escono dalla lambda: al suo interno funzionano solo in un `for` della lambda stessa.

Il compilatore dice quale regola una lambda viola:

```cromo error
void run(func() f) {
    f()
}

struct Counter {
    i64 count

    void show() {
        run([count]() { println("{}", count) })
    }
}

i32 main() {
    i64 total = 0
    run([]() {
        total++
    })
    i64 limit = 3
    run([limit]() {
        limit = 4
    })
    string name = "Anna"
    func() string give = [move name]() string { ret move name }
    ret 0
}
```

```output
error: 'count' is a field: copy it into a variable to capture it
error: 'total' is not captured: write it between the [] of the lambda
error: 'limit' is captured to be read: capture it with 'ref limit' to modify it
error: cannot move 'name': it is captured, and the lambda can run again
```

### Le catture e gli altri argomenti

Ciò che una lambda cattura per indirizzo, con `ref` o leggendo un valore non semplice, conta come usato dalla chiamata che riceve la lambda. Quindi la stessa chiamata non può passare anche quella variabile con `ref`, così come due argomenti `ref` non possono essere la stessa variabile ([Possesso](ownership.md)):

```cromo error
void add(ref i64 total, func(i64) f) {
    f(1)
}

i32 main() {
    i64 total = 0
    add(ref total, [ref total](i64 x) {
        total += x
    })
    ret 0
}
```

```output
error: 'total' overlaps 'total' in the same call, which a lambda of the call captures
```

## Le lambda come valori

Dove può andare una lambda dipende dalle sue catture:

- Una lambda che cattura per indirizzo, con `ref` o leggendo un valore non semplice, può essere passata solo come argomento, a un parametro che la legge, come `keep` di `count`, non a un parametro `move`: le variabili a cui fa riferimento vivono nella funzione chiamante, e la lambda finisce insieme alla chiamata.
- Una lambda che non cattura nulla, che cattura solo valori semplici o che prende i suoi valori con `move` possiede tutto ciò che usa. È un valore: si può tenere in una variabile, in un campo o in una lista, restituire e passare a un parametro `move`. Tenere una lambda che ha delle catture le mette sullo heap, come una `string` vi tiene il suo testo.

```cromo
func(i64) i64 adder(i64 amount) {
    ret [amount](i64 x) i64 { ret x + amount }
}

struct Button {
    string label
    func() onClick

    void click() {
        onClick()
    }
}

i32 main() {
    func(i64) i64 plus3 = adder(3)
    println("{}", plus3(4))

    list<func(i64) i64> steps = [adder(1), adder(10), adder(100)]
    i64 x = 0
    for (func(i64) i64 step in steps) {
        x = step(x)
    }
    println("{}", x)

    string file = "notes.txt"
    Button save
    save.label = "Save"
    save.onClick = [move file]() { println("saving {}", file) }
    save.click()
    ret 0
}
```

```output
7
111
saving notes.txt
```

Tenere una lambda che cattura per indirizzo non compila, e il messaggio dice come tenerla: prendere il valore con `move`.

```cromo error
i32 main() {
    list<i64> values = [1, 2, 3]
    func() show = [values]() {
        println("{:?}", values)
    }
    ret 0
}
```

```output
error: this lambda captures by address (with 'ref', or reading a value that is not simple): it can only be passed as an argument; capture with 'move' to keep it
```

Restituirne una è lo stesso errore: la variabile finirebbe con la funzione, e la lambda non farebbe più riferimento a niente.

```cromo error
func() makeCounter() {
    i64 n = 0
    ret [ref n]() { n++ }
}

i32 main() {
    ret 0
}
```

```output
error: this lambda captures by address
```

## I valori di tipo funzione

Una variabile, un campo, un elemento di una lista e un valore restituito possono essere di un tipo funzione. Un valore del genere:

- si chiama come una funzione: una variabile come `plus3(4)`, un campo come `button.onClick(1)`, e anche il valore di qualsiasi espressione, come `steps[1](5)` o `multiplier(4)(5)`;
- si sposta con `move`, non si copia mai: `func() again = hello` è l'errore `values of type func() cannot be copied: write 'move hello'`, e non ha `copy()`;
- non si può confrontare né stampare: `==` non funziona su di esso, e né `{}` né `{:?}` lo stampano;
- parte vuoto, come un campo mai assegnato: chiamare una funzione vuota è un panic, `the function is empty: no lambda was given to it`.

Una lambda può lanciare un'eccezione, come qualsiasi funzione: l'eccezione esce dalla chiamata del valore funzione, e poi dalla funzione che l'ha chiamato, fino a un `catch`.

```cromo
func(i64) i64 multiplier(i64 k) {
    ret [k](i64 x) i64 { ret x * k }
}

struct Button {
    string label
    func(i32) onClick
}

void each(list<i32> values, func(i32) f) {
    for (i32 v in values) {
        f(v)
    }
}

i32 main() {
    Button ok
    ok.onClick = [](i32 n) { println("clicked {} times", n) }
    ok.onClick(2)

    list<func(i64) i64> steps = [multiplier(2), multiplier(3)]
    println("{} {}", steps[1](5), multiplier(4)(5))

    i32 seen = 0
    try {
        each([1, 2, 30], [ref seen](i32 v) {
            if (v > 10) {
                throw Error(format("{} is too big", v))
            }
            seen++
        })
    } catch (Exception e) {
        println("{}, after {} values", e.message(), seen)
    }
    ret 0
}
```

```output
clicked 2 times
15 20
30 is too big, after 2 values
```

## const func

Un parametro `const func(...)` accetta solo una funzione che non modifica ciò che cattura, come una lambda del C++ senza `mutable`:

- una lambda che non cattura con `ref`;
- un valore funzione, come una variabile o un campo di un tipo funzione: le sue catture sono in sola lettura;
- un parametro che è anch'esso una `const func`.

```cromo
struct Button {
    func() onClick
}

void twice(const func() f) {
    f()
    f()
}

i32 main() {
    string word = "hello"
    twice([word]() { println("{}", word) })

    Button b
    b.onClick = []() { println("click") }
    twice(b.onClick)
    ret 0
}
```

```output
hello
hello
click
click
```

Una lambda che cattura con `ref` viene rifiutata, e così un parametro `func` che non è `const`, perché potrebbe essere una lambda del genere; una `const func` non può nemmeno chiamarlo, da una lambda propria:

```cromo error
void twice(const func() f) {
    f()
    f()
}

void pass(func() g) {
    twice(g)
    twice([g]() { g() })
}

i32 main() {
    i64 calls = 0
    twice([ref calls]() { calls++ })
    ret 0
}
```

```output
error: 'g' is a parameter func that is not const: only a 'const func' can be given to a 'const func'
error: 'g' is a parameter func that is not const: a const func cannot call it
error: a const func does not modify what it captures: it cannot capture 'calls' with 'ref'
```

Poiché una `const func` non modifica nulla, più thread possono chiamarla nello stesso momento, in sicurezza: è così che il server HTTP chiama lo stesso handler per molte richieste contemporaneamente, come mostra [Rete e HTTP](networking.md).

## Le lambda dei thread

Un thread o un task esegue una lambda: `thread.Thread([move lines]() { ... })`, `thread.Task(...)`. Quella lambda viene eseguita una volta sola, quindi ciò che prende con `move` è suo e, a differenza delle altre lambda, può modificarlo. Un thread o un task non può catturare un parametro `func` che non è `const`, nemmeno attraverso una lambda dentro la propria: quella funzione potrebbe modificare ciò che cattura mentre un altro thread lo legge, e il compilatore chiede di dichiarare il parametro `const func`. [Thread, task e canali](concurrency.md) spiega i thread e ciò che le loro lambda possono catturare.

```cromo
import std.thread

i32 main() {
    list<string> lines = ["first"]
    thread.Thread worker = thread.Thread([move lines]() {
        lines.add("second")
        println("{:?}", lines)
    })
    worker.start()
    worker.join()
    ret 0
}
```

```output
["first", "second"]
```

## Riepilogo

- Una lambda è `[captures](parameters) type { body }`; il suo tipo è `func(parameters) type`, dove i parametri mantengono i loro modi, come `func(ref i64)`.
- Una funzione con nome non è un valore: al suo posto va una lambda che la chiama.
- Le catture usano le parole dei parametri: un nome legge, `ref` modifica, `move` prende. Un valore semplice letto viene copiato quando la lambda viene creata; gli altri vengono letti dove si trovano.
- Una lambda usa solo ciò che cattura; un nome si cattura una volta, un campo si copia prima in una variabile, e ciò che si cattura senza `ref` è in sola lettura.
- Una lambda che cattura per indirizzo vive solo durante la chiamata che la riceve; una che possiede ciò che cattura è un valore, che si tiene in variabili, campi e liste, e si restituisce.
- I valori di tipo funzione si spostano, ma non si copiano, non si confrontano e non si stampano; chiamarne uno vuoto è un panic.
- `const func` è una funzione che non modifica ciò che cattura, e si può chiamare da più thread contemporaneamente.
- La lambda di un thread o di un task viene eseguita una volta, e può modificare ciò che prende con `move`; non può catturare un parametro `func` che non è `const`.
