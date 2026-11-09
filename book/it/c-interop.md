# Chiamare il C

Ogni sistema offre i suoi servizi attraverso funzioni C, e molte librerie sono scritte in C. Cromo le chiama direttamente, senza un compilatore C e senza file header, dai moduli che scrivono `import unsafe`. Il resto del programma usa quei moduli attraverso funzioni e struct sicure, così la parte non sicura resta piccola e facile da individuare.

Questo capitolo tratta le funzioni C: come si dichiarano, dove si trovano le loro librerie, quali tipi attraversano il confine e come le stringhe e la memoria passano al C e tornano indietro. Il resto del dialetto, come la memoria grezza e le variabili di modulo, è in [Il dialetto unsafe](unsafe.md).

## Dichiarare funzioni C

Un modulo che scrive `import unsafe` può dichiarare funzioni C in un blocco `extern`, che indica tra virgolette la loro libreria:

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

Questo programma è per Windows, la cui `kernel32.dll` contiene queste funzioni. Gli esempi di questo capitolo usano le librerie di Windows, presenti in ogni Windows; su Linux le stesse regole valgono per le librerie statiche, come si vedrà più avanti.

Le regole del blocco:

- La libreria è un nome senza estensione: `"kernel32"`, non `"kernel32.dll"`. Ogni sistema ne ricava il proprio nome di file: `kernel32.dll` su Windows, `libkernel32.a` su Linux.
- Al suo interno, ogni funzione C si scrive come una funzione Cromo senza corpo: il tipo del risultato, il nome C e i parametri con i loro tipi e nomi. I nomi dei parametri sono obbligatori, anche se il C li ignora.
- Un modulo può avere più blocchi, uno per ogni libreria, e un blocco può essere vuoto.
- La funzione C si chiama come qualsiasi funzione del modulo.

Il compilatore spiega gli errori:

```cromo error
import unsafe

extern "kernel32.dll" {
    u32 GetLastError() {
        ret 0
    }
}

i32 main() {
    ret 0
}
```

```output
error: write the library without extension, as "kernel32"
error: a C function has no body: it is in the library
```

Senza `import unsafe` non ci sono blocchi `extern`:

```cromo error
extern "kernel32" {
    u32 GetLastError()
}

i32 main() {
    ret 0
}
```

```output
error: an extern block needs 'import unsafe' at the top of the module
```

## Dove si trova la libreria

### Windows: le DLL

Su Windows, `extern "name"` è `name.dll`. `cromo` scrive da sé la libreria di importazione della DLL, a partire dalle funzioni che i blocchi dichiarano: non servono né il Windows SDK né un file `.lib`.

`name.dll` viene cercata nelle cartelle dei moduli che dichiarano la libreria, nell'ordine in cui i moduli vengono caricati, e poi nella cartella di sistema di Windows, `System32`. Si usa la prima trovata. Un progetto con una DLL propria la tiene accanto al modulo che la usa:

```text
main.cro           import maths
maths/
    maths.cro      import unsafe, extern "maths" { ... }
    maths.dll
```

Mentre compila, `cromo` legge l'elenco delle funzioni che la DLL esporta, quindi un nome scritto male è un errore di compilazione, non un crash all'avvio:

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

Il controllo riguarda ogni funzione del blocco, anche quelle che nessuno chiama. Una funzione che la DLL inoltra a un'altra DLL, come `HeapAlloc`, che `kernel32` prende da `ntdll`, conta come esportata. Anche una DLL che non si trova è un errore, e così una DLL che non è per x64:

```cromo error
import unsafe

extern "sqlite3" {
    i32 sqlite3_libversion_number()
}

i32 main() {
    ret sqlite3_libversion_number()
}
```

```output
error: cannot find the library 'sqlite3': there is no sqlite3.lib or sqlite3.dll next to the modules that use it
```

**Una `.lib` accanto al modulo.** Quando in una di quelle cartelle c'è `name.lib`, viene usata quella invece di scrivere una libreria di importazione: può essere una libreria C statica, o la libreria di importazione di una DLL con un altro nome. In quel caso `cromo` non controlla nulla, e una funzione mancante viene segnalata dal linker, con il suo messaggio:

```text
cromo: linking failed:
lld-link: error: undefined symbol: callThrice
```

**La copia delle DLL.** Una DLL trovata accanto a un modulo deve stare accanto all'eseguibile quando il programma parte. Per questo, dopo il link, `cromo` la copia nella cartella dell'eseguibile, anche quando insieme a essa è stata usata una `.lib` con lo stesso nome:

- solo quando l'eseguibile è in un'altra cartella, e lì la copia manca o è diversa, per dimensione o per data: un programma in esecuzione si tiene la sua DLL;
- mai le DLL di `System32`, presenti in ogni Windows;
- `cromo run`, che compila in una cartella temporanea, le copia lì; `--emit-ir` non scrive alcun eseguibile, e non copia nulla;
- una copia che non riesce è un errore di `cromo`.

**Compilare per Windows su Linux.** `cromo` su Linux compila anche per Windows, con `--target windows`, ma lì non ha una `System32` in cui leggere le DLL del sistema. Le loro funzioni vengono prese così come i blocchi le dichiarano, senza il controllo: un nome scritto male compila, e si scopre solo quando il programma parte su Windows, che si rifiuta di eseguirlo perché non trova il punto di ingresso nella DLL. Compilando su Windows, lo stesso errore si trova subito.

### Linux: le librerie statiche

Quando si compila per Linux, `extern "name"` è la libreria statica `libname.a`, cercata nelle cartelle dei moduli che dichiarano la libreria, nell'ordine in cui vengono caricati, come su Windows. Il suo codice entra nell'eseguibile, che non ha bisogno di nulla accanto a sé. Quando non la trova, il compilatore lo dice:

```text
error: cannot find the library 'maths': there is no libmaths.a next to the modules that use it
```

`cromo` non legge i nomi di una libreria statica: una funzione mancante viene segnalata dal linker, che spesso suggerisce quella giusta:

```text
cromo: linking failed:
ld.lld: error: undefined symbol: sumAl
>>> did you mean: sumAll
```

Le librerie condivise, i file `.so`, non sono ancora supportate su Linux.

Un modulo che chiama le DLL di Windows compila solo per Windows. Quando un programma deve girare su entrambi i sistemi, le chiamate vanno nei file di ciascun sistema, come `clock_windows.cro` e `clock_linux.cro` accanto a `clock.cro`, che offrono le stesse funzioni: vedi [Moduli e progetti](modules.md).

Per Android, `libname.a` si cerca allo stesso modo; lì una libreria C può sempre chiamare la libreria C di Android.

### La libreria C su Linux

Un programma Cromo per Linux è di norma un eseguibile statico che non usa il loader dinamico: parla da sé con il kernel, e gira su qualsiasi Linux del suo processore, x64 o ARM64.

Una libreria C, però, chiama spesso le funzioni della libreria C del sistema, come `malloc`, `snprintf` o `dlopen`. Quando lo fanno le librerie statiche dei blocchi `extern`, `cromo` collega da sé l'eseguibile con glibc: il programma diventa allora un eseguibile per il loader dinamico del sistema, che carica glibc all'avvio. Non c'è nulla da scrivere; funziona anche quando si compila per Linux da Windows. Ogni programma con una finestra rientra in questo caso, perché la libreria che disegna le finestre chiama la libreria C.

Un eseguibile del genere:

- ha bisogno di glibc 2.29 o successiva dove gira, come in Ubuntu 20.04 e nelle distribuzioni successive;
- non gira sulle distribuzioni basate su musl, come Alpine;
- per ora si può produrre solo per x64: con `--target linux-arm64`, una libreria C che chiama la libreria C non si può collegare, e il linker ne segnala le funzioni, come `undefined symbol: malloc`.

I programmi le cui librerie C non chiamano la libreria C, e tutti i programmi senza librerie C, restano statici.

## I tipi al confine

Una funzione C prende e restituisce pochi tipi di valori, quelli che il C e Cromo memorizzano allo stesso modo:

| Cromo | C |
|---|---|
| `i8`, `i16`, `i32`, `i64` | `int8_t` ... `int64_t`: `int` è un `i32`, `long long` un `i64` |
| `u8`, `u16`, `u32`, `u64` | `uint8_t` ... `uint64_t`: `size_t` è un `u64` |
| `f32`, `f64` | `float`, `double` |
| `bool` | il `bool` del C, un byte |
| `char` | `char`, un byte |
| `addr` | qualsiasi puntatore: `void*`, `const char*`, un handle |
| `ref T` | `T*`, l'indirizzo di una variabile Cromo |

