# Espressioni regolari

Un'espressione regolare è un modello per un testo: "quattro cifre, un trattino, due cifre". Il modulo [std.regex](std.regex.html) dice se un testo contiene un modello, trova dove, ne prende le parti, sostituisce e divide.

## Una prima espressione

```cromo
import std.regex

i32 main() {
    regex.Regex date = regex.Regex("(\\d{4})-(\\d{2})-(\\d{2})")
    println("{}", date.matches("released on 2026-11-12"))
    println("{}", date.matchesWhole("released on 2026-11-12"))

    regex.Match found
    if (date.find("released on 2026-11-12", ref found)) {
        println("{} at {}", found.text(), found.start())
        println("year {}, month {}, day {}", found.group(1), found.group(2), found.group(3))
    }
    ret 0
}
```

```output
true
false
2026-11-12 at 12
year 2026, month 11, day 12
```

`regex.Regex(pattern)` legge l'espressione, e questo costa un po' di lavoro: un programma crea una `Regex` una volta e la conserva. Può essere copiata e data ai task.

- `matches(text)` è vero quando il testo contiene l'espressione da qualche parte; `matchesWhole(text)` quando l'intero testo è l'espressione.
- `find(text, ref found)` trova la prima corrispondenza, quella più a sinistra: dà vero quando ce n'è una, e la mette nel `regex.Match`. `findFrom(text, from, ref found)` parte da una posizione.
- Un `Match` ha il suo `text()`, e dove si trova nel testo: `start()` ed `end()`, in byte, come le posizioni di una stringa.
- `group(1)` è il testo del primo gruppo, la parte dell'espressione fra la prima `(` e la sua `)`; `group(0)` è l'intera corrispondenza. `has(index)` dice se la corrispondenza è passata da un gruppo: in `a|(b)` su `"a"` non ci è passata, e `group(1)` è vuoto. `groupStart(index)` e `groupEnd(index)` danno dove si trova un gruppo, o -1.

## La sintassi

