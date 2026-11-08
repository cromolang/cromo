# Primi passi

In questo capitolo installi Cromo, scrivi il tuo primo programma e fai la conoscenza di `cromo`, il comando che compila, esegue, testa e formatta il codice Cromo.

## Installazione

Cromo si installa per il tuo utente, senza diritti di amministratore, con una sola riga. Su Windows, in PowerShell:

```powershell
irm https://cromolang.org/install.ps1 | iex
```

Su Linux:

```sh
curl -sSf https://cromolang.org/install.sh | sh
```

Lo script scarica l'archivio dell'ultima versione, ne verifica lo SHA-256 e lo estrae in `~/.cromo` (`%USERPROFILE%\.cromo` su Windows). Aggiunge `~/.cromo/current/bin` al `PATH` e, se trova Visual Studio Code, installa l'estensione di Cromo. Apri un nuovo terminale, poi verifica l'installazione:

```sh
cromo version
```

```output
cromo 0.49.0 (LLVM 23.1.4)
```

Tutto qui. `cromo` contiene il compilatore e il linker: non ha bisogno di un compilatore C, di Visual Studio né del Windows SDK, e i programmi che produce non dipendono dal runtime C del sistema.

Quando esce una nuova versione, `cromo update` la installa accanto a quella che hai, dopo averne verificato la firma, e la rende attiva; `cromo update 0.27.1` torna a una versione precedente.

> **Nota:** l'archivio si può installare anche a mano: scaricalo dalla [pagina di download](https://cromolang.org/download/), con il suo SHA-256, estrailo dove preferisci e aggiungi la sua cartella `bin` al `PATH`. `cromo` trova la libreria nella cartella `lib` accanto a `bin`.

## Hello, world!

Crea una cartella per i tuoi programmi e, al suo interno, un file chiamato `hello.cro`:

```cromo file=hello.cro
i32 main() {
    println("Hello, world!")
    ret 0
}
```

Poi eseguilo:

```sh
cromo run hello.cro
```

```output
Hello, world!
```

Vediamo il programma da vicino:

- `i32 main()` dichiara la funzione `main`, da cui parte ogni programma. Il tipo viene prima del nome, come in C: `main` restituisce un `i32`, un intero a 32 bit, che diventa il codice di uscita del programma.
- `println` stampa una riga. Il suo primo argomento è una stringa di formato, che può contenere `{}` al posto dei valori che la seguono: `println("{} + {} = {}", a, b, a + b)`.
- `ret` esce da una funzione. Cromo scrive `ret` dove il C scrive `return`.
- Non ci sono punti e virgola: un'istruzione finisce alla fine della sua riga.

È ammesso anche un `main` che non restituisce nulla, e in quel caso il codice di uscita è 0:

```cromo
void main() {
    println("Hello again!")
}
```

```output
Hello again!
```

## Produrre un eseguibile

`cromo run` compila il programma in una cartella temporanea, lo esegue e lo cancella. Per tenere l'eseguibile, usa `cromo build`:

```sh
cromo build hello.cro          # scrive hello.exe su Windows, hello su Linux
./hello
```

Per impostazione predefinita `cromo` produce un eseguibile di **debug**: è ottimizzato, ma controlla l'overflow di ogni operazione sugli interi e contiene le informazioni che servono a un debugger. Con `--release`, in caso di overflow gli interi ricominciano dall'estremo opposto, come in C, e l'eseguibile è più piccolo:

```sh
cromo build --release hello.cro
```

L'eseguibile funziona su qualsiasi macchina con lo stesso sistema, senza installare nulla. `cromo` può anche produrre un eseguibile per l'altro sistema:

```sh
cromo build --target linux hello.cro     # su Windows: un eseguibile per Linux
cromo build --target windows hello.cro   # su Linux: hello.exe per Windows
```

`--target linux-arm64` lo produce per Linux su ARM64, come un Raspberry Pi o un server con Graviton.

## Un progetto

Un progetto è una cartella con un file `main.cro`. Se non indichi un nome di file, `cromo build`, `cromo run` e `cromo test` usano il `main.cro` della cartella corrente, e l'eseguibile prende il nome della cartella:

```sh
mkdir greeter
cd greeter
# scrivi main.cro
cromo run
```

Gli altri file della cartella sono moduli, che `main.cro` importa per nome. Un progetto con più eseguibili li elenca in un file `cromo.mod`, e `cromo build` li costruisce tutti: il capitolo 11 spiega entrambe le cose.

## Formattazione

Il codice Cromo ha un solo stile, e `cromo fmt` lo applica: quattro spazi di rientro, la graffa aperta sulla stessa riga, uno spazio attorno agli operatori. Non ci sono opzioni su cui discutere:

```sh
cromo fmt                 # tutti i file .cro di questa cartella e delle sue sottocartelle
cromo fmt hello.cro       # un solo file
cromo fmt --check         # elenca soltanto i file non formattati
```

## Visual Studio Code

L'estensione di Cromo, che l'installazione aggiunge a Visual Studio Code, colora il codice e usa `cromo lsp`, il language server di Cromo, per mostrare:

- gli errori mentre scrivi, senza compilare a mano;
- il tipo e la dichiarazione di un nome, quando ci passi sopra con il mouse;
- la definizione di un nome, con F12;
- i membri di un valore, dopo un punto;
- la formattazione del file, al salvataggio.

Qualsiasi editor che supporti il Language Server Protocol può usare `cromo lsp` allo stesso modo.

## Riepilogo

- `cromo run file.cro` compila ed esegue un programma; `cromo build` produce l'eseguibile.
- Un programma parte da `main`, che restituisce il suo codice di uscita.
- Il tipo viene prima del nome; `ret` esce dalla funzione; le istruzioni finiscono a fine riga.
- `cromo fmt` formatta il codice, e l'estensione per Visual Studio Code mostra gli errori mentre scrivi.

Nel prossimo capitolo scriverai un programma completo, prima ancora di conoscerne i dettagli.
