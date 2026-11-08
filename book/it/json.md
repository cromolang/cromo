# JSON

JSON è il formato con cui la maggior parte dei servizi si scambia i dati. In Cromo la conversione tra una struct e il JSON la scrive il compilatore da sé, la prima volta che serve: non occorre alcuna annotazione sulla struct, nessuno strumento esterno genera codice, e la conversione è veloce quanto quella scritta a mano. Il modulo è [std.json](std.json.html).

## Da struct a JSON e viceversa

```cromo
import std.json

struct Address {
    string city
    string country
}

struct Person {
    string name
    i64 born
    list<string> languages
    Address address
}

i32 main() {
    Person p
    p.name = "Grace"
    p.born = 1906
    p.languages = ["COBOL", "FLOW-MATIC"]
    p.address.city = "New York"
    p.address.country = "USA"

    string text = json.encode(p)
    println("{}", text)

    Person back = json.decode<Person>(text)
    println("{} was born in {}", back.name, back.born)
    ret 0
}
```

```output
{"name":"Grace","born":1906,"languages":["COBOL","FLOW-MATIC"],"address":{"city":"New York","country":"USA"}}
Grace was born in 1906
```

- `json.encode(value)` scrive un JSON compatto; `json.pretty(value)` lo scrive su più righe, indentato.
- `json.decode<T>(text)` legge un `T` dal suo testo.
- Una struct è un oggetto con un membro per ogni campo, nell'ordine dei campi.

## I tipi convertibili

Il compilatore converte questi tipi, senza alcuna dichiarazione:

| Tipo | In JSON |
|---|---|
| `i8` ... `i64`, `u8` ... `u64` | un numero senza punto decimale |
| `f32`, `f64` | un numero |
| `bool` | `true` o `false` |
| `string` | una stringa |
| `list<T>` | un array |
| `T[n]`, un array fisso | un array di esattamente `n` elementi |
| `map<string, T>` | un oggetto |
| un enum | il nome del suo case, o un oggetto per un case con dati |
| una struct, anche generica | un oggetto con un membro per ogni campo |
| `json.Value` | qualsiasi valore JSON |

Anche gli elementi, i valori e i campi devono essere di questi tipi. Un campo che non si può convertire, come una funzione, è un errore di compilazione che ne indica il nome e il tipo, sulla chiamata che richiede la conversione:

```cromo error
import std.json

struct Job {
    string name
    func() run
}

i32 main() {
    Job job
    println("{}", json.encode(job))
    ret 0
}
```

```output
error: values of type Job cannot be converted to JSON
```

Il messaggio completo termina con `Job.run has type func()`. Anche una mappa le cui chiavi non sono stringhe, come `map<i64, string>`, è un errore: in JSON i nomi dei membri sono stringhe.

## Scrivere JSON

`json.encode` scrive tutto su una riga, senza spazi; `json.pretty` indenta di due spazi per ogni livello, con uno spazio dopo ogni `:`:

```cromo
import std.json

struct Shop {
    string name
    map<string, i64> stock
    f64[2] position
    list<string> tags
}

i32 main() {
    Shop shop
    shop.name = "Corner"
    shop.stock["pears"] = 4
    shop.stock["apples"] = 10
    shop.stock["Bananas"] = 2
    shop.position[0] = 45.5
    println("{}", json.encode(shop))
    println("{}", json.pretty(shop))
    ret 0
}
```

```output
{"name":"Corner","stock":{"Bananas":2,"apples":10,"pears":4},"position":[45.5,0.0],"tags":[]}
{
  "name": "Corner",
  "stock": {
    "Bananas": 2,
    "apples": 10,
    "pears": 4
  },
  "position": [
    45.5,
    0.0
  ],
  "tags": []
}
```

- Le chiavi di una mappa sono ordinate per byte, quindi le maiuscole vengono prima delle minuscole: il testo non cambia con l'ordine della mappa.
- Vengono scritti tutti i campi, anche quelli che non sono `public`, così come `copy()` li copia tutti. `[[json(skip)]]`, più avanti, ne esclude uno.
- Una lista vuota è `[]`, una mappa vuota `{}`.

I numeri e le stringhe:

```cromo
import std.json

i32 main() {
    list<f64> numbers = [1.0, 0.1, 1.0 / 3.0, 1e20, 0.000001]
    println("{}", json.encode(numbers))
    println("{}", json.encode("quote \", tab \t, accent é, slash /"))
    f64 zero = 0.0
    try {
        json.encode(zero / zero)
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
[1.0,0.1,0.3333333333333333,1e+20,1e-06]
"quote \", tab \t, accent é, slash /"
cannot encode JSON: nan is not a number of JSON
```

