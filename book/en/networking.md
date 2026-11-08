# Networking and HTTP

The standard library has everything a network program needs, and needs no other library:

| Module | For |
|---|---|
| [std.net](std.net.html) | TCP and UDP, the names of the hosts, TLS |
| [std.http](std.http.html) | HTTP servers and clients |
| [std.websocket](std.websocket.html) | WebSocket, servers and clients |
| [std.gzip](std.gzip.html) | compression in the format of gzip |
| [std.crypto](std.crypto.html) | hashes, random keys, ciphers and signatures |
| [std.jwt](std.jwt.html) | JSON Web Tokens |

TLS comes from Mbed TLS, compiled into the runtime of Cromo, so the executables stay self-contained, on Windows and on Linux.

The calls block until they are done, as in C. In a task, a call that waits for the network leaves its thread to the other tasks, so a server written in the simplest way, one task for each connection, serves thousands of clients at once: [Threads, tasks and channels](concurrency.md) explains the tasks. An error of the network is an exception of type `Error`, whose message says what could not be done and why, as `cannot connect to example.com:80: connection refused`.

The chapter explains the ideas and the rules; the page of each module, linked above, lists all its functions.

## TCP

A server listens with `net.listen`, accepts connections with `accept()`, and reads and writes on each `net.Connection`. A client connects with `net.connect`. The program below is both: its server runs in a task, and its client talks to it.

```cromo
import std.net
import std.thread

i32 main() {
    net.Listener listener = net.listen("127.0.0.1", 0)  // port 0: any free port
    i32 port = listener.port()
    thread.Task server = thread.Task([move listener]() {
        net.Connection client = listener.accept()
        list<u8> request = client.read(1024)
        client.writeText("echo: " + string(request))
    })
    server.start()

    net.Connection connection = net.connect("127.0.0.1", port)
    connection.writeText("hello")
    println("{}", string(connection.read(1024)))
    server.join()
    ret 0
}
```

```output
echo: hello
```

- `net.listen(host, port)` listens on an address: `"127.0.0.1"` for this computer alone, `"0.0.0.0"` for all its addresses of IPv4, `"::"` for those of IPv6. Port 0 asks the system for a free port, which `port()` gives.
- `accept()` waits for the next connection and gives it.
- `net.connect(host, port)` takes a name or an address, and tries the addresses of the name in turn until one answers.
- Listeners and connections can be moved, as into the task above, but not copied.

### Reading and writing

`read(most)` waits until some bytes arrive and gives up to `most` of them, which can be fewer than what the other side wrote: TCP carries a stream of bytes, not messages. An empty list means that the other side has closed the connection. So a program that reads all that comes reads in a loop:

```cromo
import std.net
import std.thread

i32 main() {
    net.Listener listener = net.listen("127.0.0.1", 0)
    i32 port = listener.port()
    thread.Task server = thread.Task([move listener]() {
        net.Connection client = listener.accept()
        client.writeText("one ")
        client.writeText("two ")
        client.writeText("three")
        client.close()  // the client reads an empty list after the data
    })
    server.start()

    net.Connection connection = net.connect("127.0.0.1", port)
    string received
    for {
        list<u8> part = connection.read(1024)
        if (part.len() == 0) {
            break
        }
        received += string(part)
    }
    println("{}", received)
    server.join()
    ret 0
}
```

```output
one two three
```

- `write(bytes)` and `writeText(text)` write everything before they return, waiting while the system has no room; nothing is kept after.
- `close()` closes the connection before its destructor, which does it otherwise. A second `close()` does nothing; a `read` or a `write` after it is an exception.
- `listener.close()` stops listening: `accept()` throws after it, and the connections already accepted stay open.

### Timeouts and errors

Without a timeout, an operation waits as long as it takes. `timeout(ms)` sets a limit in milliseconds that holds for each following `read` and `write` of a connection, or for each `accept()` of a listener; past it, the operation throws. `-1` waits again as long as it takes.

```cromo
import std.net

i32 main() {
    try {
        net.connect("127.0.0.1", 1)  // nobody listens on port 1
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        net.resolve("no-such-host.invalid")
    } catch (Exception e) {
        println("{}", e.message())
    }

    net.Listener listener = net.listen("127.0.0.1", 0)
    listener.timeout(100)
    try {
        listener.accept()
    } catch (Exception e) {
        println("{}", e.message())
    }

    net.Connection connection = net.connect("127.0.0.1", listener.port())
    connection.timeout(200)
    try {
        connection.read(100)  // the other side never writes
    } catch (Exception e) {
        println("{}", e.message())
    }
    connection.close()
    try {
        connection.writeText("hello")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot connect to 127.0.0.1:1: connection refused
cannot find the host 'no-such-host.invalid': no such host
cannot accept a connection: timed out after 100 ms
cannot read: timed out after 200 ms
cannot write: the socket is closed
```

The connection above completes although `accept()` is not called again: the system accepts the connections of a listener by itself, and `accept()` only takes the next one.

- `net.connect` has no timeout of its own: it waits for the connection as long as the system does, and `timeout` holds from the connection on.
- A port that another program uses is an exception: `cannot use 127.0.0.1 port 8080: the address is in use`. A listener on Linux uses `SO_REUSEADDR`, so that a server started again takes its port at once.

### Many clients

A server gives each connection to a task of its own, which takes it with `move`, so that a slow client does not keep the others waiting. A `thread.Group` starts the tasks:

```cromo
import std.net
import std.thread

i32 main() {
    net.Listener listener = net.listen("127.0.0.1", 0)
    i32 port = listener.port()
    thread.Task server = thread.Task([move listener]() {
        thread.Group clients
        for (i64 i = 0; i < 3; i++) {  // a real server loops forever: for { ... }
            net.Connection client = listener.accept()
            clients.start([move client]() {
                string name = string(client.read(100))
                client.writeText("hello, " + name)
            })
        }
    })
    server.start()

    list<net.Connection> connections
    for (string name in ["Ada", "Grace", "Linus"]) {
        net.Connection connection = net.connect("127.0.0.1", port)
        connection.writeText(name)
        connections.add(move connection)
    }
    for (ref net.Connection connection in connections) {
        println("{}", string(connection.read(100)))
    }
    server.join()
    ret 0
}
```

```output
hello, Ada
hello, Grace
hello, Linus
```

The lambda of a task runs once, so it can modify what it takes with `move`, as `client` above. `accept()` is a `const` method: several tasks or threads can also accept from the same listener, capturing it by reading, as the HTTP server of this chapter does.

## Names and addresses

`net.resolve(host)` gives the addresses of a name, as text; an address written with numbers gives itself. IPv6 is written in the short form of RFC 5952, with the longest run of zeros as `::`:

```cromo
import std.net

i32 main() {
    println("{:?}", net.resolve("127.0.0.1"))
    println("{:?}", net.resolve("2001:0db8:0000:0000:0000:ff00:0042:8329"))
    ret 0
}
```

```output
["127.0.0.1"]
["2001:db8::ff00:42:8329"]
```

IPv4 and IPv6 work on both systems. A name can have several addresses: `localhost` is often `::1` and `127.0.0.1`. `net.connect` tries them in turn, while `net.listen` listens on the first one only; so a server for this computer alone is clearer with an address, `"127.0.0.1"`, than with `"localhost"`.

Where the addresses come from depends on the system:

- On **Windows**, the resolver of the system answers, the one that every program uses.
- On **Linux**, a program of Cromo does not use the C library, and a resolver written in Cromo reads `/etc/hosts` first; then it asks the servers of `/etc/resolv.conf` in their order, with UDP, first the addresses of IPv4 and then those of IPv6, waiting two seconds for each server, and `127.0.0.1` when the file names none. It does not use the `search` domains of the file for short names yet, nor TCP for the answers that do not fit in UDP.

## UDP

`net.udp(host, port)` gives a `net.Udp`, a socket bound to an address and a port. It sends and receives messages, each whole or not at all, with no connection, and no promise that they arrive, nor in which order:

```cromo
import std.net

i32 main() {
    net.Udp server = net.udp("127.0.0.1", 0)
    net.Udp client = net.udp("127.0.0.1", 0)
    client.send("127.0.0.1", server.port(), "ping".bytes())

    net.Datagram message = server.receive(1500)
    println("{} from {}", string(message.data), message.host)
    server.send(message.host, message.port, "pong".bytes())  // back to who sent it

    println("{}", string(client.receive(1500).data))
    client.timeout(100)
    try {
        client.receive(1500)
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
ping from 127.0.0.1
pong
cannot receive: timed out after 100 ms
```

- `send(host, port, bytes)` sends one message, to a name or an address of the same version of IP as the socket: a socket bound to `"0.0.0.0"` sends to IPv4, one bound to `"::"` to IPv6. A message lost on the way is not an error.
- `receive(most)` waits for the next message and gives a `net.Datagram`, with `data`, and the `host` and the `port` it comes from. Give room for the largest message: a longer one is cut to `most` bytes on Linux, and is an exception on Windows.
- `timeout(ms)`, `port()` and `close()` work as for TCP.

## TLS

`net.connectTls(host, port)` opens a connection encrypted with TLS, a `net.Connection` with the same `read`, `write`, `timeout` and `close`:

```cromo norun
import std.net

i32 main() {
    net.Connection site = net.connectTls("example.com", 443)
    site.timeout(10000)
    site.writeText("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
    println("{}", string(site.read(200)))
    ret 0
}
```

- The versions are TLS 1.2 and TLS 1.3.
- The certificate of the server is always verified: it must be signed by an authority that the system trusts, be valid today, and name the host. Otherwise the connection fails with an exception that says why:
  - `cannot connect to example.com:443 with TLS: the certificate is not signed by an authority that the system trusts`
  - `... the certificate is not for example.com`
  - `... the certificate has expired`
  - `... the certificate is not valid yet`
  - `... what arrives is not TLS`, when the other side does not speak TLS.
- The authorities are those of the system, updated with it, the ones a company adds too: on Windows its store of root certificates, and Windows itself for those it downloads only when a chain needs them; on Linux the file of the distribution, as `/etc/ssl/certs/ca-certificates.crt`.
- The environment variable `SSL_CERT_FILE`, when it is set, names a file in PEM whose certificates replace those of the system, as for OpenSSL, curl and Go: a test can trust an authority of its own. A file without certificates leaves none, and the connections fail with `no certificates in ..., the file of SSL_CERT_FILE`. The authorities are read once, at the first connection.
- Revoked certificates are not checked, as in Go: OCSP and the lists of revoked certificates need further connections, and the certificates of the web last a few months.
- `timeout()` holds for the handshake too. Like `connect`, `connectTls` has no timeout of its own: it waits for the connection and for the handshake as long as the other side takes.

