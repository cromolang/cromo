# Possesso

Il possesso è il modo in cui Cromo gestisce la memoria e ogni altra risorsa senza garbage collector e senza puntatori. Le regole sono poche, il compilatore le controlla e, una volta imparate, rendono visibile il costo di ogni riga.

Gli esempi di questo capitolo usano struct con un costruttore e un distruttore, per mostrare quando i valori nascono e quando finiscono. [Struct](structs.md), il capitolo successivo, li spiega per intero; qui basta sapere che il distruttore, `~Name()`, viene eseguito quando il valore finisce.

## Ogni valore ha un solo proprietario

Un valore appartiene alla variabile, al campo o all'elemento di lista che lo contiene: il suo **proprietario**. Quando il proprietario finisce, il valore finisce con lui. Per una variabile locale questo succede alla fine del blocco in cui è dichiarata, nell'ordine inverso delle dichiarazioni.

Una struct può stabilire che cosa fare quando finisce, con un **distruttore**: un metodo che si chiama `~` seguito dal nome della struct. Il programma qui sotto ne usa uno per mostrare quando finisce ogni valore:

```cromo
struct Log {
    string name

    Log(move string label) {
        name = move label
        println("open {}", name)
    }

    ~Log() {
        println("close {}", name)
    }
}

void inner() {
    Log b = Log("b")
    println("inner")
}

i32 main() {
    Log a = Log("a")
    inner()
    Log d = Log("d")
    println("end of main")
    ret 0
}
```

```output
open a
open b
inner
close b
open d
end of main
close d
close a
```

`b` finisce quando `inner` termina; `d` e `a` alla fine di `main`, a partire dall'ultima dichiarata. È il RAII, come in C++: una struct che possiede un file lo chiude nel suo distruttore, una che possiede una connessione chiude la connessione, e il codice che le usa non può dimenticarsene, nemmeno quando lo attraversa un'eccezione. Una `string` e una `list` liberano la loro memoria allo stesso modo.

## Quando finisce un valore

Le regole sono quelle del C++:

- Una variabile locale finisce alla fine del blocco in cui è dichiarata. Le variabili dello stesso blocco finiscono nell'ordine inverso delle loro dichiarazioni.
- Un **temporaneo**, un valore senza nome come il risultato di una chiamata, finisce alla fine dell'istruzione che lo contiene.
- `ret`, `break` e `continue` fanno finire tutto ciò che è vivo nei blocchi da cui escono, e lo stesso fa un'eccezione che li attraversa.
- I campi di una struct finiscono con lei, dopo il suo distruttore, a partire dall'ultimo dichiarato.
- Un elemento di una lista finisce quando viene rimosso, quando gli si assegna un nuovo valore o quando finisce la lista.
- Assegnare un nuovo valore a una variabile fa finire il valore che conteneva.
- Un valore che è stato spostato finisce con il suo nuovo proprietario, e solo lì.

Un temporaneo vive fino alla fine della sua istruzione, abbastanza a lungo da poter essere usato al suo interno. Ogni iterazione di un ciclo è un blocco a sé, da cui escono `continue` e `break`. `Log` è la struct del primo esempio, con un metodo `label()` che ne restituisce il nome:

```cromo
# struct Log {
#     string name
#
#     Log(move string label) {
#         name = move label
#         println("open {}", name)
#     }
#
#     ~Log() {
#         println("close {}", name)
#     }
#
#     string label() const {
#         ret name
#     }
# }
#
i32 main() {
    println("got {}", Log("temp").label())
    println("next statement")
    for (i32 i = 0; i < 3; i++) {
        Log a = Log(format("loop {}", i))
        if (i == 1) {
            continue
        }
        if (i == 2) {
            break
        }
        println("end of iteration {}", i)
    }
    println("after the loop")
    ret 0
}
```

```output
open temp
got temp
close temp
next statement
open loop 0
end of iteration 0
close loop 0
open loop 1
close loop 1
open loop 2
close loop 2
after the loop
```

