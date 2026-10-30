# Funzioni anonime

Una funzione anonima, o *lambda*, è una funzione scritta nel punto in cui serve, come argomento o come valore. Può usare le variabili che ha intorno, e il modo in cui le prende segue le regole del possesso viste nel capitolo 4.

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

- tra parentesi quadre, le variabili che **cattura**, qui `limit`;
- i suoi parametri, come quelli di una funzione;
- il tipo che restituisce, dopo i parametri; se manca, la lambda non restituisce nulla;
- il suo corpo.

`func(i64) bool` è il tipo di una funzione che riceve un `i64` e restituisce un `bool`. Un parametro di tipo funzione si chiama proprio come una funzione: `keep(v)`.

## Catture

Una lambda dichiara come usa ogni variabile che cattura, con le stesse parole che si usano per i parametri:

| Cattura | La lambda |
|---|---|
| `[limit]` | legge la variabile |
| `[ref total]` | modifica la variabile |
| `[move name]` | prende il valore, che la variabile perde |

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

Senza `ref`, una lambda non può modificare ciò che cattura, e il compilatore te lo segnala.

## Le lambda come valori

Una lambda che cattura per indirizzo, con `ref` oppure leggendo un valore di tipo non semplice, può essere passata solo come argomento: le variabili a cui fa riferimento vivono nella funzione chiamante, e la lambda finisce insieme alla chiamata. Una lambda che cattura solo valori di tipo semplice, o che prende i suoi valori con `move`, è invece un valore: si può tenere in una variabile, in un campo o in una lista, restituire e spostare.

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

Restituire una lambda che cattura con `ref` non compila: la variabile smetterebbe di esistere al termine della funzione, e la lambda non farebbe più riferimento a niente.

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

Un valore di tipo funzione viene spostato, non copiato. Se è vuoto, come un campo mai assegnato, chiamarlo provoca un panic.

## const func

Un parametro `const func(...)` accetta solo una funzione che non modifica ciò che cattura. Più thread possono chiamare una funzione del genere nello stesso momento, in sicurezza: è così che il server HTTP del capitolo 15 chiama lo stesso handler per molte richieste contemporaneamente.

## Riepilogo

- Una lambda è `[captures](parameters) type { body }`; il suo tipo è `func(parameters) type`.
- Le catture usano le stesse parole dei parametri: lettura, `ref`, `move`.
- Una lambda che fa riferimento a variabili del chiamante vive solo per la durata della chiamata; una che possiede ciò che cattura è un valore.
- `const func` è una funzione che si può chiamare da più thread contemporaneamente.
