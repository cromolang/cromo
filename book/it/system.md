# File, processi e tempo

Questo capitolo tratta dei moduli che dialogano con il sistema operativo:

| Modulo | A cosa serve |
|---|---|
| [std.fs](std.fs.html) | file e cartelle, percorsi, link |
| [std.os](std.os.html) | il sistema, l'ambiente, lo standard input, la fine del programma |
| [std.process](std.process.html) | altri programmi, con il loro input e output |
| [std.time](std.time.html) | orologi, date e pause |
| [std.cmdline](std.cmdline.html) | le opzioni della riga di comando, e l'aiuto |
| [std.zip](std.zip.html), [std.tar](std.tar.html) | archivi |

Funzionano allo stesso modo su Windows e su Linux, e seguono i due tipi di errore di [Errori](errors.md): ciò che può andare storto nel mondo, come un file che non esiste o un programma che non riesce a partire, è un'eccezione, con un messaggio che dice che cosa non è riuscito e perché; un uso sbagliato di una funzione, come una posizione negativa in un file, è un panic. Il capitolo spiega le idee e gli usi comuni; la pagina di ciascun modulo, collegata qui sopra, elenca tutte le sue funzioni.

## Gli argomenti del programma

Gli argomenti della riga di comando arrivano in `main`, come lista di stringhe. `args[0]` è il programma, così come è stato avviato; gli argomenti lo seguono:

```cromo args=report.txt,--verbose
i32 main(list<string> args) {
    println("{} arguments", args.len() - 1)
    for (i64 i = 1; i < args.len(); i++) {
        println("{}: {}", i, args[i])
    }
    ret 0
}
```

```output
2 arguments
1: report.txt
2: --verbose
```

Con `cromo run`, gli argomenti del programma vanno dopo `--`, così che non vengano letti come opzioni di `cromo`:

```sh
cromo run show.cro -- report.txt --verbose
```

Il parametro può avere qualsiasi nome, come `argv`, ma deve essere una `list<string>` che `main` legge soltanto: `ref`, `move`, un altro tipo o un secondo parametro sono un errore. `main` può anche non avere parametri, quando non gli servono gli argomenti.

```cromo error
i32 main(ref list<string> args) {
    ret 0
}
```

```output
error: main has no parameters, or one with the arguments of the program: list<string> args
```

Il modo in cui gli argomenti arrivano al programma dipende dal sistema:

- Su **Linux** è la shell a dividere la riga di comando, e il programma riceve gli argomenti come li dà il sistema, come byte. Non vengono controllati: di norma sono UTF-8, ma un nome di file scritto in un'altra codifica arriva così com'è.
- Su **Windows** un programma riceve l'intera riga di comando, come testo in UTF-16. Il runtime di Cromo la converte in UTF-8 e la divide con le regole del runtime C di Microsoft, le stesse che seguono i programmi scritti in C e C++:

| Sulla riga di comando | L'argomento |
|---|---|
| `"two words"` | `two words`: le virgolette tengono insieme gli spazi |
| `a\"b` | `a"b`: una barra rovesciata prima di una virgoletta la rende una virgoletta |
| `"c""d"` | `c"d`: due virgolette dentro le virgolette valgono una |
| `e\\f` | `e\\f`: le barre rovesciate che non precedono una virgoletta restano come sono |
| `"g\\"` | `g\`: due barre rovesciate prima di una virgoletta valgono una |
| `città` | `città`, in UTF-8 |

Un programma vede di rado queste regole: la shell, e `std.process` quando avvia un programma, scrivono la riga di comando in modo che ogni argomento arrivi così come è stato dato.

## Percorsi

Un percorso è una `string`: assoluto, o relativo alla cartella corrente, che si ottiene con `os.currentFolder()`. I nomi in un percorso sono separati da `/` su ogni sistema, e su Windows anche da `\`, quindi `"data/notes.txt"` funziona ovunque.

`std.fs` scompone un percorso, senza guardare il disco:

```cromo
import std.fs

i32 main() {
    string path = "photos/2026/beach.jpg"
    println("{}", fs.parent(path))
    println("{}", fs.fileName(path))
    println("{}", fs.extension(path))
    println("{}", fs.extension("backup.tar.gz"))
    println("{:?} {:?}", fs.extension(".profile"), fs.parent("beach.jpg"))
    ret 0
}
```

```output
photos/2026
beach.jpg
.jpg
.gz
"" ""
```

- `parent(path)` è la cartella del percorso, `""` per un nome da solo. La radice resta sé stessa: il genitore di `/etc` è `/`, e su Windows il genitore di `C:\data` è `C:\`.
- `fileName(path)` è l'ultimo nome; per `"a/b/"` è `"b"`.
- `extension(path)` è l'ultima estensione del nome, con il suo punto; un nome che comincia con un punto, come `.profile`, non ne ha.

`joinPath(first, second)` mette tra due parti il separatore del sistema, `/` su Linux e `\` su Windows; `fs.separator()` lo restituisce:

```cromo ignore
fs.joinPath("photos", "beach.jpg")   // "photos/beach.jpg" on Linux, "photos\beach.jpg" on Windows
fs.joinPath("photos/", "beach.jpg")  // "photos/beach.jpg": the first part ends with a separator already
fs.joinPath("", "beach.jpg")         // "beach.jpg"
```

## Leggere e scrivere file interi

Il modo più semplice di usare un file è leggerlo o scriverlo per intero:

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

    fs.writeBytes("data/header.bin", [0x43, 0x52, 0x01])
    println("{:?}", fs.readBytes("data/header.bin"))
    fs.removeAll("data")
    ret 0
}
```

```output
> first line
> second line
23 bytes
[67, 82, 1]
```

