# Moduli e progetti

Quando un programma cresce oltre un singolo file, il suo codice si divide in **moduli**. Cromo non ha file header né preprocessore: un file è un modulo, una cartella è un modulo, e `import` rende visibili i nomi pubblici di un modulo. Un **progetto** con più programmi li elenca in un file `cromo.mod`.

## Un file è un modulo

Accanto a `main.cro`, un file `geometry.cro`:

```cromo file=geometry.cro
public struct Point {
    public f64 x
    public f64 y
    i32 visits
}

public f64 distance(Point a, Point b) {
    ret root(square(a.x - b.x) + square(a.y - b.y))
}

f64 square(f64 v) {
    ret v * v
}

// The square root with Newton's method, enough for an example.
f64 root(f64 v) {
    f64 guess = v / 2.0
    for (i32 i = 0; i < 20; i++) {
        guess = (guess + v / guess) / 2.0
    }
    ret guess
}
```

Il programma lo importa con il suo nome, il nome del file senza `.cro`:

```cromo file=main.cro
import geometry

i32 main() {
    Point a
    Point b
    b.x = 3.0
    b.y = 4.0
    println("{}", distance(a, b))
    ret 0
}
```

```output
5
```

`cromo build main.cro` o `cromo run main.cro` compilano tutti insieme, in un unico eseguibile, `main.cro`, i moduli che importa e, a loro volta, i moduli importati da questi. Non c'è nulla da compilare a parte e nulla da collegare: un modulo viene compilato in ogni programma che lo importa, e l'eseguibile tiene solo le funzioni che chiama.

Anche il file principale è un modulo, con il nome del suo file: qui `main`. In una traccia dello stack le funzioni degli altri moduli hanno davanti il loro modulo, come `geometry.square`, e quelle del file principale solo il loro nome.

I commenti all'inizio di un modulo, separati da ciò che segue da una riga vuota, lo documentano: l'editor li mostra sul nome del modulo, nel suo `import` e in `geometry.distance`. [Strumenti: formattazione, editor e debugger](tools.md) spiega i commenti di documentazione.

## Visibilità

Tutto ciò che sta in un modulo è privato del modulo, a meno che non sia marcato `public`: le funzioni, le struct, gli enum e le interfacce, e anche i campi, i metodi e il costruttore di una struct. Qui sopra, `Point`, i suoi campi `x` e `y`, e `distance` sono pubblici; `square`, `root` e il campo `visits` no:

```cromo error
import geometry

i32 main() {
    println("{}", square(2.0))
    Point p
    println("{}", p.visits)
    ret 0
}
```

```output
error: 'square' of geometry is private to its module: it is not marked public
error: field 'visits' of Point is private to its module: it is not marked public
```

Dentro il suo modulo un nome privato si usa liberamente: `distance` chiama `root` e `square`. Un campo privato di una struct pubblica fa comunque parte di ogni suo valore, con il suo valore predefinito: gli altri moduli semplicemente non possono nominarlo.

Alcune dichiarazioni sono pubbliche insieme a ciò che le contiene, e non prendono `public`:

- i **casi** di un enum, con il loro enum;
- i **metodi** di un'interfaccia, con la loro interfaccia;
- il metodo di una struct che **implementa un'interfaccia**: si può comunque chiamare attraverso l'interfaccia, quindi in un altro modulo è visibile quanto la struct;
- il **distruttore**: viene eseguito ovunque l'oggetto finisca.

Il **costruttore**, invece, è privato a meno che non sia marcato `public`, come un metodo: solo il suo modulo può chiamarlo, e il modulo può offrire una funzione pubblica che lo fa. Gli altri moduli possono comunque dichiarare una variabile della struct, `Ticket t`, che ha il suo valore predefinito. Un modulo `figures.cro` mostra tutti questi casi:

```cromo file=figures.cro
public enum Unit {
    Meter
    Foot
}

public interface Shape {
    f64 area() const
}

public struct Square : Shape {
    public f64 side
    public Unit unit

    f64 area() const {
        ret side * side
    }
}

public struct Ticket {
    i64 number

    Ticket(i64 n) {
        number = n
    }

    public i64 value() const {
        ret number
    }
}

public Ticket nextTicket() {
    ret Ticket(42)
}
```

