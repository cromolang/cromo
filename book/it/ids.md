# Id e handle: puntare a un altro oggetto

Una cartella conosce la cartella che la contiene, un ordine il suo cliente, un nodo di un grafo i suoi vicini. In C++ ognuno di loro tiene un puntatore o un riferimento all'altro oggetto. Cromo non ha puntatori, e un `ref` vive solo durante una chiamata o un blocco ([Possesso](ownership.md)): un oggetto non può tenere un riferimento a un altro.

Questo capitolo spiega perché, e che cosa scrive al suo posto un programma Cromo: un **id**, l'indice dell'altro oggetto in una lista. È la prima cosa che un programmatore C++ chiede di Cromo, e la risposta è breve. Gli oggetti vivono in una lista, e chi punta a uno di loro tiene il suo numero.

Gli esempi usano ciò che i capitoli precedenti hanno spiegato: le struct con i loro metodi ([Struct](structs.md)) e le liste ([Liste, mappe e testo](collections.md)). La sezione sui thread usa `std.thread` e le funzioni anonime, che vengono dopo, in [Funzioni anonime](lambdas.md) e [Thread, task e canali](concurrency.md): si può rileggere dopo quei capitoli.

## Perché un riferimento non si può tenere

Un riferimento è un indirizzo di memoria. Un indirizzo dice dove si trova un oggetto adesso, e niente tiene l'oggetto lì. Questo è C++, e compila senza un avviso:

```cpp
std::vector<Token> tokens = read();
Token* first = &tokens[0];   // the address of the first token, inside the vector
tokens.push_back(more);      // the vector grows, and moves its elements elsewhere
print(first->text);          // first is the address of memory that was freed
```

L'ultima riga legge memoria che non appartiene più al vettore. Il programma può stampare il testo giusto, stamparne un altro o fermarsi, e può farlo solo certi giorni. Lo stesso succede quando l'oggetto viene distrutto alla fine del suo blocco, o spostato in un'altra variabile, mentre qualcuno ne tiene ancora l'indirizzo.

Un linguaggio che lascia tenere i riferimenti ha tre modi per impedirlo:

- **I lifetime**, come Rust. Il compilatore dimostra che ogni riferimento finisce prima del suo oggetto. Funziona, e chiede annotazioni come `'a` nei tipi e nelle firme, e un borrow checker che segue ogni riferimento in tutto il programma.
- **Un garbage collector**, come Go e Java. Un oggetto resta vivo finché qualcuno punta a lui. Il programma paga con un runtime che si ferma e scorre la memoria, e non decide più quando un oggetto finisce.
- **Puntatori non controllati**, come C e C++. Ci si fida del programmatore, e gli errori si trovano quando il programma gira, oppure mai.

Cromo non prende nessuna delle tre strade: non ha lifetime da scrivere, non ha un garbage collector, e non usa memoria già liberata. Ciò a cui rinuncia è il riferimento tenuto. Un `ref` è il parametro di una chiamata, la cattura di una lambda che non vive più del suo blocco, l'elemento di un `for` o il dato di un `case`: finisce con la chiamata o con il blocco, e l'oggetto a cui si riferisce è ancora lì, perché nel frattempo il compilatore non lascia che quell'oggetto venga spostato, distrutto o modificato da altri. Un riferimento in un campo, in una variabile o in un valore restituito potrebbe vivere più del suo oggetto, quindi non si può scrivere:

```cromo error
struct Folder {
    string name
    ref Folder parent
}

i32 main() {
    ret 0
}
```

```output
error: expected a type, found 'ref'
```

## Un id è un indice in una lista

Tutti gli oggetti di un tipo vanno in una lista, e la lista ha un proprietario. Un oggetto che punta a un altro tiene il suo **id**: l'indice di quell'oggetto nella lista, un semplice `i64`. Questo è un albero di cartelle, in cui ogni cartella conosce quella che la contiene:

```cromo
struct Folder {
    string name
    i64 parent  // the id of the parent: an index into folders, -1 for none
}

struct Tree {
    list<Folder> folders

    // Adds a folder and gives its id.
    i64 add(string name, i64 parent) {
        Folder folder
        folder.name = name
        folder.parent = parent
        folders.add(folder)
        ret folders.len() - 1
    }

    // The path of a folder: from it up to the root, an id at a time.
    string path(i64 id) const {
        string text = ""
        i64 at = id
        for (at >= 0) {
            text = "/" + folders[at].name + text
            at = folders[at].parent
        }
        ret text
    }
}

i32 main() {
    Tree tree
    i64 home = tree.add("home", -1)
    i64 anna = tree.add("anna", home)
    i64 photos = tree.add("photos", anna)
    println("{}", tree.path(photos))

    // The list grows and moves in memory: the ids are still good.
    for (i64 i = 0; i < 1000; i++) {
        tree.add("more", home)
    }
    println("{}", tree.path(photos))

    // Who has the id finds the folder as it is now.
    tree.folders[anna].name = "andrea"
    println("{}", tree.path(photos))
    ret 0
}
```

```output
/home/anna/photos
/home/anna/photos
/home/andrea/photos
```

`i64 parent` sta dove C++ scriverebbe `Folder* parent`, e `-1` dove scriverebbe `nullptr`. La differenza è in ciò che il numero significa. Un indirizzo è un posto in memoria; un id è una posizione in una lista, e diventa un posto solo quando viene usato, da `folders[at]`:

- **La lista può spostarsi.** Dopo altre mille cartelle la lista ha spostato i suoi elementi più volte. Un puntatore a `photos` preso prima sarebbe l'indirizzo di memoria liberata; l'id `photos` trova ancora la stessa cartella.
- **Un id sbagliato viene trovato.** `folders[at]` controlla l'indice: un id fuori dalla lista ferma il programma con un panic che dice l'indice e la dimensione, invece di leggere la memoria di qualcos'altro.
- **Un id è un valore semplice.** Si copia liberamente, si tiene in un campo, in un'altra lista o in una mappa, si manda a un altro thread e si scrive in un file. Nessuna regola del possesso lo riguarda, perché non possiede niente.

Per seguire un id il programma ha bisogno della lista: `path` è un metodo di `Tree`, non di `Folder`, perché una cartella da sola non può raggiungere quella che la contiene. È lo schema di tutto il capitolo: l'oggetto che possiede la lista ha le funzioni che seguono gli id.

## Oggetti di tipi diversi

Con più tipi di oggetti, ogni tipo ha la sua lista, e un id è un indice nella lista del suo tipo. Qui un ordine punta a un cliente e a un prodotto:

```cromo
struct Customer {
    string name
    string city
}

struct Product {
    string name
    i64 price
}

struct Order {
    i64 customer  // an index into customers
    i64 product   // an index into products
    i64 quantity
}

struct Shop {
    list<Customer> customers
    list<Product> products
    list<Order> orders

    i64 addCustomer(string name, string city) {
        Customer customer
        customer.name = name
        customer.city = city
        customers.add(customer)
        ret customers.len() - 1
    }

    i64 addProduct(string name, i64 price) {
        Product product
        product.name = name
        product.price = price
        products.add(product)
        ret products.len() - 1
    }

    void order(i64 customer, i64 product, i64 quantity) {
        Order made
        made.customer = customer
        made.product = product
        made.quantity = quantity
        orders.add(made)
    }

    // What a customer has spent: its orders, each with the price of its product.
    i64 spent(i64 customer) const {
        i64 total = 0
        for (Order o in orders) {
            if (o.customer == customer) {
                total += o.quantity * products[o.product].price
            }
        }
        ret total
    }

    void print() const {
        for (Order o in orders) {
            println("{} of {}: {} x {}", customers[o.customer].name, customers[o.customer].city, o.quantity, products[o.product].name)
        }
    }
}

i32 main() {
    Shop shop
    i64 anna = shop.addCustomer("Anna", "Torino")
    i64 luca = shop.addCustomer("Luca", "Bari")
    i64 pen = shop.addProduct("pen", 2)
    i64 book = shop.addProduct("book", 15)

    shop.order(anna, book, 2)
    shop.order(luca, pen, 10)
    shop.order(anna, pen, 3)
    shop.print()
    println("Anna has spent {}", shop.spent(anna))

    // One customer and one product, and several orders that point to them.
    shop.customers[anna].city = "Milano"
    shop.products[pen].price = 3
    shop.print()
    println("Anna has spent {}", shop.spent(anna))
    ret 0
}
```