A server listens with `net.listenTls(host, port, certificate, key)`: the certificate in PEM, followed by those of its chain, and its private key in PEM, both as texts. Its `accept()` gives a connection at once, and the handshake happens at its first `read` or `write`, in the task that serves it, so that a slow or hostile client does not keep `accept()` waiting. An error of the handshake is the exception of that `read` or `write`.

The program below has a server whose certificate is signed by itself: a plain TCP client gets nothing, and a TLS client refuses the certificate. The functions `certificate()` and `privateKey()` give the two texts in PEM; they are in the whole program.

```cromo
import std.net
import std.thread

# string certificate() {
#     ret "-----BEGIN CERTIFICATE-----\n" +
#         "MIIBljCCATugAwIBAgIUTgk0U6DebV39C3teygXd4bdg5QQwCgYIKoZIzj0EAwIw\n" +
#         "FDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MTEwODE4MDQ0OFoYDzIxMjYxMDE1\n" +
#         "MTgwNDQ4WjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjO\n" +
#         "PQMBBwNCAAQljV6fRJiCF+BsDuLdZMtSdRZZfRvMkRgk2zxHExJEHruJQA1vw7QE\n" +
#         "k+/rVNN/wNvW/F3YwYFkeIEll3ccY1E4o2kwZzAdBgNVHQ4EFgQUWF4YTQogGdZJ\n" +
#         "TI+EQhSIVg7rmSQwHwYDVR0jBBgwFoAUWF4YTQogGdZJTI+EQhSIVg7rmSQwDwYD\n" +
#         "VR0TAQH/BAUwAwEB/zAUBgNVHREEDTALgglsb2NhbGhvc3QwCgYIKoZIzj0EAwID\n" +
#         "SQAwRgIhAOSOxoabM7nkfHXyalIHCq746lN6tBFVZd6GX7ZnjhCuAiEA5F+fELgx\n" +
#         "RpZhBsyiI8oRWTGQHlcqfbrt+l+sjqq8ZVc=\n" +
#         "-----END CERTIFICATE-----\n"
# }
#
# string privateKey() {
#     ret "-----BEGIN PRIVATE KEY-----\n" +
#         "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgYqZEZ5Cy3VTUuqxB\n" +
#         "BBlB0uk60UDC+qXMtPP9lpwBLouhRANCAAQljV6fRJiCF+BsDuLdZMtSdRZZfRvM\n" +
#         "kRgk2zxHExJEHruJQA1vw7QEk+/rVNN/wNvW/F3YwYFkeIEll3ccY1E4\n" +
#         "-----END PRIVATE KEY-----\n"
# }
#
i32 main() {
    // A certificate for localhost signed by itself, which no system trusts.
    net.Listener listener = net.listenTls("localhost", 0, certificate(), privateKey())
    i32 port = listener.port()
    thread.Task server = thread.Task([move listener]() {
        net.Connection first = listener.accept()  // at once, before the handshake
        try {
            first.read(100)  // the handshake comes here
        } catch (Exception e) {
            println("server: {}", e.message())
        }
        first.close()
        net.Connection second = listener.accept()
        try {
            second.read(100)  // the client refuses the certificate and goes away
        } catch (Exception e) {
        }
    })
    server.start()

    net.Connection plain = net.connect("localhost", port)  // TCP, without TLS
    plain.writeText("hello")
    println("plain: {} bytes", plain.read(100).len())  // the server has closed
    try {
        net.connectTls("localhost", port)
    } catch (Exception e) {
        // The port changes at each run.
        println("client: {}", e.message().replace(format("{}", port), "PORT"))
    }
    server.join()
    ret 0
}
```

```output
server: cannot read: what arrives is not TLS
plain: 0 bytes
client: cannot connect to localhost:PORT with TLS: the certificate is not signed by an authority that the system trusts
```

`net.listenTls` reads the texts at once, and refuses them with an exception: `cannot listen with TLS: the key is not the one of the certificate`, or `the certificate is not valid PEM`, or `the key is not a valid private key in PEM`, followed by the detail of Mbed TLS.

The end of a connection with TLS:

- `read()` gives an empty list when the other side ends the connection, with the message of TLS that says so, close_notify, and also when the connection ends without it, as in Go: the protocols, as HTTP, know the length of what they send.
- `close()` and the destructor send close_notify after a handshake that went well, ignoring the errors.

## An HTTP server

`http.serve(host, port, handler)` listens and gives every request to one function, the handler, which reads the `http.Request` and fills the `http.Response`. It never returns:

```cromo norun
import std.http

i32 main() {
    http.serve("0.0.0.0", 8080, [](ref http.Request req, ref http.Response res) {
        if (req.matches("GET /")) {
            res.write("Welcome!")
        } else if (req.matches("GET /hello/{name}")) {
            res.write("Hello, " + req.param("name") + "!")
        } else {
            res.status = 404
            res.write("not found")
        }
    })
    ret 0
}
```

Run it and open `http://localhost:8080/hello/world` in a browser.

`http.listen(host, port)` gives an `http.Server` that listens but does not serve yet: the connections wait in the system until `serve(handler)` is called. Its `port()` is known before, also with port 0, so a program can start the server in a task and then call it, as the examples of this chapter do. `http.listenTls(host, port, certificate, key)` gives a server of https, with the texts in PEM as for `net.listenTls`.

### Routes

The handler chooses the route itself with `req.matches(pattern)`, true when the method and the path of the request are those of the pattern:

- `"GET /users/{id}"`: a name in braces takes one segment of the path, which cannot be empty; `req.param("id")` then gives it, without its escapes, so `%20` becomes a space.
- `"GET /files/{path...}"`: a name followed by `...` takes all the rest of the path, slashes included; it can only be the last segment. `req.param("path")` gives it.
- `"/status"`: without the method, any method goes. `GET` takes `HEAD` too.
- The other segments are compared exactly, a final slash too: `"GET /users"` does not match `/users/`. The query, after `?`, does not count.
- `req.param(name)` of a name that the last pattern that matched does not have is an exception.
- `req.queryValue(name)` gives a value of the query without its escapes, with `+` as a space, and `""` when it is missing.

```cromo
import std.http
import std.os
import std.thread

void handle(ref http.Request req, ref http.Response res) {
    if (req.matches("GET /")) {
        res.write("home")
    } else if (req.matches("GET /users/{id}")) {
        res.write(format("user {} (page '{}')", req.param("id"), req.queryValue("page")))
    } else if (req.matches("GET /files/{path...}")) {
        res.write("file " + req.param("path"))
    } else if (req.matches("POST /echo")) {
        res.write(req.text())
    } else if (req.matches("GET /fail")) {
        throw Error("boom")
    } else {
        res.status = 404
        res.write("no route for " + req.method + " " + req.path)
    }
}

i32 main() {
    http.Server server = http.listen("127.0.0.1", 0)
    string base = format("http://127.0.0.1:{}", server.port())
    thread.Task task = thread.Task([move server]() {
        server.serve([](ref http.Request req, ref http.Response res) {
            handle(ref req, ref res)
        })
    })
    task.start()

    http.Client client
    list<string> targets = ["/", "/users/7?page=2", "/users/a%20b", "/users/", "/users/7/", "/files/docs/guide.txt"]
    for (string target in targets) {
        http.Response res = client.get(base + target)
        println("GET {} -> {} {}", target, res.status, res.text())
    }
    http.Response echo = client.post(base + "/echo", "ping", "text/plain")
    println("POST /echo -> {} {}", echo.status, echo.text())
    http.Response put = client.send(http.Request("PUT", base + "/echo"))
    println("PUT /echo -> {} {}", put.status, put.text())
    http.Response head = client.send(http.Request("HEAD", base + "/users/7"))
    println("HEAD /users/7 -> {}, {} bytes, Content-Length {}", head.status, head.body.len(), head.headers.get("content-length"))
    http.Response failed = client.get(base + "/fail")
    println("GET /fail -> {}", failed.status)
    os.exit(0)  // the server would run forever
    ret 0
}
```

```output
GET / -> 200 home
GET /users/7?page=2 -> 200 user 7 (page '2')
GET /users/a%20b -> 200 user a b (page '')
GET /users/ -> 404 no route for GET /users/
GET /users/7/ -> 404 no route for GET /users/7/
GET /files/docs/guide.txt -> 200 file docs/guide.txt
POST /echo -> 200 ping
PUT /echo -> 404 no route for PUT /echo
HEAD /users/7 -> 200, 0 bytes, Content-Length 16
GET /fail -> 500
http: GET /fail: boom
```

The response to `HEAD` is the one of `GET`, without the body. The exception of the handler gave the status 500, and the server wrote the last line on the error output.

The handler is a function of its own here; `serve` takes a lambda that calls it, because a named function is not a value ([Anonymous functions](lambdas.md)).

### Requests and responses

An `http.Request`, on the server, is read whole before the handler runs:

| Field | What it holds |
|---|---|
| `method` | `"GET"`, `"POST"`, as the client wrote it |
| `url` | the target of the request, `"/users/7?full=1"` |
| `path` | the path, `"/users/7"`, with its escapes |
| `query` | what follows `?`, `"full=1"`, with its escapes |
| `headers` | the headers, in their order |
| `body` | the bytes of the body; `text()` gives them as text, and throws if they are not UTF-8 |

An `http.Response` has `status`, 200 unless the handler sets another, `headers` and `body`:

- `res.write(text)` adds text to the body, `res.writeJson(text)` adds a text of JSON, as `res.writeJson(json.encode(user))`, and `res.writeBytes(bytes, type)` adds bytes. Each sets the `Content-Type` only if the headers do not have one yet: `text/plain; charset=utf-8`, `application/json`, or `type`.
- The response is written when the handler returns, in one piece, with its `Content-Length`. The `Content-Length`, `Connection` and `Transfer-Encoding` that the handler sets are left out, but `Connection: close` closes the connection after the response. The statuses 204 and 304 have no body.

`http.Headers` keeps the headers in their order, and compares their names without case, as HTTP wants:

```cromo
import std.http

i32 main() {
    http.Headers headers
    headers.add("Set-Cookie", "a=1")
    headers.add("Set-Cookie", "b=2")  // a name can have several headers
    headers.set("Content-Type", "text/plain")
    println("{} {}", headers.get("set-cookie"), headers.has("CONTENT-TYPE"))
    println("'{}'", headers.get("Accept"))  // "" without the header
    headers.remove("set-cookie")
    for (http.Header h in headers.items) {
        println("{}: {}", h.name, h.value)
    }
    try {
        headers.set("X-Note", "one\r\nInjected: yes")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
a=1 true
''
Content-Type: text/plain
the header X-Note of HTTP cannot contain a new line
```

`get` gives the first value, `set` replaces all the headers of a name with one, `add` adds one more, and `remove` removes them all. A value with a new line would start another header, and a name with a space, a control character or `:` is not a name: both are exceptions.

### What the server does for you

