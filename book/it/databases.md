# Database

Un programma che deve conservare i suoi dati, e ritrovarli a partire da una qualsiasi delle loro parti, usa un database. Il modulo [std.db](std.db.html) ha SQLite al suo interno: un database che è un solo file, letto e scritto dal programma stesso, senza un server da installare. Il programma resta un solo eseguibile senza dipendenze. Lo stesso modulo parla con un server PostgreSQL, alla fine del capitolo.

## Un primo database

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT, done INTEGER)")

    db.Statement insert = c.prepare("INSERT INTO notes (title, done) VALUES (?, ?)")
    insert.setText(1, "Buy the bread")
    insert.setBool(2, false)
    insert.exec()
    insert.setText(1, "Call Ada")
    insert.setBool(2, true)
    insert.exec()

    db.Statement all = c.prepare("SELECT id, title, done FROM notes ORDER BY id")
    for (all.next()) {
        println("{} {} {}", all.getInt(0), all.getText(1), all.getBool(2))
    }
    ret 0
}
```

```output
1 Buy the bread false
2 Call Ada true
```

Tre tipi fanno tutto:

- un `db.Database` è il database, aperto da un testo;
- una `db.Connection` vi esegue le istruzioni, una dopo l'altra, e le transazioni;
- un `db.Statement` è un'istruzione SQL con i suoi parametri, che dà le sue righe una alla volta.

## Aprire un database

Il testo dato a `db.Database` dice di quale database si tratta:

| Testo | Database |
|---|---|
| `"notes.db"`, `"data/notes.db"` | un file di SQLite, creato se manca; la sua cartella deve esistere |
| `":memory:"` | un database in memoria, vuoto all'inizio e perso alla fine del programma: per i test, e per i dati che non devono restare |

Un testo che non si può aprire è un `Error`:

```cromo
import std.db