```cromo
import figures

void show(Shape s) {
    println("area {}", s.area())
}

i32 main() {
    Square q
    q.side = 2.0
    q.unit = Unit.Foot
    show(q)
    println("{} {}", q.area(), q.unit == Unit.Foot)
    println("ticket {}", nextTicket().value())
    ret 0
}
```

```output
area 4
4 true
ticket 42
```

Il costruttore di `Ticket` non è pubblico, quindi solo `nextTicket` crea i biglietti:

```cromo error
import figures

i32 main() {
    Ticket t = Ticket(7)
    ret 0
}
```

```output
error: the constructor of Ticket is private to its module: it is not marked public
```

Scrivere `public` dove non va è un errore che spiega il perché: `the cases of an enum are public with it`, `the methods of an interface are public with it`, `a destructor has no 'public': it runs wherever its object ends`. Le costanti di un modulo, come `const i64 limit = 1024`, sono sempre private: `a constant of a module has no attributes and is not public: only its module sees it`.

Ciò che una dichiarazione pubblica espone deve essere pubblico a sua volta: una funzione pubblica non può prendere né restituire una struct privata, e un campo o un metodo pubblico non può avere un tipo privato, perché un altro modulo non potrebbe scrivere quel tipo:

```cromo error
struct Order {
    i64 id
}

public Order newest() {
    Order o
    ret o
}

i32 main() {
    ret 0
}
```

```output
error: function 'newest' is public, but its type Order is private to the module
```

## Gli import

Gli import si scrivono all'inizio del file, prima delle dichiarazioni, un modulo per ogni `import`:

```cromo error
i32 main() {
    ret 0
}

import geometry
```

```output
error: the imports come before the declarations of the module
```

Un import non è mai pubblico, e l'importazione non è transitiva: ogni file vede solo i moduli che importa lui stesso, così gli import all'inizio di un file dicono tutto ciò che usa. Due moduli possono importarsi a vicenda: il programma viene compilato tutto insieme, quindi non c'è un ordine da rispettare. Qui due moduli si chiamano a vicenda:

```cromo file=even.cro
import odd

public bool isEven(i32 n) {
    if (n == 0) {
        ret true
    }
    ret isOdd(n - 1)
}
```

```cromo file=odd.cro
import even

public bool isOdd(i32 n) {
    if (n == 0) {
        ret false
    }
    ret isEven(n - 1)
}
```

```cromo
import even

i32 main() {
    println("{} {}", isEven(10), isEven(7))
    ret 0
}
```

```output
true false
```

Il programma importa solo `even`, quindi `isOdd` non è visibile al suo interno, anche se `even` importa `odd`:

```cromo error
import even

i32 main() {
    println("{}", isOdd(3))
    ret 0
}
```

```output
error: unknown function 'isOdd'
```

`public import` è un errore: `an import is not public: each module writes its own`.

## Usare i nomi

Dopo `import geometry`, i nomi pubblici del modulo si usano direttamente: `distance(a, b)`, `Point p`. Il nome del modulo si può scrivere davanti, `geometry.distance(a, b)`, `geometry.Point p`, `figures.Unit.Foot`: serve solo quando due moduli importati danno lo stesso nome, e può rendere il codice più chiaro.

Il nome davanti è l'ultimo nome del percorso dell'import: dopo `import tools.geometry` è `geometry.distance`, mai `tools.geometry.distance`. Un nome viene cercato in tre posti, nell'ordine:

1. il modulo stesso;
2. i moduli che il file importa;
3. il nucleo della libreria standard, che ogni modulo vede.

Così un nome del modulo nasconde lo stesso nome di un import, e un nome aggiunto al nucleo della libreria non rompe mai un programma che già lo usa. Due moduli che danno lo stesso nome a un file che li importa entrambi sono un errore solo dove il nome viene usato senza il modulo davanti. Con due moduli `disk.cro` e `tile.cro`:

```cromo file=disk.cro
public f64 area(f64 radius) {
    ret 3.14159 * radius * radius
}
```

