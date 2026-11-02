# Possesso

Il possesso è il modo in cui Cromo gestisce la memoria e ogni altra risorsa senza garbage collector e senza puntatori. Le regole sono poche, il compilatore le controlla e, una volta imparate, rendono visibile il costo di ogni riga.

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

## I valori semplici si copiano

I numeri, `bool` e le struct fatte solo di questi sono tipi **semplici**: si copiano, come in C.

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

Una `string`, una `list`, una `map` e le struct che le contengono, o che hanno un distruttore, possiedono qualcosa sullo heap. Come in C++, assegnarne una a un'altra variabile la copia, ed entrambe le variabili restano utilizzabili:

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

Lo stesso vale dove un valore viene conservato: `names.add(word)` mette nella lista una copia di `word`. Una copia duplica quello che il valore possiede, quindi costa; quando la vecchia variabile non serve più, `move` dà il valore al nuovo proprietario senza copiare nulla, e la vecchia variabile non si può più usare:

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

La copia la fa il metodo `copy()`, che si può anche scrivere, `first.copy()`, per renderla visibile. Per una struct il compilatore genera `copy()` da sé, quando tutti i suoi campi si possono copiare e la struct non ha un distruttore proprio; una struct con un distruttore, come un file, scrive il suo `copy()`, oppure non si può copiare e si sposta soltanto.

Il compilatore segue ogni percorso della funzione: anche una variabile spostata in un solo ramo di un `if` non si può più usare dopo l'`if`.

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

Qui non si copia nulla: la lista costruita in `names` diventa `all`.

### Spostare un campo

Spostare un campo, o un elemento di una lista, lascia al suo posto il valore predefinito, quindi la struct resta utilizzabile:

```cromo
struct Message {
    string text
    i32 priority
}

i32 main() {
    Message m
    m.text = "urgent"
    string taken = move m.text
    println("[{}] [{}]", taken, m.text)
    ret 0
}
```

```output
[urgent] []
```

## Passare valori alle funzioni

Un parametro si passa in uno di tre modi, e **la chiamata ripete la parola della dichiarazione**, così basta leggere una chiamata per sapere che cosa succede ai suoi argomenti:

| Modo | Dichiarazione | Chiamata | La funzione |
|---|---|---|---|
| leggere | `list<i32> xs` | `f(numbers)` | legge il valore; il chiamante lo conserva |
| modificare | `ref list<i32> xs` | `f(ref numbers)` | modifica il valore del chiamante |
| prendere | `move list<i32> xs` | `f(move numbers)` | ne diventa proprietaria; il chiamante lo perde |

### Leggere

Un parametro senza parola è in sola lettura. La funzione non può modificarlo:

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

Un parametro in lettura non viene mai copiato: un valore grande si passa per indirizzo, uno piccolo, fino a 16 byte, per copia, perché così è più veloce. Sceglie il compilatore, e può farlo perché il valore non può cambiare durante la chiamata. In termini di C++, un parametro in lettura è un `const T&` che non devi mai scrivere.

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

## I riferimenti vivono solo durante una chiamata

`ref` esiste solo per i parametri. Non ci sono riferimenti nelle variabili, nei campi o nei valori restituiti: un riferimento non può sopravvivere all'oggetto a cui si riferisce, perché finisce con la chiamata. Questa sola regola sostituisce i lifetime e il borrow checker di altri linguaggi.

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

Quando il compilatore non può saperlo, come in `swap(ref v[i], ref v[j])`, il programma lo controlla durante l'esecuzione, e una chiamata con `i` uguale a `j` è un panic. La stessa regola permette al compilatore di dire a LLVM che due parametri `ref` non si sovrappongono mai, il che rende il codice più veloce, come `restrict` in C.

## const

Una variabile `const` non si può modificare, e `const` è profondo: anche le stringhe e le liste dentro un valore `const` sono `const`. Un metodo marcato `const` non modifica il suo oggetto, come mostra il capitolo 5; su un parametro in lettura si possono chiamare solo metodi `const`.

## Riepilogo

- Ogni valore ha un solo proprietario e finisce con lui: i distruttori rilasciano le risorse.
- Assegnare un valore lo copia, con `copy()` per i valori che possiedono risorse; `move` lo dà a un altro proprietario senza copiarlo.
- Un parametro viene letto, modificato con `ref` o preso con `move`, e la chiamata dice quale dei tre.
- I riferimenti esistono solo durante una chiamata: nessun lifetime da scrivere, nessun puntatore pendente.
- Due argomenti di una chiamata che li modifica non sono mai lo stesso oggetto.