Un `ret` dal profondo di una funzione fa finire i valori di ogni blocco da cui esce, a partire dal più interno, e un'eccezione fa lo stesso mentre risale verso il `catch`:

```cromo
# struct Log {
#     string name
#
#     Log(move string label) {
#         name = move label
#         println("open {}", name)
#     }
#
#     ~Log() {
#         println("close {}", name)
#     }
# }
#
i64 find(list<i32> values, i32 wanted) {
    Log outer = Log("outer")
    for (i64 i = 0; i < values.len(); i++) {
        Log inner = Log("inner")
        if (values[i] == wanted) {
            ret i
        }
    }
    ret -1
}

void fail() {
    Log a = Log("fail")
    throw Error("broken")
}

i32 main() {
    println("found at {}", find([4, 7], 4))
    try {
        fail()
    } catch (Error e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
open outer
open inner
close inner
close outer
found at 0
open fail
close fail
caught: broken
```

Gli elementi di una lista sono valori come gli altri. Rimuoverne uno lo fa finire; assegnare un nuovo valore a un elemento, o a una variabile, fa finire il vecchio valore; e quando la lista finisce, i suoi elementi finiscono nel loro ordine, a partire dal primo:

```cromo
struct Log {
    string name

    Log(move string label) {
        name = move label
    }

    ~Log() {
        println("close {}", name)
    }
}

i32 main() {
    list<Log> logs
    logs.add(Log("one"))
    logs.add(Log("two"))
    logs.add(Log("three"))
    logs.remove(1)
    println("removed")
    Log x = Log("x")
    x = Log("y")
    println("replaced")
    logs[0] = Log("uno")
    println("end")
    ret 0
}
```

```output
close two
removed
close x
replaced
close one
end
close y
close uno
close three
```

Alla fine di `main`, `x` finisce prima di `logs`, perché è stata dichiarata dopo.

Un distruttore non si chiama mai a mano: lo chiama il compilatore, esattamente una volta per ogni valore.

## I valori semplici si copiano

I numeri, `bool` e le struct fatte solo di questi, senza un distruttore proprio, sono tipi **semplici**: assegnarli, passarli e restituirli ne copia i byte, come in C.

```cromo
struct Point {
    f64 x
    f64 y
}

i32 main() {
    Point p
    p.x = 1.5
    Point q = p
    q.x = 9.0
    println("{} {}", p.x, q.x)
    ret 0
}
```

```output
1.5 9
```

## I valori che possiedono risorse: copiati o spostati

Una `string`, una `list`, una `map` e le struct che le contengono, o che hanno un distruttore, possiedono qualcosa: memoria sullo heap, un file, una connessione. Come in C++, assegnarne una a un'altra variabile la copia, ed entrambe le variabili restano utilizzabili:

```cromo
i32 main() {
    string first = "hello"
    string second = first
    second += "!"
    println("{} {}", first, second)
    ret 0
}
```

```output
hello hello!
```

La copia la fa il metodo `copy()`, che si può anche scrivere, `first.copy()`, per renderla visibile. Una copia duplica quello che il valore possiede, quindi costa; quando la vecchia variabile non serve più, `move` dà il valore al nuovo proprietario senza copiare nulla, e la vecchia variabile non si può più usare:

```cromo error
i32 main() {
    string first = "hello"
    string second = move first
    println("{}", first)
    ret 0
}
```

```output
error: 'first' has been moved and can no longer be used
```

### Dove un valore viene copiato

Un valore che possiede risorse viene copiato ovunque una variabile o un campo lo dia senza `move`. Nel programma qui sotto, `Tracked` è una struct che stampa una riga ogni volta che viene copiata (come una struct scrive il proprio `copy()` lo mostra il capitolo [Struct](structs.md)):