- Il `BOOL` di Windows non è il `bool` del C: è un `int`, quindi un `i32` in Cromo.
- Il `long` del C è a 32 bit su Windows e a 64 bit su Linux: scrivi il tipo Cromo del sistema.
- `addr` è un indirizzo senza tipo: il codice Cromo legge e scrive la memoria a cui punta solo con le funzioni descritte in [Il dialetto unsafe](unsafe.md). Senza `import unsafe`, il tipo non esiste nemmeno.

Un parametro `ref` dà alla funzione C l'indirizzo di una variabile, di un campo o di un elemento: un numero, un `bool`, un array di dimensione fissa di questi, o una struct fatta di questi, che può contenere altre struct e altri array dello stesso genere. Una struct ha il layout del C, con i campi in ordine e lo stesso allineamento, quindi le struct di un'API C si scrivono campo per campo:

```cromo
import unsafe

extern "kernel32" {
    i32 FileTimeToSystemTime(ref FileTime time, ref SystemTime result)
}

// FILETIME of Windows: the 100 ns since 1601, in two halves.
struct FileTime {
    u32 low
    u32 high
}

// SYSTEMTIME of Windows.
struct SystemTime {
    u16 year
    u16 month
    u16 dayOfWeek
    u16 day
    u16 hour
    u16 minute
    u16 second
    u16 milliseconds
}

i32 main() {
    u64 ticks = 125911584000000000  // 2000-01-01 at midnight
    FileTime time
    time.low = u32(ticks & 0xFFFFFFFF)
    time.high = u32(ticks >> 32)
    SystemTime date
    if (FileTimeToSystemTime(ref time, ref date) != 0) {
        println("{}-{:02}-{:02}, day {} of the week", date.year, date.month, date.day, date.dayOfWeek)
    }
    ret 0
}
```

```output
2000-01-01, day 6 of the week
```

L'indirizzo vale solo per la durata della chiamata, come ogni `ref` di Cromo: la funzione C non deve conservarlo dopo essere tornata. La memoria che il C conserva, come un buffer che una libreria riempie più tardi, si alloca con `alloc` oppure è una variabile di modulo, entrambe descritte in [Il dialetto unsafe](unsafe.md).

Ciò che non può attraversare il confine è un errore di compilazione:

```cromo error
import unsafe

struct Pair {
    i32 a
    i32 b
}

struct Holder {
    string name
}

extern "kernel32" {
    void TakesText(string text)
    void TakesMove(move i32 x)
    void TakesCallback(func(i32) i32 f)
    Pair ReturnsPair()
    void TakesHolder(ref Holder h)
}

i32 main() {
    ret 0
}
```

```output
error: a C function takes numbers, bool, addr and 'ref' to numbers or structs of numbers, not string
error: a C function has no 'move' parameters
error: a C function takes numbers, bool, addr and 'ref' to numbers or structs of numbers, not func(i32) i32
error: a C function returns a number, bool or addr, not Pair
error: 'ref' to a C function is a pointer to a number or to a struct of numbers, not Holder
```

Non ancora supportati:

- stringhe, liste, mappe ed enum: una stringa va al C con `cstring()`, mostrata più avanti, un enum come numero, `i32(color)`;
- struct passate o restituite per valore: passale con `ref`;
- una struct con un campo `addr`, e `ref addr`: per una struct C che contiene puntatori, alloca i suoi byte e riempili con le funzioni descritte in [Il dialetto unsafe](unsafe.md);
- funzioni date al C, come callback: il C può chiamare una funzione Cromo solo per nome, con `[[export]]`, mostrato più avanti;
- funzioni variadiche, come `printf`: i `...` del C non si possono scrivere;
- funzioni C generiche, e parametri `move`.

## Nomi e visibilità

Le funzioni C mantengono il loro nome C, e sono private al loro modulo, che offre invece funzioni sicure: un altro modulo non può chiamarle, nemmeno con il nome del modulo davanti.

```cromo file=clock.cro
import unsafe

extern "kernel32" {
    i32 QueryPerformanceCounter(ref i64 count)
    i32 QueryPerformanceFrequency(ref i64 frequency)
}

// The microseconds of a clock that never goes back.
public i64 micros() {
    i64 count
    i64 frequency
    QueryPerformanceCounter(ref count)
    QueryPerformanceFrequency(ref frequency)
    ret count / frequency * 1000000 + count % frequency * 1000000 / frequency
}
```

```cromo error
import clock

i32 main() {
    i64 count
    QueryPerformanceCounter(ref count)
    ret 0
}
```

