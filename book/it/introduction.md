# La guida di Cromo

Ti diamo il benvenuto nella *Guida di Cromo*, un'introduzione al linguaggio di programmazione Cromo.

Cromo è un linguaggio compilato e di uso generale. Ha la sintassi del C e del C++, niente puntatori e niente garbage collector. Ogni valore ha uno e un solo proprietario, e le risorse che un valore detiene, come la memoria, i file e le connessioni, vengono rilasciate dal suo distruttore quando il proprietario esce dal proprio ambito. Il compilatore controlla il possesso con poche regole, e LLVM trasforma il programma in codice nativo veloce.

## Perché Cromo

Cromo mette insieme idee che hanno già dato buona prova di sé in altri linguaggi:

- la **sintassi**, i costruttori e i **distruttori** del C e del C++, così che un programmatore C o C++ possa leggere codice Cromo fin dal primo giorno;
- il **possesso controllato dal compilatore**, come in Rust, ma con regole più semplici: i riferimenti esistono solo come parametri, quindi non serve mai scrivere un lifetime;
- **task leggeri e canali**, come in Go: migliaia di task aspettano la rete nello stesso momento, con normale codice bloccante e senza `async` né `await`;
- **un solo strumento** che compila, esegue, testa e formatta, come in Go e Zig;
- l'ottimizzatore e il linker di **LLVM**, gli stessi che usano anche Clang, Rust e Swift.

Il criterio che guida il progetto è la semplicità: quando due regole offrono la stessa sicurezza, Cromo sceglie quella più facile da spiegare a un programmatore C o C++.

## A chi è rivolta questa guida

Questa guida presuppone che tu abbia già scritto programmi in almeno un altro linguaggio. Non presuppone il C o il C++: quando Cromo li segue, la guida lo dice, e spiega tutto ciò che un programmatore C++ darebbe per scontato.

Se conosci già il C++, Go o Rust, l'[Appendice C](appendix-coming-from.md) riassume le differenze, e puoi scorrere rapidamente i primi capitoli.

## Come usare questa guida

I capitoli sono pensati per essere letti in ordine: ognuno si basa sui precedenti. Il capitolo 2 è un programma piccolo ma completo, scritto prima di spiegarne i dettagli; i capitoli dal 3 al 12 presentano il linguaggio, quelli dal 13 al 16 la libreria standard; il capitolo 17 mostra come chiamare il C, e il capitolo 18 mette tutto insieme in un servizio web.

Ogni programma di questa guida viene compilato ed eseguito quando si genera la guida, e il suo output è quello riportato subito sotto:

```cromo
i32 main() {
    println("Hello from the guide!")
    ret 0
}
```

```output
Hello from the guide!
```

Un blocco con il bordo rosso è un programma che **non** compila, mostrato di proposito, seguito dall'errore del compilatore:

```cromo error
i32 main() {
    string name = "Cromo"
    string other = name
    ret 0
}
```

```output
error: values of type string are not copied implicitly
```

La guida è disponibile anche in PDF, in inglese e in italiano: usa il pulsante **PDF** in cima alla pagina.

## Lo stato di Cromo

Cromo è giovane. La versione attuale è la 0.27, e il linguaggio può ancora cambiare prima della versione 1.0. Ogni decisione di progetto è registrata, con le sue motivazioni, nel file `docs/decisions.md` del [repository](https://github.com/cromolang/cromo), accanto alla bozza di specifica `docs/language.md`.

Cromo funziona su Windows x64 e Linux x64, e ogni versione compila programmi per entrambi i sistemi.

## Il nome

*Cromo* è il nome italiano dell'elemento 24 della tavola periodica (in inglese *chromium*), il cui simbolo, Cr, compare nel logo. Sul web il linguaggio si chiama *cromolang*, come in [cromolang.org](https://cromolang.org).
