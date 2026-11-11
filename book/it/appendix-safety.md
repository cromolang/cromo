# Appendice D: Cosa controlla Cromo, e cosa no

Cromo vuole essere abbastanza sicuro per i programmi di tutti i giorni con regole che stanno in una pagina, e non il linguaggio più sicuro che esista. Questa appendice dice con chiarezza cosa controlla il linguaggio, quando, cosa lascia al programmatore, e quanto costa la sua semplicità.

## Cosa è controllato

| L'errore | In Cromo | Quando |
|---|---|---|
| Usare memoria già liberata, un riferimento che vive più del suo oggetto | Non si può scrivere: non ci sono puntatori, e `ref` esiste solo durante una chiamata o un blocco ([Possesso](ownership.md)) | In compilazione |
| Liberare due volte, o dimenticare di liberare | Il distruttore di un valore viene eseguito una volta, quando il suo proprietario finisce | Sempre |
| Usare un valore dopo averlo spostato | Un errore | In compilazione |
| `null`, un valore mai impostato | Non c'è `null`, e ogni variabile comincia con un valore | Sempre |
| Due argomenti di una chiamata che sono lo stesso oggetto, una lista modificata mentre un `for` la percorre | Un errore; per due elementi di una lista, i cui indici si conoscono solo quando il programma gira, un panic | In compilazione, in esecuzione |
| Un indice fuori da una lista, una stringa o un array | Un panic, nelle build di debug e di release | In esecuzione |
| Una divisione tra interi per zero | Un panic, nelle build di debug e di release | In esecuzione |
| Un overflow di interi, uno scorrimento oltre la larghezza del tipo, un float che non sta in un intero | Un panic nelle build di debug; con `--release` un risultato definito, uguale su ogni sistema ([Numeri e operatori](operators.md)) | In esecuzione |
| Due thread che modificano gli stessi dati | Il compilatore controlla cosa cattura la lambda di un thread o di un task: i dati che condividono stanno in un `Mutex`, uno `Shared`, un `Atomic` o un `Channel` ([Thread, task e canali](concurrency.md)) | In compilazione |
| Uno stack che trabocca | Il programma si ferma, con la traccia dello stack | In esecuzione |

Un panic ferma il programma con un messaggio e la traccia dello stack. Il programma non prosegue mai con dati sbagliati.

## Cosa non è controllato

- **I deadlock.** Due thread che si aspettano a vicenda aspettano per sempre, e così uno `Shared` bloccato di nuovo dentro il suo stesso `lock`.
- **La memoria trattenuta.** Non ci sono perdite per aver dimenticato di liberare, ma un programma può tenere ciò che non gli serve più: una lista che cresce soltanto, e il valore di un `thread.Shared`, che resta fino alla fine del programma.
- **Un indice che non è più quello giusto.** Dove un altro linguaggio tiene un puntatore, un programma Cromo tiene un indice in una lista, o un handle, come le finestre e i componenti di `std.gui`. Dopo che l'elemento è stato tolto, l'indice dà un altro elemento o un panic, e l'handle un panic: un errore nella logica del programma, non una corruzione della sua memoria, ma il compilatore non lo trova.
- **La logica.** Un risultato sbagliato, un file non scritto fino in fondo, un lock tenuto troppo a lungo.

## Il dialetto unsafe e il C

Un file che scrive `import unsafe` può raggiungere la memoria grezza e chiamare il C, e lì tornano gli errori del C: [Il dialetto unsafe](unsafe.md) li elenca. Le garanzie qui sopra valgono per un programma finché quel codice è giusto, e questo comprende:

- la libreria standard, che è scritta nel dialetto: circa 28 000 righe, nella cartella `library` del repository;
- il runtime, in C, e le librerie che usa: SDL per le finestre, mbedTLS per TLS, e quelle che leggono le immagini e i font.

Un programma che non scrive `import unsafe` si fida solo di queste.

## Quanto costa la semplicità

- **Niente riferimenti nei campi o nelle variabili.** Un grafo, un albero con i collegamenti ai genitori e un observer si scrivono con indici o handle, che vengono cercati, e controllati, ogni volta. In Rust sarebbero riferimenti con lifetime, oppure `Rc` e `RefCell`.
- **Le copie.** Un valore che possiede memoria viene copiato dove è dato senza `move`, come in C++. Il programma è giusto, e può essere più lento del necessario: `move`, e i parametri letti e `ref`, evitano le copie.
- **I controlli in esecuzione.** I limiti degli indici sono controllati anche nelle build di release. L'overflow degli interi è controllato solo nelle build di debug: un programma rilasciato con `--release` riparte da capo dove i suoi test davano un panic.

## Quanto sono sicure queste garanzie

Sono le regole del progetto, fatte rispettare da un compilatore giovane.

- Non c'è una dimostrazione formale che le regole siano corrette, come c'è per parti di Rust.
- Il compilatore ha una suite di test, più di 450 programmi in `tests`, e ogni esempio di questa guida viene compilato ed eseguito quando la guida è costruita. Non è stato ancora usato da molte persone su programmi grandi.
- Un programma senza `import unsafe` che corrompe la memoria, o che compila pur violando una delle regole qui sopra, è un bug di Cromo. Segnalalo nelle [issue](https://github.com/cromolang/cromo/issues) del repository: sono le segnalazioni più utili.

## Riepilogo

- Senza `import unsafe`, un programma Cromo non ha riferimenti pendenti, doppie liberazioni, `null`, valori mai impostati né data race, e un indice fuori dai limiti lo ferma.
- I deadlock, la memoria trattenuta, e gli indici e gli handle che non sono più quelli giusti sono lasciati al programmatore.
- La libreria standard e il runtime sono codice di cui ci si fida, scritti nel dialetto unsafe e in C.
- Il prezzo delle regole semplici sono indici e handle al posto dei riferimenti conservati, qualche copia, e controlli in esecuzione.
- Le regole non sono dimostrate, e il compilatore è giovane: una falla in esse è un bug da segnalare.
