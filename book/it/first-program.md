# Un primo programma: contare le parole

Prima dei dettagli, un programma completo: legge un file di testo e stampa le parole che vi compaiono più spesso. Mostra al lavoro funzioni, cicli, stringhe, liste, mappe e struct; i capitoli successivi li spiegano uno per uno.

## Il programma

Ecco il testo di cui conteremo le parole:

```text file=poem.txt
The fog comes on little cat feet.
It sits looking over harbor and city
on silent haunches and then moves on.
The fog, the cat, the city: on and on.
```

Ed ecco il programma, `words.cro`:

```cromo file=words.cro args=poem.txt
import std.fs

// Una parola è fatta di lettere; i byte UTF-8 sopra 127 contano come lettere.
bool isLetter(u8 c) {
    ret (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128
}

// Le parole di un testo, in minuscolo.
list<string> words(string text) {
    list<string> found
    i64 start = -1
    for (i64 i = 0; i <= text.len(); i++) {
        bool letter = i < text.len() && isLetter(text[i])
        if (letter && start < 0) {
            start = i
        } else if (!letter && start >= 0) {
            found.add(text.slice(start, i).lower())
            start = -1
        }
    }
    ret found
}

struct Count {
    string word
    i64 times
}

// Prima le più frequenti; a parità di conteggio, in ordine alfabetico.
bool before(Count a, Count b) {
    if (a.times != b.times) {
        ret a.times > b.times
    }
    ret a.word < b.word
}

void sort(ref list<Count> counts) {
    for (i64 i = 1; i < counts.len(); i++) {
        for (i64 j = i; j > 0 && before(counts[j], counts[j - 1]); j--) {
            Count moving = move counts[j]
            counts[j] = move counts[j - 1]
            counts[j - 1] = move moving
        }
    }
}

i32 main(list<string> args) {
    if (args.len() < 2) {
        eprintln("usage: words <file>")
        ret 1
    }
    string text = fs.readText(args[1])

    map<string, i64> seen
    for (string word in words(text)) {
        seen[word] += 1
    }

    list<Count> counts
    for (string word, i64 times in seen) {
        Count c
        c.word = word.copy()
        c.times = times
        counts.add(move c)
    }
    sort(ref counts)

    for (i64 i = 0; i < counts.len() && i < 5; i++) {
        println("{:>3}  {}", counts[i].times, counts[i].word)
    }
    ret 0
}
```

Eseguilo passando il nome del file dopo `--`:

```sh
cromo run words.cro -- poem.txt
```

```output
  5  on
  4  the
  3  and
  2  cat
  2  city
```

## Come funziona

### Importare un modulo

```cromo ignore
import std.fs
```

La libreria standard è fatta di un nucleo, che ogni programma vede, e di moduli, che un programma importa. `std.fs` lavora con i file: `fs.readText(path)` restituisce l'intero contenuto di un file come `string`. Liste, mappe e stringhe stanno nel nucleo e non serve importarle.

### Funzioni e tipi

```cromo ignore
bool isLetter(u8 c) {
    ret (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128
}
```

Una funzione si scrive come in C: il tipo del risultato, il nome e i parametri, ciascuno con il tipo prima del nome. `u8` è un byte senza segno; `'a'` è un carattere, che in Cromo è un byte. Una `string` è testo in UTF-8, quindi una lettera come `è` è fatta di due byte sopra 127, che il programma conta come lettere.

### Cicli

```cromo ignore
for (i64 i = 0; i <= text.len(); i++) {
```

Cromo ha un solo ciclo, `for`, con le forme del C e qualcuna in più: qui c'è quella classica, con un `i64` che va da 0 alla lunghezza del testo. `text.len()` è la lunghezza in byte e `text[i]` è il byte in posizione `i`. Lunghezze e posizioni sono sempre `i64`. Un indice fuori dal testo ferma il programma con un errore, invece di leggere memoria che non c'è.

### Liste

```cromo ignore
list<string> found
found.add(text.slice(start, i).lower())
ret found
```

`list<string>` è una lista di stringhe che cresce secondo necessità. Dichiarata senza valore, è vuota: in Cromo ogni variabile ha un valore, e quello predefinito di una lista è la lista vuota. `ret found` consegna la lista al chiamante senza copiarla.

### Struct

```cromo ignore
struct Count {
    string word
    i64 times
}
```

Una struct raggruppa dei valori, come in C. `Count c` ne dichiara una con il suo valore predefinito, cioè `word` vuota e `times` uguale a 0; poi i campi vengono assegnati uno per uno.

### Possesso: `ref`, `move` e `copy()`

Tre parole del programma meritano attenzione, perché sono il cuore di Cromo:

- `sort(ref counts)`: la funzione **modifica** la lista, e la chiamata lo dice con `ref`, come fa la dichiarazione `void sort(ref list<Count> counts)`.
- `counts.add(move c)`: la struct si **sposta** nella lista. Dopo il `move`, `c` non si può più usare: il suo contenuto ora appartiene alla lista.
- `c.word = word.copy()`: una stringa non viene mai copiata senza dirlo. `word` appartiene alla mappa, quindi il programma ne chiede una copia.

Senza `ref`, un parametro è in sola lettura, come `text` in `words(string text)`: la funzione può leggere la stringa ma non cambiarla, e la stringa non viene copiata. Il capitolo 4 spiega queste regole, che sono il nucleo del linguaggio.

### Mappe

```cromo ignore
map<string, i64> seen
seen[word] += 1
for (string word, i64 times in seen) {
```

Una `map` associa chiavi a valori, come una tabella hash. `seen[word]` legge o scrive il valore di una chiave; `seen[word] += 1`, se la chiave manca, prima la aggiunge con il valore 0, come fa `operator[]` in C++. Un `for` con due variabili visita ogni chiave insieme al suo valore, senza un ordine particolare: per questo il programma ordina i conteggi.

### Stampa

```cromo ignore
println("{:>3}  {}", counts[i].times, counts[i].word)
```

`{:>3}` stampa un valore allineato a destra su tre caratteri. I formati sono quelli di `std::format` del C++ e di Python: il capitolo 7 li elenca.

### Errori

Se il file non esiste, `fs.readText` lancia un'eccezione. Nessuno la cattura, quindi il programma si ferma e stampa che cosa è successo, con le chiamate che hanno portato fin lì (qui abbreviate):

```sh
cromo run words.cro -- missing.txt
```

```text
uncaught exception: cannot read 'missing.txt': no such file or folder
    at std.fs.readText (cromo:std/fs.cro:34)
    at main (words.cro:52)
```

Il capitolo 8 mostra come catturare le eccezioni con `try` e `catch`.

## Riepilogo

In poche righe il programma ha usato gran parte di ciò che incontrerai in questa guida: funzioni, il ciclo `for`, stringhe, liste, mappe, una struct e i tre modi di passare un valore: in lettura, con `ref` e con `move`. Ora vediamo il linguaggio dal principio.
