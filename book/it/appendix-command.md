# Appendice B: Il comando cromo

`cromo` è l'unico strumento di Cromo: il compilatore, il linker, il test runner, il formattatore e il language server, in un solo eseguibile.

```text
cromo <command> [options] [file.cro | name] [-- arguments]
cromo fmt [--check] [files and folders | -]
```

Il comando viene sempre per primo: `cromo hello.cro` è un errore che mostra la forma giusta, `the command comes first: cromo build hello.cro`. `cromo help`, `cromo -h` e `cromo --help` stampano i comandi; `cromo` da solo li stampa come errore.

## Comandi

| Comando | Cosa fa |
|---|---|
| `cromo build [file.cro \| name]` | compila il programma in un eseguibile |
| `cromo run [file.cro \| name] [-- arguments]` | compila il programma in una cartella temporanea, lo esegue con gli argomenti che seguono `--` e termina con il suo codice di uscita |
| `cromo test [file.cro] [names]` | esegue le funzioni `[[test]]` del programma e dei suoi moduli; i nomi scelgono i test che li contengono ([Test](tests.md)) |
| `cromo fmt [--check] [files and folders \| -]` | formatta il codice, di default quello della cartella corrente e delle sue sottocartelle ([Strumenti: formattazione, editor e debugger](tools.md)) |
| `cromo lsp` | il language server, per gli editor, su standard input e standard output ([Strumenti: formattazione, editor e debugger](tools.md)) |
| `cromo design [file.form]` | il designer delle finestre: una finestra composta con il mouse, salvata come form e come modulo di Cromo ([Finestre e app](gui.md#disegnare-una-finestra-cromo-design)) |
| `cromo update [version]` | installa l'ultima versione di Cromo, o quella indicata, e la rende attiva |
| `cromo version` | stampa la versione di `cromo` e di LLVM, come `cromo 0.83.0 (LLVM 23.1.4)` |
| `cromo help` | stampa i comandi e le opzioni |

### Il programma da compilare

`cromo build`, `cromo run` e `cromo test` trovano il programma in questo modo:

- **Un file**, come `cromo build hello.cro`: quel file è il file principale. L'eseguibile prende il suo nome, `hello.exe` su Windows e `hello` su Linux, e sta accanto a esso.
- **Un nome** senza estensione che non è un file, come `cromo build server`: l'eseguibile con quel nome nel `cromo.mod` della cartella corrente. `cromo test` non prende un nome: le parole che lo seguono e che non terminano con `.cro` scelgono i test.
- **Nulla**, in una cartella il cui `cromo.mod` ha righe `bin`: `cromo build` costruisce ogni eseguibile, `cromo run` esegue l'unico, ed è un errore se ce ne sono di più, `cromo test` testa ciascuno ([Moduli e progetti](modules.md)).
- **Nulla**, altrove: il `main.cro` della cartella corrente, e l'eseguibile prende il nome della cartella. Senza `main.cro`: `there is no main.cro in this folder: write the file to compile, as cromo build hello.cro`.

## Opzioni di build, run e test

| Opzione | Significato | Comandi |
|---|---|---|
| `-o <file>` | il percorso dell'eseguibile, esattamente come è scritto: non viene aggiunta nessuna estensione, e la sua cartella deve esistere | `build` |
| `--target <system>` | il sistema dell'eseguibile, più sotto; di default quello su cui gira `cromo` | `build`; `run` solo con `android` |
| `--cpu <processor>` | il processore x86-64 di cui il programma usa le istruzioni, più sotto | `build`, `run`, `test` |
| `--release` | in caso di overflow gli interi ricominciano dall'estremo opposto, invece di fermare il programma, e non ci sono informazioni di debug | `build`, `run`, `test` |
| `-O0`, `-O1`, `-O2`, `-O3` | il livello di ottimizzazione; il default è `-O2` | `build`, `run`, `test` |
| `--emit-ir` | stampa l'LLVM IR del programma invece di scrivere l'eseguibile | `build` |

`cromo run` crea un programma per il sistema su cui gira `cromo`, e lo cancella quando termina, quindi `-o`, `--target` e `--emit-ir` sono opzioni solo di `cromo build`: `cromo run -o x hello.cro` è l'errore `-o is an option of cromo build`, e `cromo run --target linux` è `cromo run takes only --target android, which runs the app on the phone`. `cromo test` esegue i test sul sistema su cui gira `cromo`, con la stessa regola. Gli argomenti dopo `--` sono solo per `cromo run`.

### Target

| `--target` | L'eseguibile |
|---|---|
| `windows` | Windows x64: `hello.exe`, che ha bisogno solo delle DLL di Windows, e di quelle delle librerie C che il programma dichiara |
| `linux` | Linux x64: `hello`, statico, che gira su qualsiasi Linux x64; un programma le cui librerie C chiamano la libreria C, come ogni programma con `std.gui`, viene collegato con glibc e richiede glibc 2.29 o successiva |
| `linux-arm64` | Linux su ARM64, come un Raspberry Pi o un server con Graviton: statico, per ogni ARMv8; `std.gui` lì non è ancora disponibile |
| `android` | l'APK di un'app, `hello.apk`, con il programma per i telefoni, ARM64, e per gli emulatori, x86-64; Android 8 o successivo ([Finestre e app](gui.md)) |
| `android-arm64`, `android-x86_64` | un programma per una sola architettura di Android, `hello`, per il loader dinamico di Android: si esegue da una shell, come `adb shell`, non come app |

Senza `--target`, il programma è per il sistema su cui gira `cromo`. `cromo` compila per tutti, su Windows e su Linux, senza nient'altro da installare.

### --cpu

Di default un programma per x86-64 usa solo le istruzioni che ogni x86-64 ha, SSE2 compreso. `--cpu` gli permette di usarne di più, ovunque nel programma, come fanno i vettori e le funzioni sui bit:

| `--cpu` | Il processore |
|---|---|
| `x86-64` | ogni x86-64: il default |
| `x86-64-v2` | con SSE4.2 e POPCNT |
| `x86-64-v3` | con AVX2, BMI2 e FMA |
| `x86-64-v4` | con AVX-512 |
| `native` | il processore del computer che compila, con ciò che ha e ciò che gli manca |
| un nome, come `haswell` o `znver4` | un processore che LLVM conosce |

Un programma compilato con `--cpu` controlla all'avvio che il processore abbia le istruzioni che usa. Se non le ha, il programma scrive sullo standard error quali mancano, e termina con il codice di uscita 1 prima di eseguire qualsiasi parte del suo codice:

```text
this program was compiled with --cpu x86-64-v4, and this processor lacks AVX512F, AVX512BW, AVX512CD, AVX512DQ, AVX512VL
```

Un nome sconosciuto è un errore prima della compilazione, che elenca quelli accettati. `--cpu` indica un processore x86-64: con `--target linux-arm64` o `android-arm64` è l'errore `--cpu names a processor of x86-64, and linux-arm64 is ARM64`, perché quei programmi girano su ogni ARMv8. Con `--target android` vale per la parte dell'APK per x86-64. Il codice C del runtime e delle librerie resta compilato per ogni x86-64.

## cromo build

`cromo build` scrive l'eseguibile e, su Windows, senza `--release`, accanto a esso le sue informazioni di debug: `hello.pdb`. Se un modulo usa una DLL che gli sta accanto, e l'eseguibile è in un'altra cartella, la DLL viene copiata accanto all'eseguibile ([Chiamare il C](c-interop.md)). In un progetto, `cromo build` senza un file costruisce ogni eseguibile di `cromo.mod`, con le stesse opzioni, e si ferma al primo che fallisce; `-o` è allora un errore.

## cromo run

`cromo run hello.cro -- a b` compila il programma nella cartella `cromo-run-<number of the process>` della cartella temporanea del sistema, lo esegue con gli argomenti `a` e `b`, cancella la cartella e termina con il codice di uscita del programma. L'eseguibile ha il nome del sorgente, e le DLL dei suoi moduli vengono copiate accanto a esso. Su Windows il codice di uscita è l'intero valore restituito dal programma; su Linux, un programma terminato da un segnale dà 128 più il numero del segnale, come dicono le shell: 137 per `SIGKILL`, 139 per `SIGSEGV`.

### cromo run --target android

`cromo run --target android app.cro` crea l'APK, lo installa sul telefono o sull'emulatore collegato, avvia l'app e ne mostra l'output finché non termina:

- `adb` è quello in `platform-tools` di `ANDROID_HOME` o di `ANDROID_SDK_ROOT`, altrimenti quello nel `PATH`; se manca, `cromo` lo dice.
- Con un solo dispositivo collegato, viene usato quello; con più di uno, `cromo` li elenca e si ferma, e la variabile `ANDROID_SERIAL`, che `adb` conosce, ne sceglie uno.
- Ciò che l'app scrive sullo standard output e sullo standard error va nel log di Android, e `cromo run` lo mostra sul proprio standard output e standard error.
- `cromo run` termina con il codice di uscita dell'app; un'app che va in crash lo fa terminare con 1. Ctrl+C nel terminale ferma anche l'app, e `cromo run` termina con 130.
- Un'app non ha argomenti: `--` è un errore.

## cromo test

`cromo test` costruisce il programma con le funzioni `[[test]]` e i file `_test.cro` dei suoi moduli, esegue ogni test in un processo a sé, scrive `ok` o `FAIL` per ciascuno e termina con 1 se uno è fallito: lo spiega [Test](tests.md).

## cromo fmt

`cromo fmt` formatta i file sul posto e scrive i nomi di quelli che ha cambiato; `--check` non cambia nulla, li elenca e termina con 1 se ce ne sono; `-` formatta lo standard input sullo standard output. Un file con errori di sintassi resta com'è. [Strumenti: formattazione, editor e debugger](tools.md) elenca le regole.

## cromo update

`cromo update` installa una nuova versione di Cromo accanto a quella in uso, e la rende attiva:

- `cromo update` prende l'ultima versione. Se è quella attiva, lo dice e non cambia nulla. Un'ultima versione più vecchia di quella attiva viene rifiutata, perché un sito attaccato potrebbe dare un vecchio manifest, firmato quando era l'ultima: tornare indietro si può solo indicando una versione.
- `cromo update 0.27.1` prende quella versione, più nuova o più vecchia. Una versione già presente in `~/.cromo/versions` diventa soltanto quella attiva, senza la rete.
- Una nuova versione viene scaricata da cromolang.org: prima il suo manifest, di cui `cromo` controlla la firma Ed25519 con la chiave che contiene, poi il suo archivio, il cui SHA-256 deve essere quello del manifest. Una versione che non supera un controllo viene rifiutata. L'archivio viene estratto in `~/.cromo/versions/<version>`, che compare solo quando è completo.
- Il link `~/.cromo/current` punta poi alla nuova versione, che il `PATH` raggiunge già; le vecchie versioni restano in `~/.cromo/versions`.
- Quando c'è il comando `code` di Visual Studio Code, viene installata anche l'estensione della nuova versione.
- La variabile `CROMO_SITE` indica un altro sito, come per gli script di installazione.

`cromo update` termina con 0, oppure con 1 e il motivo, come `cromo update: there is no version 0.99.0 on https://cromolang.org`.

## Debug e release

| | Debug (default) | `--release` |
|---|---|---|
| Ottimizzazione | `-O2` | `-O2` |
| Overflow degli interi | panic | ricominciano dall'estremo opposto |
| Limiti degli indici, divisione per zero, esclusività | controllati | controllati |
| `assert` | controllato | controllato |
| Tracce dello stack | sì | sì |
| Informazioni per il debugger | sì: PDB su Windows, DWARF su Linux | no |

Con `-O0` un debugger segue il programma riga per riga; con le ottimizzazioni, alcune variabili risultano *optimized out* e le funzioni piccole spariscono dentro i loro chiamanti ([Strumenti: formattazione, editor e debugger](tools.md)).

## Codici di uscita

Di un programma:

| Codice | Significato |
|---|---|
| 0 | `main` ha restituito 0, o nulla |
| 1 | un'eccezione è uscita da `main` o da un thread; oppure il processore non ha le istruzioni di `--cpu` |
| 101 | un panic, o uno stack overflow |
| altro | il valore restituito da `main`, o passato a `os.exit` |

Su Linux il codice di uscita è il byte più basso del valore, come in C: `ret -1` dà 255, e `ret 300` dà 44. Su Windows è l'intero valore. `cromo run` su Linux dà 128 più il segnale per un programma terminato da un segnale.

Di `cromo`:

| Codice | Significato |
|---|---|
| 0 | fatto; per `cromo run`, il codice di uscita del programma |
| 1 | il programma ha errori; un test è fallito, o nessun test ha i nomi indicati; `cromo fmt --check` ha trovato file da formattare, o `cromo fmt` ha trovato errori di sintassi |
| 2 | la riga di comando è sbagliata: un comando o un'opzione sconosciuti, un file o un `main.cro` che non c'è, un errore in `cromo.mod` |

## File e variabili

| Percorso | Contenuto |
|---|---|
| `~/.cromo/current/bin/cromo` | la versione attiva, raggiunta dal `PATH` |
| `~/.cromo/versions/<version>` | le versioni installate |
| `lib/` accanto a `bin/` | la libreria standard e il runtime, che `cromo` legge |
| `extension/` accanto a `bin/` | l'estensione per Visual Studio Code |
| `~/.cromo/android` | la chiave con cui `cromo` firma gli APK per il debug, creata la prima volta |

| Variabile | Uso |
|---|---|
| `ANDROID_HOME`, `ANDROID_SDK_ROOT` | dove `cromo run --target android` cerca `adb` |
| `ANDROID_SERIAL` | il dispositivo di `cromo run --target android`, quando ce n'è più d'uno |
| `CROMO_SITE` | un altro sito per `cromo update` e per l'installazione |

Su Windows, `~` è `%USERPROFILE%`.

## Riepilogo

- `cromo build`, `run` e `test` prendono un file, un nome di `cromo.mod`, o nulla per gli eseguibili di `cromo.mod` o per il `main.cro` della cartella.
- `-o`, `--target` e `--emit-ir` sono opzioni di `cromo build`; `cromo run` costruisce solo per il sistema su cui gira, o un'app con `--target android`.
- `--target` è `windows`, `linux`, `linux-arm64`, `android`, `android-arm64` o `android-x86_64`; `--cpu` sceglie il processore x86-64, controllato all'avvio del programma.
- Un programma termina con 0, con 1 per un'eccezione, con 101 per un panic, o con il proprio codice; `cromo` termina con 1 per gli errori del programma e con 2 per quelli della riga di comando.
- `cromo update` installa l'ultima versione o una versione indicata, controllata con la sua firma, e la rende attiva.
- `cromo design` compone una finestra con il mouse, e la scrive come modulo di Cromo.
