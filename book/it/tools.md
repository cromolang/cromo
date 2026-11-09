# Strumenti: formattazione, editor e debugger

`cromo` è l'unico strumento di Cromo: oltre al compilatore, contiene il formattatore del codice, `cromo fmt`, e il language server usato dagli editor, `cromo lsp`. Questo capitolo li presenta, insieme all'estensione per Visual Studio Code costruita su di essi, ai commenti che documentano il codice e al modo di eseguire un programma in un debugger.

## Formattazione: cromo fmt

Tutto il codice Cromo ha un solo stile, e `cromo fmt` lo scrive. Non ha opzioni né file di configurazione, come `gofmt` di Go: nessuno ha bisogno di discutere dello stile, e il codice scritto da altri ha lo stesso aspetto del tuo.

### Il comando

```sh
cromo fmt                    # every .cro file of this folder and of its subfolders
cromo fmt main.cro shapes    # these files, and the .cro files of these folders
cromo fmt --check            # changes nothing: lists the files to format
cromo fmt -                  # formats the standard input on the standard output
```

- Senza nomi, `cromo fmt` formatta ogni file `.cro` della cartella corrente e delle sue sottocartelle, tranne le cartelle il cui nome inizia con un punto, come `.git`.
- I file vengono riscritti sul posto. `cromo fmt` scrive il nome di ogni file che modifica, e nulla per i file già formattati.
- `--check` non modifica nulla: scrive i nomi dei file non formattati, e termina con 1 se ce ne sono, con 0 altrimenti. È il controllo da eseguire prima di un commit, o in una pipeline di integrazione continua.
- `-` legge un testo dallo standard input e lo scrive formattato sullo standard output: è ciò che usa un editor. Non accetta altri nomi, né `--check`.
- Un file con errori di sintassi non viene modificato: `cromo fmt` scrive gli errori, come fa il compilatore, prosegue con gli altri file, e termina con 1.
- Un nome che non è né un file né una cartella, o un'opzione sconosciuta, fa terminare `cromo fmt` con 2 prima che modifichi qualsiasi cosa.

### Che cosa fa

Un file scritto di fretta:

```cromo ignore
struct Point
{
  f64 x // across
  f64 y   // down
  i32 visits=0     // how many times
}


i32 main( )
{
  list<i32>v=[1,2,3]
  i32 total=0
  for(i32 x in v){total+=x}
  if(total>5)
  {
     println( "big {}" ,total )
  }
  else
  {
     println("small")
  }

  i32 n = - total
  n ++
  Point p
  println("{} {}", larger(total,
  n), p.visits)

  ret 0
}
i32 larger(i32 a, i32 b) { ret a > b ? a : b }
```

dopo `cromo fmt`:

```cromo
struct Point {
    f64 x           // across
    f64 y           // down
    i32 visits = 0  // how many times
}

i32 main() {
    list<i32> v = [1, 2, 3]
    i32 total = 0
    for (i32 x in v) { total += x }
    if (total > 5) {
        println("big {}", total)
    } else {
        println("small")
    }

    i32 n = -total
    n++
    Point p
    println("{} {}", larger(total,
        n), p.visits)

    ret 0
}
i32 larger(i32 a, i32 b) { ret a > b ? a : b }
```

```output
big 6
6 0
```

Le regole, tutte:

**Graffe.** La `{` va alla fine della riga che apre il blocco, e `else`, `catch` e `finally` sulla riga della `}` che li precede: `} else {`. Il compilatore le accetta anche su una riga a sé, quindi il codice scritto nello stile di Allman compila, e `cromo fmt` le porta sulla riga sopra. Se la riga prima della `{` finisce con un commento, la `{` va prima del commento; se entrambe le righe hanno un commento in fondo, o tra di esse c'è una riga con solo un commento, restano dove sono.

**Rientro.** Ogni blocco aperto rientra di 4 spazi; le tabulazioni diventano spazi. Una riga che continua un'istruzione riceve un livello in più dell'istruzione, e ogni ulteriore riga che lascia aperta una parentesi tonda o quadra aggiunge un livello alle righe che la seguono, finché non si chiude. Una riga che inizia chiudendole torna al loro livello:

```cromo
i32 sum(i32 a, i32 b) {
    ret a + b
}

i32 main() {
    list<list<i32>> grid = [
        [1, 2],
        [3, 4],
    ]
    println("{} {}",
        grid.len(),
        sum(1,
            2))
    ret 0
}
```

```output
2 3
```

