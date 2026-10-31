# Progetto finale: un servizio web

L'ultimo capitolo mette insieme quanto visto nella guida: un piccolo servizio che tiene una lista di cose da fare, con un'API JSON su HTTP. Usa le struct e il possesso, un `thread.Mutex` condiviso tra le richieste, il JSON scritto dal compilatore, il server e il client HTTP, e un test.

Il servizio risponde a quattro richieste:

| Richiesta | Risposta |
|---|---|
| `GET /todos` | la lista, in JSON |
| `POST /todos` con `{"title": "..."}` | 201 e la nuova attività, oppure 400 se manca il titolo |
| `POST /todos/{id}/done` | 204, oppure 404 se l'attività non esiste |
| tutto il resto | 404 |

## I dati

```cromo ignore
struct Todo {
    i64 id
    string title
    bool done
}

struct NewTodo {
    string title
}
```

`Todo` è ciò che il servizio conserva e invia; `NewTodo` è ciò che un client invia per crearne una nuova. Entrambe vengono convertite da e verso JSON dal compilatore, senza scrivere una riga di codice.

## L'archivio

Le attività stanno in uno `Store`, una struct con i suoi metodi:

```cromo ignore
struct Store {
    list<Todo> items
    i64 next = 1

    Todo add(string title) {
        Todo todo
        todo.id = next
        todo.title = title.copy()
        next++
        items.add(todo.copy())
        ret todo
    }

    bool complete(i64 id) {
        for (ref Todo todo in items) {
            if (todo.id == id) {
                todo.done = true
                ret true
            }
        }
        ret false
    }
}
```

`add` conserva nella lista una copia della nuova attività e restituisce l'altra, che l'handler rimanda al client. `complete` scorre la lista con `ref`, per modificare l'attività che trova.

L'archivio non ha niente a che fare con HTTP, quindi è facile da testare:

```cromo ignore
[[test]]
void addGivesNewIds() {
    Store store
    Todo first = store.add("a")
    Todo second = store.add("b")
    assert(second.id == first.id + 1)
    assert(store.complete(first.id))
    assert(!store.complete(99))
}
```

## L'handler

Il server chiama l'handler per ogni richiesta, su molti task contemporaneamente. L'archivio è condiviso da tutte le richieste, quindi sta in un `thread.Mutex<Store>`, e l'handler vi accede solo dentro `lock`:

```cromo ignore
void handle(thread.Mutex<Store> store, ref http.Request req, ref http.Response res) {
    if (req.matches("GET /todos")) {
        store.lock([ref res](ref Store s) { res.writeJson(json.encode(s.items)) })
    } else if (req.matches("POST /todos")) {
        NewTodo input = json.decode<NewTodo>(req.text())
        if (input.title == "") {
            res.status = 400
            res.write("a todo needs a title")
            ret
        }
        store.lock([ref res, input](ref Store s) {
            res.status = 201
            res.writeJson(json.encode(s.add(input.title)))
        })
    } else if (req.matches("POST /todos/{id}/done")) {
        i64 id = i64(req.param("id"))
        bool found = false
        store.lock([ref found, id](ref Store s) { found = s.complete(id) })
        res.status = 404
        if (found) {
            res.status = 204
        }
    } else {
        res.status = 404
    }
}
```

Alcuni dettagli:

- Le lambda passate a `lock` catturano `res` con `ref`, per scrivere la risposta, e in lettura i valori che servono loro.
- Un corpo che non è JSON fa lanciare un'eccezione a `json.decode`; il server trasforma l'eccezione non gestita in una risposta con stato 500 e prosegue con le altre richieste.
- `i64(req.param("id"))` lancia un'eccezione allo stesso modo se l'id non è un numero.

## Il programma completo

Ecco il `main` del servizio: le dichiarazioni delle sezioni precedenti stanno al posto di `// ...`, e sul sito *Mostra tutto* apre il file intero. Per provare il servizio senza un browser, il `main` avvia anche un client che invia alcune richieste e stampa le risposte; un servizio vero si limiterebbe a chiamare `serve`.

