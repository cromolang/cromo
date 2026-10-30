# Rete e HTTP

La libreria standard ha tutto ciò che serve a un programma di rete: TCP e UDP in `std.net`, TLS per le connessioni cifrate, client e server HTTP in `std.http`, WebSocket in `std.websocket`, e la crittografia e la compressione su cui si appoggiano. Non richiede altre librerie: TLS viene da Mbed TLS, compilato nel runtime di Cromo, così gli eseguibili restano autosufficienti.

Le chiamate bloccano finché non hanno finito, come in C. In un task, una chiamata che aspetta la rete lascia il suo thread agli altri task, così un server scritto nel modo più semplice, con un task per ogni connessione, serve migliaia di client contemporaneamente.

## TCP

Un server si mette in ascolto con `net.listen`, accetta le connessioni con `accept()`, e legge e scrive su ogni `net.Connection`. Un client si connette con `net.connect`. Il programma qui sotto fa entrambe le cose: il suo server gira in un task, e il suo client ci dialoga.

```cromo
import std.net
import std.thread

i32 main() {
    net.Listener listener = net.listen("127.0.0.1", 0)   // porta 0: una porta libera qualsiasi
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

- `read(most)` attende dei dati e restituisce fino a `most` byte; una lista vuota significa che l'altro capo ha chiuso.
- `timeout(ms)` pone un limite a ogni operazione successiva; un timeout, come qualsiasi errore di rete, è un'eccezione.
- Il distruttore di una connessione la chiude.
- `net.udp(host, port)` apre un socket UDP, con `send` e `receive`; `net.resolve(host)` restituisce gli indirizzi associati a un nome. IPv4 e IPv6 funzionano su entrambi i sistemi.

## TLS

`net.connectTls` apre una connessione cifrata, con gli stessi `read` e `write`:

```cromo norun
import std.net

i32 main() {
    net.Connection site = net.connectTls("example.com", 443)
    site.writeText("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
    println("{}", string(site.read(200)))
    ret 0
}
```

Il certificato del server deve essere firmato da un'autorità di cui il sistema si fida, e riportare il nome dell'host; altrimenti la connessione fallisce con un'eccezione che spiega il motivo, come `the certificate has expired`. Un server usa `net.listenTls(host, port, certificate, key)`, con il certificato e la chiave come testi in formato PEM.

## Un server HTTP

`http.serve` passa ogni richiesta a un'unica funzione, l'handler, che riempie la risposta:

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

Eseguilo e apri `http://localhost:8080/hello/world` in un browser.

- Ogni connessione è servita da un task tutto suo, quindi l'handler gira su più thread contemporaneamente. È una `const func`: ciò che le richieste condividono deve stare in un `thread.Mutex` o in un `thread.Atomic`.
- `req.matches("GET /users/{id}")` controlla il metodo e il percorso, e `req.param("id")` legge la parte tra graffe; `{rest...}` prende il resto del percorso. `req.queryValue("page")` legge la query.
- `res.write`, `res.writeJson` e `res.writeBytes` scrivono il corpo e ne impostano il `Content-Type`; `res.status` vale 200 per impostazione predefinita; `res.headers` contiene gli header.
- Un'eccezione nell'handler produce lo stato 500, e il server va avanti.
- Il server mantiene attive le connessioni tra una richiesta e l'altra, e legge e scrive i corpi a blocchi.

`http.listen(host, port)` restituisce un `http.Server` senza avviarlo, la cui `port()` è nota prima di `serve(handler)`: è utile con la porta 0, e nei test.

## Un client HTTP

```cromo norun
import std.http

i32 main() {
    http.Response r = http.get("https://example.com/")
    println("{} {}", r.status, r.headers.get("content-type"))
    println("{} bytes", r.body.len())
    ret 0
}
```

`http.get(url)`, `http.post(url, body, type)` e `http.send(request)` seguono i redirect e usano TLS per `https`. Uno stato di errore, come 404, è una risposta come le altre; un guasto della rete è un'eccezione. Il client chiede gzip e decomprime il corpo da solo.

Un `http.Client` tiene aperte le connessioni tra richieste allo stesso host, il che rende molto più veloci le richieste numerose, e può essere usato da più task contemporaneamente.

Il server e il client insieme, in un solo programma:

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
    os.exit(0)   // il server andrebbe avanti all'infinito
    ret 0
}
```

```output
200 Hello, Ada!
404
```

## WebSocket

Un WebSocket tiene aperta una connessione in entrambe le direzioni, per lo scambio di messaggi. Il server lo ottiene da una richiesta con `req.webSocket`, e un client ne apre uno con `websocket.connect`:

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

`receive()` lancia `websocket.Closed` quando l'altro capo chiude, e questo fa terminare il ciclo qui sopra. `websocket.connect("wss://...")` ne apre uno con TLS.

## Crittografia e compressione

`std.crypto` offre i mattoni di base che servono ai programmi di rete, e `std.gzip` comprime:

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

- Hash: `md5`, `sha1`, `sha256`, `sha512` e `hmac`; `random(n)` prende i byte dal generatore del sistema.
- `encrypt` e `decrypt` con AES-GCM o ChaCha20-Poly1305: `decrypt` lancia un'eccezione se i dati sono stati alterati.
- Firme Ed25519 e scambio di chiavi X25519; `hex` e `base64` con le rispettive inverse.
- `gzip.decompressUpTo(data, limit)` rifiuta i dati che supererebbero un certo limite una volta decompressi, per i dati che arrivano dalla rete.

## Riepilogo

- `std.net` ha TCP, UDP e TLS, con chiamate bloccanti che, mentre attendono, lasciano il thread agli altri task.
- `http.serve` esegue un handler per ogni richiesta, su molti task contemporaneamente; `http.get` e `http.Client` effettuano le richieste.
- `std.websocket`, `std.crypto` e `std.gzip` completano la cassetta degli attrezzi, senza librerie esterne.