**Spazi.** Uno spazio attorno agli operatori binari, agli assegnamenti, a `?` e `:` dell'operatore condizionale e ai `:` delle interfacce, `struct Square : Shape`; dopo le virgole, dopo il `;` di un `for` e dopo le parole chiave, `if (`, `for (`. Nessuno spazio dentro le parentesi tonde e quadre, prima della `(` di una chiamata, attorno a `.`, dentro le parentesi angolari di un generico, `list<string>`, `map<string, i32>`, dopo `-` e `!` unari, e prima di `++` e `--`. Uno spazio prima di `{`, e dentro un blocco che resta su una riga: `{ total += x }`.

**Righe.** Al massimo una riga vuota di seguito, e nessuna all'inizio o alla fine di un blocco o del file. Nessuno spazio a fine riga, e il file termina con un solo a capo. Il file mantiene i suoi fine riga, LF o CRLF, e il suo BOM UTF-8 se ne ha uno.

**Commenti.** Il testo di un commento non cambia mai, `/* */` compresi. Un commento da solo sulla sua riga viene rientrato come il codice, a meno che non continui, nella stessa colonna, un commento a fine riga della riga sopra: allora resta in quella colonna. I commenti a fine riga di righe consecutive vengono allineati in una colonna, due spazi dopo la riga di codice più lunga del gruppo, come i campi di `Point` qui sopra; una riga vuota, o una riga senza un commento del genere, chiude il gruppo.

### Che cosa non cambia mai

- **Gli a capo dell'autore**, a parte quelli prima di `{`, `else`, `catch` e `finally`. Una riga lunga non viene spezzata, e le righe corte non vengono unite: `cromo fmt` sistema gli spazi e il rientro delle righe, anche di quelle che continuano un'istruzione, e lascia all'autore la scelta di dove andare a capo. Una modifica a una riga non sposta mai le righe che la circondano.
- **Dove finisce un'istruzione**, e quindi il significato del codice.
- **L'ordine** delle dichiarazioni, degli import e dei campi, e le righe vuote tra le dichiarazioni: toglie quelle in più, e non ne aggiunge dove non ce ne sono, come tra `}` e `i32 larger` qui sopra.
- **I blocchi su una riga**, come `{ ret a > b ? a : b }`.

### Una formattazione che non può cambiare il programma

`cromo fmt` cambia solo spazi e a capo, e lo verifica prima di scrivere un file: i token del risultato devono essere quelli del sorgente, a parte gli a capo prima di `{`, `else`, `catch` e `finally` che rimuove; il risultato non deve avere errori di sintassi; e formattarlo di nuovo non deve cambiare nulla. Se uno di questi controlli fallisce, il file non viene modificato, e `cromo fmt` scrive un errore interno del formattatore, che è un bug di `cromo` da segnalare. Per questo `cromo fmt` può girare a ogni salvataggio senza rischi.

## Visual Studio Code

L'estensione per Visual Studio Code colora il codice, e prende tutto il resto da `cromo lsp`, il language server: l'analisi è quella del compilatore, quindi l'editor e `cromo build` non sono mai in disaccordo.

### Installazione e impostazioni

Lo script che installa Cromo installa anche l'estensione, quando trova il comando `code` di Visual Studio Code, e lo stesso fa `cromo update` per ogni nuova versione. L'estensione si trova anche nell'archivio di ogni versione, come file `.vsix` nella sua cartella `extension`, e si può installare a mano con `code --install-extension` seguito dal percorso di quel file, oppure con *Install from VSIX* nel pannello delle estensioni.

L'estensione ha due impostazioni:

| Impostazione | Significato |
|---|---|
| `cromo.path` | l'eseguibile `cromo` che fa girare il language server. Se è vuota, come per impostazione predefinita: `build\cromo.exe` in una cartella del workspace, se c'è, altrimenti `cromo` dal `PATH` |
| `cromo.trace` | scrive ogni messaggio tra Visual Studio Code e `cromo lsp` nel pannello *Cromo* dell'output, per trovare un problema |

Se `cromo` non riesce a partire, un messaggio lo segnala, con un pulsante che apre l'impostazione. Per impostazione predefinita i file di Cromo vengono formattati al salvataggio: `editor.formatOnSave` si può disattivare per `[cromo]` nelle impostazioni.

### Che cosa offre