```cromo file=todo.cro
import std.http
import std.json
import std.os
import std.thread

// ...
# struct Todo {
#     i64 id
#     string title
#     bool done
# }
#
# struct NewTodo {
#     string title
# }
#
# // Le attività del servizio, tenute in memoria.
# struct Store {
#     list<Todo> items
#     i64 next = 1
#
#     Todo add(string title) {
#         Todo todo
#         todo.id = next
#         todo.title = title.copy()
#         next++
#         items.add(todo.copy())
#         ret todo
#     }
#
#     bool complete(i64 id) {
#         for (ref Todo todo in items) {
#             if (todo.id == id) {
#                 todo.done = true
#                 ret true
#             }
#         }
#         ret false
#     }
# }
#
# [[test]]
# void addGivesNewIds() {
#     Store store
#     Todo first = store.add("a")
#     Todo second = store.add("b")
#     assert(second.id == first.id + 1)
#     assert(store.complete(first.id))
#     assert(!store.complete(99))
# }
#
# void handle(thread.Mutex<Store> store, ref http.Request req, ref http.Response res) {
#     if (req.matches("GET /todos")) {
#         store.lock([ref res](ref Store s) { res.writeJson(json.encode(s.items)) })
#     } else if (req.matches("POST /todos")) {
#         NewTodo input = json.decode<NewTodo>(req.text())
#         if (input.title == "") {
#             res.status = 400
#             res.write("a todo needs a title")
#             ret
#         }
#         store.lock([ref res, input](ref Store s) {
#             res.status = 201
#             res.writeJson(json.encode(s.add(input.title)))
#         })
#     } else if (req.matches("POST /todos/{id}/done")) {
#         i64 id = i64(req.param("id"))
#         bool found = false
#         store.lock([ref found, id](ref Store s) { found = s.complete(id) })
#         res.status = 404
#         if (found) {
#             res.status = 204
#         }
#     } else {
#         res.status = 404
#     }
# }

i32 main() {
    thread.Mutex<Store> store
    http.Server server = http.listen("127.0.0.1", 0)
    string base = format("http://127.0.0.1:{}", server.port())
    thread.Task service = thread.Task([store, move server]() {
        server.serve([store](ref http.Request req, ref http.Response res) { handle(store, ref req, ref res) })
    })
    service.start()

    // Un client che prova il servizio.
    http.Client client
    http.Response created = client.post(base + "/todos", "{\"title\": \"write the guide\"}", "application/json")
    println("{} {}", created.status, created.text())
    client.post(base + "/todos", "{\"title\": \"publish it\"}", "application/json")
    http.Response done = client.post(base + "/todos/1/done", "", "text/plain")
    println("{}", done.status)
    // ...
#
#     http.Response all = client.get(base + "/todos")
#     println("{}", all.text())
#     list<Todo> todos = json.decode<list<Todo>>(all.text())
#     for (Todo t in todos) {
#         string mark = " "
#         if (t.done) {
#             mark = "x"
#         }
#         println("[{}] {}", mark, t.title)
#     }
#
#     http.Response bad = client.post(base + "/todos", "{}", "application/json")
#     println("{} {}", bad.status, bad.text())
    os.exit(0)
    ret 0
}
```

```output
201 {"id":1,"title":"write the guide","done":false}
204
[{"id":1,"title":"write the guide","done":true},{"id":2,"title":"publish it","done":false}]
[x] write the guide
[ ] publish it
400 a todo needs a title
```

E il suo test:

```sh
cromo test todo.cro
```

```text
ok    addGivesNewIds
1 passed, 0 failed
```

## Farne un servizio vero

Per servire client reali, sostituisci il client in `main` con il solo server, su una porta fissa:

```cromo ignore
i32 main() {
    thread.Mutex<Store> store
    http.serve("0.0.0.0", 8080, [store](ref http.Request req, ref http.Response res) { handle(store, ref req, ref res) })
    ret 0
}
```

Poi compilalo per il server, che può essere Linux anche se lavori su Windows:

```sh
cromo build --release --target linux todo.cro
```

Il risultato è un unico eseguibile, senza dipendenze, che gira su qualsiasi Linux x64.

Da qui, il servizio potrebbe salvare i suoi dati in un file con `std.fs`, servire HTTPS con `http.listenTls`, o controllare un token negli header di ogni richiesta. La libreria ha quanto serve per ciascuna di queste cose, e il compilatore controlla che le richieste, che girano contemporaneamente, non tocchino mai l'archivio senza il lock.

## Per proseguire

- `docs/language.md` nel [repository](https://github.com/cromolang/cromo) è la bozza della specifica, con tutte le regole del linguaggio.
- `docs/decisions.md` spiega perché ogni regola è fatta così.
- La cartella `examples/` contiene altri programmi, e `library/` la libreria standard, scritta in Cromo.

Grazie per aver letto questa guida, e benvenuto in Cromo.
