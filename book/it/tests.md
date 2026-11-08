# Test

Cromo ha i test integrati: un test è una funzione marcata `[[test]]`, `assert` controlla una condizione, e `cromo test` esegue i test e ne riporta i risultati. Non servono librerie né framework.

## Scrivere un test

Un test è una funzione senza parametri che non restituisce nulla, preceduta dall'attributo `[[test]]`:

```cromo file=stats.cro
public f64 mean(list<f64> values) {
    f64 sum = 0.0
    for (f64 v in values) {
        sum += v
    }
    ret sum / f64(values.len())
}

[[test]]
void meanOfThree() {
    list<f64> values = [1.0, 2.0, 3.0]
    assert(mean(values) == 2.0)
}
```

Un test **passa** se termina normalmente. **Fallisce** se si ferma con un panic, come un `assert` falso o un indice fuori dai limiti, con un'eccezione che non cattura, con uno stack overflow, oppure terminando il processo con un codice di uscita diverso da 0, come `os.exit(3)`.

Qualsiasi altra forma è un errore di compilazione. Un test con dei parametri, o con un risultato:

```cromo error
[[test]]
void meanOf(list<f64> values) {
}

i32 main() {
    ret 0
}
```

```output
error: a test takes no parameters and returns nothing: [[test]] void meanOf()
```

`[[test]]` si scrive prima di una funzione di un modulo, non prima di un metodo di una struct, né prima di una struct.

## Dove stanno i test

Un test può stare nel file del codice che controlla, come `meanOfThree` qui sopra, oppure in un file accanto il cui nome finisce con `_test.cro`:

- per un modulo fatto di un file, `stats.cro`, il file `stats_test.cro`;
- per un modulo-cartella, ogni file della cartella il cui nome finisce con `_test.cro`, come `shapes/area_test.cro`;
- per il file principale, `main.cro`, il file `main_test.cro`.

Un file `_test.cro` fa parte del modulo, come i file per un solo sistema ([Moduli e progetti](modules.md)): i suoi test vedono anche i nomi privati del modulo, e il file può dichiarare per loro delle funzioni di supporto sue. Lo legge solo `cromo test`: `cromo build` e `cromo run` lo escludono, così i test e le loro funzioni di supporto non arrivano mai nell'eseguibile. Un test nel file del codice viene compilato con il programma, ma lo esegue solo `cromo test`.

```cromo file=stats_test.cro
[[test]]
void meanOfOne() {
    list<f64> values = [5.0]
    assert(mean(values) == 5.0)
}

[[test]]
void meanOfTwo() {
    list<f64> values = [1.0, 2.0]
    f64 m = mean(values)
    assert(m == 2.0)
}
```

## Eseguire i test

Un programma che usa il modulo:

```cromo file=main.cro
import stats

i32 main() {
    list<f64> values = [2.0, 4.0]
    println("{}", mean(values))
    ret 0
}
```

```output
3
```

`cromo test` nella cartella di `main.cro`, oppure `cromo test main.cro`, compila il programma con i suoi test e con quelli di tutti i suoi moduli, e li esegue. Il secondo test di `stats_test.cro` è sbagliato di proposito, perché la media di 1 e 2 è 1,5:

```sh
cromo test
```

```text
ok    stats.meanOfThree
ok    stats.meanOfOne
FAIL  stats.meanOfTwo
      panic: assertion failed: m == 2.0 (m is 1.5) at C:\work\stats\stats_test.cro:11:5
          at stats.meanOfTwo (C:\work\stats\stats_test.cro:11)
2 passed, 1 failed
```

- C'è una riga per ogni test, nell'ordine dei file: prima quelli del file principale, poi quelli dei moduli, nell'ordine in cui vengono importati. `ok` vuol dire che il test è passato, `FAIL` che è fallito.
- Sotto un test fallito compare, rientrato, tutto ciò che ha scritto, sullo standard output e sullo standard error: le sue righe, poi il panic o l'eccezione con il suo stack trace. I file dei moduli sono scritti con il loro percorso completo. L'output dei test che passano non viene mostrato.
- L'ultima riga conta i test passati e quelli falliti. Non ci sono tempi, così l'output di due esecuzioni si può confrontare.
- Il codice di uscita di `cromo test` è 0 quando ogni test è passato e 1 quando uno è fallito, così uno script o un sistema di integrazione continua possono fermarsi. Se il programma non compila, `cromo test` scrive gli errori e termina con 1, come `cromo build`.