```output
error: 'QueryPerformanceCounter' of clock is private to its module: it is not marked public
```

Quindi il blocco non è mai pubblico, e non lo è nessuna funzione al suo interno:

```cromo error
import unsafe

public extern "kernel32" {
    u32 GetLastError()
}

extern "kernel32" {
    public u32 GetTickCount()
}

i32 main() {
    ret 0
}
```

```output
error: an extern block is not public: the module offers safe functions instead
error: the C functions are private to the module: it offers safe functions instead
```

- Più moduli possono dichiarare la stessa funzione C, ciascuno per sé, con la stessa firma. Una firma diversa è un errore: `the C function 'GetLastError' is already declared with another signature, in clock`. Contano anche i moduli della libreria standard che il programma usa: `std.fs` dichiara gli handle di Windows come `i64`, quindi un programma che lo usa e dichiara `CloseHandle(addr handle)` riceve l'errore `... in std.fs`. All'interno di un modulo, una funzione C si dichiara una volta sola.
- Una funzione C e una funzione Cromo dello stesso modulo non possono avere lo stesso nome.
- I nomi che iniziano con `cromo_` sono riservati al runtime di Cromo, le cui funzioni sono dichiarate solo dalla libreria standard, in un blocco `extern "cromo"`. Un programma che usa quel nome di libreria, o un nome con quell'inizio, riceve un errore.
- In un file con `import unsafe`, i nomi delle funzioni integrate del dialetto, come `alloc`, `free` e `offset`, indicano sempre le funzioni integrate, perciò una funzione C o del modulo con uno di quei nomi, come la `free` della libreria C, è un errore dove viene dichiarata: `'free' is a built-in function of the unsafe dialect, which this module calls instead: give this function another name`.

## Stringhe e memoria

Una stringa di Cromo non è una stringa del C: conosce la propria lunghezza e non ha uno zero in fondo. Due funzioni, entrambe disponibili solo nei file con `import unsafe`, attraversano il confine:

- `s.cstring()` dà un `addr` a una copia della stringa con uno zero in fondo, come la vogliono le funzioni C. La copia viene distrutta alla fine dell'istruzione, quindi si scrive nella chiamata stessa; la stringa non cambia. Un byte zero all'interno della stringa, per il C, la termina.
- `string(data, length)` crea una stringa da `length` byte a partire da `data`: una copia, che il C può poi liberare o riutilizzare. I byte devono essere UTF-8, come per `string(bytes)`; quando non lo sono, lancia un `Error` che dice dove smettono di esserlo.

La memoria che il C riempie si alloca con `alloc` e si restituisce con `free`. Una struct con un distruttore la libera su ogni percorso, anche quando un'eccezione fa uscire dalla funzione:

```cromo
import unsafe

extern "kernel32" {
    i32 SetEnvironmentVariableA(addr name, addr value)
    u32 GetEnvironmentVariableA(addr name, addr buffer, u32 size)
}

// Memory for C, freed when the variable ends.
struct Memory {
    addr data

    Memory(i64 size) {
        data = alloc(size)
    }

    ~Memory() {
        free(data)
    }
}

// The value of a variable of the environment, "" when it is not set.
string variable(string name) {
    Memory buffer = Memory(1024)
    u32 length = GetEnvironmentVariableA(name.cstring(), buffer.data, 1024)
    if (length >= 1024) {
        throw Error(format("the value of {} is too long", name))
    }
    ret string(buffer.data, i64(length))
}

i32 main() {
    SetEnvironmentVariableA("GREETING".cstring(), "hello from C".cstring())
    println("[{}]", variable("GREETING"))
    println("[{}]", variable("NOT_SET_ANYWHERE"))
    ret 0
}
```

```output
[hello from C]
[]
```

Entrambe le stringhe date a `SetEnvironmentVariableA` vivono fino alla fine della sua istruzione, ed è tutto ciò che serve al C. La libreria standard legge già l'ambiente con `os.env(name)`; l'esempio mostra come si scrive una funzione del genere. Le funzioni di Windows il cui nome finisce con `A` leggono il testo nella code page del sistema, quindi vanno bene per l'ASCII; per un testo qualsiasi, le funzioni che finiscono con `W` prendono UTF-16.

`cstring()` fuori da un file con `import unsafe` è un errore:

```cromo error
i32 main() {
    string word = "hello"
    word.cstring()
    ret 0
}
```