```cromo file=tile.cro
public f64 area(f64 side) {
    ret side * side
}
```

```cromo error
import disk
import tile

i32 main() {
    println("{}", area(1.0))
    ret 0
}
```

```output
error: 'area' is in both disk and tile: write disk.area or tile.area
```

```cromo
import disk
import tile

// A name of the module comes before those of the imports.
string join(list<string> parts, string separator) {
    ret "my own join"
}

i32 main() {
    println("{} {}", disk.area(1.0), tile.area(2.0))
    println("{}", join(["a", "b"], ","))
    ret 0
}
```

```output
3.14159 4
my own join
```

Poiché l'ultimo nome di un modulo si scrive davanti ai suoi nomi, due import con lo stesso ultimo nome nello stesso file sono un errore, e lo è anche una variabile o un parametro con il nome di un modulo importato. Cromo non ha un modo per rinominare un import: rinomina uno dei moduli.

```cromo file=plot/geometry.cro
public f64 scale() {
    ret 2.0
}
```

```cromo error
import geometry
import plot.geometry

i32 main() {
    ret 0
}
```

```output
error: another import is also called geometry
```

```cromo error
import geometry

i32 main() {
    i32 geometry = 3
    ret geometry
}
```

```output
error: 'geometry' is the name of an imported module
```

## Una cartella è un modulo

`import shapes` importa la cartella `shapes`, con tutti i suoi file `.cro`, come un unico modulo:

```cromo file=shapes/circle.cro
public f64 circleArea(f64 r) {
    ret pi() * r * r
}
```

```cromo file=shapes/square.cro
public f64 squareArea(f64 s) {
    ret s * s
}
```

```cromo file=shapes/constants.cro
// Private: only the files of the folder see it.
f64 pi() {
    ret 3.14159
}
```

```cromo file=areas.cro
import shapes

i32 main() {
    println("{:.2f} {}", circleArea(1.0), shapes.squareArea(2.0))
    ret 0
}
```

```output
3.14 4
```

I file di una cartella sono parti di un unico modulo, come i file di un package in Go:

- ogni file vede tutti i nomi degli altri, anche quelli privati, come `circle.cro` usa `pi` di `constants.cro`;
- due file non possono dichiarare lo stesso nome: è l'errore di un nome dichiarato due volte, come `function 'pi' is already defined`;
- ogni file scrive i propri import, che gli altri file della cartella non vedono;
- fuori dalla cartella si vedono solo i nomi pubblici, con il nome della cartella davanti quando serve: `shapes.squareArea`.

Così un modulo si può dividere in più file man mano che cresce, e i programmi che lo importano non cambiano. Le sottocartelle di una cartella sono moduli a sé, importati con il loro percorso: `import shapes.solid` è la cartella `shapes/solid`, i cui nomi si scrivono `solid.volume` quando serve, e `import shapes` non la include.

Un file di un modulo-cartella appartiene al modulo: importare il file oltre alla sua cartella, in qualsiasi file del programma, è un errore al secondo import. Importato da solo, `import shapes.circle` renderebbe il file un modulo a sé, senza i nomi degli altri file della sua cartella: importa la cartella.

```cromo error
import shapes
import shapes.circle

i32 main() {
    ret 0
}
```

```output
error: the file shapes/circle.cro is a part of the module shapes: import shapes
```

Anche una cartella senza file `.cro`, o solo con file di altri sistemi, è un errore: `the folder empty has no .cro files for this system`.

## Dove si trovano i moduli

`import a.b` indica il file `a/b.cro`, oppure la cartella `a/b`. I percorsi partono dalla cartella del file principale, o dalla cartella di `cromo.mod` in un progetto (più avanti): così un modulo ha lo stesso nome ovunque venga importato, anche da un file in una sottocartella. Un file e una cartella con lo stesso nome, come `geometry.cro` e `geometry/`, sono un errore all'import: `both geometry.cro and the folder geometry exist: rename one of them`.

Un modulo che non c'è è un errore che dice dove è stato cercato:

```cromo error
import charts

i32 main() {
    ret 0
}
```

```output
error: cannot find the module charts: there is no charts.cro or folder charts next to
```