- `readText(path)` restituisce il contenuto come `string`, e controlla che sia UTF-8: un file che non lo è, come un testo in Latin-1 o un'immagine, è un'eccezione. `readBytes(path)` restituisce i byte, come `list<u8>`, senza controlli.
- `writeText(path, text)` e `writeBytes(path, bytes)` creano il file, o lo sostituiscono se esiste. La cartella del file deve esistere.
- `appendText(path, text)` aggiunge il testo in fondo al file, e crea il file se manca: il modo abituale di scrivere un log.

Un file che non si può leggere o scrivere è un'eccezione, il cui messaggio contiene l'azione, il percorso e il motivo:

```cromo
import std.fs

i32 main() {
    try {
        string text = fs.readText("missing.txt")
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        fs.writeText("no-folder/notes.txt", "hello")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot read 'missing.txt': no such file or folder
cannot write 'no-folder/notes.txt': no such file or folder
```

## File e cartelle

Queste funzioni chiedono che cosa sia un percorso:

- `exists(path)`, `isFile(path)` e `isFolder(path)` non lanciano mai eccezioni: un percorso che non c'è, o che non si può raggiungere, dà `false`. `isFile` e `isFolder` guardano, attraverso un link, ciò a cui punta; `isLink` guarda il link stesso.
- `size(path)` è la dimensione di un file in byte, e `modified(path)` l'ora della sua ultima modifica, in secondi dal 1970 (vedi [Il tempo](#il-tempo)). Entrambe lanciano un'eccezione quando il percorso non esiste.

Una cartella si crea con `createFolder`, che lancia un'eccezione quando il percorso esiste già o quando manca la cartella che la contiene, oppure con `createFolders`, che crea anche le cartelle mancanti sopra di essa, come `mkdir -p`, e non fa nulla quando la cartella c'è già. `listFolder(path)` restituisce i nomi dei file, delle cartelle e dei link di una cartella, senza `.` e `..`, **in nessun ordine**: ordinali quando l'ordine conta, e metti la cartella davanti a ogni nome con `joinPath`:

```cromo
import std.fs

// The bytes of the files in a folder, and in the folders inside it.
i64 totalSize(string folder) {
    i64 total = 0
    for (string name in fs.listFolder(folder)) {
        string path = fs.joinPath(folder, name)
        if (fs.isFolder(path)) {
            total += totalSize(path)
        } else {
            total += fs.size(path)
        }
    }
    ret total
}

i32 main() {
    fs.createFolders("project/src/util")
    fs.writeText("project/readme.md", "# Project\n")
    fs.writeText("project/src/main.cro", "i32 main() {\n    ret 0\n}\n")
    fs.writeText("project/src/util/text.cro", "")

    list<string> names = fs.listFolder("project/src")
    sort(ref names)
    println("{:?}", names)
    println("{} bytes", totalSize("project"))
    fs.removeAll("project")
    ret 0
}
```

```output
["main.cro", "util"]
35 bytes
```

Copiare, spostare ed eliminare:

- `copy(from, to)` copia un file, e sostituisce `to` se esiste. Non copia una cartella. Su Linux la copia non mantiene i permessi dell'originale: un programma copiato va reso di nuovo eseguibile, con `makeExecutable`.
- `rename(from, to)` rinomina o sposta un file o una cartella, anche in un'altra cartella, e sostituisce `to` quando è un file. Entrambi i percorsi devono stare sullo stesso disco; solo su Windows un file può spostarsi su un altro.
- `remove(path)` elimina un file, un link o una cartella vuota, e lancia un'eccezione quando il percorso non esiste o la cartella non è vuota.
- `removeAll(path)` elimina un file, o una cartella con tutto ciò che contiene. Non segue mai un link: elimina il link, non ciò a cui punta. Quando il percorso non esiste non fa nulla, così può fare pulizia senza chiedere prima.

```cromo
import std.fs

i32 main() {
    fs.writeText("draft.txt", "version 1")
    fs.writeText("final.txt", "version 0")
    fs.copy("draft.txt", "final.txt")  // final.txt is replaced
    println("{}", fs.readText("final.txt"))

    fs.writeText("draft.txt", "version 2")
    fs.rename("draft.txt", "final.txt")  // replaced again, and draft.txt is gone
    println("{} {}", fs.readText("final.txt"), fs.exists("draft.txt"))

    fs.createFolders("old/logs")
    try {
        fs.remove("old")
    } catch (Exception e) {
        println("{}", e.message())
    }
    fs.removeAll("old")
    fs.removeAll("old")  // nothing left to remove: no error
    fs.remove("final.txt")
    println("{} {}", fs.exists("old"), fs.exists("final.txt"))
    ret 0
}
```

```output
version 1
version 2 false
cannot remove 'old': the folder is not empty
false false
```

Gli altri errori si leggono allo stesso modo: `cannot create 'logs': it exists already`, `cannot list 'notes.txt': a part of the path is not a folder`, `cannot copy 'photos': it is a folder` (su Windows, `access denied`).

## Link e file eseguibili

`fs.createLink(target, path)` fa di `path` un link a `target`, come `ln -s target path`. Le funzioni che leggono e scrivono seguono il link, e `readLink(path)` dice dove punta:

```cromo
import std.fs
import std.os

i32 main() {
    fs.createFolders("releases/1.2")
    fs.writeText("releases/1.2/notes.txt", "fixes")
    // the target as an absolute path, which works in the same way on every system
    string target = fs.joinPath(os.currentFolder(), "releases/1.2")
    fs.createLink(target, "releases/current")

    println("{} {}", fs.isLink("releases/current"), fs.isFolder("releases/current"))
    println("{}", fs.fileName(fs.readLink("releases/current")))
    println("{}", fs.readText("releases/current/notes.txt"))

    fs.remove("releases/current")  // the link, not the folder
    println("{}", fs.exists("releases/1.2/notes.txt"))
    fs.removeAll("releases")
    ret 0
}
```

