# File, processi e tempo

Questo capitolo è una panoramica dei moduli che dialogano con il sistema operativo: `std.fs` per file e cartelle, `std.os` per l'ambiente, `std.process` per gli altri programmi e `std.time` per orologi e date. Funzionano allo stesso modo su Windows e su Linux, e segnalano gli errori con le eccezioni.

## File

```cromo
import std.fs

i32 main() {
    fs.createFolders("data/logs")
    fs.writeText("data/notes.txt", "first line\n")
    fs.appendText("data/notes.txt", "second line\n")

    string text = fs.readText("data/notes.txt")
    for (string line in text.trimEnd().split("\n")) {
        println("> {}", line)
    }
    println("{} bytes", fs.size("data/notes.txt"))

    fs.copy("data/notes.txt", "data/copy.txt")
    fs.rename("data/copy.txt", "data/old.txt")
    println("{} {}", fs.isFile("data/old.txt"), fs.isFolder("data/logs"))

    fs.removeAll("data")
    println("{}", fs.exists("data"))
    ret 0
}
```

```output
> first line
> second line
23 bytes
true true
false
```

| Funzione | Cosa fa |
|---|---|
| `readText(path)`, `readBytes(path)` | l'intero contenuto, come `string` (verificata come UTF-8) o come byte |
| `writeText`, `writeBytes`, `appendText` | scrive un file, creandolo o sostituendolo, oppure aggiunge in fondo |
| `exists`, `isFile`, `isFolder`, `isLink` | dice che cosa è un percorso |
| `size`, `modified` | la dimensione in byte, e l'ora dell'ultima modifica in secondi dal 1970 |
| `copy`, `rename`, `remove` | copia, sposta o elimina un file |
| `createFolder`, `createFolders` | crea una cartella, oppure una cartella insieme a quelle che la contengono |
| `listFolder(path)` | i nomi contenuti in una cartella |
| `removeAll(path)` | elimina una cartella con tutto ciò che contiene, senza seguire i link |
| `joinPath`, `parent`, `fileName`, `extension` | le parti di un percorso |

Un file che non si può leggere o scrivere provoca un'eccezione, con un messaggio che dice quale file e perché:

```cromo
import std.fs

i32 main() {
    try {
        string text = fs.readText("missing.txt")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot read 'missing.txt': no such file or folder
```

## L'ambiente

`std.os` fornisce il nome del sistema, `"windows"` o `"linux"`, le variabili d'ambiente e la cartella corrente; `os.exit(code)` termina subito il programma. Gli argomenti del programma arrivano a `main`:

```cromo args=alpha,beta
import std.os

i32 main(list<string> args) {
    for (i64 i = 1; i < args.len(); i++) {
        println("argument {}: {}", i, args[i])
    }
    if (os.hasEnv("CROMO_GUIDE_EXAMPLE")) {
        println("{}", os.env("CROMO_GUIDE_EXAMPLE"))
    }
    ret 0
}
```

```output
argument 1: alpha
argument 2: beta
```

`args[0]` è il nome del programma, come in C.

## Altri programmi

`process.run` esegue un programma, ne attende la fine e restituisce il suo codice di uscita, il suo output e i suoi errori:

```cromo norun
import std.process

i32 main() {
    process.Output result = process.run("git", ["status", "--short"])
    if (result.code != 0) {
        eprintln("git failed: {}", result.err)
        ret 1
    }
    print("{}", result.out)
    ret 0
}
```

Il programma viene cercato nelle cartelle del `PATH`, e gli argomenti vengono passati così come sono, senza una shell che li interpreti. Un codice di uscita diverso da 0 è un valore da controllare; un programma che non riesce a partire provoca un'eccezione.

`process.start` avvia un programma senza attenderlo, e restituisce un `process.Process` con cui comunicare: `write` e `closeInput` per il suo standard input, `read` e `readAll` per il suo output, `wait` per attenderne la fine e `kill` per fermarlo.

## Il tempo

```cromo
import std.time

i32 main() {
    time.Instant start = time.now()
    time.msleep(50)
    time.Duration waited = start.elapsed()
    println("at least 50 ms: {}", waited.milliseconds() >= 50)

    time.DateTime now = time.utcNow()
    println("the year has four digits: {}", now.year >= 1000)
    ret 0
}
```

```output
at least 50 ms: true
the year has four digits: true
```

- `time.now()` legge un orologio che va solo in avanti, adatto alle misure; `elapsed()` e `since()` restituiscono una `Duration`, con `seconds()`, `milliseconds()`, `microseconds()` e il campo `nanoseconds`.
- `utcNow()` e `localNow()` restituiscono un `DateTime`, con i suoi campi dall'anno al nanosecondo, lo scostamento da UTC in minuti, e `text()` nel formato ISO 8601: `2026-10-30T18:21:33+01:00`.
- `sleep`, `msleep` e `usleep` mettono in pausa; in un task, nel frattempo il thread esegue altri task.

## Riepilogo

- `std.fs` legge e scrive file e cartelle; gli errori sono eccezioni con un messaggio chiaro.
- `std.os` fornisce l'ambiente; gli argomenti arrivano in `main(list<string> args)`.
- `std.process` esegue altri programmi, senza passare da una shell.
- `std.time` misura le durate e fornisce la data e l'ora.