Il messaggio termina con `next to` seguito dal file principale, come `next to main.cro`, oppure con `next to cromo.mod` in un progetto.

## Codice per ogni sistema

Un modulo può avere parti scritte per un solo sistema, in file il cui nome termina con il nome del sistema: accanto a `platform.cro`, i file `platform_windows.cro` e `platform_linux.cro`. Viene usato solo il file del sistema per cui il programma è compilato, e fa parte del modulo, con accesso ai suoi nomi privati. Allo stesso modo una parte può essere per una sola architettura, `platform_x86_64.cro` o `platform_arm64.cro`, o per un sistema e un'architettura, `platform_linux_arm64.cro`.

```cromo file=platform.cro
// What the program runs on.
public string describe() {
    ret format("{} on {}", system(), architecture())
}

string prefix() {
    ret "the system"
}
```

```cromo file=platform_windows.cro
string system() {
    ret prefix() + " Windows"
}
```

```cromo file=platform_linux.cro
string system() {
    ret prefix() + " Linux"
}
```

```cromo file=platform_x86_64.cro
string architecture() {
    ret "x86-64"
}
```

```cromo file=platform_arm64.cro
string architecture() {
    ret "ARM64"
}
```

```cromo nocheck
import platform

i32 main() {
    println("{}", platform.describe())
    ret 0
}
```

```output
the system Windows on x86-64
```

Compilato con `--target linux` lo stesso programma stampa `the system Linux on x86-64`, e con `--target linux-arm64` stampa `the system Linux on ARM64`. Ogni versione di `system` esiste solo per il suo sistema, quindi le due non entrano mai in conflitto, e un file di un altro sistema non viene nemmeno letto: può chiamare funzioni che esistono solo lì.

In un modulo-cartella la stessa regola vale per ogni file: i file di un altro sistema o di un'altra architettura restano fuori, quelli del sistema e dell'architettura della build entrano nel modulo. Un file `network_linux.cro` in una cartella fa parte del modulo solo su Linux, anche quando non c'è un `network.cro`. Il file principale di un programma non ha parti del genere: il codice di un sistema va in un modulo.

La libreria standard sfrutta questo meccanismo per chiamare Windows e Linux dietro le stesse funzioni: `std.fs` è `fs.cro`, `fs_windows.cro`, `fs_linux.cro` e i file di ogni architettura.

## La libreria standard

Il nucleo della libreria, con `list`, `map`, `string`, `format`, `Exception`, `Error`, `join`, `sort` e le funzioni built-in come `println`, è visibile in ogni modulo senza import. Gli altri moduli si importano con il prefisso `std`:

| Modulo | Contenuto | Capitolo |
|---|---|---|
| `std.fs` | file e cartelle | [File, processi e tempo](system.md) |
| `std.os` | l'ambiente del processo | [File, processi e tempo](system.md) |
| `std.cmdline` | le opzioni della riga di comando | [File, processi e tempo](system.md) |
| `std.process` | eseguire altri programmi | [File, processi e tempo](system.md) |
| `std.zip`, `std.tar` | leggere gli archivi | [File, processi e tempo](system.md) |
| `std.time` | orologi, date, pause | [File, processi e tempo](system.md) |
| `std.thread` | thread, task, mutex, canali, atomici | [Thread, task e canali](concurrency.md) |
| `std.net` | TCP, UDP, TLS | [Rete e HTTP](networking.md) |
| `std.http` | client e server HTTP | [Rete e HTTP](networking.md) |
| `std.websocket` | client e server WebSocket | [Rete e HTTP](networking.md) |
| `std.crypto` | hash, byte casuali, cifratura, firme | [Rete e HTTP](networking.md) |
| `std.jwt` | JSON Web Token | [Rete e HTTP](networking.md) |
| `std.gzip` | compressione | [Rete e HTTP](networking.md) |
| `std.json` | JSON | [JSON](json.md) |
| `std.gui` | finestre, componenti e app per Android | [Finestre e app](gui.md) |

