# Rete e HTTP

La libreria standard ha tutto ciò che serve a un programma di rete, e non richiede altre librerie:

| Modulo | Per |
|---|---|
| [std.net](std.net.html) | TCP e UDP, i nomi degli host, TLS |
| [std.http](std.http.html) | server e client HTTP |
| [std.websocket](std.websocket.html) | WebSocket, server e client |
| [std.gzip](std.gzip.html) | la compressione nel formato gzip |
| [std.crypto](std.crypto.html) | hash, chiavi casuali, cifrari e firme |
| [std.jwt](std.jwt.html) | JSON Web Token |

TLS viene da Mbed TLS, compilato nel runtime di Cromo, così gli eseguibili restano autosufficienti, su Windows e su Linux.

Le chiamate bloccano finché non hanno finito, come in C. In un task, una chiamata che aspetta la rete lascia il suo thread agli altri task, così un server scritto nel modo più semplice, con un task per ogni connessione, serve migliaia di client contemporaneamente: [Thread, task e canali](concurrency.md) spiega i task. Un errore di rete è un'eccezione di tipo `Error`, il cui messaggio dice che cosa non si è potuto fare e perché, come `cannot connect to example.com:80: connection refused`.

Il capitolo spiega le idee e le regole; la pagina di ogni modulo, collegata qui sopra, elenca tutte le sue funzioni.

## TCP

Un server si mette in ascolto con `net.listen`, accetta le connessioni con `accept()`, e legge e scrive su ogni `net.Connection`. Un client si connette con `net.connect`. Il programma qui sotto fa entrambe le cose: il suo server gira in un task, e il suo client ci dialoga.

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

- `net.listen(host, port)` si mette in ascolto su un indirizzo: `"127.0.0.1"` solo per questo computer, `"0.0.0.0"` per tutti i suoi indirizzi IPv4, `"::"` per quelli IPv6. La porta 0 chiede al sistema una porta libera, che `port()` restituisce.
- `accept()` attende la connessione successiva e la restituisce.
- `net.connect(host, port)` accetta un nome o un indirizzo, e prova a turno gli indirizzi del nome finché uno non risponde.
- Listener e connessioni si possono spostare, come nel task qui sopra, ma non copiare.

### Leggere e scrivere

`read(most)` attende che arrivino dei byte e ne restituisce fino a `most`, che possono essere meno di quelli che l'altro capo ha scritto: TCP trasporta un flusso di byte, non dei messaggi. Una lista vuota significa che l'altro capo ha chiuso la connessione. Perciò un programma che legge tutto ciò che arriva legge in un ciclo:

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

- `write(bytes)` e `writeText(text)` scrivono tutto prima di tornare, attendendo finché il sistema non ha spazio; quando tornano, non trattengono nulla.
- `close()` chiude la connessione prima del suo distruttore, che altrimenti lo fa da sé. Un secondo `close()` non fa nulla; un `read` o un `write` successivo è un'eccezione.
- `listener.close()` smette di ascoltare: dopo, `accept()` lancia un'eccezione, e le connessioni già accettate restano aperte.

### Timeout ed errori

Senza un timeout, un'operazione attende tutto il tempo necessario. `timeout(ms)` imposta un limite in millisecondi che vale per ogni `read` e `write` successivo di una connessione, o per ogni `accept()` di un listener; superato il limite, l'operazione lancia un'eccezione. Con `-1` si torna ad attendere tutto il tempo necessario.

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

La connessione qui sopra si stabilisce anche se `accept()` non viene chiamato di nuovo: il sistema accetta da sé le connessioni di un listener, e `accept()` si limita a prendere la successiva.

- `net.connect` non ha un timeout proprio: attende la connessione quanto la attende il sistema, e `timeout` vale dalla connessione in poi.
- Una porta usata da un altro programma è un'eccezione: `cannot use 127.0.0.1 port 8080: the address is in use`. Su Linux un listener usa `SO_REUSEADDR`, così un server riavviato riprende subito la sua porta.

### Molti client

Un server affida ogni connessione a un task tutto suo, che la prende con `move`, così un client lento non fa aspettare gli altri. Un `thread.Group` avvia i task:

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