### I nomi dei test

Un test del file principale si chiama come la sua funzione, `meanOfThree`; un test di un modulo ha davanti il nome del modulo, con tutto il suo percorso: `stats.meanOfThree`, oppure `geometry.shapes.unitSquare` per un test nella cartella `geometry/shapes`. Due test di moduli diversi possono avere lo stesso nome.

### Scegliere i test

I nomi scritti dopo il file scelgono i test da eseguire: quelli il cui nome ne contiene uno.

```sh
cromo test main.cro meanOf        # meanOfThree, meanOfOne and meanOfTwo
cromo test main.cro Three Two     # meanOfThree and meanOfTwo
cromo test Three                  # the same, with main.cro of this folder
```

Un nome che finisce con `.cro` è il file del programma; gli altri scelgono i test. Quando nessun test contiene uno dei nomi, `cromo test` scrive `no test has those names` e termina con 1. Un programma senza test scrive `no tests: write them as [[test]] void name()` e termina con 0.

Per essere testato un programma non ha bisogno di `main`: `cromo test shapes.cro` esegue i test di un file che non ce l'ha, come un modulo testato da solo.

### Come vengono eseguiti i test

- `cromo test` compila un solo eseguibile con tutti i test, in una cartella temporanea, e lo avvia una volta per ogni test. Ogni test gira in un processo tutto suo, tanti alla volta quanti sono i processori: un test che va in crash ferma solo sé stesso, e i test non possono condividere uno stato attraverso le variabili del programma.
- Ogni test gira nella cartella corrente di `cromo test`, quindi trova i file del progetto con i loro percorsi relativi, e il suo standard input è vuoto.
- Un test non ha un limite di tempo: un test che non termina mai tiene `cromo test` in attesa.
- I test girano sul sistema su cui gira `cromo`. `cromo test` accetta le opzioni della build che lì hanno senso: `--release`, da `-O0` a `-O3`, `--cpu`. `-o`, `--target` e `--emit-ir` sono opzioni solo di `cromo build`, e gli argomenti dopo `--` sono solo per `cromo run`.
- I test della libreria standard non vengono eseguiti con quelli del programma.

In un progetto con un `cromo.mod`, `cromo test` senza un file esegue a turno i test di ciascuno dei suoi eseguibili, dopo una riga con il nome e il file principale di ognuno; il suo codice di uscita è 1 se uno qualsiasi di essi fallisce ([Moduli e progetti](modules.md)).

## Come fallisce un test

Questo file ha un test per ogni modo di fallire. Non ha un `main`: serve solo per i test.

```cromo file=checks.cro
import std.os

i64 parsePort(string text) {
    i64 port = i64(text)
    if (port < 1 || port > 65535) {
        throw Error(format("{} is not a port", port))
    }
    ret port
}

[[test]]
void quiet() {
    println("this line is not shown")
}

[[test]]
void wrongSum() {
    println("adding")
    i64 total = 2 + parsePort("3")
    assert(total == 6)
}

[[test]]
void badPort() {
    parsePort("70000")
}

[[test]]
void outOfRange() {
    list<i64> ports = [80, 443]
    println("{}", ports[2])
}

[[test]]
void exits() {
    os.exit(3)
}
```

```sh
cromo test checks.cro
```

```text
ok    quiet
FAIL  wrongSum
      adding
      panic: assertion failed: total == 6 (total is 5) at checks.cro:20:5
          at checks.wrongSum (checks.cro:20)
FAIL  badPort
      uncaught exception: 70000 is not a port
          at checks.parsePort (checks.cro:6)
          at checks.badPort (checks.cro:25)
FAIL  outOfRange
      panic: index out of bounds: the index is 2 but the length is 2 at checks.cro:31:24
          at checks.outOfRange (checks.cro:31)
FAIL  exits
      the test ended with the exit code 3
1 passed, 4 failed
```