- Each connection is served by a task of its own, with no fixed limit: thousands of connections kept open cost little memory. So the handler runs on several threads at once: it is a `const func`, which cannot modify what it captures. What the requests share is in a `thread.Mutex` or a `thread.Atomic`, captured by reading.
- A connection serves its requests one after the other while the client keeps it open (keep-alive). It waits 5 seconds for the start of the next request, then 30 seconds for each read and write; past them, or when the client goes away, it ends in silence.
- Before the handler, the request is read whole, also a body sent in chunks. A head larger than 64 KB gets the status 431, a body larger than 16 MB 413, a request that is not valid 400, and a version other than HTTP/1.1 and HTTP/1.0 505. `Expect: 100-continue` is answered.
- An exception of the handler replaces its response with the status 500 and an empty body, and writes a line on the error output, as `http: GET /fail: boom`; the server goes on. A panic still ends the program.

A handler that captures with `ref` is refused:

```cromo error
import std.http

i32 main() {
    i64 count = 0
    http.serve("127.0.0.1", 8080, [ref count](ref http.Request req, ref http.Response res) {
        count++
        res.write(format("{}", count))
    })
    ret 0
}
```

```output
error: a const func does not modify what it captures: it cannot capture 'count' with 'ref'
```

A counter shared by the requests is a `thread.Atomic<i64>`, captured by reading: `[count](...) { res.write(format("{}", count.add(1) + 1)) }`.

## An HTTP client

```cromo norun
import std.http

i32 main() {
    http.Response r = http.get("https://example.com/")
    println("{} {}", r.status, r.headers.get("content-type"))
    println("{} bytes", r.body.len())
    ret 0
}
```

`http.get(url)` and `http.post(url, body, type)` send a request and give back the whole response. For the other methods, and for headers of its own, a program makes an `http.Request` with the method and the URL and sends it with `http.send`:

```cromo ignore
http.Request req = http.Request("PUT", "https://example.com/users/7")
req.headers.set("Authorization", "Bearer " + token)
req.writeJson(json.encode(user))
http.Response res = http.send(req)
```

- The URL starts with `http://` or `https://`, and can have a port; `https` uses TLS, with the checks of `net.connectTls`. A name and a password in the URL are not supported. The path and the query are sent as written: `http.escape(text)` writes a value with `%XX` for every byte that is not a letter, a digit, `-`, `_`, `.` or `~`, so `http.escape("a b&c")` gives `"a%20b%26c"`.
- The client writes `Host`, `Content-Length` and `Connection` by itself, and adds `User-Agent: cromo` when the request has no `User-Agent`.
- A status of error, as 404 or 500, is a response like the others. An exception is a URL that cannot be used, a host that is not found, a failure of the network or of TLS, a timeout, an answer that is not HTTP, or a body too large.
- Connecting, with the handshake of TLS, has no timeout; then each read and write waits at most 30 seconds.
- The responses 1xx, as `100 Continue`, are skipped.

### Redirects and compression

The client follows up to 10 redirects; more are an exception, `cannot get ...: more than 10 redirects`. After 303, and after 301 or 302 to a method other than `GET` and `HEAD`, the request goes on as a `GET` without a body, as the browsers do. `Authorization` and `Cookie` are not sent to another host.

When the request names no `Accept-Encoding`, the client asks for gzip, and gives the body decompressed, without `Content-Encoding` and with the `Content-Length` of the decompressed body. A request that names its own `Accept-Encoding` gets the body as the server sent it.

A body has at most 16 MB, also once decompressed: a few kilobytes of gzip can become gigabytes. An `http.Client` has `bodyLimit`, in bytes, to set another limit.

```cromo
import std.gzip
import std.http
import std.os
import std.thread

i32 main() {
    http.Server server = http.listen("127.0.0.1", 0)
    string base = format("http://127.0.0.1:{}", server.port())
    thread.Task task = thread.Task([move server]() {
        server.serve([](ref http.Request req, ref http.Response res) {
            if (req.matches("/old")) {
                res.status = 301
                res.headers.set("Location", "/new")
            } else if (req.matches("/new")) {
                res.write(format("{} /new, from {}", req.method, req.headers.get("user-agent")))
            } else if (req.matches("GET /report")) {
                list<u8> text = "a line of the report\n".repeat(1000).bytes()
                if (req.headers.get("accept-encoding").contains("gzip")) {
                    text = gzip.compress(text)
                    res.headers.set("Content-Encoding", "gzip")
                }
                res.writeBytes(text, "text/plain")
            } else {
                res.status = 404
            }
        })
    })
    task.start()

    http.Response moved = http.get(base + "/old")
    println("{} {}", moved.status, moved.text())
    http.Response posted = http.post(base + "/old", "data", "text/plain")
    println("{} {}", posted.status, posted.text())

    http.Client client
    http.Response report = client.get(base + "/report")
    println("{} bytes, Content-Encoding '{}'", report.body.len(), report.headers.get("content-encoding"))
    client.bodyLimit = 10000
    try {
        client.get(base + "/report")
    } catch (Exception e) {
        println("{}", e.message())
    }
    os.exit(0)
    ret 0
}
```

```output
200 GET /new, from cromo
200 GET /new, from cromo
21000 bytes, Content-Encoding ''
cannot decompress: the data are larger than 10000 bytes
```

The server of HTTP does not compress by itself: a handler can, with `std.gzip`, as above. It keeps its limit of 16 MB for the bodies of the requests.

### Keeping the connections: http.Client

`http.get`, `http.post` and `http.send` open a connection for each request. An `http.Client` keeps the connections open after a response, to use them again for the next requests to the same host, which makes many requests much faster:

- It keeps at most 8 idle connections for each host; the oldest go first.
- A kept connection that the server has closed meanwhile fails before any byte of the response arrives: then the request is sent again, once, on a new connection, also a `POST`, since the server has not received it.
- Its methods, `get`, `post` and `send`, are `const`: several tasks can use the same client at once, capturing it by reading, each request on a connection of its own.
- The destructor closes the connections. It can be moved, not copied.

```cromo
import std.http
import std.json
import std.os
import std.thread

struct User {
    i64 id
    string name
}

i32 main() {
    http.Server server = http.listen("127.0.0.1", 0)
    string base = format("http://127.0.0.1:{}", server.port())
    thread.Task task = thread.Task([move server]() {
        server.serve([](ref http.Request req, ref http.Response res) {
            if (req.matches("GET /users/{id}")) {
                User user
                user.id = i64(req.param("id"))
                user.name = "user " + req.param("id")
                res.writeJson(json.encode(user))
            } else if (req.matches("POST /users")) {
                User user = json.decode<User>(req.text())
                res.status = 201
                res.headers.set("Location", format("/users/{}", user.id))
                res.writeJson(json.encode(user))
            } else {
                res.status = 404
            }
        })
    })
    task.start()

    http.Client client
    thread.Atomic<i64> total
    thread.Group jobs
    for (i64 id = 1; id <= 20; id++) {
        jobs.start([client, base, id, total]() {
            http.Response res = client.get(format("{}/users/{}", base, id))
            User user = json.decode<User>(res.text())
            total.add(user.id)
        })
    }
    jobs.wait()
    println("the sum of the ids: {}", total.load())

    User ada
    ada.id = 99
    ada.name = "Ada"
    http.Request req = http.Request("POST", base + "/users")
    req.writeJson(json.encode(ada))
    http.Response created = client.send(req)
    println("{} {} {}", created.status, created.headers.get("location"), created.text())
    os.exit(0)
    ret 0
}
```

```output
the sum of the ids: 210
201 /users/99 {"id":99,"name":"Ada"}
```

A whole service, with its data and its tests, is in [Final project: a web service](web-service.md); [JSON](json.md) explains `json.encode` and `json.decode`.

## WebSocket

A WebSocket keeps a connection open in both directions, for messages. A handler of the server turns its request into one with `req.webSocket(work)`, and a client opens one with `websocket.connect(url)`. Both sides then use a `websocket.Socket`:

```cromo
import std.http
import std.os
import std.thread
import std.websocket

i32 main() {
    http.Server server = http.listen("127.0.0.1", 0)
    i32 port = server.port()
    thread.Task task = thread.Task([move server]() {
        server.serve([](ref http.Request req, ref http.Response res) {
            if (req.matches("GET /echo")) {
                req.webSocket([](ref websocket.Socket ws) {
                    for {
                        switch (ws.receive()) {  // websocket.Closed ends the loop
                            case Text(string text) {
                                ws.send("echo: " + text)
                            }
                            case Binary(list<u8> bytes) {
                                ws.sendBytes(bytes)
                            }
                        }
                    }
                })
            } else {
                res.status = 404
            }
        })
    })
    task.start()

    websocket.Socket ws = websocket.connect(format("ws://127.0.0.1:{}/echo", port))
    ws.timeout(5000)
    ws.send("hello")
    ws.sendBytes([u8(1), 2, 3])
    for (i64 i = 0; i < 2; i++) {
        switch (ws.receive()) {
            case Text(string text) {
                println("text: {}", text)
            }
            case Binary(list<u8> bytes) {
                println("{} bytes", bytes.len())
            }
        }
    }
    ws.close()
    try {
        ws.receive()
    } catch (websocket.Closed e) {
        println("{} (code {})", e.message(), e.code)
    }
    os.exit(0)
    ret 0
}
```

```output
text: echo: hello
3 bytes
the WebSocket is closed: 1000 (code 1000)
```

On the server:

- `req.webSocket(work)` answers the request with the status 101 and runs `work` with the socket, inside the handler, on the task of the connection: `work` can use the variables of the handler and what it captures, as a mutex. When `work` returns or throws, the connection ends, without the response of the handler; an exception of `work` is not written on the error output.
- A request that is not one of WebSocket gets the status 400, and `work` is not called.

On the client:

- `websocket.connect(url)` takes a URL that starts with `ws://`, or `wss://` for TLS; without a port, 80 or 443. Connecting has no timeout, then the server must answer within 30 seconds.
- A server that does not accept the WebSocket is an exception with its answer: `cannot open the WebSocket ws://127.0.0.1:8080/other: the server answered 'HTTP/1.1 404 Not Found'`.

On both sides:

- `send(text)` sends a message of text, `sendBytes(bytes)` a binary one.
- `receive()` waits for the next message and gives a `websocket.Message`, an enum with the cases `Text(string text)` and `Binary(list<u8> bytes)`. It joins the fragments of a message, and answers the pings that arrive meanwhile. A message larger than 16 MB, a frame that is not valid, or a text that is not UTF-8, is an exception.
- Without `timeout(ms)` a socket waits as long as it takes; with it, each read and write waits at most that long.
- `close()` ends the WebSocket cleanly: it sends the close with the code 1000, waits for the answer of the other side, dropping the messages that arrive meanwhile, and closes the connection.
- After a close, `receive()`, `send` and `sendBytes` throw `websocket.Closed`, an exception with `code` and `reason`: 1000 a normal end, 1001 a side that goes away, 1005 a close without a code, 1006 a connection that ended without a close. `receive()` throws it too when the other side closes, after answering its close.
- The destructor ends the connection without a close, which the other side sees as 1006: `close()` is the clean way.

