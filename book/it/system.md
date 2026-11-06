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

`os.readLine(ref line)` legge lo standard input una riga alla volta, ed è false alla fine; `os.readInput()` lo legge tutto, come fa un filtro:

```cromo ignore
import std.os

i32 main() {
    string line
    for (os.readLine(ref line)) {
        println("> {}", line)
    }
    ret 0
}
```

## La riga di comando

`std.cmdline` legge le opzioni di un programma da quegli argomenti. Ogni opzione ha un nome lungo, uno breve di una lettera (`' '` quando non c'è) e una descrizione; un'opzione con un valore dice anche se è obbligatoria, e il suo valore predefinito:

```cromo args=-v,--port=9000,notes.txt
import std.cmdline

i32 main(list<string> args) {
    cmdline.CmdLine cmd
    cmd.flag("verbose", 'v', "print more")
    cmd.addInt("port", 'p', "port to listen on", false, 8080)
    cmd.addString("format", 'f', "output format", false, "text")
    cmd.choices("format", ["text", "json"])
    cmd.footer("files...")
    cmd.parse(args)

    println("port {}, format {}", cmd.getInt("port"), cmd.getString("format"))
    if (cmd.exists("verbose")) {
        println("files: {}", join(cmd.rest(), ", "))
    }
    ret 0
}
```

```output
port 9000, format text
files: notes.txt
```

Le opzioni si possono scrivere `--port 9000`, `--port=9000`, `-p 9000` o `-p9000`, e i flag insieme, `-vq`; dopo `--` ogni argomento è un argomento, anche quando comincia con `-`. Gli altri argomenti, come `notes.txt`, sono in `rest()`. `getInt`, `getFloat` e `getString` danno il valore di un'opzione, o quello predefinito; `exists` dice se è stata data.

`--help` stampa l'aiuto, costruito dalle opzioni, e termina il programma:

```text
usage: server [options] files...
options:
  -v, --verbose          print more
  -p, --port <int>       port to listen on (default 8080)
  -f, --format <string>  output format (text, json; default text)
  -h, --help             print this help
```

Una riga di comando sbagliata, come un'opzione sconosciuta o una porta che non è un numero, stampa l'errore e l'aiuto sullo standard error, e termina il programma con il codice di uscita 2, come fanno i comandi del sistema.

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

## Archivi

`std.zip` e `std.tar` leggono gli archivi, e con `std.gzip` i `.tar.gz`:

```cromo norun
import std.fs
import std.gzip
import std.tar
import std.zip

i32 main() {
    for (zip.Entry entry in zip.read(fs.readBytes("photos.zip"))) {
        println("{}: {} bytes", entry.name, entry.data.len())
    }
    tar.extract(gzip.decompress(fs.readBytes("site.tar.gz")), "site")
    ret 0
}
```

- `zip.read` e `tar.read` restituiscono le voci di un archivio, ciascuna con `name`, `folder`, `link`, `executable` e `data`.
- `zip.extract` e `tar.extract` scrivono i file e le cartelle di un archivio in una cartella, creata se manca. Tralasciano i link, e si fermano davanti a un nome che uscirebbe dalla cartella, come `../x`: un archivio che arriva dalla rete non può scrivere altrove.
- Leggono i file zip di Windows, di macOS e delle forge, anche oltre i 4 GB, e i file tar di GNU e di POSIX; non scrivono archivi.

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
- `utcNow()` e `localNow()` restituiscono un `DateTime`, con i suoi campi dall'anno al nanosecondo, lo scostamento da UTC in minuti, e `text()` nel formato ISO 8601: `2026-10-30T18:21:33.250+01:00`, con i millisecondi.
- `sleep`, `msleep` e `usleep` mettono in pausa; in un task, nel frattempo il thread esegue altri task.

## Riepilogo

- `std.fs` legge e scrive file e cartelle; gli errori sono eccezioni con un messaggio chiaro.
- `std.os` fornisce l'ambiente; gli argomenti arrivano in `main(list<string> args)`.
- `std.cmdline` legge le opzioni della riga di comando, e scrive l'aiuto.
- `std.process` esegue altri programmi, senza passare da una shell.
- `std.zip` e `std.tar` leggono ed estraggono gli archivi.
- `std.time` misura le durate e fornisce la data e l'ora.