```output
Anna of Torino: 2 x book
Luca of Bari: 10 x pen
Anna of Torino: 3 x pen
Anna has spent 36
Anna of Milano: 2 x book
Luca of Bari: 10 x pen
Anna of Milano: 3 x pen
Anna has spent 39
```

Due ordini puntano alla stessa Anna e due alla stessa penna. La città e il prezzo vengono cambiati in un solo posto, e ogni ordine vede il valore nuovo: gli oggetti sono condivisi, come con un puntatore, e niente è stato copiato.

È la forma di un database. `customer` e `product` in `Order` sono chiavi esterne, le tre liste sono tabelle, e `products[o.product].price` è una join. Un programma scritto così è facile da salvare e da caricare: gli id significano la stessa cosa nel file e in memoria, dove gli indirizzi andrebbero tradotti.

Gli elementi di un `for` vengono letti sul posto, senza una copia, e `for (ref Order o in orders)` lascia che il ciclo li modifichi: dentro un ciclo l'elemento ha un nome corto e l'indice non si scrive affatto.

## Un tipo per gli id di ogni tipo

Un id è un `i64`, e lo è anche ogni altro id. Una chiamata con due id nell'ordine sbagliato compila, e trova gli oggetti sbagliati quando gira:

```cromo ignore
shop.order(pen, anna, 2)  // the product where the customer goes: it compiles
```

Dove i tipi sono molti, ogni tipo di id riceve una struct sua, con l'indice come unico campo. Una struct con un solo `i64` occupa lo stesso spazio di un `i64`, e il compilatore a quel punto distingue gli id:

```cromo error
struct CustomerId {
    i64 index
}

struct ProductId {
    i64 index
}

void order(CustomerId customer, ProductId product, i64 quantity) {
    println("customer {}, product {}, {} of them", customer.index, product.index, quantity)
}

i32 main() {
    CustomerId anna
    anna.index = 0
    ProductId pen
    pen.index = 3
    order(pen, anna, 2)
    ret 0
}
```

```output
error: a value of type ProductId cannot be used as CustomerId
error: a value of type CustomerId cannot be used as ProductId
```

## Quando un oggetto viene tolto

Un id dice una posizione, e togliere un elemento da una lista sposta indietro di un posto quelli che lo seguono. Gli id tenuti altrove trovano allora un altro oggetto:

```cromo
i32 main() {
    list<string> names = ["Anna", "Luca", "Sara"]
    i64 luca = 1
    println("{}", names[luca])
    names.remove(0)
    println("{}", names[luca])
    ret 0
}
```

```output
Luca
Sara
```

Questa non è memoria liberata: il programma legge una vera stringa della sua lista, oppure si ferma con un panic se l'id è ormai oltre la fine. È un errore nella logica del programma, e il compilatore non lo trova. Ci sono tre modi per evitarlo, dal più semplice:

- **Non togliere mai.** La lista cresce soltanto, e un oggetto che non serve più viene segnato, con un `bool removed` tra i suoi campi. Gli id restano buoni per sempre. È la scelta giusta per ciò che vive quanto il suo proprietario: i nodi di un albero sintattico, le righe caricate da un file, i pezzi di un documento.
- **Riusare i posti.** Un oggetto tolto lascia libero il suo posto, e il prossimo che viene aggiunto lo prende. La lista non cresce, ma un vecchio id ora trova l'oggetto nuovo, senza nessun segno che sia un altro.
- **Una generazione nell'id.** Ogni posto conta quante volte è stato liberato, e un id tiene il conto che ha visto quando è stato dato. Un id il cui conto non è quello del suo posto si riconosce come vecchio.

Il terzo modo fa un id di due numeri, e un id così di solito si chiama **handle**:

```cromo
// A handle: the place of a name, and the generation of the place when it was given.
struct Handle {
    i64 index
    i64 generation
}

struct Place {
    string name
    i64 generation
    bool used
}

struct Names {
    list<Place> places

    // Adds a name in a place that is free, or in a new one.
    Handle add(string name) {
        i64 at = -1
        for (i64 i = 0; i < places.len(); i++) {
            if (!places[i].used) {
                at = i
                break
            }
        }
        if (at < 0) {
            Place place
            places.add(place)
            at = places.len() - 1
        }
        places[at].name = name
        places[at].used = true
        Handle handle
        handle.index = at
        handle.generation = places[at].generation
        ret handle
    }

    // The place is free again, and the handles given for it are no longer good.
    void remove(Handle handle) {
        if (has(handle)) {
            places[handle.index].used = false
            places[handle.index].generation++
        }
    }

    bool has(Handle handle) const {
        ret places[handle.index].used && places[handle.index].generation == handle.generation
    }
}

i32 main() {
    Names names
    Handle anna = names.add("Anna")
    Handle luca = names.add("Luca")
    names.remove(anna)
    Handle sara = names.add("Sara")

    println("the place of Anna now holds {}", names.places[anna.index].name)
    println("anna {}, luca {}, sara {}", names.has(anna), names.has(luca), names.has(sara))
    ret 0
}
```

```output
the place of Anna now holds Sara
anna false, luca true, sara true
```

Sara ha preso il posto lasciato da Anna, e l'handle di Anna ha lo stesso indice di quello di Sara. La sua generazione è quella vecchia, quindi `has` li distingue: una funzione che riceve un handle lo controlla, e risponde che l'oggetto non c'è più invece di lavorare su un altro.

La libreria lavora così dove un programma tiene ciò che non possiede. Una finestra e un componente di `std.gui` sono handle ([Finestre e app](gui.md)): valori semplici, copiati in ogni lambda che ne ha bisogno, e l'handle di una finestra chiusa dà un panic, non la memoria di un'altra finestra. Anche un `thread.Shared<T>` è un handle, come mostra la prossima sezione.

## Un oggetto modificato da più thread

Due thread che modificano lo stesso oggetto hanno bisogno di più di un id: ognuno deve aspettare mentre l'altro lavora. `thread.Shared<T>` tiene un valore insieme al suo lock, e ne dà un handle ([Thread, task e canali](concurrency.md)). L'handle viene copiato in ogni thread, come si farebbe con un id, e l'oggetto si raggiunge solo attraverso `lock`:

```cromo
import std.thread

struct Account {
    string owner
    i64 balance
    i64 operations

    void deposit(i64 amount) {
        balance += amount
        operations++
    }

    void withdraw(i64 amount) {
        balance -= amount
        operations++
    }
}

i32 main() {
    Account opened
    opened.owner = "Anna"
    opened.balance = 1000
    thread.Shared<Account> account = thread.Shared<Account>(move opened)

    thread.Thread salary = thread.Thread([account]() {
        for (i64 i = 0; i < 100000; i++) {
            account.lock([](ref Account a) { a.deposit(3) })
        }
    })
    thread.Thread rent = thread.Thread([account]() {
        for (i64 i = 0; i < 100000; i++) {
            account.lock([](ref Account a) { a.withdraw(2) })
        }
    })
    salary.start()
    rent.start()
    salary.join()
    rent.join()

    account.lock([](ref Account a) { println("{}: balance {}, operations {}", a.owner, a.balance, a.operations) })
    ret 0
}
```

```output
Anna: balance 101000, operations 200000
```

Qui vengono passate due cose, in due modi diversi:

- **`[account]` è una copia.** La lambda di ogni thread cattura l'handle per valore. L'handle non è il conto: dice solo dove si trovano il conto e il suo lock, quindi le due copie raggiungono lo stesso conto, e il conto in sé non viene mai copiato.
- **`ref Account a` è un riferimento.** `lock` prende il lock, dà il conto vero alla lambda interna con `ref`, e rilascia il lock quando la lambda finisce, anche quando passa un'eccezione. Questo riferimento è un normale parametro `ref`: vive solo durante quella chiamata, cioè solo mentre il lock è preso.

`move opened` mette il conto dentro lo `Shared`, e `opened` non si può più usare: dopo quella riga non c'è modo di raggiungere il conto senza il suo lock, quindi il lock non si può dimenticare. Il riferimento che dura è quello dentro `Shared`, scritto una volta nella libreria, nel dialetto unsafe; il programma vede un handle che può copiare e un `ref` che non può uscire dal lock.

Il valore di uno `Shared` resta fino alla fine del programma, e il suo distruttore non viene mai eseguito: serve per uno stato che vive quanto il programma, non per oggetti creati e buttati in un ciclo.

