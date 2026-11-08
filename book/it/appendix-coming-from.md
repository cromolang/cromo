# Appendice C: Cromo per programmatori C++, Go e Rust

Cromo prende qualcosa da ciascuno di questi linguaggi. Questa appendice elenca, per ognuno, che cosa ti risulterà familiare e che cosa invece è diverso, con il capitolo che lo spiega.

## Dal C++

**Familiare:** la sintassi, le graffe e i tipi prima dei nomi; le struct con costruttori, metodi, metodi `const` e distruttori; RAII e l'ordine di distruzione; le eccezioni con `try`, `catch` e `throw`; i generici compilati per ogni tipo, come i template; le lambda con le loro catture tra parentesi quadre; `c ? a : b`; gli attributi tra doppie parentesi quadre; `std::format` per la formattazione.

**Diverso:**

| C++ | Cromo |
|---|---|
| `return` | `ret` |
| `;` alla fine delle istruzioni | la fine della riga; un'istruzione per riga ([Concetti comuni](common-concepts.md)) |
| puntatori, `nullptr`, `new`, `delete` | niente puntatori e niente `null`; l'heap solo attraverso i contenitori. Gli indirizzi grezzi esistono solo nel dialetto unsafe ([Il dialetto unsafe](unsafe.md)) |
| `T&`, `const T&` | un parametro è in lettura per default; `ref` modifica; un riferimento vive solo durante una chiamata o un ciclo, mai in un campo ([Possesso](ownership.md)) |
| costruttori di copia, copie implicite | copie implicite anche qui, fatte da `copy()`, generato o scritto a mano ([Struct](structs.md)) |
| `std::move` | `move`, controllato: una variabile spostata non può più essere usata ([Possesso](ownership.md)) |
| `class`, ereditarietà, `virtual` | `struct` e interfacce ([Interfacce e generici](interfaces-generics.md)) |
| template con qualsiasi tipo | generici il cui corpo viene controllato dove è scritto, con ciò che promettono i loro vincoli |
| overloading di funzioni e operatori | non disponibile: ogni funzione ha il proprio nome |
| `auto` | il tipo si scrive sempre |
| `while`, `do` | `for (condition)`, `for { }` |
| `switch` con fall-through | niente fall-through; sugli enum ogni case va gestito ([Enum e switch](enums.md)) |
| `#include`, header | `import` di moduli; nessun preprocessore ([Moduli e progetti](modules.md)) |
| `int`, `long`, `size_t` | `i32`, `i64`, `u64`: dimensioni fisse; le lunghezze sono `i64` |
| conversioni implicite tra numeri | solo quelle che non perdono nulla, come da `i32` a `i64`; numeri con segno e senza segno non si mescolano ([Numeri e operatori](operators.md)) |
| `std::string`, `std::vector`, `std::unordered_map` | `string`, `list<T>`, `map<K, V>` ([Liste, mappe e testo](collections.md)) |
| comportamento indefinito per overflow e accessi fuori dai limiti | l'overflow è un panic nelle build di debug e ricomincia dall'estremo opposto con `--release`; i limiti sono sempre controllati |
| variabili non inizializzate | ogni variabile ha il suo valore predefinito |
| `std::thread`, `std::mutex` | `thread.Thread`, `thread.Mutex<T>` che contiene i suoi dati ([Thread, task e canali](concurrency.md)) |
| `extern "C"`, header del C | blocchi `extern "library" { ... }`, senza un compilatore C ([Chiamare il C](c-interop.md)) |

## Da Go

**Familiare:** un solo strumento che compila, esegue, testa e formatta; eseguibili che non richiedono nulla di installato e si compilano anche per altri sistemi; task leggeri che si bloccano senza bisogno di `async`; i canali; una libreria standard per le reti e per HTTP; il dialetto dietro `import unsafe`; e i pacchetti, che arriveranno più avanti.

Su Linux gli eseguibili sono statici, a meno che le librerie C del programma non chiamino la libreria C del sistema: allora `cromo` li collega a glibc, come fa Go con cgo, e richiedono glibc 2.29 o successiva ([Chiamare il C](c-interop.md)). Ogni programma con una finestra rientra in questo caso.

