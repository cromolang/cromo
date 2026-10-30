# JSON

JSON è il formato con cui la maggior parte dei servizi si scambia i dati. In Cromo la conversione tra una struct e il JSON la scrive il compilatore da sé, la prima volta che serve: non occorre alcuna annotazione sulla struct, nessuno strumento esterno genera codice, e la conversione è veloce quanto quella scritta a mano.

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
- `json.decode<T>(text)` legge un `T`. Un membro assente, o `null`, lascia al suo campo il valore predefinito; un membro che non corrisponde ad alcun campo viene ignorato.
- La conversione funziona per i numeri, `bool`, `string`, le liste, le mappe con chiavi stringa, gli array fissi, gli enum e le struct i cui campi siano a loro volta convertibili. Un campo che non lo è, come una funzione, dà un errore di compilazione che lo indica per nome.

## Nomi e campi da escludere

L'attributo `[[json(...)]]` davanti a un campo ne cambia il nome nel JSON, oppure lo esclude:

```cromo
import std.json

struct Account {
    [[json("user_id")]] i64 id
    string name
    [[json(skip)]] string password
}

i32 main() {
    Account a
    a.id = 7
    a.name = "Ada"
    a.password = "secret"
    println("{}", json.encode(a))
    ret 0
}
```

```output
{"user_id":7,"name":"Ada"}
```

## Enum

Un case senza dati si scrive con il suo nome; un case con dati, come un oggetto che ha per unico membro il case stesso:

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

## Valori di forma qualsiasi

Quando la forma dei dati non è nota in anticipo, `json.parse` restituisce un `json.Value`, un enum con i case `Null`, `Bool`, `Int`, `UInt`, `Float`, `Text`, `Array` e `Object`:

```cromo
import std.json

i32 main() {
    json.Value v = json.parse("{\"items\": [1, 2.5, \"x\", null], \"ok\": true}")
    json.Value items = v.get("items")
    println("{} items, the first is {}", items.len(), items.at(0).asInt())
    println("{} {}", items.at(2).asText(), items.at(3).isNull())

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

    json.Value made = json.object()
    made.set("language", json.Value.Text("Cromo"))
    made.set("version", json.Value.Int(27))
    println("{}", json.encode(made))
    ret 0
}
```

```output
4 items, the first is 1
x true
a float: 2.5
{"language":"Cromo","version":27}
```

Gli interi e i numeri in virgola mobile restano distinti: un identificatore a 64 bit conserva tutte le sue cifre.

## Errori

Un testo che non è JSON, o un JSON che non corrisponde al tipo, genera un'eccezione che indica dove si trova il problema:

```cromo
import std.json

struct Point {
    i64 x
    i64 y
}

i32 main() {
    try {
        Point p = json.decode<Point>("{\"x\": 1, \"y\": \"two\"}")
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
cannot decode JSON: at y: expected an integer, found a string
cannot read JSON: line 1, column 9: expected the name of a member, in double quotes
```

## Riepilogo

- `json.encode`, `json.pretty` e `json.decode<T>` convertono struct, liste, mappe ed enum, con codice scritto dal compilatore.
- `[[json("name")]]` rinomina un campo o un case; `[[json(skip)]]` esclude un campo.
- `json.parse` restituisce un `json.Value` per dati di forma qualsiasi.
- Gli errori indicano dove si trova il problema.