La lambda di un task viene eseguita una sola volta, quindi può modificare ciò che prende con `move`, come `client` qui sopra. `accept()` è un metodo `const`: anche più task o thread possono accettare dallo stesso listener, catturandolo in lettura, come fa il server HTTP di questo capitolo.

## Nomi e indirizzi

`net.resolve(host)` restituisce gli indirizzi di un nome, come testo; un indirizzo scritto in cifre restituisce sé stesso. IPv6 viene scritto nella forma breve della RFC 5952, con la sequenza di zeri più lunga resa con `::`:

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

IPv4 e IPv6 funzionano su entrambi i sistemi. Un nome può avere più indirizzi: `localhost` è spesso `::1` e `127.0.0.1`. `net.connect` li prova a turno, mentre `net.listen` si mette in ascolto solo sul primo; perciò un server riservato a questo computer è più chiaro con un indirizzo, `"127.0.0.1"`, che con `"localhost"`.

Da dove vengono gli indirizzi dipende dal sistema:

- Su **Windows** risponde il resolver del sistema, quello che usano tutti i programmi.
- Su **Linux** un programma Cromo non usa la libreria C, e un resolver scritto in Cromo legge prima `/etc/hosts`; poi interroga i server di `/etc/resolv.conf` nel loro ordine, con UDP, prima gli indirizzi IPv4 e poi quelli IPv6, attendendo due secondi per ogni server, e `127.0.0.1` quando il file non ne indica nessuno. Non usa ancora i domini `search` del file per i nomi brevi, né TCP per le risposte che non stanno in UDP.

## UDP

`net.udp(host, port)` restituisce un `net.Udp`, un socket legato a un indirizzo e a una porta. Invia e riceve messaggi, ciascuno per intero o per niente, senza connessione, e senza garanzia che arrivino, né in quale ordine:

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

- `send(host, port, bytes)` invia un messaggio, a un nome o a un indirizzo della stessa versione di IP del socket: un socket legato a `"0.0.0.0"` invia a IPv4, uno legato a `"::"` a IPv6. Un messaggio perso per strada non è un errore.
- `receive(most)` attende il messaggio successivo e restituisce un `net.Datagram`, con `data`, e l'`host` e la `port` da cui proviene. Lascia spazio per il messaggio più grande: uno più lungo viene troncato a `most` byte su Linux, ed è un'eccezione su Windows.
- `timeout(ms)`, `port()` e `close()` funzionano come per TCP.

## TLS

`net.connectTls(host, port)` apre una connessione cifrata con TLS, una `net.Connection` con gli stessi `read`, `write`, `timeout` e `close`:

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

- Le versioni sono TLS 1.2 e TLS 1.3.
- Il certificato del server viene sempre verificato: deve essere firmato da un'autorità di cui il sistema si fida, essere valido oggi e riportare il nome dell'host. Altrimenti la connessione fallisce con un'eccezione che spiega il motivo:
  - `cannot connect to example.com:443 with TLS: the certificate is not signed by an authority that the system trusts`
  - `... the certificate is not for example.com`
  - `... the certificate has expired`
  - `... the certificate is not valid yet`
  - `... what arrives is not TLS`, quando l'altro capo non parla TLS.
- Le autorità sono quelle del sistema, che si aggiornano con esso, comprese quelle che aggiunge un'azienda: su Windows il suo archivio dei certificati radice, e Windows stesso per quelli che scarica solo quando una catena ne ha bisogno; su Linux il file della distribuzione, come `/etc/ssl/certs/ca-certificates.crt`.
- La variabile d'ambiente `SSL_CERT_FILE`, quando è impostata, indica un file PEM i cui certificati sostituiscono quelli del sistema, come per OpenSSL, curl e Go: così un test può fidarsi di un'autorità tutta sua. Un file senza certificati non ne lascia nessuno, e le connessioni falliscono con `no certificates in ..., the file of SSL_CERT_FILE`. Le autorità vengono lette una volta sola, alla prima connessione.
- I certificati revocati non vengono controllati, come in Go: OCSP e le liste dei certificati revocati richiedono altre connessioni, e i certificati del web durano pochi mesi.
- `timeout()` vale anche per l'handshake. Come `connect`, `connectTls` non ha un timeout proprio: attende la connessione e l'handshake per tutto il tempo che impiega l'altro capo.