I moduli della libreria seguono le regole degli altri: dopo `import std.fs`, i suoi nomi si trovano anche senza il modulo davanti. La guida e la libreria li scrivono comunque con il loro ultimo nome davanti, `fs.readText(path)`, `time.now()`, perché senza di esso un nome come `readText` o `now` dice meno.

Un modulo che la libreria non ha è un errore che elenca quelli che ha:

```cromo error
import std.io

i32 main() {
    ret 0
}
```

```output
error: the standard library has no module std.io: it has std.cmdline, std.crypto, std.fs,
```

## Progetti: cromo.mod

Un programma con pochi moduli non ha bisogno d'altro: il suo file principale e i moduli che gli stanno accanto. Un progetto che produce più eseguibili, come un server, un client e uno strumento, tiene i loro file principali in una cartella a sé e i moduli che condividono in cima. Il file `cromo.mod` segna la cima del progetto ed elenca i suoi eseguibili:

```text
progetto/
  cromo.mod
  geometry.cro       un modulo-file
  shapes/            un modulo-cartella
  cmd/
    areas.cro        un programma, con main
    report.cro       un altro programma
```

```text file=cromo.mod
// The executables of the project.
module example.com/project

bin areas cmd/areas.cro
bin report cmd/report.cro
```

`cromo.mod` ha una cosa per riga, con le parole separate da spazi, e righe vuote e commenti `//` dove servono:

- `module name` è la prima riga, una sola volta: il nome del progetto, fatto di lettere, cifre e `.`, `-`, `_` e `/`, come `example.com/project`. Darà il nome al progetto come pacchetto; per ora ne segna la cima.
- `bin name path` è un eseguibile: il suo nome, fatto di lettere, cifre, `-` e `_`, diverso per ogni riga, e il suo file principale, un file `.cro` scritto a partire dalla cartella di `cromo.mod`, con `/`.

### La radice degli import

`cromo` cerca `cromo.mod` nella cartella del file principale e nelle cartelle sopra di essa, fino alla radice del disco, e si ferma al primo che trova. Gli import di ogni file del programma partono allora dalla cartella di `cromo.mod`, non da quella del file principale. Così `cmd/areas.cro` importa `shapes`, in cima al progetto, come faceva `areas.cro` che gli stava accanto:

```cromo file=cmd/areas.cro
import shapes

i32 main() {
    println("{:.2f}", circleArea(2.0))
    ret 0
}
```

```output
12.57
```

Un modulo accanto a `cmd/areas.cro`, come `cmd/units.cro`, si importa con `import cmd.units`: in un progetto ogni modulo ha un solo nome, a partire dalla cima. Senza `cromo.mod` i percorsi partono dalla cartella del file principale, come nelle prime sezioni di questo capitolo.

### Costruire, eseguire e testare un progetto

Nella cartella di `cromo.mod`, i comandi prendono gli eseguibili delle sue righe `bin`:

| Comando | Cosa fa |
|---|---|
| `cromo build` | costruisce ogni eseguibile, uno dopo l'altro, con le stesse opzioni, ognuno con il nome della sua riga: `areas.exe` e `report.exe` su Windows, `areas` e `report` su Linux, nella cartella di `cromo.mod`; si ferma al primo che fallisce |
| `cromo build areas` | costruisce solo `areas` |
| `cromo run areas -- x y` | costruisce `areas` in una cartella temporanea e lo esegue con gli argomenti dopo `--` |
| `cromo run` | esegue l'unico eseguibile di `cromo.mod`; se ce n'è più d'uno, è un errore che li elenca |
| `cromo test` | esegue a turno i test di ogni eseguibile, dopo una riga con il suo nome e il suo file principale; il codice di uscita è 1 se fallisce un test di uno qualsiasi di essi |

Un nome senza estensione che non è un file, come `areas`, è un eseguibile di `cromo.mod`. Con `--target android` ogni eseguibile è un APK, `areas.apk`. Un `-o` con `cromo build` senza un nome è un errore, perché ogni eseguibile ha il nome della sua riga; con un nome, `cromo build areas -o tool.exe`, dà il nome a quell'eseguibile. Dopo `cromo test` i nomi scelgono i test, come per un programma singolo ([Test](tests.md)), in ogni eseguibile.