- **Colori** per parole chiave, tipi, numeri, stringhe con i loro segnaposto, `{}` e `{:.2f}`, commenti e attributi; `Ctrl+/` commenta le righe, le parentesi si chiudono da sole e la riga dopo `{` viene rientrata.
- **Errori mentre scrivi.** Circa 300 millisecondi dopo che smetti di scrivere, il server analizza il testo nell'editor, anche quando non è salvato, e sottolinea gli errori e gli avvisi, come `unreachable code`. Controlla soltanto il programma, senza generare codice, quindi è veloce. Ogni file del programma riceve i suoi errori, anche i file che non sono aperti.
- **Hover.** Il mouse sopra un nome mostra che cos'è: per una variabile il suo tipo e il suo nome; per una funzione o un metodo la sua firma, con `ref` e `move`; per un campo il suo tipo e la sua struct; per un tipo se è una struct, un enum o un'interfaccia; per un caso il suo enum; per un modulo, nel suo `import` o davanti a un nome, la sua documentazione. Sopra compare il nome del modulo in cui è dichiarato, quando è un altro, e sotto la documentazione della dichiarazione. Anche le funzioni integrate, come `println` e `assert`, e i tipi dei numeri hanno la loro documentazione. Dentro una funzione generica un valore di tipo `T` mostra `T`; in una chiamata, la funzione mostra i tipi di quella chiamata, come `i64 first<i64>(list<i64> items)`.
- **Vai alla definizione**, con F12 o Ctrl+clic: per i nomi del programma e dei moduli della libreria, come `fs.readText`, che aprono il loro sorgente in `lib/std`. I nomi del nucleo della libreria, come i metodi di `list` e `string`, e le funzioni integrate hanno un hover ma ancora nessuna definizione a cui andare.
- **Outline**: le struct, gli enum, le interfacce e le funzioni del file, con i loro campi e metodi, nella vista Outline e con Ctrl+Shift+O. Vengono dal solo file, quindi ci sono anche quando il programma ha errori.
- **Completamento dopo un punto.** Dopo il nome di una variabile, di un parametro o di un campo, anche alla fine di una catena come `a.b.`, propone i campi e i metodi del suo tipo che il modulo può usare, con il loro tipo o la loro firma e la loro documentazione, come `len`, `add` e `insert` dopo una `list`. Un metodo viene inserito con il suo nome, senza parentesi. Dopo il nome di un enum, come `Color.`, propone i suoi casi; dopo il nome di un modulo importato, come `fs.`, le sue funzioni e i suoi tipi pubblici. L'elenco si apre quando si scrive `.`, e Ctrl+Spazio lo riapre subito dopo un punto o dentro il nome che lo segue.
- **Aiuto sulla firma.** Dopo `(` e `,` in una chiamata, una finestra mostra la firma della funzione, con in evidenza il parametro che si sta scrivendo, e la sua documentazione: per una funzione, un costruttore, un metodo come `c.area(` e un caso con dati come `Shape.Circle(`.
- **Formattazione**, con le regole di `cromo fmt`, con Shift+Alt+F e quando il file viene salvato.

Mentre un file ha errori di sintassi, l'hover e la definizione usano l'ultima analisi senza errori. Il completamento dei membri di una variabile ha bisogno che il resto del file sia senza errori di sintassi: mentre un'altra riga è incompleta, dopo `p.` non propone nulla.

### Quale programma analizza

Un errore dipende dall'intero programma: una chiamata a una funzione di un modulo viene controllata rispetto alla sua dichiarazione. Per questo il server analizza ogni file aperto come parte di un programma:

1. il file stesso, se ha `main`;
2. altrimenti, il `main.cro` più vicino nella sua cartella o nelle cartelle sopra di essa, come farebbe `cromo build` senza un file; la ricerca si ferma alla cartella del workspace;
3. altrimenti, il primo eseguibile delle righe `bin` del `cromo.mod` del suo progetto, come in un progetto i cui programmi sono in `cmd/`;
4. altrimenti, il file da solo, come un modulo a cui non si chiede di avere `main`.

Gli import vengono risolti come fa il compilatore, a partire dalla cartella di `cromo.mod` quando c'è. Un modulo che il programma scelto non importa viene analizzato da solo: allora gli errori dell'editor sono quelli del modulo preso da solo, e il riferimento è `cromo build`. Il server legge i file del sistema su cui gira, come `fs_windows.cro` su Windows: gli errori di un file di un altro sistema, come `fs_linux.cro` su Windows, compaiono solo quando il programma viene compilato per quel sistema.

### Non ancora disponibile

I riferimenti, la ridenominazione, il completamento dei nomi che non seguono un punto, il completamento dopo una chiamata o un indice, come `f().` o `v[i].`, e l'aiuto sulla firma in quei punti.