- Un float viene scritto con il testo più breve che, riletto, dà lo stesso numero, con `.0` quando è intero, così che venga riletto come float.
- NaN e gli infiniti sono un'eccezione: JSON non ha modo di scriverli.
- In una stringa le sequenze di escape compaiono solo dove JSON le richiede: `"`, `\` e i caratteri di controllo. Gli altri caratteri, anche le lettere accentate e le emoji, vengono scritti così come sono, in UTF-8.

## Leggere JSON

`json.decode<T>` parte dal valore predefinito di `T`, poi lo riempie con ciò che il testo contiene:

- Un membro assente lascia il campo come lo crea il valore predefinito della struct, con il suo valore iniziale.
- Un membro `null` dà il valore predefinito del tipo del campo: 0, `""`, `false`, una lista vuota, il primo case di un enum. Lo stesso vale per gli elementi di una lista e per i valori di una mappa.
- Un membro senza un campo corrispondente viene ignorato.
- Di un membro scritto due volte, vale l'ultimo.

```cromo
import std.json

struct Settings {
    i64 retries = 3
    string mode = "fast"
    list<string> hosts
}

i32 main() {
    Settings a = json.decode<Settings>("{\"hosts\": [\"alpha\", null], \"debug\": true}")
    println("{}", json.encode(a))
    Settings b = json.decode<Settings>("{\"retries\": null, \"mode\": null}")
    println("{}", json.encode(b))
    Settings c = json.decode<Settings>("{\"retries\": 1, \"retries\": 2}")
    println("{}", c.retries)
    ret 0
}
```

```output
{"retries":3,"mode":"fast","hosts":["alpha",""]}
{"retries":0,"mode":"","hosts":[]}
2
```

In Cromo non ci sono valori opzionali, quindi un `null` non può essere conservato in un `i64`: un programma che deve distinguere un `null` da un valore dichiara il campo di tipo `json.Value`, come mostrato più avanti.

### Numeri

Un campo intero accetta solo un intero JSON che stia nel suo tipo: `1.0` e `1e3` sono float, come in Go. Un campo float accetta anche un intero. Un `u64` legge tutte le sue cifre, fino a 2^64 - 1.

```cromo
import std.json

struct Reading {
    i8 level
    i64 count
    u64 serial
    f64 ratio
}

void show(string text) {
    try {
        Reading r = json.decode<Reading>(text)
        println("ok: {}", json.encode(r))
    } catch (Exception e) {
        println("{}", e.message())
    }
}

i32 main() {
    show("{\"ratio\": 2, \"serial\": 18446744073709551615}")
    show("{\"count\": 2.0}")
    show("{\"level\": 300}")
    show("{\"serial\": -1}")
    ret 0
}
```

```output
ok: {"level":0,"count":0,"serial":18446744073709551615,"ratio":2.0}
cannot decode JSON: at count: expected an integer, found a float
cannot decode JSON: at level: 300 does not fit in i8
cannot decode JSON: at serial: -1 does not fit in u64
```

### Array fissi

Un array fisso accetta un array JSON con esattamente il suo numero di elementi; una lunghezza diversa è un errore che le indica entrambe:

```cromo
import std.json