Un panic termina un processo con il codice di uscita 101, e un'eccezione non catturata con 1: in quei casi il messaggio sullo standard error dice che cosa è successo. Un test che termina con un qualsiasi altro codice, senza un messaggio, riceve la riga `the test ended with the exit code` seguita dal suo codice. Anche uno stack overflow è un panic, con la sua traccia.

## assert

`assert(condition)` ferma il programma con un panic quando la condizione è falsa. Il messaggio riporta la condizione così come è scritta nel sorgente, il file, la riga e la colonna dell'`assert`, e lo stack trace.

In un confronto tra numeri o tra valori `bool`, con `==`, `!=`, `<`, `<=`, `>` o `>=`, il messaggio riporta anche il valore di ciascun lato che non sia un letterale. Ogni lato viene calcolato una volta sola, quindi un lato con un effetto, come una chiamata che modifica il suo argomento, viene eseguito una volta sola anche quando l'asserzione fallisce:

```cromo file=counter.cro
i32 next(ref i32 counter) {
    counter++
    ret counter
}

i32 main() {
    i32 c = 1
    assert(next(ref c) == 2)
    assert(next(ref c) == 5)
    ret 0
}
```

```output
panic: assertion failed: next(ref c) == 5 (next(ref c) is 3) at counter.cro:9:5
    at main (counter.cro:9)
```

Per qualsiasi altra condizione, come un confronto tra stringhe, una chiamata che restituisce un `bool` o due condizioni unite da `&&`, il messaggio riporta solo la condizione. Un confronto con i valori di entrambi i lati, `assert(ready == wanted)`, scrive `(ready is false, wanted is true)`.

`assert` è una funzione integrata: funziona in qualsiasi funzione, non solo nei test, e anche negli eseguibili compilati con `--release`. Il panic non si può catturare, quindi un `assert` falso termina sempre il programma, o il test, con il codice di uscita 101. [Errori](errors.md) spiega i panic.

## Testare gli errori

Un test che si aspetta un'eccezione la cattura, e fallisce se l'eccezione non arriva:

```cromo
i64 parsePort(string text) {
    i64 port = i64(text)
    if (port < 1 || port > 65535) {
        throw Error(format("{} is not a port", port))
    }
    ret port
}

[[test]]
void rejectsZero() {
    bool thrown = false
    try {
        parsePort("0")
    } catch (Exception e) {
        thrown = true
        assert(e.message() == "0 is not a port")
    }
    assert(thrown)
}

i32 main() {
    println("{}", parsePort("8080"))
    ret 0
}
```

```output
8080
```

Un panic invece non si può catturare: un test che ne incontra uno fallisce, quindi non si può verificare con un test che avvengano gli errori con cui un programma si ferma di proposito, come un indice fuori dai limiti.

## Riepilogo

- `[[test]] void name()` è un test: passa se termina normalmente, e fallisce con un panic, un'eccezione non catturata, uno stack overflow o un codice di uscita diverso da 0.
- I test stanno nel codice o in file `_test.cro` accanto, che fanno parte del modulo, ne vedono i nomi privati e vengono compilati solo da `cromo test`.
- `cromo test [file.cro] [names]` esegue ogni test in un processo a sé, in parallelo, nella cartella corrente; scrive `ok` o `FAIL` per ciascuno, l'output di quelli falliti e il conteggio, e termina con 1 se uno è fallito.
- I test di un modulo portano il suo nome, come `stats.meanOfTwo`; i nomi scritti dopo il file scelgono i test che li contengono.
- `cromo test` accetta `--release`, `-O` e `--cpu`, ed esegue i test sul sistema su cui gira.
- `assert(condition)` è un panic quando la condizione è falsa, con i valori dei lati di un confronto tra numeri o `bool`, calcolati una volta sola.