```cromo
# struct Tracked {
#     string name
#
#     ~Tracked() {
#     }
#
#     Tracked copy() const {
#         println("  copy of {}", name)
#         Tracked other
#         other.name = name
#         ret other
#     }
# }
#
struct Holder {
    Tracked item

    Tracked get() const {
        ret item
    }
}

Tracked make(string name) {
    Tracked t
    t.name = name
    ret t
}

void keep(move Tracked t) {
    println("  keeping {}", t.name)
}

i32 main() {
    println("a variable:")
    Tracked a = make("a")
    println("assigned:")
    Tracked b = a
    println("moved:")
    Tracked c = move a
    println("to a move parameter:")
    keep(b)
    keep(move b)
    println("a temporary:")
    keep(make("t"))
    println("a field returned:")
    Holder h
    h.item = move c
    Tracked d = h.get()
    println("in a list:")
    list<Tracked> all
    all.add(d)
    all.add(move d)
    ret 0
}
```

```output
a variable:
assigned:
  copy of a
moved:
to a move parameter:
  copy of a
  keeping a
  keeping a
a temporary:
  keeping t
a field returned:
  copy of a
in a list:
  copy of a
```

Un valore **viene copiato**:

- in un'inizializzazione o in un assegnamento da una variabile o da un campo: `Tracked b = a`, `b = a`;
- quando una variabile viene data a un parametro `move` senza `move`, come in `keep(b)` o `all.add(d)`: la funzione riceve una copia tutta sua, e la variabile conserva il suo valore;
- dal `ret` di un campo o di un parametro in lettura, che appartengono a qualcun altro.

Un valore **non viene copiato**:

- quando viene dato con `move`;
- quando è un temporaneo, come il risultato di `make("t")`;
- dal `ret` di una variabile locale, come in `make`;
- quando viene passato a un parametro in lettura o `ref`: la funzione lavora sull'originale.

### I valori che non si possono copiare

Una struct con un distruttore proprio non si copia, a meno che non scriva un metodo `copy()`: il compilatore non può sapere come duplicare ciò che il distruttore rilascia, come un file. Un valore del genere si può solo spostare:

```cromo error
struct Handle {
    i32 fd = -1

    ~Handle() {
        println("closing {}", fd)
    }
}

i32 main() {
    Handle a
    Handle b = a
    ret 0
}
```

```output
error: values of type Handle cannot be copied
write 'move a'
```

Non si può copiare nemmeno una struct che contiene un campo che non si può copiare.

### Temporanei e valori restituiti

Un valore senza nome, come il risultato di una chiamata o un letterale, si sposta senza bisogno di scrivere `move`. Lo stesso vale per una variabile locale restituita con `ret`:

```cromo
list<string> names() {
    list<string> result = ["Ada", "Grace"]
    ret result
}

i32 main() {
    list<string> all = names()
    all.add("Alan")
    println("{}", all.len())
    ret 0
}
```

```output
3
```

Qui non si copia nulla: la lista costruita in `names` diventa `all`. Scrivere `move` davanti a un nuovo valore è un errore, perché non direbbe nulla:

```cromo error
string greeting() {
    ret "hello"
}

i32 main() {
    string s = move greeting()
    ret 0
}
```

```output
error: 'move' is not needed before a new object: it is moved anyway
```

### Spostare un valore semplice

`move` su un valore semplice lo copia, e la variabile resta utilizzabile: nessuno rinuncia a nulla. Per lo stesso motivo, `move` si può omettere quando un valore semplice viene dato a un parametro `move`:

```cromo
void show(move i32 n) {
    println("{}", n)
}

i32 main() {
    i32 n = 5
    show(move n)
    show(n)
    i32 m = move n
    println("{} {}", n, m)
    ret 0
}
```

```output
5
5
5 5
```

Così una funzione con un parametro `move` funziona allo stesso modo per un `i32` e per una `string`, il che conta per i generici ([Interfacce e generici](interfaces-generics.md)).

## Dopo uno spostamento

