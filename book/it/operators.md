# Numeri e operatori

Questo capitolo parla dei numeri: i tipi interi e in virgola mobile, il tipo di un letterale, gli operatori e la loro precedenza, che cosa succede quando un risultato non sta nel suo tipo, e le conversioni, anche dal testo. Quasi tutte le regole sono quelle del C; dove il C lascia il risultato indefinito, Cromo stabilisce una regola, e il programma si ferma invece di proseguire con un valore sbagliato.

## I tipi numerici

| Tipo | Dimensione | Valori |
|---|---|---|
| `i8` | 8 bit | da −128 a 127 |
| `u8` | 8 bit | da 0 a 255 |
| `i16` | 16 bit | da −32 768 a 32 767 |
| `u16` | 16 bit | da 0 a 65 535 |
| `i32` | 32 bit | da −2 147 483 648 a 2 147 483 647 |
| `u32` | 32 bit | da 0 a 4 294 967 295 |
| `i64` | 64 bit | da −9 223 372 036 854 775 808 a 9 223 372 036 854 775 807 |
| `u64` | 64 bit | da 0 a 18 446 744 073 709 551 615 |
| `f32` | 32 bit | IEEE 754 a precisione singola, circa 7 cifre significative |
| `f64` | 64 bit | IEEE 754 a precisione doppia, circa 16 cifre significative |

I tipi `i` sono con segno, in complemento a due, e i tipi `u` sono senza segno. Ogni tipo ha la stessa dimensione su tutti i sistemi: non c'è un `int` la cui dimensione dipende dalla macchina, né un `size_t`. Lunghezze e posizioni sono `i64`. Non esistono interi a 128 bit.

`char` non è un tipo a sé: è un altro nome di `u8`, un byte di una stringa UTF-8, come mostra [Concetti comuni](common-concepts.md). Ciò che questo capitolo dice di `u8` vale anche per `char`.

Il valore predefinito di ogni numero è 0.

## Letterali

Un letterale intero si scrive in decimale, in esadecimale dopo `0x` o in binario dopo `0b`, con `_` per separare le cifre: `1_000_000`, `0xFF`, `0b1010`, `0xFFFF_FFFF`. Uno zero davanti non lo rende ottale come in C: `017` è diciassette. Non ci sono suffissi come `5u` o `5L`.

Un letterale in virgola mobile ha un punto decimale con delle cifre da entrambi i lati, un esponente dopo `e`, o tutti e due: `3.14`, `1e-9`, `2.5e3`. `.5` e `5.` non sono letterali: si scrive `0.5` e `5.0`. Un numero negativo è un letterale preceduto da `-`.

### Il tipo di un letterale

Un letterale non ha un tipo suo: prende quello richiesto dal punto in cui si trova, cioè il tipo della variabile, del parametro o dell'altro operando.

```cromo
i32 main() {
    i32 apples = 5
    i64 total = apples + 10  // 10 is an i32, as apples
    u8 mask = 0xF0           // 0xF0 is a u8
    f64 half = 10 / 4        // 10 and 4 are f64: 2.5, not 2
    f64 whole = 5            // an integer literal where a float is expected
    println("{} {} {} {}", total, mask, half, whole)
    ret 0
}
```

```output
15 240 2.5 5
```

`f64 half = 10 / 4` vale 2,5, mentre in C vale 2: i due letterali sono `f64`, quindi la divisione è tra numeri in virgola mobile. Un letterale intero si può usare dove ci si aspetta un numero in virgola mobile, ma non il contrario:

```cromo error
i32 main() {
    i32 n = 2.5
    ret 0
}
```

```output
error: this decimal literal cannot be used as i32
```

Un letterale deve stare nel suo tipo. Il compilatore lo controlla, anche quando il tipo viene dall'altro operando:

```cromo error
i32 main() {
    u8 small = 300
    u8 negative = -1
    i32 count = 3
    i64 big = 5000000000 + count
    ret 0
}
```

```output
error: value 300 is out of range for u8 (0 .. 255)
error: value -1 is out of range for u8 (0 .. 255)
error: value 5000000000 is out of range for i32 (-2147483648 .. 2147483647)
```

Nell'ultima riga `5000000000` prende il tipo di `count`, un `i32`: scrivendo `i64(count)` la somma diventa un `i64`.

Dove nulla indica il tipo, il compilatore chiede di specificarlo:

```cromo error
i32 main() {
    println("{}", 10 / 3)
    ret 0
}
```

```output
error: the type of this expression cannot be deduced
```

Basta scrivere `i32(10) / 3`, o usare una variabile, per risolvere.

## Mescolare i tipi

Un numero diventa da solo un numero di un altro tipo solo quando quel tipo ne contiene tutti i valori:

- un intero con segno diventa un intero con segno più grande: `i8` → `i16` → `i32` → `i64`;
- un intero senza segno diventa un intero più grande, con o senza segno: `u8` → `u16`, `u8` → `i16`, `u32` → `i64`;
- un intero diventa un numero in virgola mobile che lo rappresenta esattamente: quelli a 8 e 16 bit un `f32`, quelli fino a 32 bit un `f64`;
- un `f32` diventa un `f64`.

In un'operazione tra due tipi, il valore del tipo più piccolo viene convertito in quello più grande: `u8 + i32` è un `i32`, `i32 * f64` un `f64`. Quando nessuno dei due tipi contiene l'altro, come `i32` e `u32`, o un intero con segno e uno senza segno a 64 bit, l'operazione è un errore:

```cromo error
i32 main() {
    i32 a = -5
    u32 b = 7
    i64 sum = a + b
    ret 0
}
```

```output
error: cannot mix i32 and u32: convert one of them explicitly, for example i64(x)
```

In C, `-1 < 1u` è falso, perché -1 diventa in silenzio il più grande numero senza segno. In Cromo la conversione si scrive, verso un tipo che contiene entrambi i valori:

```cromo
i32 main() {
    i32 a = -5
    u32 b = 7
    u8 small = 200
    f64 ratio = 0.5
    println("{} {} {}", i64(a) + i64(b), small + a, a * ratio)
    ret 0
}
```

```output
2 195 -2.5
```

Nemmeno `i64` e `f64` si mescolano, perché un `f64` non può contenere ogni `i64`: un `i64` moltiplicato per un `f64` è l'errore `mismatched types i64 and f64`, e la soluzione è `f64(n) * ratio`.

Il tipo di un'operazione è quello dei suoi operandi, mai quello del punto in cui va il risultato. Con `i32 a = 100000`, `i64 big = a * 100000` calcola un prodotto `i32`, che non ci sta e ferma il programma; il prodotto diventa un `i64` solo dopo. Per calcolarlo come `i64`, si converte prima:

```cromo
i32 main() {
    i32 a = 100000
    i64 big = i64(a) * 100000
    println("{}", big)
    ret 0
}
```

```output
10000000000
```

## Operatori

Dal più forte al più debole:

| Operatori | Significato | Raggruppamento |
|---|---|---|
| `.` `()` `[]` | membro, chiamata, indice | da sinistra a destra |
| `!` `-` | not, inversione dei bit, negazione | prima del valore |
| `*` `/` `%` | moltiplicazione, divisione, resto | da sinistra a destra |
| `+` `-` | addizione, sottrazione | da sinistra a destra |
| `<<` `>>` | shift | da sinistra a destra |
| `&` | and bit a bit | da sinistra a destra |
| `^` | or esclusivo bit a bit | da sinistra a destra |
| `\|` | or bit a bit | da sinistra a destra |
| `<` `<=` `>` `>=` | confronti | da sinistra a destra |
| `==` `!=` | uguale, diverso | da sinistra a destra |
| `&&` | and logico, a cortocircuito | da sinistra a destra |
| `\|\|` | or logico, a cortocircuito | da sinistra a destra |
| `? :` | condizionale | da destra a sinistra |

L'ordine è quello del C, con una differenza: gli operatori bit a bit legano più dei confronti, quindi `flags & 4 == 4` significa `(flags & 4) == 4`, come ci si aspetterebbe, mentre in C significa `flags & (4 == 4)`. Come in C, `==` e `!=` legano meno degli altri confronti: `a < b == c < d` confronta due valori `bool`.

```cromo
i32 main() {
    i32 a = 20
    i32 b = 5
    i32 c = 2
    println("{} {} {}", a - b - c, a / b * c, a + b * c)
    i32 flags = 6
    println("{} {}", flags & 4 == 4, 1 + flags << 2)
    bool x = true
    bool y = false
    println("{}", !x || y && x)
    ret 0
}
```

```output
13 8 30
true 28
false
```