```output
error: cstring() is for the C functions: the module must write 'import unsafe'
```

Il compilatore non impedisce a un programma di conservare in una variabile l'`addr` di `cstring()`, ma dopo l'istruzione punta a memoria già liberata: un errore che il dialetto lascia al suo autore.

## Incapsulare una risorsa

Una risorsa C, come un handle o un descrittore, va messa in una struct con un distruttore. Il resto del programma usa poi la struct, e la risorsa viene sempre rilasciata, come ogni altra risorsa in Cromo:

```cromo file=logfile.cro
import unsafe

extern "kernel32" {
    addr CreateFileA(addr name, u32 access, u32 share, addr security, u32 creation, u32 flags, addr template)
    i32 WriteFile(addr file, addr data, u32 size, ref u32 written, addr overlapped)
    i32 CloseHandle(addr handle)
}

// A file open for writing, closed when the variable ends.
public struct LogFile {
    addr handle

    public LogFile(string path) {
        // GENERIC_WRITE, no sharing, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL
        handle = CreateFileA(path.cstring(), 0x40000000, 0, addr(0), 2, 0x80, addr(0))
        if (handle == addr(-1)) {
            throw Error(format("cannot create {}", path))
        }
    }

    ~LogFile() {
        CloseHandle(handle)
        println("closed")
    }

    public void write(string text) {
        u32 written
        if (WriteFile(handle, text.cstring(), u32(text.len()), ref written, addr(0)) == 0) {
            throw Error("cannot write")
        }
    }
}
```

Il modulo offre `LogFile`; i moduli che lo importano non scrivono `import unsafe`, e non possono toccare l'handle né le funzioni C:

```cromo
import logfile

void writeLog() {
    LogFile log = LogFile("log.txt")
    log.write("first line\n")
    println("written")
}

i32 main() {
    writeLog()
    println("after writeLog")
    try {
        LogFile bad = LogFile("no/such/folder/log.txt")
    } catch (Error e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
written
closed
after writeLog
cannot create no/such/folder/log.txt
```

- L'handle viene chiuso dove `log` finisce, anche quando un'eccezione fa uscire da `writeLog`.
- Un costruttore che lancia un'eccezione non lascia alcun oggetto, quindi il distruttore non vede mai un handle non valido.
- `LogFile` ha un distruttore e nessun `copy()`, quindi non si può copiare, solo spostare: l'handle viene chiuso una volta sola.

Il compilatore controlla che il resto del programma usi la struct correttamente; solo le poche righe del modulo che parlano con il C dipendono dalla cura del loro autore.

## Il C che chiama Cromo

Il C non può ricevere una funzione Cromo come valore, ma una libreria C statica collegata al programma può chiamare una funzione Cromo per nome, quando la funzione è esportata con `[[export("name")]]`:

```cromo ignore
import unsafe

extern "twice" {
    i64 callTwice(i64 x)  // in twice.lib, which calls twice_from_cromo
}

[[export("twice_from_cromo")]]
i64 twice(i64 x) {
    ret x * 2
}
```

```c
long long twice_from_cromo(long long x);

long long callTwice(long long x) {
    return twice_from_cromo(x) + 1;
}
```

Le regole di `[[export]]` sono in [Il dialetto unsafe](unsafe.md).

## Riepilogo

- `import unsafe` permette i blocchi `extern "name" { ... }` di funzioni C, scritte come funzioni Cromo senza corpo.
- Su Windows `extern "name"` è `name.dll`, accanto ai moduli o in `System32`, le cui funzioni vengono controllate durante la compilazione; una `name.lib` accanto al modulo viene usata al suo posto, senza il controllo. Le DLL accanto ai moduli vengono copiate accanto all'eseguibile.
- Su Linux è `libname.a` accanto ai moduli, controllata dal linker. Quando le librerie C chiamano la libreria C, l'eseguibile viene collegato con glibc, e ha bisogno di glibc 2.29 o successiva; non ancora su ARM64.
- Da Linux, le funzioni delle DLL di Windows non vengono controllate: una funzione mancante ferma il programma all'avvio.
- Attraversano il confine i numeri, `bool`, `addr` e i `ref` a numeri o a struct di numeri; le stringhe vanno con `cstring()` e tornano con `string(data, length)`.
- Le funzioni C mantengono il loro nome e sono private al loro modulo; le risorse C vanno in struct con distruttori, e il resto del programma usa quelle.