Una variabile che è stata spostata non si può più usare. Il compilatore segue ogni percorso della funzione: anche una variabile spostata in un solo ramo di un `if` non si può più usare dopo l'`if`.

```cromo error
i32 main(list<string> args) {
    string name = "Ada"
    list<string> names
    if (args.len() > 1) {
        names.add(move name)
    }
    println("{}", name)
    ret 0
}
```

```output
error: 'name' may have been moved, on another path or in a previous iteration of a loop
```

Assegnarle un nuovo valore rende la variabile di nuovo utilizzabile:

```cromo
i32 main() {
    list<string> lines
    string current = "first"
    lines.add(move current)
    current = "second"
    lines.add(move current)
    println("{:?}", lines)
    ret 0
}
```

```output
["first", "second"]
```

In un ciclo, una variabile dichiarata fuori dal ciclo e spostata al suo interno deve ricevere un nuovo valore prima dell'iterazione successiva, perché l'iterazione successiva la userebbe di nuovo:

```cromo error
i32 main() {
    list<string> lines
    string current = "line"
    for (i32 i = 0; i < 3; i++) {
        lines.add(move current)
    }
    ret 0
}
```

```output
error: 'current' may have been moved, on another path or in a previous iteration of a loop
```

Scrivere `current = ""` dopo l'`add`, alla fine del corpo, rende corretto il ciclo.

Un valore spostato su un solo percorso viene distrutto da chi lo possiede alla fine: la funzione che lo ha preso, oppure la variabile, se non è stato spostato. Non viene mai distrutto due volte, e mai dimenticato:

```cromo
# struct Log {
#     string name
#
#     Log(move string label) {
#         name = move label
#     }
#
#     ~Log() {
#         println("close {}", name)
#     }
# }
#
void consume(move Log log) {
    println("consume {}", log.name)
}

void maybe(bool give) {
    Log log = Log(format("give={}", give))
    if (give) {
        consume(move log)
    }
    println("end of maybe")
}

i32 main() {
    maybe(true)
    maybe(false)
    ret 0
}
```

```output
consume give=true
close give=true
end of maybe
end of maybe
close give=false
```

### Spostare un campo, un elemento o un parametro `ref`

Una variabile locale intera non si può usare dopo uno spostamento. Spostare un campo, un elemento di una lista o un parametro `ref`, invece, prende il valore e lascia al suo posto il **valore predefinito del suo tipo**, così la struct, la lista e la variabile del chiamante restano utilizzabili:

```cromo
struct Settings {
    string host = "localhost"
    list<string> paths = ["/", "/health"]
}

void take(ref string s) {
    string old = move s
    println("took [{}]", old)
}

i32 main() {
    Settings s
    list<string> paths = move s.paths
    string host = move s.host
    println("{:?} [{}] {:?} [{}]", paths, host, s.paths, s.host)
    Settings t
    take(ref t.host)
    println("[{}]", t.host)
    list<string> words = ["a", "b"]
    string w = move words[0]
    println("{:?} {}", words, w)
    ret 0
}
```

```output
["/", "/health"] [localhost] [] []
took [localhost]
[]
["", "b"] a
```

Il campo che resta ha il valore predefinito del suo tipo, una stringa vuota o una lista vuota, non il valore iniziale che la struct dichiara per lui: uno spostamento non calcola nulla e non alloca nulla. Una struct deve quindi accettare il valore predefinito dei suoi campi, e il suo distruttore deve saperlo gestire; [Struct](structs.md) mostra come.

### Dove si può scrivere `move`

`move` si scrive davanti a una variabile, a un campo o a un elemento, e solo dove un valore cambia proprietario:

- in un'inizializzazione, `string t = move s`;
- in un assegnamento, `t = move s`;
- come argomento di un parametro `move`, `names.add(move s)`;
- dopo `ret`, `ret move s.text`.

In qualsiasi altro punto è un errore, perché nessuno prenderebbe il valore:

```cromo error
i32 main() {
    string s = "hello"
    println("{}", move s)
    ret 0
}
```