i32 main() {
    try {
        db.Database data = db.Database("no/such/folder/notes.db")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot open the database no/such/folder/notes.db: unable to open database file
```

Un `db.Database` è un handle, come una finestra di `std.gui`: le sue copie sono lo stesso database, e i thread e i task lo catturano come un valore semplice. Un programma lo apre una volta, in `main`, e lo dà a chi ne ha bisogno.

Il file è un database di SQLite come ogni altro: il comando `sqlite3` e gli altri programmi lo leggono e lo scrivono, anche mentre il programma è in esecuzione.

## Connessioni

`data.connect()` dà una connessione. Quando la sua variabile finisce, la connessione torna al database, che la conserva per il prossimo `connect()`: prenderne una costa poco, quindi una funzione prende una connessione, la usa e la lascia finire. L'handler di un server fa così per ogni richiesta:

```cromo ignore
void handle(db.Database data, ref http.Request req, ref http.Response res) {
    db.Connection c = data.connect()
    // ... the statements of this request
}  // the connection goes back to the database
```

- Una connessione non si può copiare, solo spostare, ed esegue un'istruzione alla volta.
- `data.setMaxIdle(count)` imposta quante connessioni il database conserva quando gli tornano, 4 di default: le altre vengono chiuse.
- Un database in memoria ha una sola connessione, che è il database: `connect()` la aspetta finché un'altra parte del programma la tiene.

`c.exec(sql)` esegue istruzioni senza parametri, anche più d'una, separate da `;`, e dà quante righe hanno inserito, modificato ed eliminato:

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT); CREATE INDEX byTitle ON notes (title)")
    i64 added = c.exec("INSERT INTO notes (title) VALUES ('one'), ('two'), ('three')")
    i64 gone = c.exec("DELETE FROM notes WHERE title LIKE 't%'")
    println("added {}, deleted {}", added, gone)
    println("{} {}", c.hasTable("notes"), c.hasColumn("notes", "author"))
    ret 0
}
```

```output
added 3, deleted 2
true false
```

`hasTable(name)` e `hasColumn(table, name)` chiedono cosa c'è nel database: un programma li usa per creare le sue tabelle la prima volta, e per aggiungere una colonna che serve a una nuova versione.

## Istruzioni e parametri

Un valore che arriva da fuori del programma non si scrive mai dentro il testo di un'istruzione: un titolo con un apice la romperebbe, e un titolo scritto apposta la cambierebbe, che è il modo in cui funzionano le iniezioni di SQL. L'istruzione ha un `?` dove va ogni valore, e il valore si imposta con il suo numero, da 1:

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT)")

    string title = "it's here'); DROP TABLE notes; --"
    db.Statement insert = c.prepare("INSERT INTO notes (title) VALUES (?)")
    insert.setText(1, title)
    insert.exec()

    db.Statement find = c.prepare("SELECT title FROM notes WHERE id = ?")
    find.setInt(1, 1)
    find.next()
    println("{}", find.getText(0))
    ret 0
}
```

```output
it's here'); DROP TABLE notes; --
```

Cromo non ha l'overloading, quindi ogni tipo ha il suo metodo:

| Per impostare un parametro | Per leggere una colonna | Il valore |
|---|---|---|
| `setInt(index, value)` | `getInt(index)` | un `i64` |
| `setFloat(index, value)` | `getFloat(index)` | un `f64` |
| `setText(index, value)` | `getText(index)` | una `string` |
| `setBytes(index, value)` | `getBytes(index)` | una `list<u8>`, come un'immagine |
| `setBool(index, value)` | `getBool(index)` | un `bool`, conservato come 1 o 0 |
| `setNull(index)` | `isNull(index)` | nessun valore, il NULL di SQL |

- I parametri si contano da 1, come in SQL, e le colonne da 0, come gli elementi di una lista.
- Una colonna senza valore si legge come 0, `""`, nessun byte o `false`: `isNull` la distingue da uno 0 vero.
- SQLite converte tra i tipi dove può: `getText` di un numero dà le sue cifre.
- `columnCount()` e `columnName(index)` dicono cosa hanno le righe dell'istruzione.
- Una data si conserva come testo, per esempio `"2026-11-11 18:30:00"`, o come numero di secondi: le date della libreria arriveranno con un loro modulo.

Un'istruzione si esegue in uno di due modi:

- `exec()` la esegue fino in fondo e dà quante righe ha inserito, modificato o eliminato. Poi l'istruzione è pronta per essere eseguita di nuovo, e i parametri conservano i loro valori finché non vengono reimpostati: un'istruzione preparata una volta inserisce mille righe.
- `next()` passa alla riga successiva, la prima la prima volta, e dà `false` quando non ce ne sono più: `for (rows.next())` le legge tutte. `reset()` rende l'istruzione pronta a dare di nuovo le sue righe dalla prima.

Un'istruzione che è su una riga sta ancora leggendo: vede il database com'era alla sua prima riga, e finché non finisce le altre connessioni non possono scrivere tutto ciò che vogliono. Un'istruzione letta fino all'ultima riga, e una la cui variabile finisce, hanno finito; una che resta dopo aver letto solo alcune righe, come la prima, si lascia andare con `reset()`:

```cromo ignore
db.Statement newest = c.prepare("SELECT title FROM notes ORDER BY id DESC")
newest.next()
string title = newest.getText(0)
newest.reset()  // it has read what it needed
```

Un parametro o una colonna che l'istruzione non ha, e una colonna letta quando l'istruzione non è su una riga, sono errori del programma, e panic:

```cromo nocheck
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    db.Statement one = c.prepare("SELECT 1")
    println("{}", one.getInt(0))
    ret 0
}
```

```text
panic: db: the statement is not on a row: next() goes to one
```

## Transazioni

Le istruzioni che vanno fatte tutte o nessuna stanno in una transazione: `begin()` la comincia, `commit()` conserva ciò che ha fatto, e `rollback()` lo annulla. Una connessione che finisce dentro una transazione la annulla, quindi un'eccezione che esce dalla funzione annulla ciò che la funzione aveva cominciato:

```cromo
import std.db

void transfer(ref db.Connection c, i64 from, i64 to, i64 amount) {
    c.begin()
    db.Statement take = c.prepare("UPDATE accounts SET balance = balance - ? WHERE id = ? AND balance >= ?")
    take.setInt(1, amount)
    take.setInt(2, from)
    take.setInt(3, amount)
    if (take.exec() == 0) {
        c.rollback()
        throw Error(format("the account {} cannot pay {}", from, amount))
    }
    db.Statement give = c.prepare("UPDATE accounts SET balance = balance + ? WHERE id = ?")
    give.setInt(1, amount)
    give.setInt(2, to)
    give.exec()
    c.commit()
}

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE accounts (id INTEGER PRIMARY KEY, balance INTEGER)")
    c.exec("INSERT INTO accounts (balance) VALUES (100), (0)")
    transfer(ref c, 1, 2, 30)
    try {
        transfer(ref c, 1, 2, 500)
    } catch (Exception e) {
        println("{}", e.message())
    }
    db.Statement all = c.prepare("SELECT id, balance FROM accounts ORDER BY id")
    for (all.next()) {
        println("account {}: {}", all.getInt(0), all.getInt(1))
    }
    ret 0
}
```

```output
the account 1 cannot pay 500
account 1: 70
account 2: 30
```

Una transazione rende anche veloci molti inserimenti: SQLite scrive il file una volta, al commit, e non una volta per ogni riga.

## Errori

Un errore del database è un `Error` con il messaggio di SQLite, dopo ciò che il programma stava facendo:

```cromo
import std.db