`a - b - c` è `(a - b) - c`, e `a / b * c` è `(a / b) * c`. `1 + flags << 2` è `(1 + flags) << 2`, e `!x || y && x` è `(!x) || (y && x)`. Quando l'ordine non è ovvio per chi legge, le parentesi non costano nulla.

I confronti non si concatenano: `a < b < c` confronta il `bool` di `a < b` con un numero, ed è un errore. Si scrive `a < b && b < c`.

### Aritmetica

`+`, `-`, `*` e `/` funzionano su ogni numero, e `+` unisce anche le stringhe. `%`, il resto, funziona solo sugli interi: non esiste il resto tra numeri in virgola mobile.

```cromo error
i32 main() {
    f64 angle = 7.5
    u32 count = 3
    println("{}", angle % 2.0)
    println("{}", -count)
    ret 0
}
```

```output
error: '%' only works on integers
error: cannot negate an unsigned value (u32)
```

Anche `-x` su un intero senza segno è un errore: il suo risultato non ci starebbe quasi mai. Va convertito prima: `-i64(count)`.

### Confronti e logica

I confronti danno un `bool`. `&&`, `||` e `!` funzionano solo sui valori `bool`, non sui numeri. `&&` e `||` calcolano il loro lato destro solo quando il sinistro non basta già a decidere, quindi il lato destro può contare sul sinistro:

```cromo
bool positiveAt(list<i32> values, i64 i) {
    ret i < values.len() && values[i] > 0
}

i32 main() {
    list<i32> values = [4, -1]
    println("{} {} {}", positiveAt(values, 0), positiveAt(values, 1), positiveAt(values, 5))
    ret 0
}
```

```output
true false false
```

Con `i` uguale a 5, `values[i]` non viene mai letto: l'indice sarebbe fuori dalla lista.

### Operatori bit a bit

`&`, `|` e `^` sono l'and, l'or e l'or esclusivo dei bit di due interi. `!` davanti a un intero ne inverte i bit: Cromo non ha `~`, che compare solo nel nome di un distruttore.

```cromo
i32 main() {
    u8 flags = 0b0110
    println("{} {} {} {}", flags & 0b0100, flags | 0b0001, flags ^ 0b0010, !flags)
    i32 n = 6
    println("{}", !n)
    ret 0
}
```

```output
4 7 4 249
-7
```

Gli operatori bit a bit funzionano solo sugli interi. Sui valori `bool` sono errori: gli operatori logici sono `&&`, `||` e `!`, e l'or esclusivo di due valori `bool` è `!=`.

```cromo error
i32 main() {
    bool a = true
    bool b = false
    bool both = a & b
    ret 0
}
```

```output
error: '&' needs numbers, found bool
```

### L'espressione condizionale

`c ? a : b` vale `a` quando `c` è vera e `b` altrimenti, come in C. Viene calcolato solo il ramo scelto. Lega meno di `||` e raggruppa da destra, quindi una catena di espressioni condizionali si legge come una catena di `else if`: `a ? b : c ? d : e` è `a ? b : (c ? d : e)`.

```cromo
string size(i64 n) {
    ret n < 10 ? "small" : n < 1000 ? "medium" : "large"
}

i32 noisy(i32 n) {
    println("computing {}", n)
    ret n
}

i32 main() {
    println("{} {} {}", size(5), size(50), size(5000))
    i64 n = 7
    i64 next = n % 2 == 0 ? n / 2 : 3 * n + 1
    println("{}", next)
    bool first = false
    i32 chosen = first ? noisy(1) : noisy(2)
    println("{}", chosen)
    ret 0
}
```

```output
small medium large
22
computing 2
2
```

La condizione deve essere un `bool`, come in un `if`:

```cromo error
i32 main() {
    i32 count = 3
    i32 shown = count ? count : 1
    ret 0
}
```

```output
error: the condition must be a bool, found i32
```

I due rami hanno lo stesso tipo, oppure uno si converte da solo nell'altro, come `i32` in `i64`. Un letterale in un ramo prende il tipo dell'altro ramo, o del contesto: in `f64 weight = heavy ? 1 : 0.5` l'`1` è un `f64`. Una riga che finisce con `?` o `:` prosegue nella successiva.

Un ramo che è una variabile che possiede delle risorse, come una `string`, ne dà una copia, e la variabile resta com'era. Per ora `move` non si può scrivere in un ramo: per spostare uno di due valori si usa un `if`.