Un server si mette in ascolto con `net.listenTls(host, port, certificate, key)`: il certificato in PEM, seguito da quelli della sua catena, e la sua chiave privata in PEM, entrambi come testi. Il suo `accept()` restituisce subito una connessione, e l'handshake avviene al primo `read` o `write` della connessione, nel task che la serve, così un client lento od ostile non fa attendere `accept()`. Un errore dell'handshake è l'eccezione di quel `read` o `write`.

Il programma qui sotto ha un server il cui certificato è firmato da sé stesso: un client TCP semplice non riceve nulla, e un client TLS rifiuta il certificato. Le funzioni `certificate()` e `privateKey()` restituiscono i due testi in PEM; si trovano nel programma completo.

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

`net.listenTls` legge subito i testi, e li rifiuta con un'eccezione: `cannot listen with TLS: the key is not the one of the certificate`, oppure `the certificate is not valid PEM`, oppure `the key is not a valid private key in PEM`, seguiti dal dettaglio di Mbed TLS.

La fine di una connessione TLS:

- `read()` restituisce una lista vuota quando l'altro capo termina la connessione con il messaggio TLS che lo annuncia, close_notify, e anche quando la connessione termina senza, come in Go: i protocolli, come HTTP, conoscono la lunghezza di ciò che inviano.
- `close()` e il distruttore inviano close_notify dopo un handshake andato a buon fine, ignorando gli errori.

## Un server HTTP

`http.serve(host, port, handler)` si mette in ascolto e passa ogni richiesta a un'unica funzione, l'handler, che legge la `http.Request` e riempie la `http.Response`. Non ritorna mai:

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

`http.listen(host, port)` restituisce un `http.Server` che è in ascolto ma non serve ancora: le connessioni attendono nel sistema finché non viene chiamato `serve(handler)`. La sua `port()` è nota prima, anche con la porta 0, così un programma può avviare il server in un task e poi chiamarlo, come fanno gli esempi di questo capitolo. `http.listenTls(host, port, certificate, key)` restituisce un server https, con i testi in PEM come per `net.listenTls`.

### Le route

L'handler sceglie da sé la route con `req.matches(pattern)`, vero quando il metodo e il percorso della richiesta sono quelli del pattern:

- `"GET /users/{id}"`: un nome tra graffe prende un segmento del percorso, che non può essere vuoto; poi `req.param("id")` lo restituisce, senza le sue sequenze di escape, così `%20` diventa uno spazio.
- `"GET /files/{path...}"`: un nome seguito da `...` prende tutto il resto del percorso, barre comprese; può essere solo l'ultimo segmento. `req.param("path")` lo restituisce.
- `"/status"`: senza il metodo, va bene qualsiasi metodo. `GET` accetta anche `HEAD`.
- Gli altri segmenti vengono confrontati esattamente, anche una barra finale: `"GET /users"` non corrisponde a `/users/`. La query, dopo `?`, non conta.
- `req.param(name)` con un nome che l'ultimo pattern corrispondente non ha è un'eccezione.
- `req.queryValue(name)` restituisce un valore della query senza le sue sequenze di escape, con `+` come spazio, e `""` quando manca.

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

La risposta a `HEAD` è quella di `GET`, senza il corpo. L'eccezione dell'handler ha dato lo stato 500, e il server ha scritto l'ultima riga sullo standard error.

Qui l'handler è una funzione a sé; `serve` riceve una lambda che la chiama, perché una funzione con nome non è un valore ([Funzioni anonime](lambdas.md)).

### Richieste e risposte

Una `http.Request`, sul server, viene letta per intero prima che l'handler venga eseguito:

| Campo | Che cosa contiene |
|---|---|
| `method` | `"GET"`, `"POST"`, come l'ha scritto il client |
| `url` | la destinazione della richiesta, `"/users/7?full=1"` |
| `path` | il percorso, `"/users/7"`, con le sue sequenze di escape |
| `query` | ciò che segue `?`, `"full=1"`, con le sue sequenze di escape |
| `headers` | gli header, nel loro ordine |
| `body` | i byte del corpo; `text()` li restituisce come testo, e lancia un'eccezione se non sono UTF-8 |

