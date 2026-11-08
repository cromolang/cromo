# Moduli

Quando un programma cresce oltre un singolo file, il suo codice si divide in **moduli**. Cromo non ha file header né preprocessore: un file è un modulo, una cartella è un modulo, e `import` rende visibili i nomi pubblici di un modulo.

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

// La radice quadrata con il metodo di Newton, sufficiente per un esempio.
f64 root(f64 v) {
    f64 guess = v / 2.0
    for (i32 i = 0; i < 20; i++) {
        guess = (guess + v / guess) / 2.0
    }
    ret guess
}
```

Il programma lo importa con il suo nome:

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

`cromo build` o `cromo run` nella cartella compilano tutti insieme `main.cro`, i moduli che importa e, a loro volta, i moduli importati da questi.

## Visibilità

Tutto ciò che sta in un modulo è privato del modulo, a meno che non sia marcato `public`: le funzioni, le struct, e anche i campi, i metodi e i costruttori di una struct. Qui sopra, `Point` e `distance` sono pubblici; `square`, `root` e il campo `visits` no:

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

Ciò che una dichiarazione pubblica espone deve essere pubblico a sua volta: una funzione pubblica non può restituire una struct privata. Il distruttore non ha `public`: viene eseguito ovunque l'oggetto finisca la sua vita.

## Usare i nomi

Dopo `import geometry`, i suoi nomi pubblici si usano direttamente: `distance(a, b)`, `Point p`. Il nome del modulo davanti, `geometry.distance(a, b)`, serve solo quando due moduli importati esportano lo stesso nome, oppure per rendere il codice più chiaro. Un nome viene cercato prima nel modulo stesso, poi nei moduli importati e infine nella libreria standard: così un nome aggiunto alla libreria non rompe mai un programma esistente.

Gli import si scrivono all'inizio del file, e ogni file scrive i propri: l'importazione non è transitiva. Due moduli possono importarsi a vicenda.

Il commento all'inizio di un file, prima degli import e separato da ciò che segue da una riga vuota, documenta il modulo: VS Code lo mostra sul nome del modulo, nel suo `import` e in `geometry.distance`, e dopo `geometry.` propone i nomi pubblici del modulo con la loro documentazione.

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
// Privata: la vedono solo i file della cartella.
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

I file di una cartella sono parti di un unico modulo, come i file di un package in Go: ognuno vede tutti i nomi degli altri, anche quelli privati, come `circle.cro` usa `pi` di `constants.cro`, e due di loro non possono dichiarare lo stesso nome. Ogni file scrive i propri import. Fuori dalla cartella si vedono solo i nomi pubblici, con il nome della cartella davanti quando serve: `shapes.squareArea`.

`import a.b` indica il file `a/b.cro`, oppure la cartella `a/b`; le sottocartelle di una cartella sono moduli a sé. I percorsi partono dalla cartella del file principale, o da quella di `cromo.mod` in un progetto, così un modulo ha lo stesso nome ovunque venga importato. Un file di un modulo-cartella non si importa da solo: `import shapes.circle` in un programma che importa `shapes` è un errore.

## Un progetto con più programmi

Un progetto che produce più eseguibili, come un server, un client e uno strumento, li tiene in cartelle proprie e le librerie in cima. Il file `cromo.mod` segna la cima del progetto: la prima riga dà il nome del progetto, e una riga `bin` per ogni eseguibile ne dà il nome e il file principale.

```text
progetto/
  cromo.mod
  shapes/            una libreria: un modulo-cartella
  geometry.cro       un'altra libreria: un modulo-file
  cmd/
    areas.cro        un programma, con main
    report.cro       un altro programma
```

```text file=cromo.mod
module example.com/progetto

bin areas cmd/areas.cro
bin report cmd/report.cro
```

Gli import di ogni file partono dalla cartella di `cromo.mod`, che `cromo` cerca nella cartella del file principale e in quelle sopra. Così `cmd/areas.cro` importa `shapes` come faceva `areas.cro`:

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

Nella cartella di `cromo.mod`, `cromo build` senza file costruisce tutti gli eseguibili delle sue righe `bin`, ognuno con il nome della sua riga: `areas.exe` e `report.exe` su Windows, `areas` e `report` su Linux. `cromo build areas` ne costruisce solo uno, `cromo run areas` lo esegue, e `cromo test` esegue i test di ciascuno.

Una libreria di un progetto è un modulo, compilato in ogni eseguibile che la importa, come in Go e in Rust: non c'è una libreria da costruire a parte e da collegare, e un eseguibile tiene solo le funzioni che chiama.

## La libreria standard

Il nucleo della libreria, con `list`, `map`, `string`, `format` ed `Exception`, è visibile ovunque. Gli altri moduli si importano con il prefisso `std`:

| Modulo | Contenuto | Capitolo |
|---|---|---|
| `std.fs` | file e cartelle | 14 |
| `std.os` | l'ambiente del processo | 14 |
| `std.process` | eseguire altri programmi | 14 |
| `std.zip`, `std.tar` | leggere gli archivi | 14 |
| `std.time` | orologi, date, pause | 14 |
| `std.thread` | thread, task, mutex, canali, atomici | 13 |
| `std.net` | TCP, UDP, TLS | 15 |
| `std.http` | client e server HTTP | 15 |
| `std.websocket` | client e server WebSocket | 15 |
| `std.json` | JSON | 16 |
| `std.crypto` | hash, byte casuali, cifratura, firme | 15 |
| `std.jwt` | JSON Web Token | 15 |
| `std.gzip` | compressione | 15 |

Dopo `import std.fs`, le funzioni del modulo si scrivono precedute dall'ultima parte del suo nome: `fs.readText(path)`.

## Codice per ogni sistema

Un modulo può avere parti scritte per un solo sistema: accanto a `files.cro`, i file `files_windows.cro` e `files_linux.cro`. Viene usato solo il file del sistema per cui il programma è compilato, e fa parte del modulo, con accesso ai suoi nomi privati. La libreria standard sfrutta questo meccanismo per chiamare il sistema di Windows e quello di Linux dietro le stesse funzioni.

Allo stesso modo una parte può essere per una sola architettura, `files_arm64.cro` o `files_x86_64.cro`, o per un sistema e un'architettura, `files_linux_arm64.cro`.

## Pacchetti

Le librerie scritte da altri saranno **pacchetti**: repository Git, identificati dal loro URL, con le versioni date dai loro tag, elencati in un file `cromo.mod` e scaricati da `cromo`. Il loro design è deciso ed è descritto in `docs/language.md`, ma non sono ancora disponibili.

## Riepilogo

- Un file è un modulo; una cartella è un unico modulo formato dai suoi file, che condividono tutti i nomi; `import` rende visibili i loro nomi pubblici.
- `cromo.mod` segna la cima di un progetto: gli import partono dalla sua cartella, e le sue righe `bin` sono gli eseguibili che `cromo build` costruisce.
- Tutto è privato, a meno che non sia `public`: anche i campi e i metodi di una struct.
- I nomi di un modulo importato si usano direttamente, con il modulo davanti quando due nomi entrano in conflitto.
- `std.x` sono i moduli della libreria standard; i file `_windows` e `_linux` contengono il codice per un solo sistema.