```cromo
i32 main() {
    string short = "Ada"
    string long = "Grace"
    bool wantShort = true
    string chosen = wantShort ? short : long
    chosen += "!"
    println("{} {}", chosen, short)
    ret 0
}
```

```output
Ada! Ada
```

```cromo error
i32 main() {
    string a = "first"
    string b = "second"
    bool useA = true
    string taken = useA ? move a : b
    ret 0
}
```

```output
error: 'move' is not written in a branch of '?' for now
```

### Assegnamenti

`=` dà a una variabile un nuovo valore. Ogni operatore aritmetico e bit a bit ha il suo assegnamento composto, `x op= y`, che equivale a `x = x op y` con gli stessi controlli: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=` e `>>=`. `x++` e `x--` aggiungono e tolgono uno.

```cromo
i32 main() {
    i32 x = 10
    x *= 3
    x -= 2
    x /= 4
    x <<= 2
    x |= 1
    x++
    println("{}", x)
    ret 0
}
```

```output
30
```

Un assegnamento è un'istruzione, non un'espressione, e lo stesso vale per `x++` e `x--`: stanno su una riga tutta loro. `if (x = 5)`, `a = b = 0` e `v[i++]` non sono Cromo:

```cromo error
i32 main() {
    i32 a = 0
    i32 b = 0
    a = b = 3
    ret 0
}
```

```output
error: expected a newline at the end of the statement (one statement per line), found '='
```

## Divisione intera e resto

`/` tra interi tronca verso lo zero, e `%` ha il segno del dividendo, il numero alla sua sinistra, come in C. Così `(a / b) * b + a % b` è sempre `a`:

```cromo
i32 main() {
    i32 a = 7
    i32 b = -7
    println("{} {} {} {}", a / 2, b / 2, a % 3, b % 3)
    println("{} {}", a % -3, b % -3)
    ret 0
}
```

```output
3 -3 1 -1
1 -1
```

`-7 / 2` è -3, non -4, e `-7 % 3` è -1. Un resto che non è mai negativo, come una posizione su un cerchio di `n` posti, si ottiene con `((a % n) + n) % n`.

## Overflow

Un'operazione tra interi il cui risultato non sta nel suo tipo è un overflow. In una build di debug, quella predefinita, ferma il programma:

```cromo file=overflow.cro
i32 main() {
    i8 small = 120
    for (i32 i = 0; i < 10; i++) {
        small += 1
    }
    println("{}", small)
    ret 0
}
```

```output
panic: integer overflow at overflow.cro:4:9
    at main (overflow.cro:4)
```

Un *panic* ferma il programma, stampa dove è avvenuto e le chiamate che hanno portato fin lì, e restituisce il codice di uscita 101. Il controllo copre `+`, `-`, `*`, gli assegnamenti composti, `++` e `--`, e due casi facili da dimenticare: la negazione del valore più piccolo, `-x` con `x` uguale a −2 147 483 648 per un `i32`, e la divisione del valore più piccolo per -1. Entrambi i risultati sarebbero 2 147 483 648, uno più del più grande `i32`.

In una build di release, con `--release`, il controllo non c'è e gli interi ricominciano dall'estremo opposto (il *wrap-around*), come fanno gli interi senza segno del C: il risultato tiene i bit più bassi, quindi 127 + 1 in un `i8` dà −128. Il controllo ha un piccolo costo, e un programma già testato può farne a meno. Il risultato è lo stesso su ogni sistema, e mai indefinito, anche per gli interi con segno.

### Il wrap-around voluto

Alcuni calcoli vogliono il wrap-around, come gli hash, i checksum e i numeri casuali. `wrapAdd(a, b)`, `wrapSub(a, b)` e `wrapMul(a, b)` sommano, sottraggono e moltiplicano due interi dello stesso tipo tenendo i bit più bassi, senza il controllo, anche nelle build di debug:

```cromo
// The FNV-1a hash of a text: its arithmetic wraps around on purpose.
u64 fnv1a(string text) {
    u64 hash = 0xcbf29ce484222325
    for (i64 i = 0; i < text.len(); i++) {
        hash = wrapMul(hash ^ text[i], 0x100000001b3)
    }
    ret hash
}