Una `http.Response` ha `status`, 200 a meno che l'handler non ne imposti un altro, `headers` e `body`:

- `res.write(text)` aggiunge del testo al corpo, `res.writeJson(text)` aggiunge un testo JSON, come `res.writeJson(json.encode(user))`, e `res.writeBytes(bytes, type)` aggiunge dei byte. Ciascuno imposta il `Content-Type` solo se gli header non ne hanno già uno: `text/plain; charset=utf-8`, `application/json`, oppure `type`.
- La risposta viene scritta quando l'handler ritorna, in un pezzo solo, con il suo `Content-Length`. I `Content-Length`, `Connection` e `Transfer-Encoding` impostati dall'handler vengono tralasciati, ma `Connection: close` chiude la connessione dopo la risposta. Gli stati 204 e 304 non hanno corpo.

`http.Headers` conserva gli header nel loro ordine, e ne confronta i nomi senza distinguere maiuscole e minuscole, come vuole HTTP:

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

`get` restituisce il primo valore, `set` sostituisce tutti gli header di un nome con uno solo, `add` ne aggiunge un altro, e `remove` li rimuove tutti. Un valore con un a capo inizierebbe un altro header, e un nome con uno spazio, un carattere di controllo o `:` non è un nome: entrambi sono eccezioni.

### Che cosa fa il server per te

- Ogni connessione è servita da un task tutto suo, senza un limite fisso: migliaia di connessioni tenute aperte costano poca memoria. Perciò l'handler gira su più thread contemporaneamente: è una `const func`, che non può modificare ciò che cattura. Ciò che le richieste condividono sta in un `thread.Mutex` o in un `thread.Atomic`, catturato in lettura.
- Una connessione serve le sue richieste una dopo l'altra finché il client la tiene aperta (keep-alive). Attende 5 secondi l'inizio della richiesta successiva, poi 30 secondi per ogni lettura e scrittura; superati questi tempi, o quando il client se ne va, termina in silenzio.
- Prima dell'handler, la richiesta viene letta per intero, anche un corpo inviato a blocchi. Un'intestazione più grande di 64 KB riceve lo stato 431, un corpo più grande di 16 MB il 413, una richiesta non valida il 400, e una versione diversa da HTTP/1.1 e HTTP/1.0 il 505. `Expect: 100-continue` riceve la sua risposta.
- Un'eccezione dell'handler ne sostituisce la risposta con lo stato 500 e un corpo vuoto, e scrive una riga sullo standard error, come `http: GET /fail: boom`; il server va avanti. Un panic termina comunque il programma.

Un handler che cattura con `ref` viene rifiutato:

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

Un contatore condiviso dalle richieste è un `thread.Atomic<i64>`, catturato in lettura: `[count](...) { res.write(format("{}", count.add(1) + 1)) }`.

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

`http.get(url)` e `http.post(url, body, type)` inviano una richiesta e restituiscono la risposta intera. Per gli altri metodi, e per aggiungere degli header propri, un programma crea una `http.Request` con il metodo e l'URL e la invia con `http.send`:

```cromo ignore
http.Request req = http.Request("PUT", "https://example.com/users/7")
req.headers.set("Authorization", "Bearer " + token)
req.writeJson(json.encode(user))
http.Response res = http.send(req)
```

- L'URL inizia con `http://` o `https://`, e può avere una porta; `https` usa TLS, con i controlli di `net.connectTls`. Un nome utente e una password nell'URL non sono supportati. Il percorso e la query vengono inviati così come sono scritti: `http.escape(text)` scrive un valore con `%XX` per ogni byte che non è una lettera, una cifra, `-`, `_`, `.` o `~`, così `http.escape("a b&c")` restituisce `"a%20b%26c"`.
- Il client scrive da sé `Host`, `Content-Length` e `Connection`, e aggiunge `User-Agent: cromo` quando la richiesta non ha uno `User-Agent`.
- Uno stato di errore, come 404 o 500, è una risposta come le altre. Sono eccezioni un URL che non si può usare, un host che non viene trovato, un guasto della rete o di TLS, un timeout, una risposta che non è HTTP, o un corpo troppo grande.
- La connessione, con l'handshake TLS, non ha timeout; poi ogni lettura e scrittura attende al massimo 30 secondi.
- Le risposte 1xx, come `100 Continue`, vengono saltate.