```output
error: 'move' is written only in an initialization, an assignment, the argument of a 'move' parameter or after 'ret'
```

Ciò che appartiene a qualcun altro non si può spostare: un parametro in lettura, un campo di un parametro in lettura, l'elemento di un `for` che legge una lista. Un parametro in lettura si copia, con `ret` o con un assegnamento, ma non si sposta mai:

```cromo error
void keep(string s) {
    list<string> all
    all.add(move s)
}

i32 main() {
    keep("a")
    ret 0
}
```

```output
error: parameter 's' is read-only: it belongs to the caller and cannot be moved
```

## Passare valori alle funzioni

Un parametro si passa in uno di tre modi, e **la chiamata ripete la parola della dichiarazione**, così basta leggere una chiamata per sapere che cosa succede ai suoi argomenti:

| Modo | Dichiarazione | Chiamata | La funzione |
|---|---|---|---|
| leggere | `list<i32> xs` | `f(numbers)` | legge il valore; il chiamante lo conserva |
| modificare | `ref list<i32> xs` | `f(ref numbers)` | modifica il valore del chiamante |
| prendere | `move list<i32> xs` | `f(move numbers)` | ne diventa proprietaria; il chiamante lo perde |

### Leggere

Un parametro senza parola è in sola lettura. La funzione non può modificarlo, e su di esso può chiamare solo i metodi `const`:

```cromo error
void addOne(list<i32> numbers) {
    numbers.add(1)
}

i32 main() {
    list<i32> values
    addOne(values)
    ret 0
}
```

```output
error: cannot call 'add', which is not const
parameter 'numbers' cannot be modified: parameters are read-only
```

Un parametro in lettura non viene copiato: un valore grande si passa per indirizzo, uno piccolo, fino a 16 byte, per copia, perché così è più veloce. Sceglie il compilatore, e può farlo perché il valore non può cambiare durante la chiamata. In termini di C++, un parametro in lettura è un `const T&` che non devi mai scrivere; per questo `const` non si scrive su un parametro in lettura, e `ref const` non esiste.

Un parametro in lettura appartiene al chiamante: per conservarne il valore, la funzione lo copia, con un assegnamento o con `ret`.

### Modificare: `ref`

Con `ref` la funzione lavora sul valore del chiamante. Anche la chiamata scrive `ref`:

```cromo
void addOne(ref list<i32> numbers) {
    numbers.add(1)
}

i32 main() {
    list<i32> values
    addOne(ref values)
    addOne(ref values)
    println("{}", values.len())
    ret 0
}
```

```output
2
```

Dimenticare il `ref` nella chiamata è un errore, non una copia silenziosa:

```cromo error
void addOne(ref list<i32> numbers) {
    numbers.add(1)
}

i32 main() {
    list<i32> values
    addOne(values)
    ret 0
}
```

```output
error: parameter 'numbers' is 'ref': write 'ref' before the argument
```

`ref` è anche il modo in cui una funzione restituisce più di un risultato:

```cromo
bool find(list<string> items, string wanted, ref i64 position) {
    for (i64 i = 0; i < items.len(); i++) {
        if (items[i] == wanted) {
            position = i
            ret true
        }
    }
    ret false
}

i32 main() {
    list<string> planets = ["Mercury", "Venus", "Earth"]
    i64 at
    if (find(planets, "Earth", ref at)) {
        println("found at {}", at)
    }
    ret 0
}
```

```output
found at 2
```

L'argomento di un parametro `ref` deve essere qualcosa che la funzione può modificare sul posto:

- una variabile, un campo o un elemento di una lista, come `ref at`, `ref p.x`, `ref items[0]`; un temporaneo o un'espressione non si possono passare con `ref`;
- modificabile: non una variabile `const` e non un parametro in lettura;
- esattamente del tipo del parametro. Un `i32` non si passa a un `ref i64`, perché la funzione scriverebbe otto byte in quattro:

```cromo error
void next(ref i64 n) {
    n++
}

i32 main() {
    i32 small = 1
    next(ref small)
    ret 0
}
```

```output
error: parameter 'n' is 'ref i64': the argument must have exactly that type, found i32
```

### Prendere: `move`

Un parametro `move` riceve il possesso del valore. La funzione può tenerlo, per esempio in una lista, senza copiarlo:

```cromo
struct Library {
    list<string> titles

    void add(move string title) {
        titles.add(move title)
    }
}

i32 main() {
    Library library
    string book = "The C Programming Language"
    library.add(move book)
    library.add("Structure and Interpretation of Computer Programs")
    println("{} books", library.titles.len())
    ret 0
}
```

```output
2 books
```

La seconda chiamata passa un temporaneo, che non ha bisogno di `move`. Una variabile passata senza `move` viene copiata, come si è visto sopra, e la funzione riceve la copia: `library.add(book)` lascerebbe `book` utilizzabile.

Dentro la funzione, un parametro `move` è una variabile locale come le altre: si può modificare e spostare, e finisce alla fine della funzione, a meno che non sia stato spostato.

### `move const`

Un parametro `move` può anche essere `const`, con il modo per primo: la funzione diventa proprietaria del valore ma non lo modifica. Può comunque spostarlo, come qualsiasi variabile `const`:

```cromo
struct Registry {
    list<string> names

    void keep(move const string name) {
        println("keeping {} ({} letters)", name, name.len())
        names.add(move name)
    }
}

i32 main() {
    Registry r
    string a = "Ada"
    r.keep(move a)
    r.keep("Grace")
    println("{:?}", r.names)
    ret 0
}
```

```output
keeping Ada (3 letters)
keeping Grace (5 letters)
["Ada", "Grace"]
```

Sugli altri parametri `const` non ha senso: un parametro in lettura è già in sola lettura, e `ref` esiste per modificare.

```cromo error
void show(const string name) {
    println("{}", name)
}

i32 main() {
    show("x")
    ret 0
}
```

```output
error: only a 'move' parameter can be const, written 'move const'
```

## I riferimenti vivono solo durante una chiamata

`ref` esiste solo per i parametri. Non ci sono riferimenti nelle variabili, nei campi o nei valori restituiti: un riferimento non può sopravvivere all'oggetto a cui si riferisce, perché finisce con la chiamata. Questa sola regola sostituisce i lifetime e il borrow checker di altri linguaggi.

```cromo error
struct Cursor {
    ref string text
}

i32 main() {
    ret 0
}
```

```output
error: expected a type, found 'ref'
```

Quando un programma deve puntare a un altro oggetto, come in un grafo, tiene un indice in una lista al posto di un puntatore.

## Esclusività

Durante una chiamata, un oggetto passato con `ref` non può essere raggiunto attraverso nessun altro argomento della stessa chiamata. Altrimenti una funzione potrebbe vedersi cambiare un valore sotto i piedi:

```cromo error
void swap(ref i32 a, ref i32 b) {
    i32 t = a
    a = b
    b = t
}

i32 main() {
    i32 x = 1
    swap(ref x, ref x)
    ret 0
}
```

```output
error: 'x' overlaps 'x' in the same call, and the call can modify both
```

Due argomenti si sovrappongono quando sono lo stesso oggetto, o quando uno è una parte dell'altro: `ref p` e `ref p.x`, una lista e uno dei suoi elementi. Quando il compilatore non può saperlo, come in `swap(ref v[i], ref v[j])`, il programma lo controlla durante l'esecuzione, e una chiamata con `i` uguale a `j` si ferma con un panic: lo stesso elemento viene passato due volte a una chiamata che lo modifica.

L'oggetto di un metodo che non è `const` conta come un argomento passato con `ref`, perché il metodo può modificarlo. Un argomento in lettura che vi si sovrappone è accettato quando è un valore semplice: viene copiato, e il metodo vede il valore che aveva al momento della chiamata.