i32 main() {
    println("{:x}", fnv1a("hello"))
    u8 a = 200
    u8 b = 100
    println("{} {} {}", wrapAdd(a, b), wrapSub(b, a), wrapMul(a, b))
    ret 0
}
```

```output
a430d84680aabd0b
44 156 32
```

Funzionano solo sugli interi. Il loro tipo viene dagli argomenti, quindi con due letterali, come `wrapAdd(1, 2)`, il compilatore chiede di scriverlo: `wrapAdd(u32(1), 2)`.

## Divisione per zero

Un intero diviso per zero, con `/` o `%`, è un panic, sia nelle build di debug sia in quelle di release: non esiste un valore che possa rappresentare il risultato.

```cromo file=average.cro
i32 average(i32 total, i32 count) {
    ret total / count
}

i32 main() {
    println("{}", average(10, 2))
    println("{}", average(10, 0))
    ret 0
}
```

```output
5
panic: division by zero at average.cro:2:15
    at average (average.cro:2)
    at main (average.cro:7)
```

I numeri in virgola mobile seguono invece lo standard IEEE 754: una divisione per zero dà infinito, `inf` o `-inf`, e 0,0 / 0,0 dà `nan`, "not a number", senza panic. Anche un risultato troppo grande per il tipo è `inf`. `nan` non è uguale a niente, nemmeno a sé stesso, quindi `x != x` è vero solo quando `x` è `nan`:

```cromo
i32 main() {
    f64 one = 1.0
    f64 zero = 0.0
    f64 nan = zero / zero
    println("{} {} {}", one / zero, -one / zero, nan)
    println("{} {}", nan == nan, nan != nan)
    ret 0
}
```

```output
inf -inf nan
false true
```

Come si stampano i numeri in virgola mobile, e con quante cifre, è spiegato in [Formattare il testo](formatting.md).

## Shift

`x << n` sposta i bit di `x` a sinistra di `n` posizioni, e `x >> n` a destra. Il risultato ha il tipo di `x`; il numero di posizioni `n` può essere un intero di qualsiasi tipo.

- `<<` fa entrare degli zeri da destra. I bit che escono a sinistra vanno persi, senza controllo di overflow.
- `>>` su un intero con segno ne conserva il segno, facendo entrare copie del bit più alto: `-16 >> 2` è -4. Su un intero senza segno fa entrare degli zeri.
- Un letterale a sinistra prende il tipo dal contesto, come in `u64 bit = 1 << k`. Senza un contesto, come in `println("{}", 1 << k)`, il compilatore chiede di scriverlo: `u64(1) << k`.

```cromo
i32 main() {
    i32 negative = -16
    u32 high = 0xF0000000
    u8 n = 2
    println("{} {}", negative >> n, high >> n)
    u8 b = 0x81
    println("{}", b << 1)
    i64 k = 40
    u64 bit = 1 << k
    println("{}", bit)
    ret 0
}
```

```output
-4 1006632960
2
1099511627776
```

Un numero di posizioni negativo, o non minore della larghezza del tipo, come `x << 40` su un `i32`, è un panic in una build di debug: `panic: shift count out of the width of i32`. In C è un comportamento indefinito. Con `--release` il numero di posizioni viene preso modulo la larghezza, quindi `x << 40` su un `i32` è `x << 8`, e -1 posizioni diventano 31.

## Bit

Cinque funzioni integrate lavorano sui bit di un intero di qualsiasi tipo. Ognuna diventa una sola istruzione del processore, o poche dove il processore non ne ha una:

- `countOnes(x)`: quanti bit di `x` sono a 1;
- `trailingZeros(x)`: quanti bit sono a 0 sotto il bit a 1 più basso, cioè la posizione di quel bit;
- `leadingZeros(x)`: quanti bit sono a 0 sopra il bit a 1 più alto;
- `rotateLeft(x, n)` e `rotateRight(x, n)`: i bit di `x` ruotati di `n` posizioni, quelli che escono da una parte rientrando dall'altra.

Le prime tre danno un `i32`; per 0 danno la larghezza del tipo, 64 per un `u64`. Le rotazioni danno il tipo di `x`; `n` è un intero di qualsiasi tipo, preso modulo la larghezza, quindi un `n` negativo ruota nell'altro verso.

```cromo
i32 main() {
    u64 row = 0x00FF000000000000
    println("{} {} {}", countOnes(row), trailingZeros(row), leadingZeros(row))
    println("{:x} {:x}", rotateLeft(row, 16), rotateRight(row, 16))
    u8 zero = 0
    println("{} {}", trailingZeros(zero), leadingZeros(zero))
    u8 b = 0b1000_0001
    i32 back = -1
    println("{} {}", rotateLeft(b, 1), rotateLeft(b, back))

    // The positions of the bits at 1 of a set, from the lowest.
    u64 set = 0b1011_0100
    list<i32> positions
    for (set != 0) {
        positions.add(trailingZeros(set))
        set &= set - 1
    }
    println("{:?}", positions)
    ret 0
}
```

```output
8 48 8
ff ff00000000
8 8
3 192
[2, 4, 5, 7]
```

`set & (set - 1)` toglie il bit a 1 più basso, quindi il ciclo visita i bit dell'insieme uno alla volta.

Il risultato di queste funzioni dipende dalla larghezza del tipo, quindi un letterale intero non può essere il loro argomento: il suo tipo andrebbe tirato a indovinare.

```cromo error
i32 main() {
    println("{}", countOnes(5))
    ret 0
}
```

```output
error: the type of this literal cannot be deduced: write it, for example countOnes(u64(5))
```

Una rotazione di un letterale prende il tipo atteso, come in `u64 r = rotateLeft(1, 8)`, e un letterale usato come numero di posizioni `n` prende il tipo di `x`. Un valore che non è un intero è un errore.

### I bit di un numero in virgola mobile

`x.bits()` dà i bit di un numero in virgola mobile così come sono in memoria: quelli di un `f64` come `u64`, quelli di un `f32` come `u32`. `f64.fromBits(b)` e `f32.fromBits(b)` ricostruiscono il numero dai suoi bit. Servono per i formati dei file e della rete, e per gli hash dei numeri:

```cromo
i32 main() {
    f64 x = 1.0
    u64 bits = x.bits()
    println("{:x}", bits)
    println("{}", f64.fromBits(bits + 1) - x)
    f32 h = -2.5
    println("{:x} {}", h.bits(), f32.fromBits(h.bits()))
    f64 negativeZero = -0.0
    println("{} {:x}", negativeZero == 0.0, negativeZero.bits())
    ret 0
}
```

```output
3ff0000000000000
2.220446049250313e-16
c0200000 -2.5
true 8000000000000000
```

I bit che seguono quelli di 1,0 sono quelli dell'`f64` successivo, e la differenza è la precisione del tipo intorno a 1. -0,0 è uguale a 0,0, ma i suoi bit no. Gli interi non hanno `bits()`: sono già i loro bit.

## Conversioni

Una conversione che può cambiare il valore si scrive come una chiamata con il nome del tipo: `i32(x)`, `f64(n)`, `u8(c)`. Il compilatore la chiede ovunque la conversione non sia implicita:

```cromo error
i32 main() {
    i64 big = 5
    i32 small = big
    ret 0
}
```

```output
error: a value of type i64 cannot be used as i32 without an explicit conversion, for example i32(x)
```

Che cosa fa la conversione dipende dai tipi:

- **Da un intero a un altro intero**, quando il valore non ci sta ne restano i bit più bassi, come in C: `u8(300)` è 44, e `u32(-1)` è 4 294 967 295. Non è mai un panic: la conversione è scritta, quindi la perdita è voluta.
- **Da un intero a un numero in virgola mobile**, il valore è il numero in virgola mobile più vicino: un `i64` sopra 2⁵³ può perdere le sue ultime cifre.
- **Da un numero in virgola mobile a un intero**, la parte frazionaria viene scartata, verso lo zero: `i32(2.9)` è 2 e `i32(-2.9)` è -2. Un valore che non sta nell'intero, o `nan`, è un panic in una build di debug; con `--release` dà il valore del tipo più vicino, e 0 per `nan`.
- **Da `f64` a `f32`**, il valore è l'`f32` più vicino, e infinito quando è troppo grande.

```cromo
i32 main() {
    i32 wide = 300
    i32 negative = -1
    i64 big = 5000000000
    println("{} {} {} {}", u8(wide), i8(wide), u32(negative), i32(big))
    f64 price = 2.9
    println("{} {}", i32(price), i32(-price))
    f64 tenth = 0.1
    println("{}", f64(f32(tenth)))
    ret 0
}
```

```output
44 44 4294967295 705032704
2 -2
0.10000000149011612
```

`f64(f32(tenth))` mostra che cosa conserva un `f32` di 0,1. Un numero in virgola mobile fuori dall'intervallo dell'intero ferma il programma:

```cromo file=convert.cro
i32 main() {
    f64 huge = 1e20
    println("{}", i32(huge))
    ret 0
}
```

```output
panic: floating-point value out of range for i32 at convert.cro:3:19
    at main (convert.cro:3)