## Compression: std.gzip

`std.gzip` reads and writes the format of gzip, the files `.gz` and the bodies of HTTP with `Content-Encoding: gzip`:

```cromo
import std.gzip

i32 main() {
    list<u8> text = "to be or not to be, ".repeat(500).bytes()
    list<u8> packed = gzip.compress(text)
    println("{} bytes, compressed {}", text.len(), packed.len())
    println("{} bytes again", gzip.decompress(packed).len())
    try {
        gzip.decompressUpTo(packed, 1000)
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        gzip.decompress("not gzip".bytes())
    } catch (Exception e) {
        println("{}", e.message())
    }

    // The deflate inside: without the 10 bytes of the header of gzip and the 8 of its end.
    list<u8> deflate
    for (i64 i = 10; i < packed.len() - 8; i++) {
        deflate.add(packed[i])
    }
    println("{} bytes from deflate alone", gzip.inflate(deflate, 1000000).len())
    println("{:x}", gzip.crc32("123456789".bytes()))
    ret 0
}
```

```output
10000 bytes, compressed 116
10000 bytes again
cannot decompress: the data are larger than 1000 bytes
cannot decompress: the data are not gzip
10000 bytes from deflate alone
cbf43926
```

- `compress(data)` writes data that any gzip reads. It uses only the fixed codes of deflate, so the result is about a fifth larger than that of `gzip -9`.
- `decompress(data)` checks the CRC-32 and the size, and throws on data that are damaged or cut. A gzip of several members, one after the other, gives them joined.
- `decompressUpTo(data, limit)` throws when the result would be larger than `limit` bytes: use it for the data that come from the network.
- `inflate(data, limit)` reads deflate without the header and the check of gzip around it, as a zip holds it, with a limit too.
- `crc32(data)` gives the CRC-32 of the data, the check of gzip and of zip.

## Cryptography: std.crypto

`std.crypto` has the building blocks that network programs need. The bytes are `list<u8>`; a key or a nonce of the wrong size, and a text that is not hex or base64, are exceptions of type `Error`.

### Hashes, HMAC and encodings

```cromo
import std.crypto

i32 main() {
    list<u8> data = "hello".bytes()
    println("{}", crypto.hex(crypto.sha256(data)))
    println("{}", crypto.hex(crypto.hmac(crypto.Hash.Sha256, "key".bytes(), data)))

    crypto.Hasher hasher = crypto.Hasher(crypto.Hash.Sha256)
    hasher.add("hel".bytes())  // the data in pieces, as a large file
    hasher.add("lo".bytes())
    println("{}", crypto.hex(hasher.finish()))

    println("{} {}", crypto.base64("hi?>".bytes()), crypto.base64Url("hi?>".bytes()))
    println("{:?}", crypto.fromHex("00FFa0"))
    try {
        crypto.fromBase64("aGk_Pg")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
9307b3b915efb5171ff14d8cb55fbcc798c6c0ef1456d66ded1a6aa723a58b7b
2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
aGk/Pg== aGk_Pg
[0, 255, 160]
cannot read the base64: its length is not valid
```

- `md5`, `sha1`, `sha256`, `sha384` and `sha512` give the hash of the data: 16, 20, 32, 48 and 64 bytes. MD5 and SHA-1 are broken: they are there for the formats that still use them, as WebSocket.
- `hmac(hash, key, data)` proves that who made it has the key, with `crypto.Hash`, an enum with the cases `Md5`, `Sha1`, `Sha256`, `Sha384` and `Sha512`. To check an HMAC received, compare all its bytes, not only up to the first that differs.
- `crypto.Hasher(hash)` computes a hash in pieces: `add` adds data, `finish()` gives the hash and starts again, empty. It can be moved, not copied.
- `hex(bytes)` writes two lowercase digits for each byte; `fromHex(text)` reads them in either case, and throws on an odd number of digits or a character that is not a digit.
- `base64(bytes)` writes base64 with its padding `=`, which `fromBase64(text)` requires. `base64Url(bytes)` writes the form for URLs, with `-` and `_` and without the padding; `fromBase64Url(text)` reads it with or without.
- `random(count)` gives bytes from the generator of the system, good for keys and nonces: an exception if the system gives none.

### Encryption

`crypto.encrypt(cipher, key, nonce, data, extra)` encrypts and authenticates the data, with `crypto.Cipher.AesGcm` or `crypto.Cipher.ChaCha20Poly1305`, and `crypto.decrypt` with the same arguments gives them back:

```cromo
import std.crypto

i32 main() {
    list<u8> key = crypto.random(32)
    list<u8> nonce = crypto.random(12)  // a new one for each message
    list<u8> header = "message 1".bytes()
    list<u8> sealed = crypto.encrypt(crypto.Cipher.ChaCha20Poly1305, key, nonce, "secret".bytes(), header)
    println("{} bytes", sealed.len())
    println("{}", string(crypto.decrypt(crypto.Cipher.ChaCha20Poly1305, key, nonce, sealed, header)))

    sealed[0] = sealed[0] ^ 1  // one bit changed on the way
    try {
        crypto.decrypt(crypto.Cipher.ChaCha20Poly1305, key, nonce, sealed, header)
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        crypto.encrypt(crypto.Cipher.AesGcm, crypto.random(20), nonce, "x".bytes(), list<u8>())
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
22 bytes
secret
cannot decrypt: the data are not authentic, or the key, the nonce or the extra data are wrong
cannot encrypt: a key of AES-GCM has 16, 24 or 32 bytes, not 20
```