### Redirect e compressione

Il client segue fino a 10 redirect; di più sono un'eccezione, `cannot get ...: more than 10 redirects`. Dopo un 303, e dopo un 301 o un 302 per un metodo diverso da `GET` e `HEAD`, la richiesta prosegue come `GET` senza corpo, come fanno i browser. `Authorization` e `Cookie` non vengono inviati a un altro host.

Quando la richiesta non indica un `Accept-Encoding`, il client chiede gzip, e restituisce il corpo decompresso, senza `Content-Encoding` e con il `Content-Length` del corpo decompresso. Una richiesta che indica un proprio `Accept-Encoding` riceve il corpo così come l'ha inviato il server.

Un corpo ha al massimo 16 MB, anche una volta decompresso: pochi kilobyte di gzip possono diventare gigabyte. Un `http.Client` ha `bodyLimit`, in byte, per impostare un limite diverso.

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

Il server HTTP non comprime da sé: un handler può farlo, con `std.gzip`, come qui sopra. Per i corpi delle richieste il server mantiene il suo limite di 16 MB.

### Tenere le connessioni: http.Client

`http.get`, `http.post` e `http.send` aprono una connessione per ogni richiesta. Un `http.Client` tiene aperte le connessioni dopo una risposta, per riusarle nelle richieste successive allo stesso host, il che rende molto più veloci le richieste numerose:

- Tiene al massimo 8 connessioni inattive per ogni host; le più vecchie vengono chiuse per prime.
- Una connessione tenuta da parte che nel frattempo il server ha chiuso fallisce prima che arrivi un qualsiasi byte della risposta: allora la richiesta viene inviata di nuovo, una volta sola, su una nuova connessione, anche se è un `POST`, perché il server non l'ha ricevuta.
- I suoi metodi, `get`, `post` e `send`, sono `const`: più task possono usare lo stesso client contemporaneamente, catturandolo in lettura, ogni richiesta su una connessione propria.
- Il distruttore chiude le connessioni. Un client si può spostare, non copiare.

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

Un servizio completo, con i suoi dati e i suoi test, si trova in [Progetto finale: un servizio web](web-service.md); [JSON](json.md) spiega `json.encode` e `json.decode`.

## WebSocket

Un WebSocket tiene aperta una connessione in entrambe le direzioni, per lo scambio di messaggi. Un handler del server trasforma la sua richiesta in un WebSocket con `req.webSocket(work)`, e un client ne apre uno con `websocket.connect(url)`. Entrambe le parti usano poi un `websocket.Socket`:

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

Sul server:

- `req.webSocket(work)` risponde alla richiesta con lo stato 101 ed esegue `work` con il socket, dentro l'handler, sul task della connessione: `work` può usare le variabili dell'handler e ciò che l'handler cattura, come un mutex. Quando `work` ritorna o lancia un'eccezione, la connessione termina, senza la risposta dell'handler; un'eccezione di `work` non viene scritta sullo standard error.
- Una richiesta che non è di WebSocket riceve lo stato 400, e `work` non viene chiamata.

Sul client:

- `websocket.connect(url)` accetta un URL che inizia con `ws://`, o `wss://` per TLS; senza porta, 80 o 443. La connessione non ha timeout, poi il server deve rispondere entro 30 secondi.
- Un server che non accetta il WebSocket è un'eccezione con la sua risposta: `cannot open the WebSocket ws://127.0.0.1:8080/other: the server answered 'HTTP/1.1 404 Not Found'`.

Da entrambe le parti:

- `send(text)` invia un messaggio di testo, `sendBytes(bytes)` uno binario.
- `receive()` attende il messaggio successivo e restituisce un `websocket.Message`, un enum con i case `Text(string text)` e `Binary(list<u8> bytes)`. Ricompone i frammenti di un messaggio, e risponde ai ping che arrivano nel frattempo. Un messaggio più grande di 16 MB, un frame non valido o un testo che non è UTF-8 sono un'eccezione.
- Senza `timeout(ms)` un socket attende tutto il tempo necessario; con esso, ogni lettura e scrittura attende al massimo quel tempo.
- `close()` termina il WebSocket in modo pulito: invia la chiusura con il codice 1000, attende la risposta dell'altra parte, scartando i messaggi che arrivano nel frattempo, e chiude la connessione.
- Dopo una chiusura, `receive()`, `send` e `sendBytes` lanciano `websocket.Closed`, un'eccezione con `code` e `reason`: 1000 una fine normale, 1001 una parte che se ne va, 1005 una chiusura senza codice, 1006 una connessione terminata senza chiusura. `receive()` la lancia anche quando è l'altra parte a chiudere, dopo aver risposto alla sua chiusura.
- Il distruttore termina la connessione senza chiusura, che l'altra parte vede come 1006: `close()` è il modo pulito.

## Compressione: std.gzip

`std.gzip` legge e scrive il formato gzip, quello dei file `.gz` e dei corpi HTTP con `Content-Encoding: gzip`:

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

- `compress(data)` scrive dati che qualsiasi gzip sa leggere. Usa solo i codici fissi di deflate, quindi il risultato è circa un quinto più grande di quello di `gzip -9`.
- `decompress(data)` controlla il CRC-32 e la dimensione, e lancia un'eccezione per dati danneggiati o troncati. Un gzip di più membri, uno dopo l'altro, li restituisce uniti.
- `decompressUpTo(data, limit)` lancia un'eccezione quando il risultato supererebbe `limit` byte: usalo per i dati che arrivano dalla rete.
- `inflate(data, limit)` legge deflate senza l'intestazione e il controllo di gzip intorno, come lo contiene uno zip, anche qui con un limite.
- `crc32(data)` restituisce il CRC-32 dei dati, il controllo di gzip e di zip.

## Crittografia: std.crypto

`std.crypto` offre i mattoni di base che servono ai programmi di rete. I byte sono `list<u8>`; una chiave o un nonce della dimensione sbagliata, e un testo che non è esadecimale o base64, sono eccezioni di tipo `Error`.

### Hash, HMAC e codifiche

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

- `md5`, `sha1`, `sha256`, `sha384` e `sha512` restituiscono l'hash dei dati: 16, 20, 32, 48 e 64 byte. MD5 e SHA-1 non sono più sicuri: ci sono per i formati che li usano ancora, come WebSocket.
- `hmac(hash, key, data)` dimostra che chi l'ha prodotto possiede la chiave, con `crypto.Hash`, un enum con i case `Md5`, `Sha1`, `Sha256`, `Sha384` e `Sha512`. Per controllare un HMAC ricevuto, confrontane tutti i byte, non solo fino al primo diverso.
- `crypto.Hasher(hash)` calcola un hash a pezzi: `add` aggiunge dei dati, `finish()` restituisce l'hash e riparte da vuoto. Si può spostare, non copiare.
- `hex(bytes)` scrive due cifre minuscole per ogni byte; `fromHex(text)` le legge sia minuscole sia maiuscole, e lancia un'eccezione per un numero dispari di cifre o per un carattere che non è una cifra.
- `base64(bytes)` scrive base64 con il suo padding `=`, che `fromBase64(text)` richiede. `base64Url(bytes)` scrive la forma per gli URL, con `-` e `_` e senza padding; `fromBase64Url(text)` la legge con o senza.
- `random(count)` restituisce byte dal generatore del sistema, adatti a chiavi e nonce: un'eccezione se il sistema non ne fornisce.

### Cifratura

`crypto.encrypt(cipher, key, nonce, data, extra)` cifra e autentica i dati, con `crypto.Cipher.AesGcm` o `crypto.Cipher.ChaCha20Poly1305`, e `crypto.decrypt` con gli stessi argomenti li restituisce:

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