```cromo
struct Counter {
    i64 value
    i64 step = 2

    void add(i64 amount) {
        value += amount
        step = 100
    }
}

i32 main() {
    Counter c
    c.add(c.step)
    println("{} {}", c.value, c.step)
    ret 0
}
```

```output
2 100
```

Un valore che possiede risorse non viene copiato per far funzionare la chiamata: la sovrapposizione è un errore.

```cromo error
struct Box {
    string label = "box"

    void rename(string name) {
        label = name + "!"
    }
}

i32 main() {
    Box b
    b.rename(b.label)
    ret 0
}
```

```output
error: 'b.label' overlaps 'b' in the same call, which the call can modify
```

Passare una copia, `b.rename(b.label.copy())`, dice che la copia è voluta, e compila.

La stessa regola vale per un `for` che percorre una lista: finché il `for` è in corso, la lista non si può modificare, nemmeno attraverso un altro nome:

```cromo error
i32 main() {
    list<i64> numbers = [1, 2, 3]
    for (i64 n in numbers) {
        numbers.add(n)
    }
    ret 0
}
```

```output
error: 'numbers' cannot be modified while a 'for' goes over the elements of 'numbers'
```

L'esclusività permette anche al compilatore di dire a LLVM che due parametri `ref` non si sovrappongono mai, il che rende il codice più veloce, come `restrict` in C.

## const

Una variabile `const` non si può modificare, e `const` è profondo: anche le stringhe e le liste dentro un valore `const` sono `const`, e se ne possono chiamare solo i metodi `const`. Una variabile `const` non si può passare con `ref`, ma si può copiare e si può spostare: spostarla non cambia il valore, lo dà soltanto a un altro proprietario.

```cromo
i32 main() {
    const list<string> fixed = ["a", "b"]
    list<string> more = fixed
    more.add("c")
    list<string> taken = move fixed
    println("{:?} {:?}", more, taken)
    ret 0
}
```

```output
["a", "b", "c"] ["a", "b"]
```

```cromo error
i32 main() {
    const list<string> fixed = ["a", "b"]
    fixed.add("c")
    ret 0
}
```

```output
error: cannot call 'add', which is not const
'fixed' is const and cannot be modified
```

Un metodo marcato `const` non modifica il suo oggetto, come mostra [Struct](structs.md): solo i metodi `const` si possono chiamare su un parametro in lettura, su una variabile `const` e sull'elemento di un `for` che legge una lista.

## Riepilogo

- Ogni valore ha un solo proprietario, e finisce con lui: alla fine del suo blocco, alla fine dell'istruzione per un temporaneo, quando `ret`, `break`, `continue` o un'eccezione escono dal suo blocco. I distruttori rilasciano le risorse, esattamente una volta.
- I valori semplici si copiano. I valori che possiedono risorse vengono copiati con `copy()` dove vengono dati senza `move`; `move` li dà a un altro proprietario senza copiarli, e i temporanei e il `ret` di una variabile locale si spostano da soli.
- Una struct con un distruttore e senza `copy()` non si può copiare, solo spostare.
- Una variabile spostata non si può usare, su nessun percorso, finché non riceve un nuovo valore. Un campo, un elemento o un parametro `ref` spostato resta con il valore predefinito del suo tipo.
- `move` si scrive solo in un'inizializzazione, in un assegnamento, come argomento di un parametro `move` e dopo `ret`; un parametro in lettura non si può spostare.
- Un parametro viene letto, modificato con `ref` o preso con `move`, e la chiamata dice quale dei tre; `move const` prende un valore senza modificarlo.
- `ref` si scrive su una variabile, un campo o un elemento modificabile, esattamente del tipo del parametro.
- I riferimenti esistono solo durante una chiamata: nessun lifetime da scrivere, nessun puntatore pendente.
- Due argomenti di una chiamata che li modifica non sono mai lo stesso oggetto, né uno parte dell'altro.