```text
> cromo run
cromo: cromo.mod has the executables areas and report: write which one, as cromo run areas
> cromo build nothing
cromo: there is no file nothing, and cromo.mod has the executables areas and report
> cromo build -o all.exe
cromo: -o names one executable, and those of cromo.mod are named after their lines
```

Senza righe `bin`, `cromo build`, `cromo run` e `cromo test` senza un file usano il `main.cro` della cartella corrente, come in una cartella senza `cromo.mod`. Un file si può sempre indicare, come `cromo build cmd/areas.cro`, che scrive `cmd/areas.exe` accanto a esso: in quel caso `cromo` usa la cartella di `cromo.mod` solo per gli import, e non legge le sue righe `bin`.

### Gli errori di cromo.mod

Quando `cromo` legge gli eseguibili, ogni riga sbagliata di `cromo.mod` è un errore con il suo numero, e non viene costruito nulla. Per questo `cromo.mod`:

```text
module example.com/project
module again
bin areas cmd/areas.cro
bin areas cmd/report.cro
bin tool cmd/tool.cro
bin broken
require github.com/someone/lib v1.2.0
library shapes
```

`cromo build` scrive:

```text
cromo.mod:2: error: the line module comes first, once
cromo.mod:4: error: another executable is called areas
cromo.mod:5: error: there is no file cmd/tool.cro
cromo.mod:6: error: bin takes the name of an executable and its main file, as bin server cmd/server.cro
cromo.mod:7: error: the packages are not available yet: require comes later
cromo.mod:8: error: unknown line library: cromo.mod has the lines module and bin
```

Un `cromo.mod` che non inizia con `module`, o che è vuoto, dà `cromo.mod:1: error: cromo.mod starts with the line module, with the name of the project, as module example.com/app`.

### Librerie

Una libreria di un progetto è un modulo, di un file o di una cartella, che i suoi eseguibili importano: `geometry` e `shapes` qui sopra. Come in Go e in Rust, viene compilata in ogni eseguibile che la importa, quindi non c'è una libreria da costruire a parte, nessun file da collegare e niente da installare accanto agli eseguibili. Una libreria per programmi scritti in C, una DLL o una `.so`, non è ancora disponibile.

## Pacchetti

Le librerie scritte da altri saranno **pacchetti**: repository Git identificati dal loro URL, con le versioni date dai loro tag, elencati in `cromo.mod` con righe `require` e scaricati da `cromo`. Non sono ancora disponibili: oggi una riga `require` è un errore che lo dice. Fino ad allora, una libreria di un altro progetto si può copiare nel progetto come cartella, e importare come qualsiasi modulo.

## Riepilogo

- Un file è un modulo; una cartella è un unico modulo formato dai suoi file, che condividono tutti i loro nomi, anche quelli privati, mentre ognuno scrive i propri import; le sue sottocartelle sono moduli a sé.
- Tutto è privato, a meno che non sia `public`: funzioni, tipi, campi, metodi e costruttori. I casi degli enum, i metodi delle interfacce, i metodi che implementano un'interfaccia e i distruttori sono pubblici con il loro tipo; le costanti di un modulo sono sempre private.
- Una dichiarazione pubblica non può esporre un tipo privato.
- Gli import vengono per primi in un file, non sono pubblici e non sono transitivi; i moduli possono importarsi a vicenda.
- I nomi importati si usano direttamente; il modulo va davanti, con l'ultimo nome del suo percorso, quando due import danno lo stesso nome. Un nome viene cercato nel modulo, poi nei suoi import, poi nel nucleo della libreria.
- `import a.b` è `a/b.cro` o la cartella `a/b`, a partire dalla cartella del file principale o di `cromo.mod`.
- I file che terminano con `_windows`, `_linux`, `_x86_64` e `_arm64` sono le parti di un modulo per un solo sistema o una sola architettura.
- `std.x` sono i moduli della libreria; il suo nucleo è visibile ovunque.
- `cromo.mod` segna la cima di un progetto: gli import partono dalla sua cartella, e le sue righe `bin` sono gli eseguibili che prendono `cromo build`, `cromo run name` e `cromo test`. I pacchetti verranno più avanti.