```

### bool non è un numero

Un `bool` non si converte in un numero, e un numero non si converte in un `bool`, nemmeno con una conversione scritta:

```cromo error
i32 main() {
    bool ready = true
    i32 n = i32(ready)
    ret 0
}
```

```output
error: cannot convert bool to i32
```

L'espressione condizionale e un confronto dicono che cosa si intende: `ready ? 1 : 0` e `n != 0`.

## Numeri dal testo

`i32("42")` legge un numero da una stringa. Funziona per ogni tipo intero e per `f32` e `f64`, e l'argomento può essere una `string` qualsiasi, non solo un letterale.

Il testo deve essere il numero da solo, senza spazi: un `+` o un `-` facoltativo, poi delle cifre decimali. In un intero non sono ammessi l'esadecimale, `_` e il punto decimale. Per `f32` e `f64` il testo può avere anche un punto decimale e un esponente con `e` o `E`, e può essere `inf`, `infinity` o `nan`, in maiuscolo o in minuscolo. Il numero in virgola mobile è quello più vicino al testo, come lo dà `strtod` in C.

Quando il testo non è un numero, o il numero non sta nel tipo, la conversione lancia un'eccezione, un `Error`, che il programma può catturare, come spiega [Errori](errors.md):

```cromo
void show(string text) {
    try {
        println("'{}' is {}", text, i32(text))
    } catch (Error e) {
        println("'{}' is not an i32", text)
    }
}

