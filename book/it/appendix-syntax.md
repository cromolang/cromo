# Appendice A: Parole chiave e operatori

## Parole chiave

| Parola chiave | Uso |
|---|---|
| `break` | esce da un ciclo |
| `case` | un ramo di uno `switch` |
| `catch` | gestisce un'eccezione |
| `const` | una variabile immutabile; un metodo che non modifica il suo oggetto; una `const func` |
| `continue` | passa all'iterazione successiva di un ciclo |
| `default` | il ramo di uno `switch` per i valori non elencati |
| `else` | l'alternativa di un `if` |
| `enum` | dichiara un enum |
| `extern` | dichiara funzioni C, in un modulo con `import unsafe` |
| `false`, `true` | i valori di `bool` |
| `finally` | viene eseguito a ogni uscita da un `try` |
| `for` | il ciclo, in tutte le sue forme |
| `func` | il tipo di una funzione |
| `if` | una condizione |
| `import` | importa un modulo |
| `in` | in un `for` sugli elementi |
| `interface` | dichiara un'interfaccia |
| `move` | sposta un valore; un parametro o una cattura che prende il possesso |
| `public` | rende una dichiarazione visibile fuori dal suo modulo |
| `ref` | un parametro o una cattura che modifica; gli elementi di un `for` che si possono modificare |
| `ret` | ritorna da una funzione |
| `struct` | dichiara una struct |
| `switch` | sceglie tra più case |
| `throw` | lancia un'eccezione |
| `try` | il blocco di cui si gestiscono le eccezioni |
| `unsafe` | `import unsafe`: il dialetto delle funzioni C e della memoria grezza |
| `void` | nessun risultato |

`print`, `println`, `eprint`, `eprintln`, `format`, `panic` e `assert` sono funzioni integrate, non parole chiave.

## Tipi integrati

| Tipo | Significato |
|---|---|
| `i8`, `i16`, `i32`, `i64` | interi con segno |
| `u8`, `u16`, `u32`, `u64` | interi senza segno |
| `f32`, `f64` | numeri in virgola mobile |
| `bool` | `true` o `false` |
| `char` | un byte, sinonimo di `u8` |
| `string` | testo in UTF-8 |
| `list<T>` | una lista che cresce |
| `map<K, V>` | una tabella hash |
| `T[n]` | un array di dimensione fissa |
| `func(A, B) R` | una funzione |
| `addr` | un indirizzo C, solo con `import unsafe` |

## Operatori

Dalla precedenza più alta alla più bassa:

| Operatori | Significato |
|---|---|
| `.` `()` `[]` | membro, chiamata, indice |
| `!` `-` | not, inversione dei bit, negazione |
| `*` `/` `%` | moltiplicazione, divisione, resto |
| `+` `-` | addizione, sottrazione; `+` concatena anche le stringhe |
| `<<` `>>` | shift |
| `&` | and bit a bit |
| `^` | or esclusivo bit a bit |
| `\|` | or bit a bit |
| `==` `!=` `<` `<=` `>` `>=` | confronti |
| `&&` | and logico, valutato in cortocircuito |
| `\|\|` | or logico, valutato in cortocircuito |
| `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | assegnamenti, istruzioni a sé stanti |

- `i++` e `i--` sono istruzioni a sé stanti, mai parti di un'espressione.
- Non esiste l'operatore condizionale `?:`: scrivi un `if`.
- Uno shift oltre la larghezza del tipo provoca un panic nelle build di debug.
- Gli assegnamenti sono istruzioni: `if (a = b)` non è Cromo.

## Letterali

| Letterale | Esempio |
|---|---|
| intero | `42`, `1_000_000`, `0xFF`, `0b1010` |
| virgola mobile | `3.14`, `1e-9`, `2.5e3` |
| carattere | `'a'`, `'\n'`, `'\t'`, `'\\'`, `'\''`, `'\x41'` |
| stringa | `"text"`, con le stesse sequenze di escape e `\"` |
| lista | `[1, 2, 3]` |

## Attributi

Gli attributi si scrivono come in C++11, prima di ciò a cui si applicano:

| Attributo | Significato |
|---|---|
| `[[test]]` | una funzione di test |
| `[[json("name")]]` | il nome di un campo o di un case nel JSON |
| `[[json(skip)]]` | un campo escluso dal JSON |
