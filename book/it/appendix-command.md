# Appendice B: Il comando cromo

`cromo` è l'unico strumento di Cromo: il compilatore, il linker, il test runner, il formattatore e il language server, in un solo eseguibile.

```text
cromo <command> [options] [file.cro] [-- arguments]
```

Se non indichi un file, i comandi usano il `main.cro` della cartella corrente.

## Comandi

| Comando | Cosa fa |
|---|---|
| `cromo build [file.cro]` | compila il programma in un eseguibile |
| `cromo run [file.cro] [-- arguments]` | compila il programma in una cartella temporanea, lo esegue con gli argomenti che seguono `--` e termina con il suo codice di uscita |
| `cromo run --target android [file.cro]` | crea l'APK, lo installa sul telefono o sull'emulatore che `adb` vede, avvia l'app e ne mostra l'output finché non termina |
| `cromo test [file.cro] [names]` | esegue le funzioni `[[test]]` del programma e dei suoi moduli; i nomi selezionano i test che li contengono |
| `cromo fmt [--check] [files and folders \| -]` | formatta il codice, di default quello della cartella corrente e delle sue sottocartelle; `--check` si limita a elencare i file da formattare; `-` formatta lo standard input |
| `cromo lsp` | il language server, per gli editor, su standard input e standard output |
| `cromo update [version]` | installa l'ultima versione di Cromo, o quella indicata, anche più vecchia, e la rende attiva; le altre restano in `~/.cromo/versions` |
| `cromo version` | stampa la versione di `cromo` e di LLVM |
| `cromo help` | elenca i comandi |

## Opzioni di build e run

| Opzione | Significato |
|---|---|
| `-o <file>` | il nome dell'eseguibile; di default il nome del sorgente, o della cartella se non c'è un file, con `.exe` per Windows |
| `--release` | in caso di overflow gli interi ricominciano dall'estremo opposto, invece di fermare il programma |
| `--target windows`, `--target linux`, `--target linux-arm64` | il sistema dell'eseguibile, anche Linux su ARM64; di default quello su cui gira `cromo` |
| `--target android` | l'APK di un'app per Android, per telefoni ed emulatori; `cromo run` non accetta altri target |
| `-O0` … `-O3` | il livello di ottimizzazione; il default è `-O2` |
| `--emit-ir` | stampa l'LLVM IR del programma invece di scrivere l'eseguibile |

## Debug e release

| | Debug (default) | `--release` |
|---|---|---|
| Ottimizzazione | `-O2` | `-O2` |
| Overflow degli interi | panic | ricominciano dall'estremo opposto |
| Limiti degli indici, divisione per zero, esclusività | controllati | controllati |
| Tracce dello stack | sì | sì |
| Informazioni per il debugger | sì: PDB su Windows, DWARF su Linux | no |

Con `-O0` un debugger mostra ogni variabile; con le ottimizzazioni, alcune possono risultare *optimized out*.

## Codici di uscita

| Codice | Significato |
|---|---|
| 0 | il programma è terminato normalmente, o `main` ha restituito 0 |
| 1 | un'eccezione è uscita da `main`, oppure `cromo` ha trovato degli errori |
| 101 | un panic, o uno stack overflow |
| altro | il valore restituito da `main`, o passato a `os.exit` |

## File

| Percorso | Contenuto |
|---|---|
| `~/.cromo/current/bin/cromo` | la versione attiva, raggiunta dal `PATH` |
| `~/.cromo/versions/<version>` | le versioni installate |
| `lib/` accanto a `bin/` | la libreria standard e il runtime, che `cromo` legge |
| `~/.cromo/android` | la chiave con cui `cromo` firma gli APK per il debug, creata la prima volta |

Per `cromo run --target android`, `adb` è quello di `ANDROID_HOME` o del `PATH`, e `ANDROID_SERIAL` sceglie il dispositivo quando ne sono collegati più d'uno.

Su Windows, `~` è `%USERPROFILE%`.
