# Chiamare il C

Ogni sistema offre i suoi servizi attraverso funzioni C, e molte librerie sono scritte in C. Cromo le chiama direttamente, senza un compilatore C e senza file header, dai moduli che lo dichiarano con `import unsafe`. Il resto del programma usa quei moduli attraverso funzioni e struct sicure, così la parte non sicura resta piccola e facile da individuare.

## Dichiarare funzioni C

Un modulo che scrive `import unsafe` può dichiarare funzioni C in un blocco `extern`, che ne indica la libreria:

```cromo
import unsafe

extern "kernel32" {
    i32 MulDiv(i32 number, i32 numerator, i32 denominator)
    i32 QueryPerformanceFrequency(ref i64 frequency)
}

i32 main() {
    println("{}", MulDiv(10, 3, 4))
    i64 ticks
    QueryPerformanceFrequency(ref ticks)
    println("the counter runs: {}", ticks > 0)
    ret 0
}
```

```output
8
the counter runs: true
```

Questo programma è per Windows, la cui `kernel32.dll` contiene queste funzioni.

- Su Windows, `extern "name"` è `name.dll`. `cromo` legge le funzioni esportate dalla DLL mentre compila, quindi un nome scritto male è un errore di compilazione, non un crash all'avvio. La libreria di importazione la scrive da sé: il Windows SDK non serve. Se accanto al modulo c'è una `name.lib`, viene usata quella, per esempio una libreria C statica.
- Su Linux, `extern "name"` è la libreria statica `libname.a` accanto al modulo.
- Una DLL accanto a un modulo viene copiata accanto all'eseguibile.

```cromo error
import unsafe

extern "kernel32" {
    i32 MulDvi(i32 number, i32 numerator, i32 denominator)
}

i32 main() {
    ret MulDvi(1, 2, 3)
}
```

```output
error: kernel32.dll has no function 'MulDvi'
```

## I tipi al confine

Le funzioni C prendono e restituiscono numeri, `bool` e `addr`, un indirizzo C che il codice Cromo non può seguire. Un parametro `ref` è un puntatore C a un numero o a una struct di numeri, che ha lo stesso layout del C. Il `BOOL` di Windows è un `i32`.

Le funzioni C mantengono il loro nome e sono private al loro modulo: gli altri moduli non possono chiamarle.

## Incapsulare una risorsa

Una risorsa C, come un handle o un descrittore, va messa in una struct con un distruttore. Il resto del programma usa poi la struct, e la risorsa viene sempre rilasciata, come ogni altra risorsa in Cromo:

```cromo norun
import unsafe

extern "kernel32" {
    i32 QueryPerformanceCounter(ref i64 count)
    i32 QueryPerformanceFrequency(ref i64 frequency)
}

// Misura il tempo trascorso dalla sua creazione.
public struct Stopwatch {
    i64 start
    i64 frequency

    public Stopwatch() {
        QueryPerformanceFrequency(ref frequency)
        QueryPerformanceCounter(ref start)
    }

    public i64 micros() const {
        i64 now
        QueryPerformanceCounter(ref now)
        ret (now - start) * 1000000 / frequency
    }
}
```

Il modulo offre `Stopwatch`; i moduli che lo importano non scrivono `import unsafe` e non possono toccare le funzioni C.

## Il dialetto unsafe

`import unsafe` dà accesso anche al tipo `addr` e alle funzioni integrate che lavorano sulla memoria grezza, come `alloc` e `free`. La libreria standard è scritta in questo dialetto: `list`, `string` e `map` sono struct di Cromo che gestiscono la propria memoria con queste funzioni, dietro un'interfaccia sicura.

Di norma un programma non ha motivo di usare il dialetto, se non per chiamare il C. Cercando `import unsafe` si trovano tutti i punti in cui la sicurezza del programma dipende dalla cura di chi l'ha scritto.

## Riepilogo

- `import unsafe` permette i blocchi `extern` di funzioni C, il tipo `addr` e la memoria grezza.
- `extern "name"` è `name.dll` su Windows, `libname.a` su Linux; le funzioni vengono controllate in fase di compilazione.
- Le risorse C vanno in struct con distruttori, e il resto del programma usa quelle.