- The key has 16, 24 or 32 bytes for AES-GCM, and 32 for ChaCha20-Poly1305.
- The nonce has 12 bytes, and must never be used twice with the same key: a new `random(12)` for each message is the simple way, sent with the message, in clear.
- The result is the data encrypted, followed by a tag of 16 bytes: 6 bytes of data gave 22.
- The extra data are authenticated but not encrypted, and not in the result, as a header sent in clear: `decrypt` needs the same ones. Often they are empty, `list<u8>()`.
- `decrypt` throws when anything is not as it was: the data, the tag, the key, the nonce or the extra data. The message does not say which.

### Signatures and key exchange

Ed25519 signs messages: the private key is 32 random bytes, and the public key, of 32 bytes too, comes from it. X25519 lets two sides compute the same secret from their private keys and each other's public key:

```cromo
import std.crypto

i32 main() {
    list<u8> privateKey = crypto.random(32)
    list<u8> publicKey = crypto.ed25519PublicKey(privateKey)
    list<u8> message = "pay 10 to Ada".bytes()
    list<u8> signature = crypto.ed25519Sign(privateKey, message)
    println("{} bytes, valid: {}", signature.len(), crypto.ed25519Verify(publicKey, message, signature))
    println("changed: {}", crypto.ed25519Verify(publicKey, "pay 99 to Ada".bytes(), signature))

    list<u8> mine = crypto.random(32)
    list<u8> theirs = crypto.random(32)
    list<u8> mySecret = crypto.x25519(mine, crypto.x25519PublicKey(theirs))
    list<u8> theirSecret = crypto.x25519(theirs, crypto.x25519PublicKey(mine))
    println("the same secret: {}", crypto.hex(mySecret) == crypto.hex(theirSecret))
    ret 0
}
```

```output
64 bytes, valid: true
changed: false
the same secret: true
```

- `ed25519Sign` gives a signature of 64 bytes, always the same for the same key and message. `ed25519Verify` gives `false` also for a key or a signature of the wrong size, never an exception.
- `x25519` gives a secret of 32 bytes, and throws for a public key that is not valid, one that would make the secret zero. A key for a cipher is better made from the secret with a hash, as `sha256`.
- A private key that does not have 32 bytes is an exception: `the private key of Ed25519 has 32 bytes, not 16`.

## Tokens: JWT

A service that has many clients often recognizes them by a JSON Web Token, which it signs when they log in, or which a provider of identity such as Keycloak signs for it. `std.jwt` signs and verifies them, and the claims are a struct of the program:

```cromo
import std.jwt

struct Session {
    string sub
    string role
    i64 exp
}

i32 main() {
    jwt.Key key = jwt.secret(jwt.Algorithm.Hs256, "a secret of at least 32 bytes!!!".bytes())
    Session session
    session.sub = "anna"
    session.role = "admin"
    session.exp = 4102444800  // 1 January 2100
    string token = jwt.sign(session, key)
    println("{}", token)

    Session back = jwt.verify<Session>(token, key)
    println("{} is {}", back.sub, back.role)

    session.exp = 1000000000  // 2001
    try {
        Session old = jwt.verify<Session>(jwt.sign(session, key), key)
    } catch (jwt.Invalid e) {
        println("refused: {}", e.message())
    }
    ret 0
}
```

```output
eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJhbm5hIiwicm9sZSI6ImFkbWluIiwiZXhwIjo0MTAyNDQ0ODAwfQ._8C6CxM0js35LjfNm-nPjDEC94QszRTMVLAB4QtXlcQ
anna is admin
refused: the token has expired
```

- `jwt.secret` makes a key of HMAC, HS256, HS384 or HS512, at least as long as the hash; `jwt.publicKey` and `jwt.privateKey` read a key in PEM for RS256, RS384, RS512, ES256, ES384 or EdDSA.
- The algorithm is the one of the key, never the one written in the token: a token with `"alg": "none"`, or signed in HS256 for a key of RSA, is refused.
- `jwt.verify` checks the signature, and `exp` and `nbf` when they are there. A `jwt.Verifier` takes several keys, also from the JWK Set of a provider with `jwt.keySet(text)`, and checks the issuer and the audience too, with a `leeway` in seconds for the clocks that differ.
- A refused token is a `jwt.Invalid`, whose `reason` says why: `Expired`, `BadSignature`, `UnknownKey`, and the others.
- The times are seconds from 1970: `time.unix() + 3600` is an hour from now.

## Summary

- `std.net` has TCP, UDP and TLS, with calls that block and leave the thread to other tasks while they wait. Without `timeout(ms)` an operation waits as long as it takes; `connect` has no timeout of its own.
- `read` gives what has arrived, an empty list at the end; `write` writes everything; `close()` and the destructor close.
- TLS verifies the certificate and the name of the server against the authorities of the system, or those of `SSL_CERT_FILE`; a server makes its handshake at the first `read` or `write`.
- `http.serve` runs a `const func` handler for each request, a task for each connection, with routes chosen by `req.matches`; limits and timeouts protect the server.
- `http.get`, `http.post` and `http.send` make requests, follow redirects and decompress gzip; `http.Client` keeps the connections and serves several tasks at once.
- `std.websocket` sends and receives messages in both directions, and ends with `close()` and `websocket.Closed`.
- `std.gzip`, `std.crypto` and `std.jwt` complete the toolbox, with no external library.