La sintassi è quella di RE2, usata anche da Go e Rust. Un carattere vale se stesso, tranne questi: `. * + ? ( ) [ ] { } | ^ $ \`.

Caratteri:

- `.` è un carattere qualsiasi tranne la fine riga.
- `[abc]` è uno di quei caratteri, `[a-z0-9_]` uno di quegli intervalli, `[^a-z]` uno che non è fra quelli.
- `\d` è una cifra, `\w` una lettera, una cifra o `_`, `\s` uno spazio, una tabulazione o una fine riga; `\D`, `\W` e `\S` sono i caratteri che non lo sono. Possono stare dentro una classe: `[\d.]`.
- `[[:alpha:]]` e le altre classi POSIX, dentro una classe: `alnum`, `digit`, `upper`, `lower`, `space`, `punct`, `xdigit`.
- `\.` `\*` `\\` e gli altri sono il carattere stesso; `\n` `\t` `\r` una fine riga, una tabulazione, un ritorno a capo; `\x41` e `\x{1F600}` il carattere con quel numero; `\Q...\E` un testo così com'è.

Ripetizioni, dopo un carattere, una classe o un gruppo:

- `x*` è un numero qualsiasi di `x`, anche nessuno; `x+` uno o più; `x?` uno o nessuno.
- `x{3}` è tre, `x{2,}` due o più, `x{2,5}` da due a cinque.
- Prendono quanti più caratteri possono; con un `?` dopo, `x*?`, `x+?`, `x??`, `x{2,5}?`, quanti meno possono.

Composizione:

- `ab` è `a` e poi `b`; `a|b` è `a` oppure `b`, il primo che corrisponde.
- `(x)` è un gruppo, numerato da 1 secondo la sua `(`; `(?:x)` non è numerato; `(?P<name>x)`, o `(?<name>x)`, ha anche un nome.

Posizioni, che non prendono caratteri:

- `^` e `$` sono l'inizio e la fine del testo; anche `\A` e `\z`, sempre.
- `\b` è il bordo di una parola, fra un carattere di `\w` e uno che non lo è; `\B` è dove non c'è un bordo.

Flag, per il resto del gruppo in cui si trovano, o con `(?i:x)` solo per `x`:

- `(?i)` maiuscole o minuscole, `(?m)` rende `^` e `$` l'inizio e la fine di ogni riga, `(?s)` fa prendere a `.` anche la fine riga, `(?U)` fa prendere alle ripetizioni quanto meno possono, e quanto più possono con un `?`.

Una stringa di Cromo usa il `\` per i propri escape, quindi un'espressione scrive ogni `\` due volte: `"\\d+"` è l'espressione `\d+`.

## Gruppi con un nome, e tutte le corrispondenze

```cromo
import std.regex

i32 main() {
    regex.Regex pair = regex.Regex("(?P<key>\\w+)=(?P<value>[^;]*)")
    for (regex.Match found in pair.findAll("name=Ada;role=admin;note=")) {
        println("{} is {:?}", found.named("key"), found.named("value"))
    }
    ret 0
}
```

```output
name is "Ada"
role is "admin"
note is ""
```

`named(name)` dà il testo di un gruppo dal suo nome, e `findAll(text)` tutte le corrispondenze del testo, in ordine, ognuna dopo la fine della precedente.

## Sostituire e dividere

```cromo
import std.regex

i32 main() {
    regex.Regex date = regex.Regex("(\\d{4})-(\\d{2})-(\\d{2})")
    println("{}", date.replace("from 2026-01-31 to 2026-02-28", "$3/$2/$1"))
    println("{}", date.replaceFirst("from 2026-01-31 to 2026-02-28", "${1}"))
    println("{:?}", regex.Regex("\\s+").replace("  too   many spaces ", " "))

    list<string> names = regex.Regex("\\s*[,;]\\s*").split("ada , grace;linus")
    println("{} {}", names.len(), names[1])
    println("{}", regex.Regex(regex.escape("1+1")).replace("1+1=2", "two"))
    ret 0
}
```

```output
from 31/01/2026 to 28/02/2026
from 2026 to 2026-02-28
" too many spaces "
3 grace
two=2
```

- `replace(text, replacement)` sostituisce ogni corrispondenza, e `replaceFirst` la prima. Nella sostituzione `$1` è il gruppo 1 della corrispondenza, `${1}` lo stesso quando segue una cifra, `${name}` un gruppo dal suo nome, `$0` l'intera corrispondenza e `$$` un `$`.
- `split(text)` dà i pezzi del testo fra le corrispondenze.
- `regex.escape(text)` mette un `\` davanti ai caratteri che hanno un significato: un'espressione che trova un testo così com'è, per i testi che vengono da fuori.

## Quanti più, quanti meno, e i flag

```cromo
import std.regex

i32 main() {
    string page = "<b>one</b> and <b>two</b>"
    regex.Match found
    regex.Regex("<b>.*</b>").find(page, ref found)
    println("{}", found.text())
    regex.Regex("<b>.*?</b>").find(page, ref found)
    println("{}", found.text())

    println("{}", regex.Regex("(?i)hello").matches("HELLO"))
    println("{}", regex.Regex("(?m)^\\w+$").findAll("one\ntwo\n").len())
    println("{}", regex.Regex("^.$").matches("è"))
    ret 0
}
```

```output
<b>one</b> and <b>two</b>
<b>one</b>
true
2
true
```

`.*` prende quanti più caratteri può, fino all'ultimo `</b>`; `.*?` quanti meno può. I caratteri sono quelli di Unicode: `.` prende `è`, due byte, per intero. `\d`, `\w`, `\s`, `\b` e `(?i)` conoscono solo le lettere ASCII.

## Cosa non c'è, e perché

```cromo
import std.regex

i32 main() {
    try {
        println("{}", regex.Regex("(a|b").groups())
    } catch (Exception e) {
        println("{}", e.message())
    }
    regex.Regex heavy = regex.Regex("(a+)+b")
    println("{}", heavy.matches("a".repeat(5000)))
    ret 0
}
```

```output
cannot read the regular expression '(a|b': a ( without its ), at 4
false
```

Un'espressione non valida lancia un `Error` che dice cosa è sbagliato e dove.

Il tempo di una ricerca cresce con la lunghezza del testo, e mai di più. I motori che provano una strada dopo l'altra, come quelli di Perl, di JavaScript e `std::regex` del C++, possono impiegare anni su `(a+)+b` con un testo di qualche decina di `a`, e un servizio che riceve un'espressione o un testo da fuori può essere fermato da una sola richiesta. `std.regex` percorre tutte le strade insieme, un carattere del testo alla volta, come fanno RE2, Go e Rust.

Il prezzo è ciò che non si può cercare in quel modo, e che non c'è: i riferimenti a un gruppo, come `\1`, e il lookahead e il lookbehind, `(?=x)`, `(?!x)`, `(?<=x)`. Sono errori che lo dicono. Le classi di Unicode, come `\p{L}`, non ci sono ancora.

## Riepilogo

- `regex.Regex(pattern)` legge un'espressione con la sintassi di RE2; si crea una volta e si conserva.
- `matches` e `matchesWhole` dicono sì o no; `find`, `findFrom` e `findAll` danno le corrispondenze, ognuna un `Match` con il suo testo, la sua posizione e i suoi gruppi, per numero o per nome.
- `replace`, `replaceFirst` e `split` cambiano e tagliano un testo; `regex.escape` crea un'espressione da un testo così com'è.
- Una ricerca impiega un tempo che cresce con il testo, qualunque sia l'espressione; non ci sono riferimenti a un gruppo né lookahead.
