# Test

Cromo ha i test integrati: un test è una funzione marcata `[[test]]`, `assert` controlla una condizione, e `cromo test` esegue i test e ne riporta i risultati. Non servono librerie né framework.

## Scrivere un test

Un test è una funzione senza parametri che non restituisce nulla, preceduta dall'attributo `[[test]]`. Passa se termina normalmente, e fallisce con un panic, con un'eccezione che non cattura o con uno stack overflow:

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

I test possono stare nel file del codice, oppure in un file accanto il cui nome finisce con `_test.cro`. Quel file fa parte dello stesso modulo, quindi i suoi test vedono anche i nomi privati, e `cromo build` lo esclude dall'eseguibile:

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

```cromo file=main.cro
import stats

i32 main() {
    list<f64> values = [2.0, 4.0]
    println("{}", mean(values))
    ret 0
}
```

`cromo test` nella cartella di `main.cro` esegue i test del programma e di tutti i suoi moduli. Il secondo test qui sopra è sbagliato di proposito, perché la media di 1 e 2 è 1,5:

```sh
cromo test
```

```text
ok    stats.meanOfThree
ok    stats.meanOfOne
FAIL  stats.meanOfTwo
      panic: assertion failed: m == 2.0 (m is 1.5) at stats_test.cro:11:5
          at stats.meanOfTwo (stats_test.cro:11)
2 passed, 1 failed
```

- Ogni test gira in un processo tutto suo, tanti alla volta quanti sono i processori: un test che va in crash ferma solo sé stesso.
- L'output di un test viene mostrato solo quando il test fallisce.
- Il codice di uscita di `cromo test` è 1 se un test è fallito, così uno script o un sistema di integrazione continua possono fermarsi.
- I nomi dei test sono preceduti dal loro modulo: `stats.meanOfThree`.

I nomi scritti dopo il file scelgono quali test eseguire: `cromo test main.cro meanOf` esegue i test il cui nome contiene `meanOf`.

## assert

`assert(condition)` ferma il test con un panic quando la condizione è falsa. In un confronto tra numeri o tra `bool`, il messaggio riporta il valore di ciascun lato che non sia un letterale, calcolato una volta sola, come `m is 1.5` qui sopra: il più delle volte basta per capire cosa è andato storto senza ricorrere a un debugger.

`assert` funziona in qualsiasi funzione, non solo nei test, e anche nelle build di release.

## Testare gli errori

Un test che si aspetta un'eccezione la cattura:

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

## Riepilogo

- `[[test]] void name()` è un test; `assert(condition)` esegue il controllo.
- I test stanno nel codice o in `name_test.cro`, che vede i nomi privati del modulo.
- `cromo test` esegue ogni test in un processo a sé, in parallelo, e riporta i fallimenti con la loro causa.