i32 main() {
    db.Database data = db.Database(":memory:")
    db.Connection c = data.connect()
    c.exec("CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT NOT NULL)")
    list<string> wrong = ["SELEC 1", "DELETE FROM tasks", "INSERT INTO notes (title) VALUES (NULL)"]
    for (string sql in wrong) {
        try {
            c.exec(sql)
        } catch (Exception e) {
            println("{}", e.message())
        }
    }
    ret 0
}
```

```output
cannot run the statement: near "SELEC": syntax error
cannot run the statement: no such table: tasks
cannot run the statement: NOT NULL constraint failed: notes.title
```

## Più connessioni, thread e task

I task di un server prendono ciascuno la sua connessione, e SQLite tiene separato il loro lavoro con i lock del file, gli stessi che tengono separati due programmi sullo stesso database: molti possono leggere insieme, e uno alla volta scrive.

- Un'istruzione che trova il file bloccato lo aspetta, e riprova, per il timeout del database: 5 secondi di default, `data.setTimeout(milliseconds)` per cambiarlo per le connessioni create da quel momento. Dopo è un `Error`, `database is locked`.
- Una transazione che scriverà conviene cominciarla con `c.exec("BEGIN IMMEDIATE")` al posto di `begin()`: prende subito il diritto di scrivere, e lo aspetta, invece di trovarlo preso al suo primo `UPDATE`, quando due transazioni che hanno entrambe letto non possono più proseguire entrambe.
- Tieni brevi le transazioni: mentre una scrive, le altre aspettano.

```cromo
import std.db
import std.thread

i32 main() {
    db.Database data = db.Database(":memory:")
    if (true) {
        db.Connection c = data.connect()
        c.exec("CREATE TABLE hits (task INTEGER, n INTEGER)")
    }
    thread.Group tasks
    for (i64 t = 0; t < 8; t++) {
        tasks.start([data, t]() {
            db.Connection c = data.connect()
            db.Statement insert = c.prepare("INSERT INTO hits (task, n) VALUES (?, ?)")
            for (i64 n = 0; n < 25; n++) {
                insert.setInt(1, t)
                insert.setInt(2, n)
                insert.exec()
            }
        })
    }
    tasks.wait()
    db.Connection c = data.connect()
    db.Statement count = c.prepare("SELECT count(*) FROM hits")
    count.next()
    println("{} rows", count.getInt(0))
    ret 0
}
```

```output
200 rows
```

## Copiare un database

`c.backup(file)` copia l'intero database in un altro file, anche mentre il programma e gli altri lo usano: la copia è un database com'era in un certo momento.

```cromo ignore
db.Connection c = data.connect()
c.backup("notes-backup.db")
```

## Cosa ha questo SQLite, e cosa no

SQLite è compilato nel runtime di Cromo senza la libreria C, così gli eseguibili restano senza dipendenze, anche su Linux. Alcune sue parti sono lasciate fuori:

- **Le connessioni lavorano insieme.** Due task, ciascuno con la sua connessione, sono dentro SQLite nello stesso istante, sullo stesso database o su due: si aspettano solo sui lock dei file. Una connessione esegue un'istruzione alla volta.
- **Il write-ahead log c'è, ed è spento di default,** come in SQLite. `c.exec("PRAGMA journal_mode = WAL")`, una volta, lo accende per il database, che lo conserva: da allora chi legge non aspetta chi scrive, e un commit scrive meno. Un server con molti lettori lo vuole. Il database ha allora altri due file accanto, con `-wal` e `-shm` alla fine del nome, e deve stare su un disco del computer, non della rete.
- **Orari in UTC.** Le funzioni di SQLite per le date non hanno `localtime`.
- **Nessuna estensione caricata da file.**
- **Le chiavi esterne** sono controllate solo dopo `c.exec("PRAGMA foreign_keys = ON")`, su ogni connessione, come fa SQLite di default.

## PostgreSQL

Lo stesso modulo parla con un server PostgreSQL: il testo che apre il database è un indirizzo al posto del nome di un file, e il resto del programma è lo stesso. Il client è scritto in Cromo, quindi il programma non ha librerie da installare.

```cromo ignore
db.Database data = db.Database("postgresql://notes:secret@db.example.com:5432/notes?sslmode=require")
db.Connection c = data.connect()
c.exec("CREATE TABLE IF NOT EXISTS notes (id SERIAL PRIMARY KEY, title TEXT, done BOOLEAN)")
db.Statement insert = c.prepare("INSERT INTO notes (title, done) VALUES (?, ?)")
insert.setText(1, "Buy the bread")
insert.setBool(2, false)
insert.exec()
```

L'indirizzo è `postgresql://user:password@host:port/name`. Senza porta è la 5432, senza host questo computer, e senza nome il database ha il nome dell'utente. Un carattere che un indirizzo non può avere nell'utente o nella password si scrive con il suo codice, come `%40` per `@` e `%3F` per `?`.