```output
true true
1.2
fixes
true
```

I due sistemi creano link diversi:

- Su **Linux** il link è un link simbolico, a un file o a una cartella, che contiene `target` così come è scritto. Una destinazione relativa si legge a partire dalla cartella del link, non dalla cartella corrente: `createLink("1.2", "releases/current")` punta a `releases/1.2`.
- Su **Windows** il link è una *junction*, che qualsiasi utente può creare, mentre un link simbolico richiede i diritti di amministratore o la modalità sviluppatore di Windows. Una junction punta solo a una cartella, e ne contiene il percorso assoluto: una destinazione relativa si legge a partire dalla cartella corrente. Una destinazione che non è una cartella è l'eccezione `cannot link to 'notes.txt': on Windows a link is a junction, to a folder`.

Quindi un link che funziona su entrambi i sistemi punta a una cartella, con un percorso assoluto, come nell'esempio. `readLink` restituisce la destinazione così come la contiene il link: su Windows, il percorso assoluto. Un percorso che non è un link è l'eccezione `cannot read the link 'releases': it is not a link`. Un link la cui destinazione manca è comunque un link: `isLink` è `true`, mentre `exists` è `false`.

`fs.makeExecutable(path)` permette a un file di essere eseguito come programma: su Linux aggiunge il permesso di eseguirlo a chi può leggerlo, come `chmod +x`; su Windows, che esegue un file in base alla sua estensione, non fa nulla. Chiamala dopo aver scritto o copiato un programma o uno script:

```cromo ignore
fs.writeText("hello.sh", "#!/bin/sh\necho hello\n")
fs.makeExecutable("hello.sh")  // now ./hello.sh runs on Linux
```

## File a pezzi

Un file troppo grande per leggerlo per intero, come un database o un indice, si apre con `fs.File` e si legge e si scrive a una posizione:

```cromo
import std.fs

i32 main() {
    fs.File file = fs.File("records.bin", fs.Mode.Create)
    for (i32 i = 0; i < 100; i++) {
        file.write([u8(i), u8(i * 2), 0, 0])  // a record of 4 bytes
    }
    list<u8> record = file.readAt(4 * 37, 4)  // the record 37, without reading the others
    println("{} bytes, record 37: {:?}", file.size(), record)
    ret 0
}
```

```output
400 bytes, record 37: [37, 74, 0, 0]
```

Il secondo argomento dice come si apre il file:

| Modo | Il file | Lettura | Scrittura |
|---|---|---|---|
| `fs.Mode.Read` | deve esistere | sì | no |
| `fs.Mode.Update` | deve esistere, e resta com'è | sì | sì |
| `fs.Mode.Create` | viene creato, o svuotato se esiste | sì | sì |
| `fs.Mode.Append` | viene creato se manca | no | in fondo |

Un `File` ha una posizione, da 0, dove comincia la prossima lettura o scrittura:

- `read(count)` legge fino a `count` byte dalla posizione, e `write(bytes)` scrive lì; entrambe spostano la posizione dopo i byte. `position()` la restituisce e `seek(position)` la sposta.
- `readAt(position, count)` e `writeAt(position, bytes)` lavorano a una posizione propria, e lasciano la posizione del file dov'è: il modo per raggiungere un record di un indice.
- Alla fine del file una lettura restituisce meno byte di quelli chiesti, e dopo la fine nessuno. Una scrittura oltre la fine fa crescere il file, con degli zeri in mezzo. In `Append` ogni scrittura va in fondo, anche con `writeAt`.
- `size()` è la dimensione attuale del file; `sync()` manda al disco ciò che è stato scritto prima di tornare, così che i dati sopravvivano a un crash del sistema.

```cromo
import std.fs

i32 main() {
    fs.File log = fs.File("events.log", fs.Mode.Create)
    log.write("start\n".bytes())
    println("position {}, size {}", log.position(), log.size())

    log.writeAt(10, "!".bytes())  // beyond the end: zeros between
    println("size {}", log.size())

    log.seek(0)
    println("{:?}", log.read(100))  // fewer than 100: the file ends
    println("{:?}", log.read(100))  // at the end: none
    ret 0
}
```

```output
position 6, size 6
size 11
[115, 116, 97, 114, 116, 10, 0, 0, 0, 0, 33]
[]
```

Ciò che il sistema rifiuta è un'eccezione, con il percorso, come per le altre funzioni di `std.fs`: un file che non si può aprire, una scrittura su un file aperto in lettura, una lettura da un file aperto per aggiungere in fondo. Una posizione o un conteggio negativi sono un errore del programma, e un panic: `the position in a file is from 0, found -1`.

```cromo
import std.fs

i32 main() {
    fs.writeText("config.ini", "port=8080\n")
    fs.File config = fs.File("config.ini", fs.Mode.Read)
    try {
        config.write("debug=1\n".bytes())
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        fs.File other = fs.File("missing.ini", fs.Mode.Update)
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot write 'config.ini': it is open to read
cannot open 'missing.ini': no such file or folder
```

Il file si chiude quando la variabile finisce, grazie al suo distruttore, come ogni risorsa in Cromo ([Struct](structs.md)). Per lo stesso motivo un `File` non si può copiare, solo spostare: due copie chiuderebbero due volte lo stesso file.

```cromo error
import std.fs

i32 main() {
    fs.File first = fs.File("data.bin", fs.Mode.Create)
    fs.File second = first
    ret 0
}
```

```output
error: values of type File cannot be copied
```

`fs.File b = move a` lo sposta, e `a` non si può più usare ([Possesso](ownership.md)).

## File mappati in memoria

`fs.Mapping(path)` mappa un intero file in memoria, in sola lettura. Il sistema legge solo le parti che si usano, quindi una mappatura è veloce anche per un file di gigabyte, letto in molti punti:

```cromo file=header.cro
import std.fs

i32 main() {
    fs.writeBytes("header.bin", [0x43, 0x52, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00])
    fs.Mapping header = fs.Mapping("header.bin")
    println("{} bytes: {}", header.size(), header.text(0, 2))
    println("version {}, count {}", header.u16At(2), header.u32At(4))
    println("{}", header.u32At(6))
    ret 0
}
```

```output
8 bytes: CR
version 1, count 10000
panic: 4 bytes from 6 go beyond the 8 bytes of the mapping at cromo:std/fs.cro:374:13
    at Mapping.check (cromo:std/fs.cro:374)
    at Mapping.little (cromo:std/fs.cro:380)
    at Mapping.u32At (cromo:std/fs.cro:322)
    at main (header.cro:8)
```

- `u8At`, `u16At`, `u32At` e `u64At` leggono un numero senza segno a una posizione, da 0; `i8At`, `i16At`, `i32At`, `i64At`, `f32At` e `f64At` gli altri. I numeri sono little-endian, l'ordine dei byte di x86-64 e di ARM64, a qualsiasi posizione.
- `bytes(position, count)` restituisce una parte come `list<u8>`, e `text(position, count)` come `string`, che deve essere UTF-8.
- Una lettura oltre la dimensione, o con una posizione negativa, è un panic, come un indice fuori da una lista: la mappatura non ha indirizzi che il programma potrebbe leggere male.
- Un file vuoto non mappa nulla, e ha dimensione 0. Un file che cambia mentre è mappato può mostrare la modifica.

La mappatura finisce con la sua variabile e, come un `File`, non si può copiare. Su Android, i documenti restituiti dalle finestre di dialogo di [Finestre e app](gui.md), come `content://...`, non si possono aprire con `fs.File` né mappare: si leggono e si scrivono solo per intero.

## Il sistema e l'ambiente

`std.os` descrive il sistema su cui gira il programma:

- `os.name()` è `"windows"` o `"linux"`; su Android è anch'esso `"linux"`.
- `os.env(name)` è il valore di una variabile d'ambiente, o `""` quando non è impostata; `os.hasEnv(name)` distingue una variabile impostata a `""` da una che manca. Su Windows i nomi non distinguono maiuscole e minuscole: `os.env("path")` è il `PATH`.
- `os.currentFolder()` è il percorso assoluto della cartella corrente, da cui partono i percorsi relativi. `os.processId()` è il numero che il sistema assegna al programma.

```cromo norun
import std.fs
import std.os

// Where the program keeps its settings: NOTES_HOME, if it is set, or the usual place.
string settingsFolder() {
    if (os.hasEnv("NOTES_HOME")) {
        ret os.env("NOTES_HOME")
    }
    if (os.name() == "windows") {
        ret fs.joinPath(os.env("APPDATA"), "notes")
    }
    ret fs.joinPath(os.env("HOME"), ".config/notes")
}

i32 main() {
    fs.createFolders(settingsFolder())
    println("settings in {}", settingsFolder())
    ret 0
}
```

`os.exit(code)` termina subito il programma con il codice di uscita, come `exit()` del C, da qualsiasi funzione. Ciò che è stato stampato viene scritto, ma gli oggetti ancora vivi non vengono distrutti: i loro distruttori non vengono eseguiti.

```cromo
import std.os

struct Connection {
    ~Connection() {
        println("connection closed")
    }
}

i32 main(list<string> args) {
    Connection connection
    if (args.len() < 2) {
        println("usage: client <host>")
        os.exit(2)
    }
    ret 0
}
```

```output
usage: client <host>
```

Per terminare con un codice ed eseguire i distruttori, restituiscilo da `main`.

## Lo standard input

`os.readLine(ref line)` legge in `line` la riga successiva dello standard input, senza il suo terminatore, `\n` o `\r\n`, e restituisce `false` alla fine dell'input:

```cromo norun file=number.cro
import std.os

i32 main() {
    string line
    i64 count = 0
    for (os.readLine(ref line)) {
        count++
        println("{}: {}", count, line)
    }
    ret 0
}
```

```sh
printf 'apples\npears' | cromo run number.cro
```

```output
1: apples
2: pears
```

`os.readInput()` legge tutto l'input che resta, fino alla fine, come un'unica `string`, con i terminatori di riga così come sono: è ciò che fa un filtro, come `cromo fmt -`. Le due funzioni condividono un unico buffer, quindi si possono alternare: qui la prima riga è un titolo e il resto è il corpo.

```cromo norun
import std.os

i32 main() {
    string title
    os.readLine(ref title)
    string body = os.readInput()
    println("{}: {} lines", title, body.trimEnd().split("\n").len())
    ret 0
}
```

Le regole:

- Viene letta anche un'ultima riga senza `\n`; alla fine, `readLine` restituisce `false` e lascia `line` vuota. Alla fine `readInput` restituisce `""`.
- L'input deve essere UTF-8: una riga che non lo è è un'eccezione, come per `fs.readText`. Su una console di Windows il testo arriva in UTF-16, e viene convertito.
- Da un terminale, l'input finisce con Ctrl+D su Linux, e su Windows con Ctrl+Z all'inizio di una riga seguito da Invio.
- Lo standard input si legge da un thread alla volta.

## La riga di comando

Gli argomenti di `main` sono spesso opzioni, come `--port 9000` o `-v`. `std.cmdline` le legge: un `cmdline.CmdLine` dichiara le opzioni, `parse` legge gli argomenti, e i metodi che cominciano con `get` restituiscono i valori.