**Diverso:**

| Go | Cromo |
|---|---|
| garbage collector | possesso e distruttori; la memoria viene liberata quando finisce la vita del proprietario ([Possesso](ownership.md)) |
| `go f()` | `thread.Task` o `thread.Group`, di cui il compilatore controlla le catture ([Thread, task e canali](concurrency.md)) |
| `defer` | distruttori, e `finally` |
| valori `error` e `if err != nil` | eccezioni ([Errori](errors.md)) |
| `panic` e `recover` | `panic` ferma il programma e non si può catturare; le eccezioni servono per gli errori che gestisce il chiamante |
| `nil` | niente `null`; ogni variabile ha un valore |
| `x := 1` | `i64 x = 1`: il tipo si scrive sempre |
| iniziali maiuscole per i nomi esportati | `public` |
| interfacce soddisfatte implicitamente | una struct nomina le sue interfacce: `struct File : Reader` ([Interfacce e generici](interfaces-generics.md)) |
| `for i, x := range v` | `for (T x in v)`, `for (K key, V value in m)` |
| `select` | non disponibile |
| slice che condividono la memoria | liste che possiedono i loro elementi; `slice()` di una stringa crea una copia |
| cgo | blocchi `extern`, senza un compilatore C ([Chiamare il C](c-interop.md)) |

## Da Rust

**Familiare:** il possesso controllato dal compilatore, lo spostamento come comportamento predefinito per i tipi che possiedono risorse, niente `null`, niente data race al di fuori del dialetto unsafe; gli enum con dati e il match esaustivo; i generici vincolati da interfacce, come i trait; LLVM; i test integrati nello strumento, con `[[test]]` al posto di `#[test]`; un dialetto unsafe per il codice che il compilatore non può controllare.

**Diverso:**

| Rust | Cromo |
|---|---|
| riferimenti con lifetime, il borrow checker | `ref` solo per i parametri, le catture, gli elementi di un `for` e i dati di un `case`: un riferimento non sopravvive mai alla chiamata o al blocco, e non ci sono lifetime ([Possesso](ownership.md)) |
| `&T`, `&mut T` | un parametro in lettura, un parametro `ref` |
| `Clone`, `.clone()` | `copy()`, chiamato da solo dove un valore viene dato senza `move` |
| tipi `Copy` | tipi semplici: numeri, `bool` e le struct composte solo da questi, senza distruttore |
| `Result<T, E>` e `?` | eccezioni; enum con dati per i risultati che sono valori ([Errori](errors.md)) |
| `Option<T>` | un valore convenzionale, un `bool` con un parametro `ref`, oppure un enum generico scritto da te |
| `match` con pattern | `switch` sui case, dando un nome ai dati; niente pattern annidati ([Enum e switch](enums.md)) |
| `Drop` | distruttori |
| `impl Trait for Type` | `struct Type : Interface`, con i metodi dentro la struct |
| `String`, `Vec<T>`, `HashMap<K, V>` | `string`, `list<T>`, `map<K, V>` |
| `async`/`.await` | task che si bloccano, come in Go |
| `Arc<Mutex<T>>` | `thread.Mutex<T>` catturato dai task che lo usano |
| blocchi `unsafe { }` | `import unsafe`, per un file intero, che offre un'interfaccia sicura ([Il dialetto unsafe](unsafe.md)) |
| `static mut` | variabili di modulo, solo con `import unsafe` |
| `#[no_mangle] extern "C" fn`, `#[cold]` | `[[export("name")]]`, `[[cold]]` |
| macro | non disponibili |
| Cargo | il comando `cromo` e `cromo.mod` ([Appendice B: Il comando cromo](appendix-command.md)) |

## Riepilogo

- Cromo si scrive come il C++: graffe, tipi prima dei nomi, struct con costruttori e distruttori, eccezioni, template come generici.
- Gestisce la memoria come Rust, con meno regole: possesso e spostamenti controllati dal compilatore, riferimenti che non sopravvivono mai a una chiamata o a un ciclo, niente lifetime.
- Esegue il codice concorrente come Go: task che si bloccano, canali, un solo strumento per tutto.
- Nel dubbio, vince la regola più semplice.