i32 main() {
    show("42")
    show("-7")
    show("+007")
    show(" 42")
    show("4.0")
    show("0x1F")
    show("3000000000")
    println("{} {} {} {}", f64("2.5"), f64("-1e3"), f64("INF"), f32("0.1"))
    ret 0
}
```

```output
'42' is 42
'-7' is -7
'+007' is 7
' 42' is not an i32
'4.0' is not an i32
'0x1F' is not an i32
'3000000000' is not an i32
2.5 -1000 inf 0.1
```

Il messaggio dell'eccezione dice quale dei due errori è: `'abc' is not a valid i32`, oppure `'300' does not fit in u8`. Un numero negativo non sta in un tipo senza segno, tranne `u8("-0")`, che è 0. Un testo letto da un file o scritto da un utente può finire con un a capo o uno spazio: `trim()` li toglie prima della conversione.

## Riepilogo

- Gli interi vanno da `i8` a `i64` e da `u8` a `u64`, i numeri in virgola mobile sono `f32` e `f64`; `char` è `u8`; non esistono interi a 128 bit.
- Un letterale prende il tipo del suo contesto e deve starci; un'operazione ha il tipo dei suoi operandi, non quello della variabile che riceve il risultato.
- Un numero si converte da solo solo in un tipo che ne contiene tutti i valori; gli interi con segno e quelli senza segno della stessa dimensione non si mescolano.
- Gli operatori bit a bit legano più dei confronti; `c ? a : b` lega meno di tutti e raggruppa da destra; gli assegnamenti, `++` e `--` sono istruzioni.
- La divisione intera tronca verso lo zero, e `%` ha il segno del dividendo.
- L'overflow degli interi, la divisione intera per zero e uno shift di un numero di posizioni fuori dalla larghezza sono panic nelle build di debug; con `--release` l'overflow ricomincia dall'estremo opposto, il numero di posizioni viene preso modulo la larghezza, e la divisione per zero resta un panic. I numeri in virgola mobile danno `inf` e `nan`.
- `wrapAdd`, `wrapSub` e `wrapMul` fanno il wrap-around di proposito; `countOnes`, `trailingZeros`, `leadingZeros`, `rotateLeft` e `rotateRight` lavorano sui bit; `x.bits()` e `f64.fromBits(b)` danno e prendono i bit di un numero in virgola mobile.
- Le conversioni si scrivono come `i32(x)`: gli interi tengono i bit più bassi, i numeri in virgola mobile vengono troncati, e `bool` non è un numero. `i32("42")` legge un numero da un testo e lancia un `Error` quando non ci riesce.
