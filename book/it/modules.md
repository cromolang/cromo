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

## Una cartella è un modulo

`import shapes` importa la cartella `shapes`, con tutti i suoi file `.cro`, come un unico modulo:

```cromo file=shapes/circle.cro
public f64 circleArea(f64 r) {
    ret 3.14159 * r * r
}
```

```cromo file=shapes/square.cro
public f64 squareArea(f64 s) {
    ret s * s
}
```

```cromo file=areas.cro
import shapes

i32 main() {
    println("{:.2f} {}", circleArea(1.0), squareArea(2.0))
    ret 0
}
```

```output
3.14 4
```

`import a.b` indica il file `a/b.cro`, oppure la cartella `a/b`. I percorsi partono dalla cartella del file principale, così un modulo ha lo stesso nome ovunque venga importato. I file di una cartella condividono i loro nomi privati, come parti di un unico modulo; le sottocartelle sono moduli a sé.

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

## Pacchetti

Le librerie scritte da altri saranno **pacchetti**: repository Git, identificati dal loro URL, con le versioni date dai loro tag, elencati in un file `cromo.mod` e scaricati da `cromo`. Il loro design è deciso ed è descritto in `docs/language.md`, ma non sono ancora disponibili.

## Riepilogo

- Un file è un modulo; una cartella è un modulo formato dai suoi file; `import` rende visibili i loro nomi pubblici.
- Tutto è privato, a meno che non sia `public`: anche i campi e i metodi di una struct.
- I nomi di un modulo importato si usano direttamente, con il modulo davanti quando due nomi entrano in conflitto.
- `std.x` sono i moduli della libreria standard; i file `_windows` e `_linux` contengono il codice per un solo sistema.
