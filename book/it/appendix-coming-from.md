# Appendice C: Cromo per chi programma in C++, Go e Rust

Cromo prende qualcosa da ciascuno di questi linguaggi. Questa appendice elenca, per ognuno, che cosa ti risulterà familiare e che cosa invece è diverso.

## Dal C++

**Familiare:** la sintassi, le graffe e i tipi prima dei nomi; le struct con costruttori, metodi, metodi `const` e distruttori; RAII e l'ordine di distruzione; le eccezioni con `try`, `catch` e `throw`; i generici compilati per ogni tipo, come i template; `std::format` per la formattazione.

**Diverso:**

| C++ | Cromo |
|---|---|
| `return` | `ret` |
| `;` alla fine delle istruzioni | la fine della riga |
| puntatori, `nullptr`, `new`, `delete` | niente puntatori e niente `null`; l'heap solo attraverso i contenitori |
| `T&`, `const T&` | un parametro è in lettura per default; `ref` modifica; nessun riferimento al di fuori dei parametri |
| costruttori di copia, copie implicite | copie implicite anche qui, fatte da `copy()`, generato o scritto a mano |
| `std::move` | `move`, controllato: una variabile spostata non può più essere usata |
| `class`, ereditarietà, `virtual` | `struct` e interfacce |
| template con qualsiasi tipo | generici vincolati da interfacce, controllati una volta sola |
| `while`, `do` | `for (condition)`, `for { }` |
| `switch` con fall-through | niente fall-through; sugli enum ogni case va gestito |
| `#include`, header | `import` di moduli; nessun preprocessore |
| `int`, `long`, `size_t` | `i32`, `i64`, `u64`: dimensioni fisse; le lunghezze sono `i64` |
| comportamento indefinito per overflow e accessi fuori dai limiti | un panic nelle build di debug; i limiti sono sempre controllati |
| variabili non inizializzate | ogni variabile ha il suo valore predefinito |
| `std::thread`, `std::mutex` | `thread.Thread`, `thread.Mutex<T>` che contiene i suoi dati |

## Da Go

**Familiare:** un solo strumento che compila, esegue, testa e formatta; task leggeri che si bloccano senza bisogno di `async`; i canali; una libreria standard per le reti e per HTTP; eseguibili statici con la cross-compilazione; e, quando arriveranno i pacchetti, dipendenze indicate dall'URL del loro repository, con la selezione della versione minima.

**Diverso:**

| Go | Cromo |
|---|---|
| garbage collector | possesso e distruttori; la memoria viene liberata quando finisce la vita del proprietario |
| `go f()` | `thread.Task` o `thread.Group`, di cui il compilatore controlla le catture |
| `defer` | distruttori, e `finally` |
| valori `error` e `if err != nil` | eccezioni |
| `nil` | niente `null`; ogni variabile ha un valore |
| `x := 1` | `i64 x = 1`: il tipo si scrive sempre |
| iniziali maiuscole per i nomi esportati | `public` |
| `select` | non ancora |
| slice che condividono la memoria | liste che possiedono i loro elementi; `slice()` crea una copia |

## Da Rust

**Familiare:** il possesso controllato dal compilatore, lo spostamento come comportamento predefinito per i tipi che possiedono risorse, niente `null`, niente data race; gli enum con dati e il match esaustivo; i generici vincolati da interfacce, come i trait; LLVM; i test integrati nello strumento.

**Diverso:**

| Rust | Cromo |
|---|---|
| riferimenti con lifetime, il borrow checker | `ref` solo per i parametri: un riferimento non sopravvive mai a una chiamata, e non ci sono lifetime |
| `&T`, `&mut T` | un parametro in lettura, un parametro `ref` |
| `Clone`, `.clone()` | `copy()`, chiamato da solo dove un valore viene dato senza `move` |
| tipi `Copy` | tipi semplici: numeri, `bool`, struct composte solo da questi |
| `Result<T, E>` e `?` | eccezioni; enum con dati per i risultati che sono valori |
| `Option<T>` | un valore convenzionale, un `bool` con un parametro `ref`, oppure un enum |
| `match` con pattern | `switch` sui case, dando un nome ai dati; niente pattern annidati |
| `Drop` | distruttori |
| `impl Trait for Type` | `struct Type : Interface`, con i metodi dentro la struct |
| `async`/`.await` | task che si bloccano, come in Go |
| `Arc<Mutex<T>>` | `thread.Mutex<T>` catturato dai task che lo usano |
| macro | non disponibili |

## In breve

Cromo si scrive come il C++, gestisce la memoria come Rust ma con meno regole, ed esegue il codice concorrente come Go. Nel dubbio, vince la regola più semplice.
