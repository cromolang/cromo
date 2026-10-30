# Networking and HTTP

The standard library has everything a network program needs: TCP and UDP in `std.net`, TLS for encrypted connections, HTTP clients and servers in `std.http`, WebSocket in `std.websocket`, and the cryptography and compression they rely on. It needs no other library: TLS comes from Mbed TLS, compiled into the runtime of Cromo, so the executables stay self-contained.

The calls block until they are done, as in C. In a task, a call that waits for the network leaves its thread to the other tasks, so a server written in the simplest way, one task for each connection, serves thousands of clients at once.

## TCP

A server listens with `net.listen`, accepts connections with `accept()`, and reads and writes on each `net.Connection`. A client connects with `net.connect`. The program below is both: its server runs in a task, and its client talks to it.

```cromo
import std.net
import std.thread

i32 main() {
    net.Listener listener = net.listen("127.0.0.1", 0)   // port 0: any free port
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

- `read(most)` waits for data and returns up to `most` bytes; an empty list means that the other side has closed.
- `timeout(ms)` limits every following operation; a timeout, as any network error, is an exception.
- The destructor of a connection closes it.
- `net.udp(host, port)` opens a UDP socket, with `send` and `receive`; `net.resolve(host)` gives the addresses of a name. IPv4 and IPv6 work on both systems.

## TLS

`net.connectTls` opens an encrypted connection, with the same `read` and `write`:

```cromo norun
import std.net

i32 main() {
    net.Connection site = net.connectTls("example.com", 443)
    site.writeText("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
    println("{}", string(site.read(200)))
    ret 0
}
```

The certificate of the server must be signed by an authority that the system trusts, and name the host; otherwise the connection fails with an exception that says why, as `the certificate has expired`. A server uses `net.listenTls(host, port, certificate, key)`, with the certificate and the key as texts in PEM.

## An HTTP server

`http.serve` gives every request to one function, the handler, which fills the response:

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

- Each connection is served by a task of its own, so the handler runs on several threads at once. It is a `const func`: what the requests share must be in a `thread.Mutex` or a `thread.Atomic`.
- `req.matches("GET /users/{id}")` checks the method and the path, and `req.param("id")` reads the part in braces; `{rest...}` takes the rest of the path. `req.queryValue("page")` reads the query.
- `res.write`, `res.writeJson` and `res.writeBytes` write the body and set its `Content-Type`; `res.status` is 200 by default; `res.headers` holds the headers.
- An exception in the handler gives the status 500, and the server goes on.
- The server keeps the connections alive between requests, and reads and writes bodies in chunks.

`http.listen(host, port)` gives a `http.Server` without starting it, whose `port()` is known before `serve(handler)`: useful with port 0, and in tests.

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

`http.get(url)`, `http.post(url, body, type)` and `http.send(request)` follow redirects and use TLS for `https`. A status of error, as 404, is a response like the others; a failure of the network is an exception. The client asks for gzip and decompresses the body by itself.

An `http.Client` keeps the connections open between requests to the same host, which makes many requests much faster, and can be used by several tasks at once.

The server and the client together, in one program:

```cromo
import std.http
import std.os
import std.thread

i32 main() {
    http.Server server = http.listen("127.0.0.1", 0)
    i32 port = server.port()
    thread.Task task = thread.Task([move server]() {
        server.serve([](ref http.Request req, ref http.Response res) {
            if (req.matches("GET /hello/{name}")) {
                res.write("Hello, " + req.param("name") + "!")
            } else {
                res.status = 404
            }
        })
    })
    task.start()

    http.Client client
    http.Response ok = client.get(format("http://127.0.0.1:{}/hello/Ada", port))
    http.Response missing = client.get(format("http://127.0.0.1:{}/other", port))
    println("{} {}", ok.status, ok.text())
    println("{}", missing.status)
    os.exit(0)   // the server would run forever
    ret 0
}
```

```output
200 Hello, Ada!
404
```

## WebSocket

A WebSocket keeps a connection open in both directions, for messages. The server takes it from a request with `req.webSocket`, and a client opens one with `websocket.connect`:

```cromo norun
import std.http
import std.websocket

i32 main() {
    http.serve("0.0.0.0", 8080, [](ref http.Request req, ref http.Response res) {
        if (req.matches("GET /echo")) {
            req.webSocket([](ref websocket.Socket ws) {
                for {
                    switch (ws.receive()) {
                        case Text(string text) {
                            ws.send("echo: " + text)
                        }
                        case Binary(list<u8> bytes) {
                            ws.sendBytes(bytes)
                        }
                    }
                }
            })
        }
    })
    ret 0
}
```

`receive()` throws `websocket.Closed` when the other side closes, which ends the loop above. `websocket.connect("wss://...")` opens one with TLS.

## Cryptography and compression

`std.crypto` has the building blocks that network programs need, and `std.gzip` compresses:

```cromo
import std.crypto
import std.gzip

i32 main() {
    list<u8> data = "hello".bytes()
    println("{}", crypto.hex(crypto.sha256(data)))
    println("{}", crypto.base64(data))

    list<u8> key = crypto.random(32)
    list<u8> nonce = crypto.random(12)
    list<u8> sealed = crypto.encrypt(crypto.Cipher.ChaCha20Poly1305, key, nonce, data, list<u8>())
    list<u8> opened = crypto.decrypt(crypto.Cipher.ChaCha20Poly1305, key, nonce, sealed, list<u8>())
    println("{} bytes sealed, opened: {}", sealed.len(), string(opened))

    list<u8> text = "abc".repeat(1000).bytes()
    list<u8> packed = gzip.compress(text)
    println("{} bytes, compressed below 100: {}", text.len(), packed.len() < 100)
    println("{} bytes again", gzip.decompress(packed).len())
    ret 0
}
```

```output
2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
aGVsbG8=
21 bytes sealed, opened: hello
3000 bytes, compressed below 100: true
3000 bytes again
```

- Hashes: `md5`, `sha1`, `sha256`, `sha512`, and `hmac`; `random(n)` takes bytes from the generator of the system.
- `encrypt` and `decrypt` with AES-GCM or ChaCha20-Poly1305: `decrypt` throws if the data were changed.
- Ed25519 signatures and X25519 key exchange; `hex` and `base64` with their inverses.
- `gzip.decompressUpTo(data, limit)` refuses data that would grow beyond a limit, for data from the network.

## Summary

- `std.net` has TCP, UDP and TLS, with blocking calls that leave the thread to other tasks while they wait.
- `http.serve` runs a handler for each request, on many tasks at once; `http.get` and `http.Client` make requests.
- `std.websocket`, `std.crypto` and `std.gzip` complete the toolbox, with no external library.