Senza `Shared`, due thread che catturano la stessa variabile per indirizzo vengono rifiutati dal compilatore:

```cromo error
import std.thread

i32 main() {
    i64 balance = 1000
    thread.Thread a = thread.Thread([ref balance]() { balance += 3 })
    thread.Thread b = thread.Thread([ref balance]() { balance -= 2 })
    a.start()
    b.start()
    a.join()
    b.join()
    println("{}", balance)
    ret 0
}
```

```output
error: the thread 'b' cannot start: 'balance' is used by the thread 'a' until a.join()
```

## Quanto costa

Gli id non sono gratuiti, ed è giusto conoscerne il prezzo prima di sceglierli, anche se in Cromo non c'è un'altra scelta per un oggetto che viene tenuto.

- **Più da scrivere.** `shop.customers[o.customer].name` dove C++ scrive `o.customer->name`, e la lista deve essere a portata di mano: le funzioni che seguono gli id sono metodi del proprietario delle liste, oppure lo ricevono come parametro. Un `for` sulla lista, e un parametro `ref` per l'oggetto su cui lavorare, tengono i nomi lunghi in pochi posti.
- **Il controllo dell'indice.** Ogni `list[i]` confronta `i` con la dimensione, anche in una build `--release`. In un ciclo sulla lista il compilatore di solito toglie il controllo, perché sa già che l'indice è dentro; resta dove l'indice viene dai dati, come in `products[o.product]`. È un confronto e un salto che il processore prevede quasi sempre: poco, tranne in un ciclo corto che non fa altro.
- **L'indirizzo viene calcolato.** Un puntatore si segue; un id viene moltiplicato per la dimensione di un elemento e sommato all'inizio della lista. Sono un paio di istruzioni.
- **Un id può invecchiare**, come mostra la sezione sopra, dove un puntatore resterebbe appeso: l'errore è dello stesso genere, il suo effetto no. Un id vecchio trova un oggetto sbagliato o un panic, mai la memoria di qualcos'altro.

Di fronte a questi costi c'è ciò che gli id danno:

- **Gli oggetti sono uno accanto all'altro.** Una lista tiene i suoi elementi in un solo blocco di memoria, in ordine, e scorrerli usa bene la cache del processore. Gli oggetti allocati uno per uno sono sparsi, e seguire un puntatore a ciascuno è spesso un cache miss, che costa molto più del controllo di un indice.
- **Meno allocazioni.** Mille cartelle sono una lista che cresce poche volte, non mille allocazioni, e vengono liberate tutte insieme.
- **Id più piccoli.** Dove gli oggetti sono meno di due miliardi, un id può essere un `i32`, la metà di un puntatore: le struct sono più piccole, e nella cache ce ne stanno di più.

Rispetto a un programma C++ che alloca ogni oggetto e li collega con puntatori, lo stesso programma con gli id è spesso più veloce. Rispetto a uno che tiene i suoi oggetti in un `std::vector` e punta dentro di esso, è un po' più lento, per il controllo dell'indice, e non può rompersi quando il vettore cresce.

## Riepilogo

- Un oggetto non può tenere un riferimento a un altro: un riferimento tenuto potrebbe vivere più del suo oggetto, e Cromo non ha né lifetime né un garbage collector per impedirlo.
- Gli oggetti di un tipo vivono in una lista, e chi punta a uno di loro tiene il suo id, l'indice in quella lista: un valore semplice, copiato e conservato liberamente.
- Un id resta buono quando la lista cresce e si sposta, e uno sbagliato è un panic, non memoria letta per errore.
- Le funzioni che seguono gli id appartengono al proprietario delle liste.
- Una struct con un solo campo per ogni tipo di id lascia che il compilatore li distingua.
- Togliere da una lista cambia ciò che trovano gli id che vengono dopo: non togliere mai, oppure usare un handle, un indice con la generazione del suo posto.
- Una finestra, un componente e un `thread.Shared<T>` sono handle della libreria. Uno `Shared` viene copiato in ogni thread e dà il suo valore con `ref` solo dentro `lock`.
- Il prezzo sono nomi più lunghi e il controllo di ogni indice; il guadagno sono oggetti vicini in memoria e meno allocazioni.