### Altri editor

Qualsiasi editor che parli il Language Server Protocol, versione 3.17, può usare `cromo lsp`: avvia il comando e comunica con esso sullo standard input e output. Il server offre diagnostica, hover, definizione, i simboli di un documento, completamento, aiuto sulla firma e formattazione. Un editor senza language server può formattare con `cromo fmt -`.

## Commenti di documentazione

I commenti `//` subito sopra una dichiarazione, senza righe vuote in mezzo, sono la sua **documentazione**, come in Go: l'editor li mostra nell'hover, nel completamento e nell'aiuto sulla firma, e da essi sono generate le pagine della libreria sul sito di Cromo. Non c'è nulla di nuovo da imparare, e la libreria è documentata in questo modo.

Le regole:

- Documentano una funzione, un metodo, un costruttore, una struct, un enum, un'interfaccia, un campo o un caso.
- Tra il commento e la dichiarazione può stare un attributo, come `[[test]]`.
- Anche il commento a fine riga di un campo o di un caso lo documenta, insieme alle righe che lo continuano sotto, nella sua colonna. Se ci sono entrambi, viene prima il commento sopra.
- Solo i commenti `//` sono documentazione: un commento `/* */` non lo è mai.
- Ogni commento `//` subito sopra una dichiarazione è documentazione, anche una nota come `// TODO: make it faster`: lascia una riga vuota tra una nota e la dichiarazione, per tenere fuori la nota.
- I commenti all'inizio di un file, prima dei suoi import e delle sue dichiarazioni, documentano il **modulo**, se li segue una riga vuota o un `import`; se li segue subito una dichiarazione, documentano quella dichiarazione. L'editor mostra la documentazione di un modulo sul suo nome, nell'`import` e davanti ai suoi nomi.

Il testo è Markdown, senza il backtick:

- il codice dentro una frase va tra apici singoli, `'separator'`. Un apice apre dove non è preceduto da una lettera o una cifra, e chiude dove non è seguito da una lettera o una cifra, così `it's` e `don't` restano apostrofi, e un letterale carattere, `'/'`, appare come codice;
- un blocco di codice va tra due righe `'''`, e l'editor lo colora come Cromo;
- una riga di commento vuota separa due paragrafi;
- `-` inizia gli elementi di un elenco, `*text*` è enfasi e `[text](url)` un link;
- i parametri si nominano nel testo, come `'t'` qui sotto: non c'è `@param`.

Un modulo con la sua documentazione:

```cromo file=temperature.cro
// Temperatures in Celsius and Fahrenheit.
//
// Each value keeps its scale, and 'convert' changes it.

// A scale of temperature.
public enum Scale {
    Celsius     // water freezes at 0
    Fahrenheit  // water freezes at 32
}

// A temperature on its 'scale'.
public struct Temperature {
    public f64 degrees
    public Scale scale  // Celsius, unless set
}

// The temperature 't' on the scale 'to'. It doesn't change 't': it
// returns a *new* value.
//
// - from Celsius to Fahrenheit, times 1.8 plus 32;
// - from Fahrenheit to Celsius, the other way.
//
// '''
// Temperature boiling
// boiling.degrees = 100.0
// f64 f = convert(boiling, Scale.Fahrenheit).degrees   // 212
// '''
public Temperature convert(Temperature t, Scale to) {
    Temperature out
    out.scale = to
    if (t.scale == to) {
        out.degrees = t.degrees
    } else if (to == Scale.Fahrenheit) {
        out.degrees = t.degrees * 1.8 + 32.0
    } else {
        out.degrees = (t.degrees - 32.0) / 1.8
    }
    ret out
}
```

```cromo file=weather.cro
import temperature

i32 main() {
    Temperature boiling
    boiling.degrees = 100.0
    println("{}", convert(boiling, Scale.Fahrenheit).degrees)
    ret 0
}
```

```output
212
```

In `weather.cro` il mouse sopra `convert` mostra, sotto la sua firma e il nome del suo modulo, `temperature`:

```text
The temperature t on the scale to. It doesn't change t: it returns a new value.

  • from Celsius to Fahrenheit, times 1.8 plus 32;
  • from Fahrenheit to Celsius, the other way.

  Temperature boiling
  boiling.degrees = 100.0
  f64 f = convert(boiling, Scale.Fahrenheit).degrees   // 212
```

con `t`, `to` e il blocco come codice, e *new* in corsivo. Sopra `Fahrenheit` mostra `water freezes at 32`, e sopra `import temperature` il primo commento del file.