```cromo file=server.cro args=--config,site.ini,-v,--port=9000,notes.txt
import std.cmdline

i32 main(list<string> args) {
    cmdline.CmdLine cmd
    cmd.flag("verbose", 'v', "print more")
    cmd.addInt("port", 'p', "port to listen on", false, 8080)
    cmd.addString("format", 'f', "output format", false, "text")
    cmd.choices("format", ["text", "json"])
    cmd.addString("config", ' ', "configuration file", true, "")
    cmd.footer("files...")
    cmd.parse(args)

    println("config {}, port {}, format {}", cmd.getString("config"), cmd.getInt("port"), cmd.getString("format"))
    if (cmd.exists("verbose")) {
        println("files: {}", join(cmd.rest(), ", "))
    }
    ret 0
}
```

```output
config site.ini, port 9000, format text
files: notes.txt
```

Ogni opzione ha un nome lungo, scritto dopo `--`, un nome breve di una lettera, scritto dopo `-` (o `' '` quando non ne ha), e una descrizione per l'aiuto:

- `flag(name, letter, description)` aggiunge un'opzione senza valore; `exists(name)` dice se è stata data.
- `addInt`, `addFloat` e `addString(name, letter, description, required, default)` aggiungono un'opzione con un `i64`, un `f64` o una `string`. `getInt`, `getFloat` e `getString` restituiscono il valore sulla riga di comando, o quello predefinito quando l'opzione non è stata data; `exists` distingue i due casi. Un'opzione obbligatoria deve essere sulla riga di comando, e il suo valore predefinito non si usa. Cromo non ha l'overloading, quindi il tipo sta nel nome del metodo.
- `choices(name, values)` elenca i valori che un'opzione accetta, come testi; un numero si confronta così come lo scrive `format("{}", n)`.
- `footer(text)` è ciò che la riga d'uso mostra dopo le opzioni, come `files...`.
- `rest()` restituisce gli argomenti che non sono opzioni, nel loro ordine, senza `args[0]`.

Le opzioni si possono scrivere nelle forme consuete:

- `--port 9000` e `--port=9000`; `-p 9000` e `-p9000`.
- Più flag insieme, `-vq`; l'ultima lettera può prendere un valore, come `-vp 9000`.
- Un'opzione data due volte tiene il suo ultimo valore.
- Un numero si legge come lo leggono `i64("...")` e `f64("...")` ([Liste, mappe e testo](collections.md)): `-5` e `1e3` sono numeri, `0x10` e `1_000` no.
- Dopo `--` ogni argomento va in `rest()`, anche quando comincia con `-`; anche `-` da solo è un argomento, che molti programmi leggono come lo standard input.

```cromo args=-vp,7000,--port,7001,-,--,-x
import std.cmdline

i32 main(list<string> args) {
    cmdline.CmdLine cmd
    cmd.flag("verbose", 'v', "print more")
    cmd.addInt("port", 'p', "port to listen on", false, 8080)
    cmd.parse(args)
    println("{} {} {:?}", cmd.exists("verbose"), cmd.getInt("port"), cmd.rest())
    ret 0
}
```

```output
true 7001 ["-", "-x"]
```

`-h` e `--help` stampano sullo standard output l'aiuto, costruito dalle opzioni, e terminano il programma con il codice di uscita 0. La riga d'uso contiene il nome del programma, preso da `args[0]` senza la sua cartella e senza `.exe`, le opzioni obbligatorie e il testo di `footer`; poi ogni opzione ha una riga con il suo tipo, le sue scelte e il suo valore predefinito:

```sh
cromo run server.cro -- --help
```

```output
usage: server --config <string> [options] files...
options:
  -v, --verbose          print more
  -p, --port <int>       port to listen on (default 8080)
  -f, --format <string>  output format (text, json; default text)
      --config <string>  configuration file (required)
  -h, --help             print this help
```

`cmd.help()` restituisce lo stesso testo, per stamparlo in un altro momento; prima di `parse`, che legge il nome del programma, la riga d'uso dice `program`.

Una riga di comando sbagliata è un errore di chi esegue il programma: `parse` stampa l'errore e l'aiuto sullo standard error, e termina il programma con il codice di uscita 2, come fanno i comandi del sistema. Qui manca l'opzione obbligatoria:

```sh
cromo run server.cro -- --port 9000 notes.txt
```

```output
server: the option --config is required

usage: server --config <string> [options] files...
options:
  -v, --verbose          print more
  -p, --port <int>       port to listen on (default 8080)
  -f, --format <string>  output format (text, json; default text)
      --config <string>  configuration file (required)
  -h, --help             print this help
```

Gli altri errori si scrivono allo stesso modo: `unknown option --nope`, `the option --port needs a value`, `the value of --port is not an integer: 'abc'`, `the value of --format is 'xml', and it can be text, json`, `the option --verbose takes no value`.

Un uso sbagliato del modulo è invece un errore del programma, e un panic: due opzioni con lo stesso nome o la stessa lettera (`cmdline: the short name -v is used twice`), il nome `help` o la lettera `h`, riservati all'aiuto, un valore predefinito che non è tra le scelte, oppure `getInt` di un'opzione di testo (`cmdline: --format is not an option of int`).

## Eseguire altri programmi

`process.run(program, args)` esegue un programma fino alla fine, e restituisce un `process.Output` con il suo codice di uscita, il suo standard output e il suo standard error. Gli esempi di questa sezione avviano l'esempio stesso, con un argomento che lo fa agire come l'altro programma, così da non avere bisogno di altri programmi:

```cromo
import std.process

i32 main(list<string> args) {
    if (args.len() > 1) {
        // the program started by run(): it shows its arguments, writes an error and ends with 3
        for (i64 i = 1; i < args.len(); i++) {
            println("[{}]", args[i])
        }
        eprintln("a warning")
        ret 3
    }
    process.Output result = process.run(args[0], ["two words", "a \"quote\"", "", "C:\\folder\\"])
    println("code {}", result.code)
    print("{}", result.out)
    print("errors: {}", result.err)
    ret 0
}
```