- **Il login.** La password è verificata con SCRAM-SHA-256, il metodo di PostgreSQL dalla versione 14: non viaggia mai verso il server, e anche il server deve dimostrare di conoscerla. Ai server che chiedono ancora MD5, o la password così com'è, viene risposto lo stesso.
- **TLS.** Con `?sslmode=require` alla fine dell'indirizzo la connessione è cifrata, e il certificato del server deve venire da un'autorità di cui il sistema si fida e nominare l'host, come per `net.connectTls`: un database nel cloud lo richiede. Senza, o con `?sslmode=disable`, la connessione non è cifrata, cosa giusta sullo stesso computer o su una rete propria.
- **I parametri** sono `?` come con SQLite, e std.db li passa al server come `$1`, `$2`. I valori viaggiano come testi che il server legge secondo il tipo che l'istruzione vuole, così `setBool` funziona con una colonna `BOOLEAN` e con una intera. Un `?` dentro un testo, un nome fra virgolette o un commento resta com'è; gli operatori di PostgreSQL che hanno un `?`, come quelli di `jsonb`, si scrivono `??`.
- **Le righe** di un'istruzione arrivano tutte al suo primo `next()`, e poi si leggono una alla volta: un'istruzione con milioni di righe vuole un `LIMIT`.
- **Gli errori** sono un `Error` con il messaggio del server e, fra parentesi alla fine, il suo codice di cinque caratteri: `23505` è una chiave che c'è già, `42P01` una tabella che manca. Il messaggio è nella lingua del server.
- **Le connessioni** restituite sono conservate e ridate, come con SQLite; una che finisce dentro una transazione la annulla. `data.setTimeout(milliseconds)` è quanto un'istruzione aspetta un lock tenuto da un'altra (`lock_timeout`), per le connessioni aperte da quel momento.
- `hasTable` e `hasColumn` cercano negli schemi del percorso di ricerca. `backup` è solo di SQLite: un database PostgreSQL si copia con gli strumenti del suo server, e il metodo lancia un `Error`.

Ciò che cambia fra i due database è il loro SQL: i tipi delle colonne, `SERIAL` o `INTEGER PRIMARY KEY` per una chiave che si conta da sé, le funzioni. Un programma che deve girare su entrambi si attiene a ciò che hanno in comune, o tiene in due forme le poche istruzioni che differiscono.

## Riepilogo

- `db.Database(text)` apre un database: un file di SQLite, creato se manca, `":memory:"`, o un indirizzo di PostgreSQL, `postgresql://user:password@host/name`. È un handle che i thread e i task catturano.
- `data.connect()` dà una `db.Connection`, che torna al database quando la sua variabile finisce.
- `c.exec(sql)` esegue istruzioni senza parametri e dà le righe cambiate; `c.prepare(sql)` dà un `db.Statement` con un `?` per ogni parametro.
- I parametri si impostano da 1 con `setInt`, `setFloat`, `setText`, `setBytes`, `setBool` e `setNull`; le colonne si leggono da 0 con `getInt`, `getFloat`, `getText`, `getBytes`, `getBool` e `isNull`.
- `exec()` esegue un'istruzione e la rende pronta per essere eseguita di nuovo; `next()` dà le sue righe una alla volta.
- `begin()`, `commit()` e `rollback()` sono una transazione; una connessione che finisce dentro una transazione la annulla.
- Un errore del database è un `Error` con il suo messaggio; un uso sbagliato di un'istruzione è un panic.
- Un'istruzione che trova il file bloccato aspetta fino al timeout del database.