i32 main() {
    i32[3] rgb = json.decode<i32[3]>("[255, 128, 0]")
    println("{} {} {}", rgb[0], rgb[1], rgb[2])
    try {
        i32[3] short = json.decode<i32[3]>("[255, 128]")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
255 128 0
cannot decode JSON: expected an array of 3 elements, found 2
```

### Un lettore rigoroso

Il testo deve essere JSON come lo definisce la RFC 8259: niente commenti, niente virgola dopo l'ultimo elemento, stringhe tra virgolette doppie, e dopo il valore nient'altro che spazi. Array e oggetti possono essere annidati fino a 512 livelli, così che un testo ostile non possa esaurire lo stack. Un testo che non è JSON è un'eccezione con la riga e la colonna:

```cromo
import std.json

void show(string text) {
    try {
        json.Value v = json.parse(text)
        println("ok: {}", json.encode(v))
    } catch (Exception e) {
        println("{}", e.message())
    }
}

i32 main() {
    show("[1, 2]")
    show("[1, 2,]")
    show("{'a': 1}")
    show("// a comment\n[1]")
    show("{\"a\": 1}\n{\"b\": 2}")
    show("\"\\ud800\"")
    show("[".repeat(600))
    ret 0
}
```

```output
ok: [1,2]
cannot read JSON: line 1, column 7: expected a value
cannot read JSON: line 1, column 2: expected the name of a member, in double quotes
cannot read JSON: line 1, column 1: expected a value
cannot read JSON: line 2, column 1: the text goes on after the value
cannot read JSON: line 1, column 8: the first half of a pair of \u escapes comes without the second
cannot read JSON: line 1, column 513: the arrays and objects are nested more than 512 times
```

È un errore anche una sequenza di escape `\u` per una metà di un carattere oltre i primi 65536, senza l'altra metà: non è un carattere.

## Nomi e campi da escludere

L'attributo `[[json(...)]]` davanti a un campo ne cambia il nome nel JSON, oppure lo esclude. Può stare sulla riga del campo o su una riga a sé, prima del campo:

```cromo
import std.json

struct Account {
    [[json("user_id")]] i64 id
    string name
    [[json(skip)]]
    string password = "unset"
}

i32 main() {
    Account a
    a.id = 7
    a.name = "Ada"
    a.password = "secret"
    println("{}", json.encode(a))

    Account b = json.decode<Account>("{\"user_id\": 8, \"name\": \"Grace\", \"password\": \"x\"}")
    println("{} {} {}", b.id, b.name, b.password)
    ret 0
}
```

```output
{"user_id":7,"name":"Ada"}
8 Grace unset
```

Un campo con `skip` non viene né scritto né letto: conserva il suo valore predefinito. Il compilatore controlla gli attributi:

```cromo error
struct Account {
    [[json("id")]] i64 id
    [[json("id")]] i64 owner
    [[json(hidden)]] string password
    [[secret]] string token
}

i32 main() {
    ret 0
}
```

```output
error: another field of Account has the name 'id' in JSON
error: json takes the name of the field in JSON, as json("user_id"), or skip, as json(skip)
error: unknown attribute 'secret': for now there is only json
```

## Enum

Un case senza dati si scrive con il suo nome; un case con dati, come un oggetto che ha per unico membro il case stesso, con i suoi dati come i membri di una struct. `[[json("name")]]` davanti a un case gli dà un altro nome:

```cromo
import std.json

enum Shape {
    Empty
    Circle(f64 radius)
    [[json("rect")]] Rectangle(f64 width, f64 height)
}

i32 main() {
    list<Shape> shapes = [Shape.Empty, Shape.Circle(2.5), Shape.Rectangle(1.0, 2.0)]
    string text = json.encode(shapes)
    println("{}", text)
    list<Shape> back = json.decode<list<Shape>>(text)
    println("{:?}", back[2])
    ret 0
}
```

```output
["Empty",{"Circle":{"radius":2.5}},{"rect":{"width":1.0,"height":2.0}}]
Rectangle(1.0, 2.0)
```

La lettura di un enum:

- Un case senza dati deve essere una stringa, il suo nome; un case con dati un oggetto con esattamente un membro, il case.
- Un membro dei dati assente, o `null`, dà il valore predefinito del suo tipo.
- Un `null` al posto dell'enum dà il suo primo case.
- Un altro nome, o un altro tipo di valore, è un errore che elenca i nomi.

```cromo
import std.json

enum Shape {
    Empty
    Circle(f64 radius)
    [[json("rect")]] Rectangle(f64 width, f64 height)
}

void show(string text) {
    try {
        Shape s = json.decode<Shape>(text)
        println("ok: {:?}", s)
    } catch (Exception e) {
        println("{}", e.message())
    }
}

i32 main() {
    show("{\"rect\": {\"width\": 3}}")
    show("null")
    show("\"Square\"")
    show("{\"Circle\": {\"radius\": 1}, \"Empty\": {}}")
    ret 0
}
```

```output
ok: Rectangle(3.0, 0.0)
ok: Empty
cannot decode JSON: expected one of Empty, Circle, rect, found "Square"
cannot decode JSON: expected an object with one member, the case, found an object
```

Un case ha solo l'attributo `json("name")`, non `skip`: un valore dell'enum deve sempre avere un nome con cui essere scritto. Due case con lo stesso nome in JSON sono un errore.

## Valori di forma qualsiasi: json.Value

Quando la forma dei dati non è nota in anticipo, `json.parse(text)` restituisce un `json.Value`, un enum con un case per ogni tipo di valore JSON:

| Case | Per |
|---|---|
| `Null` | `null` |
| `Bool(bool value)` | `true`, `false` |
| `Int(i64 value)` | un intero che sta in un `i64` |
| `UInt(u64 value)` | un intero da 2^63 a 2^64 - 1 |
| `Float(f64 value)` | un numero con il punto decimale o con un esponente, o un intero troppo grande per gli altri case |
| `Text(string value)` | una stringa |
| `Array(list<json.Value> items)` | un array |
| `Object(list<json.Member> members)` | un oggetto: un `json.Member` ha `name` e `value` |

Gli interi e i numeri in virgola mobile restano distinti, così un identificatore a 64 bit conserva tutte le sue cifre. Un oggetto conserva i suoi membri nel loro ordine, anche due con lo stesso nome.

I metodi leggono un valore senza uno `switch`:

| Metodo | Restituisce |
|---|---|
| `isNull()` | se il valore è `null` |
| `asBool()` | il `bool` di un `Bool` |
| `asInt()` | l'`i64` di un `Int`; non quello di un `Float` come `2.0`, né quello di un `UInt` |
| `asFloat()` | un `f64` da un `Float`, un `Int` o un `UInt`; oltre 2^53 un intero può perdere le ultime cifre |
| `asText()` | una copia della stringa di un `Text` |
| `len()` | il numero di elementi di un array, o di membri di un oggetto |
| `has(name)` | se il valore è un oggetto con quel membro; `false` per gli altri tipi di valore |
| `get(name)` | una copia del membro di un oggetto; se il nome compare due volte, l'ultimo |
| `at(index)` | una copia dell'elemento di un array, contando da 0 |
| `keys()` | i nomi dei membri di un oggetto, nel loro ordine |
| `set(name, value)` | modifica il membro di un oggetto, o lo aggiunge in fondo |
| `add(value)` | aggiunge un elemento in fondo a un array |

`json.object()` e `json.array()` restituiscono un oggetto vuoto e un array vuoto, da riempire con `set` e `add`. Un metodo usato sul tipo di valore sbagliato, un membro assente e un indice fuori dall'array sono eccezioni, non panic: i dati vengono dall'esterno.

```cromo
import std.json

i32 main() {
    json.Value v = json.parse("{\"items\": [1, 2.5, \"x\", null], \"ok\": true, \"id\": 18446744073709551615}")
    json.Value items = v.get("items")
    println("{} items, the first is {}", items.len(), items.at(0).asInt())
    println("{} {} {}", items.at(2).asText(), items.at(3).isNull(), v.get("ok").asBool())
    println("{:?} {}", v.keys(), v.has("name"))
    println("{:?}", v.get("id"))

    switch (items.at(1)) {
        case Int(i64 n) {
            println("an integer: {}", n)
        }
        case Float(f64 f) {
            println("a float: {}", f)
        }
        default {
            println("something else")
        }
    }

    try {
        v.get("name")
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        items.at(2).asInt()
    } catch (Exception e) {
        println("{}", e.message())
    }

    json.Value made = json.object()
    made.set("language", json.Value.Text("Cromo"))
    made.set("version", json.Value.Int(27))
    json.Value tags = json.array()
    tags.add(json.Value.Text("fast"))
    tags.add(json.Value.Bool(true))
    made.set("tags", tags)
    println("{}", json.encode(made))
    ret 0
}
```

```output
4 items, the first is 1
x true true
["items", "ok", "id"] false
UInt(18446744073709551615)
a float: 2.5
the object of JSON has no member 'name'
the value of JSON is a string, not an integer
{"language":"Cromo","version":27,"tags":["fast",true]}
```

`set` e `add` copiano il valore, a meno che non gli venga passato con `move`.

### Tra valori e tipi

Un `json.Value` si converte in un tipo e viceversa, con le stesse regole del testo:

- `json.toValue(x)` restituisce il `json.Value` di `x`, lo stesso che darebbe `json.parse(json.encode(x))`: un programma può aggiungervi dei membri prima di scriverlo.
- `json.fromValue<T>(v)` legge un `T` da un valore, come `json.decode<T>` lo legge da un testo.

Un campo di tipo `json.Value` accetta qualsiasi valore, così com'è. È il modo per distinguere un `null`, o un membro assente, da un valore:

```cromo
import std.json

struct Patch {
    string name
    json.Value age  // null clears the age, a number sets it
}

struct Point {
    i64 x
    i64 y
}

i32 main() {
    list<string> texts = ["{\"name\": \"Ada\", \"age\": 36}", "{\"name\": \"Ada\", \"age\": null}", "{\"name\": \"Ada\"}"]
    for (string text in texts) {
        Patch p = json.decode<Patch>(text)
        if (p.age.isNull()) {
            println("{}: clear the age", p.name)
        } else {
            println("{}: the age is {}", p.name, json.fromValue<i64>(p.age))
        }
    }

    Point origin
    json.Value v = json.toValue(origin)
    v.set("label", json.Value.Text("origin"))
    println("{}", json.encode(v))
    ret 0
}
```

```output
Ada: the age is 36
Ada: clear the age
Ada: clear the age
{"x":0,"y":0,"label":"origin"}
```

Un membro assente lascia il campo a `Null`, il valore predefinito di `json.Value`, come fa un `null`. Un programma che deve distinguere un membro assente da un `null` legge il testo con `json.parse`, e chiede all'oggetto `has(name)`.

## Codice generico: json.Data

`json.Data` è l'interfaccia dei tipi convertibili, che tutti soddisfano da sé, senza dichiararlo. Una funzione generica che converte il suo `T` lo dice con un vincolo, come fa `json.encode`:

```cromo
import std.json

struct Point {
    i64 x
    i64 y
}

string message<T : json.Data>(string kind, T value) {
    ret format("{{\"kind\":\"{}\",\"data\":{}}}", kind, json.encode(value))
}

i32 main() {
    Point p
    p.x = 3
    println("{}", message("point", p))
    println("{}", message("numbers", [i64(1), 2, 3]))
    ret 0
}
```

```output
{"kind":"point","data":{"x":3,"y":0}}
{"kind":"numbers","data":[1,2,3]}
```

Senza il vincolo, il compilatore non sa che `T` si può convertire ([Interfacce e generici](interfaces-generics.md) spiega i vincoli):

```cromo error
import std.json

string send<T>(T value) {
    ret json.encode(value)
}

i32 main() {
    println("{}", send(1))
    ret 0
}
```

```output
error: T can be converted to JSON only with a constraint: write <T : json.Data>
```

## Errori

Un testo che non è JSON, o un JSON che non corrisponde al tipo, è un'eccezione che dice dove si trova il problema: la riga e la colonna per il testo, il percorso del valore per la conversione.

```cromo
import std.json

struct Point {
    i64 x
    i64 y
}

struct Drawing {
    list<Point> points
}

i32 main() {
    try {
        Drawing d = json.decode<Drawing>("{\"points\": [{\"x\": 1, \"y\": 2}, {\"x\": 1, \"y\": \"two\"}]}")
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        Point p = json.decode<Point>("[1, 2]")
    } catch (Exception e) {
        println("{}", e.message())
    }
    try {
        json.Value v = json.parse("{\"x\": 1,, }")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
cannot decode JSON: at points[1].y: expected an integer, found a string
cannot decode JSON: expected an object, found an array
cannot read JSON: line 1, column 9: expected the name of a member, in double quotes
```

## Riepilogo

- `json.encode`, `json.pretty` e `json.decode<T>` convertono numeri, stringhe, liste, array fissi, mappe con chiavi stringa, enum e struct, con codice scritto dal compilatore; un campo che non si può convertire è un errore di compilazione.
- Scrittura: i campi nel loro ordine, le chiavi delle mappe ordinate, i float con il testo più breve e con `.0`, né NaN né infiniti, solo le sequenze di escape che JSON richiede.
- Lettura: JSON rigoroso, annidato fino a 512 livelli; un membro assente conserva il valore predefinito della struct, un `null` dà il valore predefinito del tipo, un membro sconosciuto viene ignorato; gli interi devono essere interi che stanno nel tipo.
- `[[json("name")]]` rinomina un campo o un case; `[[json(skip)]]` esclude un campo.
- `json.parse` restituisce un `json.Value` per dati di forma qualsiasi, con `Int`, `UInt` e `Float` distinti; `toValue` e `fromValue` convertono tra valori e tipi.
- Una funzione generica che converte il suo `T` scrive `<T : json.Data>`.
- Gli errori dicono dove si trova il problema: la riga e la colonna, o il percorso del valore.