```output
code 3
[two words]
[a "quote"]
[]
[C:\folder\]
errors: a warning
```

- Gli argomenti arrivano al programma così come sono, senza una shell che li interpreti: uno spazio, una virgoletta, `*` o `$` sono solo caratteri, e un argomento vuoto resta un argomento. Su Windows `std.process` scrive la riga di comando con le virgolette e le barre rovesciate che richiedono le regole degli [argomenti](#gli-argomenti-del-programma).
- Un codice di uscita diverso da 0 è un valore da controllare, non un'eccezione: molti programmi lo usano per dare una risposta, come `grep` quando non trova nulla.
- Lo standard input del programma è vuoto. Il suo output e i suoi errori si leggono nello stesso momento, su due thread, così che un programma che scrive molto su uno dei due non resti in attesa per sempre.
- Il programma ha l'ambiente e la cartella corrente di questo.
- Si possono avviare programmi da più thread nello stesso momento: ogni programma riceve solo le proprie pipe.

Il programma viene cercato come lo cerca una shell: un nome senza cartella, come `git`, si cerca nelle cartelle del `PATH`, e su Windows anche nella cartella corrente, aggiungendo `.exe` quando non ha estensione. `./tool` esegue il programma `tool` della cartella corrente su entrambi i sistemi. Un programma che non riesce a partire è un'eccezione:

```cromo
import std.process

i32 main() {
    try {
        process.Output result = process.run("no-such-program", ["--version"])
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot start 'no-such-program': no such program
```

### Un programma che gira in parallelo

`process.start(program, args)` avvia un programma senza attenderlo, e restituisce un `process.Process` con cui comunicare mentre gira:

```cromo
import std.os
import std.process

i32 main(list<string> args) {
    if (args.len() > 1) {
        // the program started below: it writes its input in upper case
        string line
        for (os.readLine(ref line)) {
            println("{}", line.upper())
        }
        ret 0
    }
    process.Process child = process.start(args[0], ["upper"])
    child.write("hello\n")
    child.write("world\n")
    child.closeInput()  // the end of its input: the loop of the child ends
    print("{}", child.readAll())
    println("code {}", child.wait())
    ret 0
}
```

```output
HELLO
WORLD
code 0
```

- `write(text)` e `writeBytes(bytes)` scrivono sullo standard input del programma, e restano in attesa finché il programma non legge. `closeInput()` lo chiude: il programma vede la fine del suo input, che molti programmi, come `sort`, attendono prima di rispondere.
- `read(most)` restituisce fino a `most` byte dello standard output, non appena ne arriva qualcuno, e una lista vuota alla sua fine; `readAll()` lo legge fino alla fine, come testo.
- `wait()` attende la fine del programma, e restituisce il suo codice di uscita; chiamata di nuovo, restituisce lo stesso codice.
- `kill()` termina subito il programma; `wait()` restituisce poi 1 su Windows, e 137 su Linux, cioè 128 + il segnale che lo ha terminato, come dicono le shell. Anche un programma terminato da un altro segnale restituisce 128 + quel segnale.
- Gli errori del programma non vengono letti: vanno dove vanno gli errori di questo programma, come il terminale.

`read` serve per i programmi che rispondono un pezzo alla volta, come un server o una build lunga:

```cromo ignore
for {
    list<u8> piece = child.read(4096)
    if (piece.len() == 0) {
        break  // the program has closed its output
    }
    total += piece.len()
}
```

L'esempio scrive tutto l'input prima di leggere l'output. Questo funziona quando il programma risponde solo alla fine del suo input, come `sort`, o quando la sua risposta è piccola: la pipe tra due programmi contiene qualche kilobyte. Un programma che risponde mentre legge, dato un input grande, riempirebbe il suo output mentre questo sta ancora scrivendo, e resterebbero entrambi in attesa per sempre: in quel caso dagli l'input in un file.

Quando un `Process` viene distrutto, come alla fine del suo blocco, chiude l'input e l'output del programma, e ne attende la fine, a meno che non l'abbia già fatto `wait()`. Così un programma avviato in una funzione è terminato quando la funzione ritorna:

```cromo
import std.fs
import std.os
import std.process

void save(string program, list<string> lines) {
    process.Process child = process.start(program, ["save"])
    for (string line in lines) {
        child.write(line + "\n")
    }
}  // here the destructor closes the input of the child, and waits for its end

i32 main(list<string> args) {
    if (args.len() > 1) {
        fs.writeText("saved.txt", os.readInput())  // the child: its input to a file
        ret 0
    }
    save(args[0], ["one", "two"])
    print("{}", fs.readText("saved.txt"))
    ret 0
}
```

```output
one
two
```

Come un `File`, un `Process` non si può copiare, solo spostare.

Scrivere a un programma che è terminato è un'eccezione, come scrivere dopo `closeInput()`. Su Linux il sistema terminerebbe il programma che scrive con il segnale `SIGPIPE`; un programma Cromo lo ignora a partire dal primo programma che avvia, così la scrittura fallisce come qualsiasi altra. I programmi che avvia ricevono `SIGPIPE` come di consueto.

```cromo
import std.process
import std.time

i32 main(list<string> args) {
    if (args.len() > 1) {
        if (args[1] == "slow") {
            time.sleep(60)
        }
        ret 5
    }
    process.Process slow = process.start(args[0], ["slow"])
    slow.kill()
    println("killed: {}", slow.wait() != 0)

    process.Process quick = process.start(args[0], ["quick"])
    println("code {}", quick.wait())
    try {
        quick.write("too late\n")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
killed: true
code 5
cannot write to the program: it does not read its input any more
```

### L'output come testo

`out`, `err` e `readAll()` restituiscono l'output come `string`, che è UTF-8. Un programma può scrivere in un'altra codifica, come la code page di una console di Windows: i byte che non sono UTF-8 diventano il carattere U+FFFD, `�`, invece di provocare un'eccezione. Qui il sistema mostra un file scritto in Latin-1, dove `à` è il singolo byte 224:

```cromo
import std.fs
import std.os
import std.process

i32 main() {
    fs.writeBytes("latin1.txt", [99, 105, 116, 116, 224, 10])  // "città" in Latin-1
    process.Output result
    if (os.name() == "windows") {
        result = process.run("cmd", ["/c", "type", "latin1.txt"])
    } else {
        result = process.run("cat", ["latin1.txt"])
    }
    print("{}", result.out)
    println("{:?}", result.out.bytes())
    ret 0
}
```

```output
citt�
[99, 105, 116, 116, 239, 191, 189, 10]
```

I byte 239, 191 e 189 sono U+FFFD in UTF-8. Per avere i byte esatti dell'output, leggili con `read`.

## Il tempo

`std.time` ha due tipi di tempo: un orologio che misura quanto dura qualcosa, e la data con l'ora del giorno.

### Misurare

`time.now()` legge un orologio monotono, che va solo in avanti, anche quando cambia la data del sistema: un `Instant`. `elapsed()` restituisce il tempo da quell'istante a ora, e `later.since(earlier)` il tempo tra due istanti, come `Duration`:

```cromo
import std.time

i32 main() {
    time.Instant start = time.now()
    time.msleep(20)
    time.Instant middle = time.now()
    time.msleep(30)

    time.Duration first = middle.since(start)
    time.Duration whole = start.elapsed()
    println("{} {}", first.milliseconds() >= 20, whole.milliseconds() >= 50)
    println("{}", start.since(middle).nanoseconds < 0)
    ret 0
}
```

```output
true true
true
```

Una `Duration` ha il campo `nanoseconds`, e `seconds()`, un `f64` con le frazioni, `milliseconds()` e `microseconds()`, che tralasciano il resto. È negativa quando gli istanti sono nell'ordine inverso.

`time.sleep(seconds)`, `time.msleep(milliseconds)` e `time.usleep(microseconds)` mettono in pausa il programma; un valore di 0 o meno non fa nulla. In un task, la pausa lascia il thread agli altri task ([Thread, task e canali](concurrency.md)).

### Date

`time.utcNow()` restituisce la data e l'ora attuali in UTC, e `time.localNow()` nel fuso orario del sistema, come `DateTime`. I suoi campi sono `year`, con tutte le sue cifre, `month` da 1, `day` da 1, `hour`, `minute`, `second`, `nanosecond`, e `offset`, i minuti di scostamento da UTC: 60 a Roma in inverno, 0 in UTC. Su Linux il fuso orario è quello di `/etc/localtime`, e UTC se manca.

`text()` lo scrive nel formato ISO 8601, con i millisecondi. Un `DateTime` è una struct con campi pubblici, quindi uno costruito a mano si stampa allo stesso modo:

```cromo
import std.time

i32 main() {
    time.DateTime launch
    launch.year = 2026
    launch.month = 3
    launch.day = 7
    launch.hour = 9
    launch.minute = 5
    launch.nanosecond = 250000000
    println("{}", launch.text())
    launch.offset = 60
    println("{}", launch.text())

    time.DateTime now = time.utcNow()
    println("{}", now.year >= 2026 && now.text().endsWith("Z"))
    ret 0
}
```

```output
2026-03-07T09:05:00.250Z
2026-03-07T09:05:00.250+01:00
true
```

`time.unix()` restituisce i secondi dal 1970-01-01 UTC, il tempo di Unix, come `i64`: l'unità di `fs.modified()`, e dei tempi in un JSON Web Token. La differenza tra due di questi valori è un numero di secondi:

```cromo
import std.fs
import std.time

i32 main() {
    fs.writeText("stamp.txt", "now")
    i64 age = time.unix() - fs.modified("stamp.txt")
    println("written less than 2 seconds ago: {}", age < 2)
    ret 0
}
```

```output
written less than 2 seconds ago: true
```

Così `time.unix() - fs.modified(path) > 24 * 3600` dice che un file ha più di un giorno.

## Archivi

`std.zip` legge gli archivi zip, come li creano Windows, macOS e le forge, anche oltre i 4 GB; `std.tar` legge gli archivi tar, come li creano GNU tar e bsdtar. Un `.tar.gz` è un tar compresso con gzip: prima lo decomprime `std.gzip`. Leggono ed estraggono archivi, ma non li scrivono.

`zip.read(bytes)` e `tar.read(bytes)` restituiscono le voci di un archivio, nel suo ordine. Ogni voce ha il suo `name`, un percorso con `/` e senza `/` in fondo, `folder`, se è una cartella, `link`, la destinazione di un link simbolico o `""`, `executable`, se si può eseguire, e `data`, il contenuto di un file. Gli esempi scrivono prima i loro archivi, dai byte nelle righe nascoste:

```cromo
import std.fs
import std.zip

i32 main() {
#     fs.writeBytes("notes.zip", notesZip())
    for (zip.Entry entry in zip.read(fs.readBytes("notes.zip"))) {
        if (entry.folder) {
            println("{}/", entry.name)
        } else {
            println("{}: {} bytes", entry.name, entry.data.len())
        }
    }
    zip.extract(fs.readBytes("notes.zip"), "out")
    print("{}", fs.readText("out/notes/todo.txt"))
    fs.removeAll("out")
    ret 0
}
#
# // The bytes of notes.zip: a folder, notes, with a file, todo.txt.
# list<u8> notesZip() {
#     ret [
#         80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 0, 96, 97, 93, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0,
#         110, 111, 116, 101, 115, 47, 80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 0, 96, 97, 93, 245, 208, 237, 14,
#         9, 0, 0, 0, 9, 0, 0, 0, 14, 0, 0, 0, 110, 111, 116, 101, 115, 47, 116, 111, 100, 111, 46, 116,
#         120, 116, 98, 117, 121, 32, 109, 105, 108, 107, 10, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 0,
#         96, 97, 93, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 237, 65,
#         0, 0, 0, 0, 110, 111, 116, 101, 115, 47, 80, 75, 1, 2, 20, 3, 20, 0, 0, 0, 0, 0, 0, 96, 97, 93,
#         245, 208, 237, 14, 9, 0, 0, 0, 9, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 164, 129, 36,
#         0, 0, 0, 110, 111, 116, 101, 115, 47, 116, 111, 100, 111, 46, 116, 120, 116, 80, 75, 5, 6, 0,
#         0, 0, 0, 2, 0, 2, 0, 112, 0, 0, 0, 89, 0, 0, 0, 0, 0
#     ]
# }
```

```output
notes/
notes/todo.txt: 9 bytes
buy milk
```

`zip.extract(bytes, folder)` e `tar.extract(bytes, folder)` scrivono i file e le cartelle di un archivio in una cartella, che viene creata se manca. Un file che esiste viene sostituito, e su Linux i file eseguibili si possono eseguire, come li rende `makeExecutable`. I link simbolici vengono tralasciati, perché potrebbero puntare fuori dalla cartella:

```cromo
import std.fs
import std.gzip
import std.tar

i32 main() {
#     fs.writeBytes("tool.tar.gz", toolTarGz())
    list<u8> archive = gzip.decompress(fs.readBytes("tool.tar.gz"))
    for (tar.Entry entry in tar.read(archive)) {
        println("{} folder={} executable={} link={:?}", entry.name, entry.folder, entry.executable, entry.link)
    }
    tar.extract(archive, "out")
    list<string> names = fs.listFolder("out/tool")
    sort(ref names)
    println("{:?}", names)
    fs.removeAll("out")
    ret 0
}
#
# // The bytes of tool.tar.gz: a folder, tool, with two files and a link.
# list<u8> toolTarGz() {
#     ret [
#         31, 139, 8, 0, 0, 0, 0, 0, 2, 10, 237, 214, 77, 10, 194, 48, 16, 134, 225, 172, 61, 69, 78, 80,
#         147, 52, 63, 231, 41, 26, 168, 16, 13, 180, 17, 60, 190, 53, 59, 187, 16, 55, 169, 96, 223,
#         103, 51, 33, 155, 89, 12, 223, 48, 37, 231, 116, 20, 109, 169, 69, 112, 174, 214, 197, 186,
#         214, 183, 118, 38, 104, 111, 108, 168, 255, 62, 120, 39, 164, 19, 27, 184, 207, 101, 152, 150,
#         150, 98, 159, 202, 107, 254, 211, 253, 214, 205, 227, 239, 230, 175, 251, 247, 249, 107, 165,
#         141, 17, 82, 49, 255, 230, 226, 105, 204, 114, 140, 41, 229, 131, 192, 94, 243, 31, 135, 243,
#         53, 118, 229, 81, 154, 229, 223, 91, 251, 97, 255, 135, 117, 254, 131, 87, 228, 127, 11, 203,
#         234, 151, 151, 66, 246, 119, 157, 255, 52, 148, 56, 151, 102, 61, 106, 194, 67, 248, 254, 254,
#         211, 186, 183, 90, 72, 211, 248, 46, 33, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 254, 201, 19, 63,
#         156, 135, 9, 0, 40, 0, 0
#     ]
# }
```

```output
tool folder=true executable=false link=""
tool/run.sh folder=false executable=true link=""
tool/readme.txt folder=false executable=false link=""
tool/latest folder=false executable=false link="run.sh"
["readme.txt", "run.sh"]
```

Un archivio che arriva dalla rete non può scrivere fuori dalla cartella: un nome che ne uscirebbe, come `../x` o `/x`, ferma l'estrazione con l'eccezione `cannot extract the zip: the name '../x' would leave the folder`. Le voci precedenti sono già state scritte. Anche dati che non sono un archivio, o un archivio danneggiato o troncato, sono eccezioni: `cannot read the zip: the data are not a zip`, `cannot read the tar: the data are not a tar`, e `std.zip` controlla il CRC-32 di ogni file. Uno zip cifrato, o compresso con un metodo diverso da deflate, viene rifiutato.

## Riepilogo

- `main(list<string> args)` riceve gli argomenti, con il programma in `args[0]`: su Windows divisi con le regole del runtime C di Microsoft e convertiti in UTF-8, su Linux i byte che dà il sistema.
- `std.fs` legge e scrive file per intero, o a pezzi con `fs.File`, o li mappa con `fs.Mapping`; crea, elenca, copia, sposta ed elimina file e cartelle, crea link e file eseguibili, e scompone i percorsi. I suoi errori sono eccezioni con il percorso e il motivo.
- `std.os` fornisce il sistema, l'ambiente, la cartella corrente e lo standard input, e `os.exit` termina il programma senza eseguire i distruttori.
- `std.cmdline` legge le opzioni della riga di comando, scrive l'aiuto, e gestisce una riga di comando sbagliata con il codice di uscita 2.
- `std.process` esegue programmi fino alla fine con `run`, o in parallelo con `start`, senza una shell; un codice di uscita è un valore, un programma che non riesce a partire è un'eccezione, e un `Process` attende il suo programma quando viene distrutto.
- `std.time` misura le durate con un orologio monotono, fornisce la data e l'ora, i secondi di Unix, e le pause.
- `std.zip` e `std.tar`, con `std.gzip`, leggono ed estraggono archivi, senza scrivere fuori dalla cartella.