## Debug

Un programma compilato senza `--release` porta con sé le informazioni che servono a un debugger: le funzioni, le righe, le variabili e i loro tipi. Su Windows sono in un file PDB accanto all'eseguibile, `hello.pdb` accanto a `hello.exe`, che leggono Visual Studio, WinDbg e l'estensione C/C++ di Visual Studio Code; su Linux sono in formato DWARF, dentro l'eseguibile, per gdb e lldb. I file sono indicati con il loro percorso completo, quindi il debugger li trova da qualsiasi cartella. Un programma compilato con `--release` non le ha: mantiene i suoi stack trace, ma un debugger vede solo il codice macchina.

`cromo run` cancella l'eseguibile quando il programma termina: per fare il debug, compila il programma con `cromo build` ed esegui l'eseguibile nel debugger.

### Che cosa mostra il debugger

```cromo file=dbg.cro
struct Counter {
    i32 count
    string name

    void add(i32 n) {
        count += n
    }
}

i32 main() {
    Counter c
    c.name = "clicks"
    list<i32> v = [1, 2, 3]
    c.add(i32(v.len()))
    println("{} {}", c.name, c.count)
    ret 0
}
```

```output
clicks 3
```

- Le variabili e i parametri hanno i loro nomi, e le struct i loro campi.
- In un metodo, l'oggetto non ha un nome in Cromo, perché i suoi campi si usano direttamente: il debugger lo mostra come `this`, come in C++.
- Una `string` e una `list` mostrano i loro tre campi: `data`, l'indirizzo dei byte o degli elementi, `length` e `capacity`. Una vista del loro testo e dei loro elementi non è ancora disponibile.
- Nelle informazioni di debug le funzioni hanno davanti il loro modulo, anche quelle del file principale: `add` è `dbg.Counter.add`, che è il nome da dare a un breakpoint.

Con gdb, su Linux, una sessione sul programma qui sopra, compilato con `cromo build -O0 dbg.cro`, si ferma in `add` ed esamina i valori:

```text
(gdb) break 'dbg.Counter.add'
(gdb) run
(gdb) info args
this = 0x7fffffffe398
n = 3
(gdb) print *this
$1 = {count = 0, name = {data = 0x2002c0, length = 6, capacity = 0}}
(gdb) up
(gdb) print v
$2 = {data = 0x7ffff7bf9010, length = 3, capacity = 4}
```

### Codice ottimizzato

`cromo` ottimizza per impostazione predefinita, con `-O2`, anche le build di debug: il programma nel debugger è quello che gira davvero. Con le ottimizzazioni, il compilatore mette le funzioni piccole dentro quelle che le chiamano, tiene le variabili solo nei registri e toglie quelle che non gli servono: il debugger mostra allora alcune variabili come *optimized out*, salta da una riga all'altra, e non può fermarsi in una funzione che non esiste più da sola, come `add` qui sopra, che `-O2` mette dentro `main`. Per seguire un programma riga per riga, compilalo con `-O0`:

```sh
cromo build -O0 dbg.cro
```

`-O0` è più lento, e non cambia nient'altro: i controlli di overflow, degli indici e di esclusività sono gli stessi.

## Riepilogo

- `cromo fmt` formatta il codice con un solo stile e nessuna opzione: graffe sulla stessa riga, 4 spazi, uno spazio attorno agli operatori, commenti allineati. Mantiene gli a capo dell'autore, cambia solo spazi e a capo, e controlla che i token restino gli stessi.
- `cromo fmt --check` elenca i file da formattare e termina con 1; `cromo fmt -` formatta lo standard input per gli editor.
- L'estensione per Visual Studio Code usa `cromo lsp`: errori mentre scrivi, hover, definizione, outline, completamento dopo un punto, aiuto sulla firma e formattazione. `cromo.path` sceglie il `cromo` da eseguire.
- Il server analizza un file con il suo `main`, o con il `main.cro` più vicino sopra di esso, oppure da solo, con i file del sistema su cui gira.
- I commenti `//` subito sopra una dichiarazione, o a fine riga di un campo o di un caso, sono la sua documentazione, in Markdown con `'code'` e blocchi `'''`; il commento in cima a un file documenta il suo modulo.
- Senza `--release` un programma ha le informazioni di debug, PDB su Windows e DWARF su Linux; un debugger mostra `this` e i campi di stringhe e liste. Compila con `-O0` per seguirlo riga per riga.
