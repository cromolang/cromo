# Final project: a web service

The last chapter puts together what the guide has shown: a small service that keeps a list of things to do, with a JSON API over HTTP. It uses structs and ownership, a `thread.Mutex` shared by the requests, the JSON written by the compiler, the HTTP server and client, and a test.

The service answers four requests:

| Request | Answer |
|---|---|
| `GET /todos` | the list, in JSON |
| `POST /todos` with `{"title": "..."}` | 201 and the new todo, or 400 without a title |
| `POST /todos/{id}/done` | 204, or 404 if the todo does not exist |
| anything else | 404 |

## The data

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

`Todo` is what the service keeps and sends; `NewTodo` is what a client sends to create one. Both are converted to and from JSON by the compiler, without a line of code.

## The store

The todos live in a `Store`, a struct with its methods:

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

`add` keeps a copy of the new todo in the list and returns the other one, which the handler sends back to the client. `complete` visits the list with `ref`, to modify the todo it finds.

The store has nothing to do with HTTP, so it is easy to test:

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

## The handler

The server calls the handler for each request, on many tasks at once. The store is shared by all the requests, so it lives in a `thread.Mutex<Store>`, and the handler reaches it only inside `lock`:

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

Some details:

- The lambdas given to `lock` capture `res` with `ref`, to write the response, and the values they need by reading them.
- A body that is not JSON makes `json.decode` throw; the server turns the uncaught exception into a response with status 500, and goes on with the other requests.
- `i64(req.param("id"))` throws in the same way for an id that is not a number.

## The whole program

Here is the `main` of the service: the declarations of the previous sections stand in place of `// ...`, and on the site *Show all* opens the whole file. To try the service without a browser, `main` also starts a client that sends some requests and prints the answers; a real service would only call `serve`.

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
# // The todos of the service, kept in memory.
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

    // A client that tries the service.
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

And its test:

```sh
cromo test todo.cro
```

```text
ok    addGivesNewIds
1 passed, 0 failed
```

## Making it a real service

To serve real clients, replace the client in `main` with the server alone, on a fixed port:

```cromo ignore
i32 main() {
    thread.Mutex<Store> store
    http.serve("0.0.0.0", 8080, [store](ref http.Request req, ref http.Response res) { handle(store, ref req, ref res) })
    ret 0
}
```

Then build it for the server, which can be Linux even when you work on Windows:

```sh
cromo build --release --target linux todo.cro
```

The result is one executable, without dependencies, that runs on any Linux x64; with `--target linux-arm64`, on any Linux on ARM64.

From here, the service could keep its data in a file with `std.fs`, serve HTTPS with `http.listenTls`, or check a token in the headers of each request. The library has what is needed for each, and the compiler checks that the requests, running at the same time, never touch the store without the lock.

## Where to go next

- `docs/language.md` in the [repository](https://github.com/cromolang/cromo) is the draft specification, with every rule of the language.
- `docs/decisions.md` explains why each rule is as it is.
- The folder `examples/` has more programs, and `library/` the standard library, written in Cromo.

Thank you for reading, and welcome to Cromo.