- La chiave ha 16, 24 o 32 byte per AES-GCM, e 32 per ChaCha20-Poly1305.
- Il nonce ha 12 byte, e non va mai usato due volte con la stessa chiave: il modo semplice è un nuovo `random(12)` per ogni messaggio, inviato insieme al messaggio, in chiaro.
- Il risultato sono i dati cifrati, seguiti da un tag di 16 byte: da 6 byte di dati se ne sono ottenuti 22.
- I dati extra vengono autenticati ma non cifrati, e non fanno parte del risultato, come un'intestazione inviata in chiaro: `decrypt` ha bisogno degli stessi. Spesso sono vuoti, `list<u8>()`.
- `decrypt` lancia un'eccezione quando qualcosa non è com'era: i dati, il tag, la chiave, il nonce o i dati extra. Il messaggio non dice quale.

### Firme e scambio di chiavi

Ed25519 firma i messaggi: la chiave privata è fatta di 32 byte casuali, e la chiave pubblica, anch'essa di 32 byte, si ricava da quella. X25519 permette a due parti di calcolare lo stesso segreto ciascuna dalla propria chiave privata e dalla chiave pubblica dell'altra:

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

- `ed25519Sign` restituisce una firma di 64 byte, sempre la stessa per la stessa chiave e lo stesso messaggio. `ed25519Verify` restituisce `false` anche per una chiave o una firma della dimensione sbagliata, mai un'eccezione.
- `x25519` restituisce un segreto di 32 byte, e lancia un'eccezione per una chiave pubblica non valida, una che renderebbe nullo il segreto. Una chiave per un cifrario è meglio ricavarla dal segreto con un hash, come `sha256`.
- Una chiave privata che non ha 32 byte è un'eccezione: `the private key of Ed25519 has 32 bytes, not 16`.

## Token: JWT

Un servizio con molti client spesso li riconosce da un JSON Web Token, che firma quando accedono, o che firma per lui un provider di identità come Keycloak. `std.jwt` li firma e li verifica, e i claim sono una struct del programma:

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

- `jwt.secret` crea una chiave HMAC, HS256, HS384 o HS512, lunga almeno quanto l'hash; `jwt.publicKey` e `jwt.privateKey` leggono una chiave in PEM per RS256, RS384, RS512, ES256, ES384 o EdDSA.
- L'algoritmo è quello della chiave, mai quello scritto nel token: un token con `"alg": "none"`, o firmato in HS256 per una chiave RSA, viene rifiutato.
- `jwt.verify` controlla la firma, e `exp` e `nbf` quando ci sono. Un `jwt.Verifier` accetta più chiavi, anche dal JWK Set di un provider con `jwt.keySet(text)`, e controlla anche l'emittente e il destinatario, con una tolleranza `leeway` in secondi per gli orologi che non coincidono.
- Un token rifiutato è un `jwt.Invalid`, il cui `reason` dice perché: `Expired`, `BadSignature`, `UnknownKey` e gli altri.
- I tempi sono secondi dal 1970: `time.unix() + 3600` è fra un'ora.

## Riepilogo

- `std.net` ha TCP, UDP e TLS, con chiamate bloccanti che, mentre attendono, lasciano il thread agli altri task. Senza `timeout(ms)` un'operazione attende tutto il tempo necessario; `connect` non ha un timeout proprio.
- `read` restituisce ciò che è arrivato, una lista vuota alla fine; `write` scrive tutto; `close()` e il distruttore chiudono.
- TLS verifica il certificato e il nome del server rispetto alle autorità del sistema, o a quelle di `SSL_CERT_FILE`; un server fa il suo handshake al primo `read` o `write`.
- `http.serve` esegue un handler `const func` per ogni richiesta, con un task per ogni connessione e le route scelte da `req.matches`; limiti e timeout proteggono il server.
- `http.get`, `http.post` e `http.send` effettuano le richieste, seguono i redirect e decomprimono gzip; `http.Client` tiene le connessioni e serve più task contemporaneamente.
- `std.websocket` invia e riceve messaggi in entrambe le direzioni, e termina con `close()` e `websocket.Closed`.
- `std.gzip`, `std.crypto` e `std.jwt` completano la cassetta degli attrezzi, senza librerie esterne.
