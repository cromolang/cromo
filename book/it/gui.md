# Finestre e app

`std.gui` crea finestre con i loro componenti: etichette, pulsanti, caselle di testo, liste, tabelle, dialoghi. Li disegna da sé, dentro il programma, perciò una finestra appare uguale su Windows e su Linux: piatta e sobria, con un accento ocra, chiara o scura, compatta come un'applicazione desktop. Lo stesso programma, compilato con `--target android`, è un'app per Android.

Questo capitolo spiega come funziona il modulo, poi ogni famiglia di componenti con i suoi metodi principali e un breve esempio. [std.gui](std.gui.html), nel riferimento della libreria, elenca ogni metodo.

> Il funzionamento di `std.gui` è deciso: componenti come handle, handler sul thread delle finestre, posizioni in pixel con le ancore. I nomi dei componenti e dei loro metodi sono ancora una bozza, e possono cambiare prima della versione 1.0.

## Una prima finestra

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Greetings", 420, 230)
    w.add(gui.Label("Your name", 24, 24, 200, 20))
    gui.TextBox name = w.add(gui.TextBox("", 24, 48, 372, 32))
    name.setPlaceholder("Write your name")
    gui.Button greet = w.add(gui.Button("Greet", 24, 96, 100, 32))
    greet.setStyle(gui.ButtonStyle.Primary)
    gui.Label answer = w.add(gui.Label("", 24, 148, 372, 20))
    greet.onClick([name, answer]() {
        answer.setText(format("Hello, {}!", name.text()))
    })
    w.start()
    ret 0
}
```

![La finestra, dopo un clic su Greet](images/first-window.png)

- `gui.Window(title, width, height)` crea una finestra, e `w.start()` la mostra. Quando `main` ritorna, il programma aspetta che le sue finestre siano chiuse.
- `w.add(...)` mette un componente nella finestra e lo restituisce. Un componente è un *handle*: un piccolo valore che lo indica, e che si può copiare liberamente. La lambda di `onClick` cattura `name` e `answer` per copia, e legge e cambia i componenti sullo schermo.
- Posizioni e dimensioni sono in pixel logici, dall'angolo in alto a sinistra del contenuto, sotto la barra del titolo: `std.gui` le scala con la densità dello schermo.
- `setStyle(gui.ButtonStyle.Primary)` riempie con l'accento il pulsante dell'azione principale.
- `[[window]]` produce su Windows un programma senza console, come sono i programmi con finestre; senza, la console resta, comoda per `println` mentre si scrive il programma. [Programmi con finestre](#programmi-con-finestre) lo spiega, insieme all'icona dell'eseguibile.

## Come funziona std.gui

Cinque regole spiegano l'intero modulo.

- **Finestre e componenti sono handle.** Un `gui.Window`, un `gui.Button` o un `gui.Table` è un piccolo valore che indica una finestra o un componente tenuto dalla libreria. Le sue copie indicano lo stesso oggetto, e una lambda lo cattura per copia. Una finestra non ha distruttore: resta aperta quando la sua variabile finisce, finché l'utente o il programma non la chiude.
- **Un solo thread disegna tutte le finestre.** Il primo `start()` avvia il *thread delle finestre*: disegna le finestre del programma quando qualcosa cambia in esse, al massimo a 60 fotogrammi al secondo di default, ed esegue gli handler degli eventi, uno alla volta, nell'ordine degli eventi. Un handler può cambiare qualunque componente di qualunque finestra.
- **Un handler deve ritornare presto.** Mentre un handler è in esecuzione, nessuna finestra viene disegnata e nessun altro handler viene eseguito: un handler che impiega molto blocca tutte le finestre. Il lavoro lungo va a un altro thread, che cambia i componenti quando ha il risultato.
- **I metodi si possono chiamare da qualunque thread.** I metodi delle finestre e dei componenti prendono un lock, perciò `main`, un thread o un task possono cambiare un'etichetta o una barra di avanzamento mentre l'utente lavora: il cambiamento si vede al fotogramma successivo. Quando è il programma a cambiare un componente, come con `setText` o `setChecked`, gli handler non vengono chiamati: sono per ciò che fa l'utente.
- **La fine di `main` aspetta le finestre.** Quando `main` ritorna, il programma aspetta che tutte le sue finestre siano chiuse, e poi termina con il codice di uscita di `main`. `os.exit` lo termina subito.

I metodi funzionano anche prima che la finestra sia mostrata. Questo programma non avvia mai la sua finestra, perciò non mostra nulla: stampa soltanto ciò che contengono i suoi componenti.

```cromo
import std.gui

i32 main() {
    gui.Window w = gui.Window("Settings", 400, 300)
    gui.CheckBox news = w.add(gui.CheckBox("Send me the news", 16, 16, 260, 22))
    news.onChange([](bool checked) { println("the user changed it: {}", checked) })
    gui.CheckBox same = news  // a copy of the handle: the same check box
    same.setChecked(true)     // the program changes it: no handler is called
    println("checked: {}", news.checked())
    ret 0
}
```

```output
checked: true
```

Qui `main` fa il lavoro dopo aver mostrato la finestra, e cambia i componenti dal proprio thread. La finestra resta reattiva: l'utente può spostarla, ridimensionarla o chiuderla mentre il lavoro va avanti.

```cromo norun
import std.gui
import std.time

[[window]]
i32 main() {
    gui.Window w = gui.Window("Copy", 420, 140)
    gui.Label status = w.add(gui.Label("Copying the files", 16, 16, 388, 20))
    gui.ProgressBar bar = w.add(gui.ProgressBar(16, 48, 388, 8))
    bar.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Top)
    w.start()
    for (i64 file = 1; file <= 40 && !w.closed(); file++) {
        time.msleep(100)  // the work on one file
        bar.setValue(f64(file) / 40)
        status.setText(format("Copied {} files of 40", file))
    }
    status.setText("Done")
    ret 0  // the program waits until the window is closed
}
```

Quando il lavoro parte da un handler, come dopo un clic, l'handler avvia un thread, lo stacca e ritorna subito ([Thread, task e canali](concurrency.md)). Il thread dà il risultato ai componenti, i cui metodi funzionano da qualunque thread:

```cromo norun
import std.gui
import std.thread

// How many primes there are below the limit: a long work.
i64 primesBelow(i64 limit) {
    i64 count = 0
    for (i64 n = 2; n < limit; n++) {
        bool prime = true
        for (i64 d = 2; d * d <= n && prime; d++) {
            prime = n % d != 0
        }
        count += prime ? 1 : 0
    }
    ret count
}

[[window]]
i32 main() {
    gui.Window w = gui.Window("Primes", 420, 150)
    gui.Button count = w.add(gui.Button("Count", 16, 16, 100, 28))
    gui.Label result = w.add(gui.Label("", 16, 60, 388, 20))
    count.onClick([count, result]() {
        count.setEnabled(false)
        result.setText("Counting...")
        thread.Thread work = thread.Thread([count, result]() {
            i64 limit = 5000000
            result.setText(format("{} primes below {}", primesBelow(limit), limit))
            count.setEnabled(true)
        })
        work.start()
        work.detach()
    })
    w.start()
    ret 0
}
```

- L'handler di `Count` si limita ad avviare il thread e ritorna: la finestra continua a essere disegnata mentre il thread conta.
- Il thread cattura solo gli handle dei componenti, che sono valori semplici: possiede tutto ciò che usa, perciò si può staccare, e niente lo attende. Un thread non staccato verrebbe atteso dal suo distruttore alla fine dell'handler, e la finestra si bloccherebbe.
- La fine di `main` attende le finestre, non i thread staccati: una finestra chiusa mentre il thread conta termina il programma.
- Per un lavoro che attende, come una richiesta alla rete, si stacca allo stesso modo un `thread.Task`.

Un'eccezione che esce da un handler ferma il programma, come una che esce da un thread che nessuno attende: stampa `uncaught exception in a thread:` con il messaggio e lo stack trace, e il programma termina con codice di uscita 1. Un handler che può fallire cattura le sue eccezioni, e mostra l'errore nella finestra, per esempio con una snackbar.

## La finestra

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Sketch", 640, 420)
    w.maximize()  // before start: the window starts maximized
    gui.Label where = w.add(gui.Label("", 16, 16, 400, 20))
    w.onMouseMove([where](gui.Mouse m) { where.setText(format("{}, {}", i32(m.x), i32(m.y))) })
    w.onMouseDown([where](gui.Mouse m) {
        if (m.button == 3) {
            where.setText("the right button")
        }
    })
    w.onKeyDown([w](gui.Key k) {
        if (k.code == 27) {  // Esc
            w.close()
        } else if (k.code == 109 && k.control) {  // Ctrl+M
            if (w.maximized()) {
                w.restore()
            } else {
                w.maximize()
            }
        }
    })
    w.onResize([w](i32 width, i32 height) { w.setTitle(format("Sketch: {} x {}", width, height)) })
    w.onClose([]() { println("closed") })
    w.start()
    ret 0
}
```

### Dimensione e stato

- `gui.Window(title, width, height)` prende la dimensione dell'intera finestra in pixel logici, compresa la sua barra del titolo di 35 pixel: il contenuto, dove vanno i componenti, è largo `width` e alto `height - 35`.
- `setTitle(text)` e `title()`: il titolo è mostrato al centro della barra del titolo e dal sistema, come nella barra delle applicazioni.
- `setSize(width, height)`, `width()` e `height()`: la dimensione dell'intera finestra, che segue l'utente quando la ridimensiona. Prima di `start`, `setSize` dà la dimensione con cui la finestra parte.
- `setResizable(false)` impedisce all'utente di ridimensionare la finestra dai suoi bordi e di massimizzarla; `resizable()` lo legge, vero di default.
- `minimize()`, `maximize()` e `restore()` cambiano lo stato della finestra, e `minimized()` e `maximized()` lo leggono. Chiamati prima di `start`, scelgono come parte la finestra.
- `setFps(n)` imposta il numero massimo di fotogrammi disegnati ogni secondo, da 1 a 240, e `fps()` lo legge: 60 di default. Una finestra viene disegnata solo quando qualcosa cambia in essa: un evento, una modifica del programma, o ciò che si muove da solo, come il cursore che lampeggia; una finestra ferma non usa il processore. Un valore fuori dall'intervallo è un panic.

### Eventi del mouse e dei tasti

- `onMouseMove`, `onMouseDown` e `onMouseUp` ricevono un `gui.Mouse`: `x` e `y` in pixel logici dall'angolo in alto a sinistra del contenuto, `button`, 1 il sinistro, 2 il centrale, 3 il destro e 0 mentre il mouse si muove, e `shift`, `control` e `alt`, i tasti premuti. Arrivano qui anche le pressioni e i rilasci sui componenti, dopo i componenti. Su un telefono un dito è il tasto sinistro.
- `onKeyDown` riceve un `gui.Key` per ogni tasto che il componente con la tastiera non usa, come F5 in una casella di testo; un tasto tenuto premuto arriva di nuovo a ogni ripetizione. Il suo `code` è il carattere del tasto senza Maiusc per le lettere, le cifre e i segni, 97 per A, anche con Maiusc, e 49 per 1; poi 13 è Invio, 27 Esc, 32 Spazio, 8 Backspace, 127 Canc, e le frecce sono `0x40000050` sinistra, `0x4000004F` destra, `0x40000052` su e `0x40000051` giù. Ha anche `shift`, `control` e `alt`. Tab, che sposta la tastiera al componente successivo, e i tasti premuti mentre è mostrato un dialogo non arrivano qui.
- `onResize` riceve la nuova larghezza e la nuova altezza dell'intera finestra quando la sua dimensione cambia. Le ancore spostano i componenti da sé: l'handler serve per ciò che esse non possono fare.
- `onClose` viene chiamato dopo che la finestra è stata chiusa: dal suo pulsante, dai tasti del sistema, dal tasto indietro di un telefono, o da `close()`.

Ogni metodo `on...`, qui e nei componenti, aggiunge un handler: una finestra o un componente può averne diversi, chiamati nell'ordine in cui sono stati aggiunti.

### La barra del titolo

Una finestra non ha una barra del titolo del sistema: `std.gui` disegna la propria, con il titolo al centro e a destra i pulsanti per ridurre a icona, per massimizzare o ripristinare, e per chiudere, nei colori del tema. Si comporta come la barra del sistema: trascinarla sposta la finestra, un doppio clic la massimizza, i bordi della finestra la ridimensionano, e su Windows la finestra si aggancia ai bordi dello schermo.

`w.titleBar()` dà un pannello nella barra, dove il programma mette i propri componenti, come un pulsante di menu o le viste di un documento:

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Agenda", 560, 200)
    w.setTheme(gui.Theme.Dark)
    gui.Panel bar = w.titleBar()
    gui.Button menu = bar.add(gui.Button("", 6, 4, 28, 27))
    menu.setIcon(gui.Icon.Menu)
    menu.setTooltip("Menu")
    gui.Segmented view = bar.add(gui.Segmented(["Day", "Week", "Month"], 42, 4, 180, 27))
    view.setSelected(1)
    gui.ListView events = w.add(gui.ListView(0, 0, 560, 165))
    events.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    events.addItem(gui.Icon.Calendar, "Review of the plan", "Room 2", "Mon 9:00")
    events.addItem(gui.Icon.Users, "Meeting with the team", "", "Wed 14:30")
    events.addItem(gui.Icon.Plane, "Flight to Lisbon", "Terminal 1", "Fri 7:10")
    view.onChange([view](i64 index) { println("the view of the {}", view.text().lower()) })
    w.start()
    ret 0
}
```

![Componenti nella barra del titolo, nel tema scuro](images/title-bar.png)

- Il pannello occupa la barra dal suo bordo sinistro fino ai pulsanti della finestra, ed è alto quanto la barra, 35 pixel. I suoi componenti sono disegnati sopra la barra; dove non ce n'è nessuno, la barra sposta ancora la finestra, e il titolo resta al suo centro, sotto di essi.
- Ogni chiamata dà lo stesso pannello, creato la prima volta. Su un telefono, dove una finestra non ha barra del titolo, il pannello non è mostrato.

### Temi

`w.setTheme(theme)` imposta i colori della finestra e dei suoi componenti, tutti insieme: `gui.Theme.Light`, testo scuro su grigi chiari; `gui.Theme.Dark`, testo chiaro su grigi scuri; e `gui.Theme.System`, il predefinito, che segue il tema di Windows, del desktop di Linux o del telefono, e cambia con esso mentre il programma gira. `w.theme()` lo legge. Su un telefono anche le barre del sistema prendono i colori del tema.

Il testo ricco segue il tema da sé, poiché i suoi colori sono ruoli del tema; un disegno su un canvas ha i colori che il programma gli dà.

### Più finestre, chiusura e attesa

Un programma può avere più finestre: ognuna si crea e si avvia come la prima, e tutte condividono il thread delle finestre, perciò un handler di una può cambiare i componenti di un'altra. Una finestra si può creare e avviare anche dentro un handler:

```cromo norun
import std.gui

gui.Window aboutWindow() {
    gui.Window about = gui.Window("About", 300, 160)
    about.setResizable(false)
    about.add(gui.Label("Notes 1.0, made with Cromo", 16, 16, 268, 20))
    gui.Button ok = about.add(gui.Button("OK", 184, 77, 100, 28))
    ok.onClick([about]() { about.close() })
    ret about
}

[[window]]
i32 main() {
    gui.Window w = gui.Window("Notes", 480, 300)
    gui.Button info = w.add(gui.Button("About", 16, 16, 100, 28))
    info.onClick([]() { aboutWindow().start() })
    w.start()
    w.wait()
    println("the main window is closed")
    ret 0  // the program waits also for the windows About still open
}
```

- `w.close()` chiude la finestra, come fa il suo pulsante, e poi chiama i suoi handler di `onClose`. Una finestra chiusa prima di `start` non viene mai mostrata.
- `w.closed()` è vero una volta che la finestra è chiusa e i suoi handler di `onClose` sono stati eseguiti.
- `w.wait()` aspetta che la finestra sia chiusa, per un programma che ha altro da fare dopo. Aspetta per sempre per una finestra mai avviata, e quando è chiamato da un handler, che tiene il thread che chiuderebbe la finestra.
- Chiudere una finestra non chiude le altre: il programma termina quando sono chiuse tutte. Una finestra che deve chiudere le altre lo fa nel suo `onClose`.
- `w.start()` di una finestra già avviata è un panic.

## Ciò che ha ogni componente

Un componente si crea con il suo testo, quando ne ha uno, e il suo posto: `gui.Button("OK", x, y, width, height)`. Quelli senza testo prendono solo il posto, come `gui.Panel(x, y, width, height)`, e quelli che scelgono fra delle voci prendono prima la lista, come `gui.ComboBox(items, x, y, width, height)`.

`add` mette un componente in una finestra, in un riquadro di gruppo, in un pannello o in un dialogo, e ne restituisce l'handle. Un componente sta in un solo posto: aggiungerlo una seconda volta è un panic, `the component is already in a window or in a group`. I componenti si possono aggiungere prima o dopo `start`, e un gruppo si può mettere nella sua finestra prima o dopo che vi siano stati messi i suoi componenti.

Ogni componente ha questi metodi:

- `setPosition(x, y)` e `setSize(width, height)`; `x()`, `y()`, `width()` e `height()` dicono dove lo hanno messo ora le sue ancore.
- `setVisible(bool)` e `visible()`: un componente nascosto non viene disegnato e non prende né il mouse né i tasti.
- `setEnabled(bool)` e `enabled()`: un componente disabilitato è disegnato a metà della sua opacità e non prende né il mouse né i tasti.
- `setTooltip(text)` e `tooltip()`: un testo mostrato in una piccola card quando il mouse resta un momento sul componente, o dopo una pressione lunga su un telefono; `""` per nessuno, il predefinito, e `"\n"` comincia una riga nuova.
- `setAnchor(horizontal, vertical)`, `horizontalAnchor()` e `verticalAnchor()`, spiegati nella prossima sezione.

La barra dell'app e la barra di navigazione prendono il loro posto da sé, perciò di questi metodi hanno solo `setVisible`, `setEnabled` e quelli che leggono il posto; uno split non ha un tooltip proprio, i suoi pannelli sì.

Gli handler prendono una `move func`: il componente tiene la lambda, perciò la lambda deve possedere ciò che cattura. Handle e numeri si catturano per copia, stringhe e liste con `move`, e uno stato che più handler cambiano va in un `thread.Shared`, come in [Disegnare su un canvas](#disegnare-su-un-canvas). [Funzioni anonime](lambdas.md) spiega le regole.

## Posizioni e ancore

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Notes", 480, 300)
    gui.TextBox title = w.add(gui.TextBox("", 16, 16, 448, 32))
    title.setPlaceholder("Title")
    title.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Top)
    gui.Label status = w.add(gui.Label("", 16, 223, 300, 20))
    status.setAnchor(gui.Horizontal.Left, gui.Vertical.Bottom)
    gui.Button save = w.add(gui.Button("Save", 364, 217, 100, 32))
    save.setIcon(gui.Icon.Save)
    save.setAnchor(gui.Horizontal.Right, gui.Vertical.Bottom)
    save.onClick([title, status]() { status.setText(format("Saved: {}", title.text())) })
    w.start()
    ret 0
}
```

Quando la finestra si allarga, la casella di testo cresce con lei, il pulsante resta nell'angolo in basso a destra e lo stato in quello in basso a sinistra.

- Una posizione è in pixel logici dall'angolo in alto a sinistra del genitore: il contenuto della finestra, sotto la barra del titolo, oppure il riquadro di gruppo, il pannello o il corpo del dialogo che contiene il componente. `std.gui` scala i pixel logici con la densità dello schermo, perciò una finestra ha la stessa dimensione su uno schermo con più pixel.
- `setAnchor(gui.Horizontal, gui.Vertical)` dice quali bordi del suo genitore segue un componente quando il genitore cambia dimensione: `Left`, il predefinito, mantiene la distanza dal bordo sinistro, `Right` dal bordo destro, `Stretch` da entrambi, cambiando la larghezza, e `Center` mantiene il suo centro alla stessa distanza dal centro. `Top`, il predefinito, `Bottom`, `Stretch` e `Center` fanno lo stesso in verticale.
- Un componente mantiene le distanze che ha quando vengono impostate le sue ancore, la sua posizione o la sua dimensione. Così una finestra si scrive per una dimensione, e le ancore la adattano alle altre, anche allo schermo di un telefono.
- Una larghezza o un'altezza ridotta da un'ancora `Stretch` non scende sotto 0.

## Etichette, pulsanti e icone

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Editor", 420, 140)
    gui.Button star = w.add(gui.Button("", 16, 16, 32, 28))
    star.setIcon(gui.Icon.Star)
    star.setToggle(true)
    star.setTooltip("Mark as important")
    gui.Button send = w.add(gui.Button("Send", 56, 16, 96, 28))
    send.setIcon(gui.Icon.Send)
    send.setStyle(gui.ButtonStyle.Primary)
    gui.Button remove = w.add(gui.Button("Delete", 160, 16, 96, 28))
    remove.setStyle(gui.ButtonStyle.Danger)
    w.add(gui.IconBox(gui.Icon.CircleCheck, 16, 64, 24, 24))
    gui.Label status = w.add(gui.Label("Ready", 48, 66, 300, 20))
    star.onChange([status](bool pressed) { status.setText(pressed ? "Important" : "Normal") })
    send.onClick([status]() { status.setText("Sent") })
    remove.onClick([status]() { status.setText("Deleted") })
    w.start()
    ret 0
}
```

![Un pulsante a due stati con un'icona, un pulsante primario, un pulsante di pericolo e un riquadro con un'icona](images/buttons.png)

- `gui.Label(text, ...)` è una riga di testo, a sinistra del suo riquadro e a metà della sua altezza: `setText` e `text()`. Non prende né il mouse né i tasti.
- `gui.Button(text, ...)` chiama i suoi handler di `onClick` a un clic, a un tocco, o con Spazio e Invio mentre ha la tastiera. `setText` e `text()`; `setIcon(icon)` mette un'icona di 16 pixel prima del testo, o da sola al centro quando il testo è `""`, e `gui.Icon.None` la toglie.
- `setStyle` ne cambia l'aspetto: `gui.ButtonStyle.Normal`, il predefinito, chiaro con un bordo sottile; `Primary`, riempito con l'accento, per l'azione principale di una finestra; `Danger`, per un'azione che distrugge.
- Un pulsante con `setToggle(true)` ha due stati: ogni clic lo preme o lo rilascia, e quando è premuto è nell'accento tenue. `checked()` e `setChecked(bool)` leggono e impostano lo stato, e `onChange` riceve il nuovo stato, prima degli handler di `onClick`.
- `gui.IconBox(icon, ...)` mostra un'icona da sola, al centro del suo riquadro, grande quanto il lato più corto del riquadro: `setIcon` e `icon()`.

`gui.Icon` ha le icone di Lucide, disegnate a linee, nitide a ogni dimensione e nei colori del componente che le mostra. Nella libreria ce ne sono circa 150. I nomi sono quelli del sito di Lucide, con le maiuscole e senza trattini: `message-circle` è `gui.Icon.MessageCircle`, `trash-2` è `gui.Icon.Trash2`. L'editor completa i nomi, e un nome che non c'è è un errore di compilazione:

```cromo error
import std.gui

i32 main() {
    gui.Window w = gui.Window("Editor", 400, 300)
    gui.Button bold = w.add(gui.Button("", 16, 16, 28, 28))
    bold.setIcon(gui.Icon.Bold)
    ret 0
}
```

```output
error: Icon has no case 'Bold'
```

## Caselle di testo e aree di testo

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Note", 420, 300)
    gui.TextBox title = w.add(gui.TextBox("", 16, 16, 388, 32))
    title.setPlaceholder("Title")
    title.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Top)
    gui.TextArea body = w.add(gui.TextArea("", 16, 56, 388, 170))
    body.setPlaceholder("Write the note")
    body.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    gui.Label count = w.add(gui.Label("0 characters", 16, 237, 388, 20))
    count.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Bottom)
    title.onEnter([body](string text) { println("title: {}", text) })
    body.onChange([count](string text) { count.setText(format("{} characters", text.len())) })
    w.start()
    ret 0
}
```

- `gui.TextBox(text, ...)` è una riga in cui l'utente scrive, con il metodo di input del sistema e, su un telefono, con la tastiera sullo schermo. `text()` la dà, `setText` la sostituisce, `setPlaceholder` mostra un suggerimento in grigio finché è vuota.
- `onChange` riceve il testo a ogni cambiamento fatto dall'utente, un carattere scritto o cancellato, un testo tagliato o incollato; `onEnter` lo riceve quando l'utente preme Invio.
- `gui.TextArea(text, ...)` è un testo su più righe: Invio comincia una riga nuova, una riga più lunga dell'area continua sulla successiva ai suoi spazi, e il testo scorre in su e in giù. Ha `text()`, con `"\n"` fra le righe, `setText`, `setPlaceholder` e `onChange`. Tab dà la tastiera al componente successivo.
- In entrambe, le frecce, Home e Fine spostano il cursore, con Ctrl di una parola alla volta, e con Maiusc selezionano, come fa il mouse; Backspace, Canc, Ctrl+A, Ctrl+C, Ctrl+X e Ctrl+V fanno ciò che fanno in ogni programma, e Ctrl+Z e Ctrl+Y annullano e ripetono. Un testo incollato in una casella di testo diventa una riga sola. `setText` mette il cursore alla fine e dimentica ciò che Ctrl+Z potrebbe annullare.
- `examples/23_notepad.cro` è un blocco note.

## Caselle di spunta, interruttori, slider e combo box

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Account", 360, 385)
    gui.GroupBox account = w.add(gui.GroupBox("Account", 16, 16, 328, 140))
    account.add(gui.Label("Name", 12, 42, 200, 18))
    gui.TextBox name = account.add(gui.TextBox("", 12, 62, 304, 28))
    gui.ComboBox role = account.add(gui.ComboBox(["Developer", "Designer", "Viewer"], 12, 100, 304, 28))
    role.setPlaceholder("Choose a role")
    gui.GroupBox options = w.add(gui.GroupBox("Options", 16, 168, 328, 166))
    gui.CheckBox news = options.add(gui.CheckBox("Send me the news", 12, 44, 300, 22))
    news.setChecked(true)
    gui.Switch dark = options.add(gui.Switch("Dark theme", 12, 74, 304, 28))
    gui.Label size = options.add(gui.Label("Text size: 100%", 12, 110, 304, 18))
    gui.Slider zoom = options.add(gui.Slider(80, 150, 12, 128, 304, 28))
    zoom.setValue(100)
    role.onChange([role](i64 index) { println("role {}: {}", index, role.text()) })
    dark.onChange([w](bool on) { w.setTheme(on ? gui.Theme.Dark : gui.Theme.Light) })
    zoom.onChange([size](i64 value) { size.setText(format("Text size: {}%", value)) })
    w.start()
    ret 0
}
```

![Due gruppi, con una combo box, una casella di spunta, un interruttore e uno slider](images/form.png)

- `gui.CheckBox(text, ...)` è una casella da spuntare con il suo testo alla sua destra; un clic sulla casella o sul testo, o Spazio, la cambia. `checked()` e `setChecked(bool)`, falso all'inizio, e `onChange` con il nuovo stato; `setText` e `text()`.
- `gui.Switch(text, ...)` è la stessa scelta con l'aspetto degli interruttori di un telefono: il suo testo a sinistra e l'interruttore sul bordo destro del suo riquadro. Ha gli stessi metodi di una casella di spunta.
- `gui.Slider(minimum, maximum, ...)` sceglie un intero fra i due, entrambi compresi, trascinando la manopola, con un clic sulla guida, o con i tasti: Sinistra e Giù tolgono 1, Destra e Su aggiungono 1, Pag Giù e Pag Su si spostano di un decimo, Home e Fine vanno agli estremi. `value()`, `setValue`, che tiene il valore fra i due, `minimum()` e `maximum()`; `onChange` riceve il valore anche mentre la manopola viene trascinata. Un valore con i decimali si ottiene dividendolo: uno slider da 0 a 100 dà le percentuali. Un minimo sopra il massimo è un panic.
- `gui.ComboBox(items, ...)` sceglie una voce di una lista, che l'utente non può scrivere, come il `<select>` di HTML. Un clic apre la lista sopra gli altri componenti, sotto la casella o sopra di essa, con fino a 8 voci e le altre raggiungibili con la rotella. Con la tastiera, Su e Giù, Home, Fine e la prima lettera di una voce scelgono, e Spazio, Invio o Alt+Giù aprono la lista.
- `selected()` è l'indice della voce scelta, o -1, il predefinito, e `text()` è la voce, o `""`. `setSelected(index)` ne sceglie una, -1 per nessuna; `setItems(items)` sostituisce le voci e non ne sceglie nessuna; `addItem(item)` ne aggiunge una alla fine. `setPlaceholder` è il testo mostrato in grigio finché non è scelta nessuna voce, e `onChange` riceve l'indice che l'utente sceglie.

I gruppi che contengono questi componenti sono spiegati in [Gruppi e pannelli](#gruppi-e-pannelli).

## Gruppi di scelte, pulsanti a due stati e barre di avanzamento

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Choices", 420, 260)
    gui.RadioGroup size = w.add(gui.RadioGroup(["Small", "Medium", "Large"], 16, 16, 180, 84))
    size.setSelected(1)
    gui.Button bold = w.add(gui.Button("Bold", 220, 16, 80, 28))
    bold.setToggle(true)
    bold.setChecked(true)
    gui.Button italic = w.add(gui.Button("Italic", 308, 16, 80, 28))
    italic.setToggle(true)
    gui.ProgressBar done = w.add(gui.ProgressBar(16, 130, 388, 8))
    done.setValue(0.6)
    gui.ProgressBar busy = w.add(gui.ProgressBar(16, 160, 388, 8))
    busy.setBusy(true)
    size.onChange([size](i64 index) { println("size {}", size.text()) })
    bold.onChange([](bool pressed) { println("bold {}", pressed) })
    w.start()
    ret 0
}
```

![Un gruppo di scelte, pulsanti a due stati e barre di avanzamento](images/choices.png)

- `gui.RadioGroup(choices, ...)` è una scelta fra pochi valori, uno sotto l'altro, ognuno dei quali occupa 28 pixel dell'altezza, 44 su un telefono. All'inizio è scelto il primo. `selected()`, `setSelected(index)`, `text()`, `setItems(choices)`, che sceglie di nuovo il primo, e `onChange` con l'indice; mentre ha la tastiera, le frecce, Home e Fine scelgono.
- I pulsanti `Bold` e `Italic` hanno due stati, come spiega [Etichette, pulsanti e icone](#etichette-pulsanti-e-icone). Con la sola icona, un pulsante così è il modo di una vista in una barra.
- `gui.ProgressBar(...)` mostra la parte fatta di un lavoro: `setValue` da 0 a 1, con un valore fuori dall'intervallo portato all'estremo più vicino, e `value()`. Con `setBusy(true)` mostra invece un segmento che scorre, per un'attesa senza misura; `busy()` lo legge. La barra è alta al massimo 6 pixel, a metà del suo riquadro. Come ogni componente, si può cambiare da qualunque thread, per esempio dal thread che fa il lavoro.

## Gruppi e pannelli

Un riquadro di gruppo e un pannello contengono altri componenti, messi in essi con il loro `add`: le posizioni di quei componenti partono dall'angolo in alto a sinistra del loro gruppo, si spostano, si nascondono e vengono disabilitati con esso, ne seguono i bordi con le loro ancore, e sono disegnati solo al suo interno.

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Options", 360, 300)
    gui.CheckBox advanced = w.add(gui.CheckBox("Advanced options", 16, 16, 300, 22))
    gui.GroupBox more = w.add(gui.GroupBox("Advanced", 16, 50, 328, 199))
    more.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    more.setEnabled(false)  // its components too
    gui.Panel items = more.add(gui.Panel(0, 32, 328, 167))
    items.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    for (i32 i = 0; i < 12; i++) {
        items.add(gui.CheckBox(format("Option {}", i + 1), 12, 10 + 30 * i, 300, 22))
    }
    advanced.onChange([more](bool on) { more.setEnabled(on) })
    w.start()
    ret 0
}
```

- `gui.GroupBox(title, ...)` è una card con il suo titolo in una fascia in alto, alta 32 pixel, 40 su un telefono, perciò i suoi primi componenti vanno sotto la fascia, a circa 42 pixel. Senza titolo non ha la fascia. `setText` e `text()` cambiano e leggono il titolo.
- `gui.Panel(...)` è un contenitore senza aspetto proprio. Una pagina di una barra di navigazione, di una vista a schede e di uno split è un pannello.
- Gruppi e pannelli contengono gruppi e pannelli. Un gruppo messo in se stesso, o in un gruppo che esso contiene, è un panic: `a group cannot contain itself`.

### Lo scorrimento

Un pannello scorre in su e in giù quando i suoi componenti ancorati in alto vanno oltre il suo bordo inferiore, come le dodici opzioni qui sopra: con un dito, con la rotella del mouse, o con la barra sottile che compare alla sua destra. Non c'è niente da scrivere per questo.

- I componenti ancorati in altro modo, in basso, allargati o centrati, restano dove sono sulla parte visibile mentre gli altri scorrono, come un pulsante sopra una lista.
- `scrollY()` dice di quanto è scorso il pannello, in pixel logici, e `scrollTo(y)` lo fa scorrere: `scrollTo(0)` torna in cima.
- Un riquadro di gruppo non scorre: ciò che va oltre i suoi bordi viene tagliato. Un pannello al suo interno, come sopra, scorre.
- Le liste, le tabelle, le aree di testo e le viste di testo scorrono da sé nello stesso modo. Un pannello dentro un pannello scorre per primo.
- Su un telefono, quando si apre la tastiera sullo schermo, il contenuto della finestra finisce sopra di essa, e i pannelli scorrono per mostrare la casella di testo in cui si sta scrivendo.

## Dividere lo spazio

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Mail", 760, 455)
    gui.Split main = w.add(gui.Split(gui.Direction.Across, 8, 8, 744, 404))
    main.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    main.setFirstSize(180)
    main.setMinimums(120, 240)
    main.setDraggable(true)

    gui.ListView folders = main.first().add(gui.ListView(0, 0, 180, 404))
    folders.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    folders.addItem(gui.Icon.Inbox, "Inbox", "", "12")
    folders.addItem(gui.Icon.Send, "Sent", "", "")
    folders.addItem(gui.Icon.Archive, "Archive", "", "")

    gui.Split right = main.second().add(gui.Split(gui.Direction.Down, 0, 0, 556, 404))
    right.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    right.setRatio(0.45)
    right.setDraggable(true)
    gui.Table messages = right.first().add(gui.Table(0, 0, 556, 178))
    messages.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    messages.addColumn("From", 140, gui.Horizontal.Left)
    messages.addColumn("Subject", 0, gui.Horizontal.Left)
    messages.setRowCount(500)
    messages.onNeedRows([messages](i64 first, i64 count) {
        list<list<string>> rows
        for (i64 i = first; i < first + count; i++) {
            rows.add([format("Person {}", i % 7), format("Message {}", i + 1)])
        }
        messages.setRows(first, rows)
    })

    gui.TextView text = right.second().add(gui.TextView(0, 0, 556, 218))
    text.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    gui.TextStyle label = gui.TextStyle(gui.Ink.Faint)
    label.strong = true
    label.scale = 0.8
    label.spacing = 0.06
    gui.RichText t
    t.text("FROM  ", label)
    t.text("Person 0\n", gui.TextStyle(gui.Ink.Normal))
    t.text("The report of October is ready: the numbers are in the second table.", gui.TextStyle(gui.Ink.Normal))
    text.setText(move t)
    right.onChange([](f32 part) { println("the table takes {} of the room", part) })
    w.start()
    ret 0
}
```

![Una finestra divisa in tre, con una lista, una tabella e un testo](images/splits.png)

- `gui.Split(direction, x, y, width, height)` divide il suo spazio fra due pannelli, `first()` e `second()`: affiancati con `gui.Direction.Across`, il primo a sinistra, uno sopra l'altro con `Down`, il primo in alto. `direction()` legge la direzione.
- Il primo pannello prende metà dello spazio, o la parte che dà `setRatio`, da 0 a 1; `ratio()` la legge, e un valore fuori dall'intervallo è un panic. `setFirstSize(pixels)` o `setSecondSize(pixels)` tengono un pannello a una misura in pixel, come una barra laterale, e l'altro prende il resto; `setRatio` torna a una parte.
- `setMinimums(first, second)` impedisce a entrambi di diventare troppo piccoli: prima il primo ha almeno la sua misura, poi il secondo, per quanto lo spazio lo permette. `setGap(pixels)` cambia lo spazio fra i pannelli, 8 pixel di default. Una misura negativa è un panic.
- Quando lo split cambia misura, come con un'ancora `Stretch`, i suoi pannelli lo seguono, e i componenti al loro interno seguono i pannelli con le proprie ancore. Gli split nei pannelli di altri split fanno la griglia di un'intera pagina.
- Con `setDraggable(true)` l'utente sposta la linea fra i pannelli con il mouse o con un dito, entro le misure minime; `onChange` riceve la parte del primo pannello a ogni spostamento, da conservare per la volta dopo.
- Un `gui.TextStyle` con `spacing` spazia le sue lettere, in proporzione alla loro misura: `0.06` per i maiuscoletti, come i titoli delle tabelle. [Testo ricco](#testo-ricco) spiega gli stili.

## Il focus e la tastiera

In una finestra un solo componente alla volta ha la tastiera, il *focus*: i tasti vanno a lui.

- Un clic o un tocco dà la tastiera al componente che sta sotto. Tab e Maiusc+Tab la spostano al componente successivo e a quello precedente, nell'ordine in cui sono stati aggiunti; i componenti nascosti o disabilitati vengono saltati.
- I componenti che prendono la tastiera sono i pulsanti, le caselle di testo e le aree di testo, le caselle di spunta, gli interruttori, gli slider, le combo box, i gruppi di scelte, i controlli segmentati, le viste a schede, le liste e le tabelle. Le etichette, le icone, le immagini, i gruppi, i pannelli, le barre di avanzamento, i canvas e le viste di testo no.
- Dopo un tasto, il componente con la tastiera ha un anello attorno; dopo un clic o un tocco non ne ha. Una casella di testo con la tastiera lo mostra sempre, con il suo bordo e il suo cursore.
- Spazio e Invio premono un pulsante, Spazio cambia una casella di spunta o un interruttore, e le frecce spostano uno slider, un gruppo di scelte, un controllo segmentato, le schede, una lista o una tabella. Ogni sezione di questo capitolo dice i tasti dei suoi componenti.
- I tasti che il componente non usa vanno a `onKeyDown` della finestra, come F5 o Ctrl+S in una casella di testo.
- Un dialogo prende la tastiera mentre è aperto, e la dà al suo primo componente che la prende; quando si chiude, la tastiera torna al componente che l'aveva. Esc chiude un dialogo, un menu e la lista di una combo box.

## Immagini

```cromo
import std.gui

i32 main() {
    gui.Window w = gui.Window("Photos", 400, 300)
    gui.ImageBox photo = w.add(gui.ImageBox("", 16, 16, 200, 200))
    photo.setBytes(embed("images/ada.png"))
    photo.setScale(gui.Scale.Fill)
    println("{} x {}", photo.imageWidth(), photo.imageHeight())
    try {
        photo.setFile("holiday.jpg")
    } catch (Exception e) {
        println("{}", e.message())
    }
    ret 0
}
```

```output
128 x 128
cannot read 'holiday.jpg': no such file or folder
```

- `gui.ImageBox(file, ...)` mostra un'immagine PNG, JPEG, BMP, GIF, di cui mostra il primo fotogramma, o SVG. `setFile(path)` la legge da un file, `setBytes(bytes)` dalla memoria, come la danno `embed` e `fs.readBytes`, e `""` o una lista vuota la tolgono. `file()` è il percorso, e `imageWidth()` e `imageHeight()` la dimensione dell'immagine nei suoi pixel.
- L'immagine viene letta e decodificata dal thread che la imposta, che riceve gli errori come eccezioni: un file che non si può leggere, o dei byte che non sono un'immagine, lanciano un `Error`, e l'immagine mostrata resta. Il costruttore lancia nello stesso modo.
- `setScale` dice come l'immagine si adatta al riquadro, al suo centro: `gui.Scale.Fit`, il predefinito, l'immagine intera, grande quanto il riquadro permette; `Fill`, il riquadro coperto tutto, con l'immagine tagliata ai bordi; `Stretch`, la dimensione del riquadro, senza le proporzioni dell'immagine; `Center`, la dimensione dell'immagine, uno dei suoi pixel per ogni pixel logico.
- Un'immagine SVG è disegnata alla dimensione in cui è mostrata, nitida a ogni dimensione e densità: la scelta migliore per loghi e disegni.
- `gui.Image(bytes)` legge un'immagine una volta sola per un disegno su un canvas o per il testo ricco, e i suoi `width()` e `height()` ne danno la dimensione. Un'icona va in un `gui.IconBox`.

## File dentro il programma

Un programma che mostra immagini ne ha bisogno dove gira. `embed("file")` legge un file quando il programma viene compilato, e ne mette i byte nell'eseguibile. È una funzionalità del linguaggio, non di `std.gui`, e funziona in qualunque programma; è qui perché è in una finestra che un programma ha più spesso bisogno dei suoi file:

```text file=help.txt
Write your name and press Greet.
The answer appears under the button.
```

```cromo
i32 main() {
    string help = string(embed("help.txt"))
    for (string line in help.trim().split("\n")) {
        println("> {}", line)
    }
    ret 0
}
```

```output
> Write your name and press Greet.
> The answer appears under the button.
```

- Il percorso parte dalla cartella del file `.cro` che scrive `embed`, come fa `#include` in C: un modulo trova i suoi file accanto a sé, chiunque lo importi e ovunque giri il compilatore. Un percorso assoluto è preso così com'è.
- Ogni chiamata dà una `list<u8>` tutta sua, che il programma può cambiare; `string(...)` la rende un testo. Il file è letto così com'è, byte per byte, e i suoi byte sono nell'eseguibile una volta sola, per quante chiamate lo nominino.
- Un file che cambia richiede di ricompilare il programma.
- Un'immagine va a un `ImageBox` con `setBytes(embed("images/logo.png"))`, a una voce di una lista con `setItemImageBytes`, e a un disegno con `gui.Image(embed(...))`. Su un telefono, dove un'app non ha accanto una cartella di file, è così che porta con sé le sue immagini.

Il compilatore legge il file quando controlla la chiamata, perciò l'argomento deve essere un letterale stringa, e il file deve esserci:

```cromo error
i32 main() {
    string name = "help.txt"
    list<u8> bytes = embed(name)
    ret 0
}
```

```output
error: embed takes the name of a file as a string literal: embed("logo.png")
```

```cromo error
i32 main() {
    list<u8> bytes = embed("notes.txt")
    ret 0
}
```

```output
error: cannot embed notes.txt: there is no file notes.txt
```

Anche una cartella è un errore, `cannot embed images: images is a folder`. E `embed` è un nome integrato, come `format`: una funzione del programma chiamata `embed` è un errore, `'embed' is a built-in name`.

## Liste

```cromo
import std.gui

i32 main() {
    gui.Window w = gui.Window("Mail", 390, 760)
    gui.ListView inbox = w.add(gui.ListView(0, 0, 390, 725))
    inbox.addItem(gui.Icon.User, "Ada Lovelace", "The notes on the engine", "10:42")
    inbox.addItem(gui.Icon.Mail, "The workshop", "The report of the day", "Mon")
    inbox.setSelected(1)
    inbox.insertItem(0, gui.Icon.Bell, "Reminder", "Send the letters", "Sun")
    println("{} items, chosen {}: {}", inbox.count(), inbox.selected(), inbox.itemTitle(inbox.selected()))
    inbox.removeItem(2)
    println("{} items, chosen {}", inbox.count(), inbox.selected())
    ret 0
}
```

```output
3 items, chosen 2: The workshop
2 items, chosen -1
```

Un `gui.ListView` mostra le sue voci in righe di 44 pixel, 56 su un telefono, come i messaggi di un'app di posta. Vengono disegnate solo le righe che si vedono, perciò una lista può avere migliaia di voci.

- `addItem(icon, title, subtitle, detail)` aggiunge una voce: un'icona, un titolo, una riga sotto di esso e un testo alla sua destra, come un orario, con `gui.Icon.None` e `""` per ciò che la voce non ha. `insertItem(index, ...)` ne mette una prima della voce in posizione `index`, `setItem(index, ...)` ne sostituisce una, `removeItem(index)` e `clear()` le tolgono, e `count()` dice quante sono.
- `itemTitle`, `itemSubtitle`, `itemDetail` e `itemIcon` leggono una voce. Un indice fuori dalle voci è un panic.
- La voce scelta resta evidenziata: `selected()` è il suo indice, o -1, e `setSelected` ne sceglie una. Inserire e togliere voci mantiene scelta la stessa voce, al suo nuovo indice; toglierla non lascia nessuna voce scelta, come sopra.
- `onChoose` riceve l'indice della voce che l'utente sceglie, con un clic, un tocco o i tasti, Su, Giù, Home, Fine, Pag Su e Pag Giù, anche quando era già scelta.
- `setItemImage(index, file)` e `setItemImageBytes(index, bytes)` mostrano un'immagine rotonda di 40 pixel al posto dell'icona, come la foto di un contatto: il quadrato al centro dell'immagine.
- `scrollTo(index)` scorre il minimo necessario per mostrare una voce. La lista scorre come un pannello.

[Un'app: barre, pagine e liste](#unapp-barre-pagine-e-liste) mostra una lista nella sua finestra.

## Tabelle

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Orders", 560, 360)
    gui.Table orders = w.add(gui.Table(16, 16, 528, 293))
    orders.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    orders.addColumn("Number", 96, gui.Horizontal.Right)
    orders.addColumn("Customer", 0, gui.Horizontal.Left)
    orders.addColumn("Total", 112, gui.Horizontal.Right)
    orders.setRowCount(1000000)
    orders.onNeedRows([orders](i64 first, i64 count) {
        list<list<string>> rows
        for (i64 i = first; i < first + count; i++) {
            rows.add([format("{}", i + 1), format("Customer {}", i % 977), format("{}.{:02}", i % 1000, i % 100)])
        }
        orders.setRows(first, rows)
    })
    orders.onChoose([orders](i64 row) { println("order {}", orders.cell(row, 0)) })
    w.start()
    ret 0
}
```

![Una tabella di un milione di righe](images/table.png)

- `gui.Table` mostra righe di celle in colonne: `addColumn(title, width, align)`, con `gui.Horizontal.Left`, `Center` o `Right` per il titolo e le celle, e una larghezza di 0 per lo spazio che le altre colonne lasciano, diviso fra le colonne di larghezza 0. L'utente non può trascinare le larghezze. `setColumnVisible(index, visible)` nasconde una colonna e la mostra di nuovo, e `columnCount()` le conta tutte.
- Può avere milioni di righe: `setRowCount` dice quante, e la tabella chiede con `onNeedRows(first, count)` quelle che mostra e non ha, a blocchi di 100. Il programma le dà con `setRows(first, rows)`, anche più tardi e da un altro thread che le legge da un file o da un database; una riga non ancora data è vuota. Una tabella piccola riceve tutte le sue righe in una volta, prima di `start`. `cell(row, column)` legge una cella, `""` per una riga non data.
- Un clic su un titolo chiama `onSort(column, descending)`, crescente al primo clic e decrescente al secondo: il programma ordina i suoi dati, chiama `clearRows()`, e la tabella chiede di nuovo le sue righe, mostrando l'ordine con una freccia. `setSort(column, descending)` mostra un ordine senza chiamare gli handler, -1 per nessuno, e `sortColumn()` e `sortDescending()` lo leggono.
- Un clic sceglie una riga, Ctrl e un clic ne aggiungono o ne tolgono una, Maiusc e un clic scelgono quelle in mezzo, e Ctrl+A tutte; le frecce, Pag Su, Pag Giù, Home e Fine si spostano. `selected()` è la riga scelta per ultima, `selectedRows()` tutte in ordine, `setSelected(index)` ne sceglie una e scorre fino a essa, e `onChange` dice quando cambiano. Un doppio clic o Invio chiamano `onChoose`. `scrollTo(index)` mostra una riga.
- I dati che gli handler leggono e ordinano vanno in un `thread.Shared`, come in `examples/20_table.cro`.

## Schede e controlli segmentati

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Agenda", 480, 300)
    gui.Segmented period = w.add(gui.Segmented(["Day", "Week", "Month"], 16, 16, 240, 28))
    period.setSelected(1)
    gui.TabView views = w.add(gui.TabView(16, 60, 448, 189))
    views.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    gui.Panel events = views.addPage("Events")
    events.add(gui.Label("Review of the plan, at 9:00", 16, 16, 300, 20))
    gui.Panel tasks = views.addPage("Tasks")
    tasks.add(gui.CheckBox("Book the room for Thursday", 16, 16, 300, 24))
    period.onChange([period](i64 index) { println("period {}", period.text()) })
    views.onChange([](i64 index) { println("tab {}", index) })
    w.start()
    ret 0
}
```

![Schede e un controllo segmentato](images/tabs.png)

- `gui.TabView` è una card con le sue schede in alto: `addPage(title)` aggiunge una scheda e restituisce la sua pagina, un pannello sotto le schede che segue la dimensione della card, dove vanno i componenti di quella scheda. Un clic su una scheda mostra la sua pagina, e `onChange` dice quale ha scelto l'utente.
- `setSelected(index)` e `selected()` scelgono e leggono la scheda mostrata, la prima all'inizio; `pageCount()`, `setPageTitle(index, title)` e `pageTitle(index)` contano e rinominano le schede.
- `gui.Segmented` è una fila di pulsanti uniti, uno scelto, per una scelta fra pochi valori che si vedono tutti: i segmenti si dividono la sua larghezza in parti uguali. `selected()` e `text()` dicono quale è scelto, il primo all'inizio, `setSelected` ne sceglie uno, `setItems` li sostituisce, e `onChange` dice quale ha scelto l'utente.
- Tutti e due prendono la tastiera: Sinistra e Destra scelgono la scheda o il segmento accanto, Home e Fine il primo e l'ultimo.
- `examples/22_agenda.cro` è una piccola agenda con tutti e due.

## Testo ricco

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Notes", 480, 200)
    gui.TextView notes = w.add(gui.TextView(16, 16, 448, 133))
    notes.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    gui.TextStyle plain = gui.TextStyle(gui.Ink.Normal)
    gui.TextStyle code = gui.TextStyle(gui.Ink.Accent)
    code.mono = true
    gui.TextStyle remark = gui.TextStyle(gui.Ink.Selection)
    remark.italic = true

    gui.RichText t
    t.text("Read the file with ", plain)
    t.link(1, "fs.readText", code)
    t.tooltip(1, "Reads a whole file as text")
    t.text(", then split it into lines with ", plain)
    t.link(2, "split", code)
    t.text(". The text goes to the next line by itself when the window is narrower.\n", plain)
    t.startBlock(14, gui.BlockLine.Selection)
    t.text("Both throw an error when the file is not there.", remark)
    t.endBlock()
    notes.setText(move t)
    notes.setMarked(1)
    notes.onClick([notes](i64 piece, gui.Mouse m) { notes.setMarked(piece) })
    w.start()
    ret 0
}
```

![Testo ricco, con un pezzo segnato](images/rich-text.png)

- `gui.TextView` mostra un `gui.RichText`, che il programma costruisce, su qualunque thread, e consegna con `setText(move t)`: per cambiarlo lo costruisce di nuovo, e la vista resta dove era scorsa. Va a capo alla sua larghezza, e scorre quando il testo è più lungo della sua altezza.
- `text(value, style)` aggiunge un testo, che va a capo da sé agli spazi; `"\n"` e `newLine()` cominciano una riga nuova. `startBlock(indent, line)` e `endBlock()` fanno un blocco, con un margine e una linea alla sua sinistra, `gui.BlockLine.None`, `Plain`, `Selection` o `Accent`, anche dentro un altro blocco. `image(picture, width, height)` mette una piccola immagine fra le parole, e `gap(width)` uno spazio di qualche pixel. `clear()` lo svuota.
- Un `gui.TextStyle(ink)` ha un colore che è un ruolo del tema, un `gui.Ink`, `Normal`, `Secondary`, `Faint`, `Accent`, `Selection` o `Danger`, così il testo segue da sé il tema chiaro e quello scuro. I suoi campi sono `strong` per il semigrassetto, `italic`, `mono` per un carattere a spaziatura fissa, `scale` per la dimensione, 1 di default, e `spacing`, lo spazio dopo ogni lettera in proporzione alla sua dimensione, da -0.5 a 1: un valore fuori dall'intervallo è un panic quando il testo viene aggiunto.
- `link(piece, value, style)` e `linkImage(piece, picture, width, height)` aggiungono un pezzo che l'utente clicca, con il suo numero: i testi e le immagini con lo stesso numero, uno dopo l'altro, sono un pezzo solo, che non si spezza su più righe. `onClick` dà il numero e il mouse, con il suo tasto. `setMarked(piece)` segna un pezzo nell'accento e scorre per mostrarlo, -1 per nessuno, e `marked()` lo legge; `scrollTo(piece)` si limita a scorrere. `tooltip(piece, text)` dà un tooltip a un pezzo.
- `setTextSize(size)` imposta la dimensione del testo in pixel logici, da 6 a 72, 13 di default sul desktop e 14 su un telefono, e `setLineSpacing(value)` l'altezza delle righe, da 1 a 3 volte la dimensione, 1.5 di default.
- `examples/21_thread.cro` mostra una discussione con le sue risposte, una dentro l'altra.

## Menu e tooltip

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Files", 480, 300)
    gui.Button actions = w.add(gui.Button("Actions", 16, 16, 100, 28))
    actions.setTooltip("What can be done with the file")
    gui.Label status = w.add(gui.Label("report.pdf", 132, 16, 300, 28))
    actions.onClick([w, actions, status]() {
        gui.Menu menu
        menu.addIconItem(gui.Icon.Copy, "Copy", [status]() { status.setText("Copied") })
        menu.addIconItem(gui.Icon.Pencil, "Rename", [status]() { status.setText("Renamed") })
        menu.addSeparator()
        menu.addIconItem(gui.Icon.Trash2, "Delete", [status]() { status.setText("Deleted") })
        menu.setEnabled(3, false)
        w.showMenuUnder(move menu, actions)
    })
    w.start()
    ret 0
}
```

![Un menu sotto un pulsante](images/menu.png)

- `gui.Menu` è un valore che il programma costruisce quando gli serve: `addItem(text, handler)`, `addIconItem(icon, text, handler)` con un'icona, `addSeparator()`, e `setEnabled(index, false)` per una voce che ora non si può scegliere; l'indice conta anche i separatori, perciò `Delete` è 3. `len()` conta gli elementi, e `clear()` li toglie. Siccome il menu è costruito in quel momento, i suoi handler catturano ciò di cui parla, come la riga o il pezzo cliccato.
- `w.showMenu(move menu)` lo apre dove si trova il mouse: il posto del menu del tasto destro. `w.showMenuUnder(move menu, component)` lo apre sotto un componente, come la lista di un pulsante. Un nuovo menu prende il posto di quello aperto, e `w.closeMenu()` lo chiude.
- Il menu sparisce quando si sceglie una voce, e allora viene chiamato il suo handler; con Esc; o con un clic fuori da esso, che non fa nient'altro. Su, Giù e Invio scelgono con i tasti. Su un telefono una pressione lunga su un pezzo di testo ricco vale come tasto destro.

Una voce creata con `addSubmenu(text, move submenu)` apre un altro menu alla sua destra, quando il mouse è su di essa o con la freccia Destra; Sinistra lo chiude. Un sottomenu può avere a sua volta dei sottomenu, a qualunque profondità:

```cromo norun
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Editor", 480, 300)
    gui.Label status = w.add(gui.Label("Click with the right button", 16, 16, 448, 20))
    w.onMouseDown([w, status](gui.Mouse m) {
        if (m.button == 3) {
            gui.Menu sizes
            for (i64 size = 12; size <= 16; size += 2) {
                sizes.addItem(format("{} pixels", size), [status, size]() { status.setText(format("Text of {} pixels", size)) })
            }
            gui.Menu view
            view.addSubmenu("Text size", move sizes)
            view.addItem("Wrap the lines", [status]() { status.setText("Lines wrapped") })
            gui.Menu menu
            menu.addIconItem(gui.Icon.Copy, "Copy", [status]() { status.setText("Copied") })
            menu.addSubmenu("View", move view)
            w.showMenu(move menu)
        }
    })
    w.start()
    ret 0
}
```

- `len()` e `setEnabled` di un menu contano i suoi elementi, fra cui le voci con un sottomenu, non quelli dei sottomenu.

I tooltip sono le piccole card che spiegano che cosa fa qualcosa. Ogni componente tranne la barra dell'app, la barra di navigazione e lo split ha `setTooltip(text)`; `RichText.tooltip(piece, text)` ne dà uno a un pezzo di testo ricco, e `AppBar.setActionTooltip(index, text)` a un'azione della barra dell'app. Sul desktop il tooltip compare quando il mouse resta un momento sul componente, e su un telefono dopo una pressione lunga.

## Disegnare su un canvas

```cromo norun
import std.gui

// A white card, a curve through the values, the logo, and a ring on the point lit.
gui.Drawing chartOf(list<f32> values, i64 lit, gui.Image logo) {
    gui.Drawing d
    d.roundRect(0, 0, 400, 240, 8, gui.rgb(0xFFFFFF))
    d.image(logo, 360, 8, 32, 32)
    list<f32> points
    for (i64 i = 0; i < values.len(); i++) {
        points.add(40 + f32(i) * 64)
        points.add(210 - values[i] * 5)
    }
    d.polyline(points, 3, gui.rgb(0xB4761C))
    if (lit >= 0) {
        f32 x = points[2 * lit]
        f32 y = points[2 * lit + 1]
        d.ring(x, y, 8, 2, gui.rgb(0x2F6E8F))
        d.text(format("{}", values[lit]), x, y - 22, 13, gui.rgb(0x18212A), gui.Horizontal.Center)
    }
    ret d
}

[[window]]
i32 main() {
    list<f32> values = [12.0, 18.0, 15.0, 24.0, 21.0, 30.0]
    gui.Image logo = gui.Image(embed("images/cromo.svg"))
    gui.Window w = gui.Window("Chart", 432, 307)
    gui.Canvas chart = w.add(gui.Canvas(16, 16, 400, 240))
    chart.setDrawing(chartOf(values, -1, logo))
    chart.onMouseMove([chart, logo, move values](gui.Mouse m) {
        i64 lit = i64((m.x - 40) / 64 + 0.5)
        chart.setDrawing(chartOf(values, lit >= 0 && lit < values.len() ? lit : -1, logo))
    })
    w.start()
    ret 0
}
```

![La curva, con il mouse sul suo quarto punto](images/canvas.png)

- `gui.Canvas(x, y, width, height)` è una superficie su cui il programma disegna: un grafico, una mappa, un diagramma, un gioco. Mostra un `gui.Drawing`, che il programma costruisce, su qualunque thread, e consegna con `setDrawing(move d)`; il canvas lo tiene e lo ridisegna da sé, finché un nuovo disegno non prende il suo posto. Sotto le forme si vede il genitore del canvas.
- Un disegno ha le sue forme, ciascuna disegnata sopra le precedenti: `rect` e `roundRect` pieni, `frame` e `ring`, contorni di un rettangolo e di un cerchio, `circle`, `line` da un punto a un altro con le estremità arrotondate, `polygon` pieno e `polyline` attraverso dei punti dati come x e y uno dopo l'altro, `image` in un rettangolo con le sue proporzioni, e `text`, una riga la cui `y` è a metà della sua altezza e la cui `x` è il suo bordo sinistro, il suo centro o il suo bordo destro. `clear()` lo svuota, e `len()` conta le forme.
- Le posizioni sono in pixel logici dall'angolo in alto a sinistra del canvas. `gui.rgb(0xB4761C)` e `gui.rgba(0x2F6E8F, 115)` danno i colori, un `gui.Color`, il secondo con la sua opacità da 0 a 255.
- `gui.Image(bytes)` legge un'immagine una volta sola: PNG, JPEG, BMP, GIF o SVG. Un'immagine SVG è disegnata alla dimensione in cui è mostrata, nitida a ogni dimensione.
- `onMouseDown`, `onMouseMove` e `onMouseUp` danno la posizione dall'angolo in alto a sinistra del canvas. Mentre un tasto premuto sul canvas resta premuto, i suoi movimenti e il suo rilascio arrivano al canvas anche fuori da esso: così il programma può trascinare ciò che disegna.
- L'handler non può cambiare le variabili di `main`: possiede le sue copie, spostate dentro di lui con `move`, e a ogni movimento ridisegna da quelle e dal mouse.

Uno stato che gli handler cambiano va in un `thread.Shared`, che ogni handler cattura per copia. Qui il cerchio segue il mouse mentre il suo tasto resta premuto:

```cromo norun
import std.gui
import std.thread

struct Spot {
    f32 x = 100
    f32 y = 100
    bool held
}

gui.Drawing spotOf(Spot s) {
    gui.Drawing d
    d.roundRect(0, 0, 400, 240, 8, gui.rgb(0xFFFFFF))
    d.circle(s.x, s.y, 20, gui.rgb(s.held ? 0x2F6E8F : 0xB4761C))
    ret d
}

[[window]]
i32 main() {
    thread.Shared<Spot> spot = thread.Shared<Spot>(Spot())
    gui.Window w = gui.Window("Drag", 432, 307)
    gui.Canvas canvas = w.add(gui.Canvas(16, 16, 400, 240))
    canvas.setDrawing(spotOf(Spot()))
    canvas.onMouseDown([spot, canvas](gui.Mouse m) {
        spot.lock([canvas, m](ref Spot s) {
            s.held = (m.x - s.x) * (m.x - s.x) + (m.y - s.y) * (m.y - s.y) < 400
            canvas.setDrawing(spotOf(s))
        })
    })
    canvas.onMouseMove([spot, canvas](gui.Mouse m) {
        spot.lock([canvas, m](ref Spot s) {
            if (s.held) {
                s.x = m.x
                s.y = m.y
                canvas.setDrawing(spotOf(s))
            }
        })
    })
    canvas.onMouseUp([spot, canvas](gui.Mouse m) {
        spot.lock([canvas](ref Spot s) {
            s.held = false
            canvas.setDrawing(spotOf(s))
        })
    })
    w.start()
    ret 0
}
```

`onWheel` riceve la rotella del mouse sopra il canvas: dove si trova il mouse, con i tasti premuti, e i passi, più di 0 quando la rotella gira allontanandosi dall'utente. Qui ingrandisce un cerchio, più in fretta con Ctrl:

```cromo norun
import std.gui
import std.thread

gui.Drawing circleOf(f32 radius) {
    gui.Drawing d
    d.roundRect(0, 0, 400, 240, 8, gui.rgb(0xFFFFFF))
    d.circle(200, 120, radius, gui.rgba(0x2F6E8F, 115))
    d.text(format("{}", i32(radius)), 200, 120, 13, gui.rgb(0x18212A), gui.Horizontal.Center)
    ret d
}

[[window]]
i32 main() {
    thread.Shared<f32> radius = thread.Shared<f32>(40)
    gui.Window w = gui.Window("Zoom", 432, 307)
    gui.Canvas canvas = w.add(gui.Canvas(16, 16, 400, 240))
    canvas.setDrawing(circleOf(40))
    canvas.onWheel([radius, canvas](gui.Mouse m, f32 steps) {
        radius.lock([canvas, m, steps](ref f32 r) {
            r += steps * (m.control ? 10 : 2)
            if (r < 4) {
                r = 4
            } else if (r > 110) {
                r = 110
            }
            canvas.setDrawing(circleOf(r))
        })
    })
    w.start()
    ret 0
}
```

- Senza handler di `onWheel`, la rotella fa scorrere il pannello che contiene il canvas; con essi, no.
- Il `gui.Mouse` di ogni handler ha `shift`, `control` e `alt`, i tasti premuti, così un clic con Ctrl può aggiungere a una selezione e un trascinamento con Maiusc può tenere dritta una linea.

## Un'app: barre, pagine e liste

```cromo norun
import std.gui

[[window]]
[[android("org.example.mail", "Mail")]]
i32 main() {
    gui.Window w = gui.Window("Mail", 390, 760)
    gui.AppBar top = w.add(gui.AppBar("Inbox"))
    gui.NavigationBar bar = w.navigationBar()

    // Each page fills the room between the two bars, and has its size at once.
    gui.Panel inbox = bar.addPage("Inbox", gui.Icon.Inbox)
    gui.ListView messages = inbox.add(gui.ListView(0, 0, inbox.width(), inbox.height()))
    messages.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
    messages.addItem(gui.Icon.User, "Ada Lovelace", "The notes on the engine", "10:42")
    messages.setItemImageBytes(0, embed("images/ada.png"))
    messages.addItem(gui.Icon.User, "Charles Babbage", "The plans of the machine", "09:15")
    messages.setItemImageBytes(1, embed("images/charles.png"))
    for (i64 day = 2; day <= 30; day++) {
        messages.addItem(gui.Icon.Mail, "The workshop", format("The report of day {}", day), format("Oct {}", day))
    }
    messages.onChoose([messages](i64 index) { println("open {}", messages.itemTitle(index)) })
    gui.Button write = inbox.add(gui.Button("Write", inbox.width() - 128, inbox.height() - 56, 112, 40))
    write.setIcon(gui.Icon.Pencil)
    write.setStyle(gui.ButtonStyle.Primary)
    write.setAnchor(gui.Horizontal.Right, gui.Vertical.Bottom)

    gui.Panel settings = bar.addPage("Settings", gui.Icon.Settings)
    gui.Switch notify = settings.add(gui.Switch("Notifications", 16, 16, 358, 32))
    notify.setChecked(true)
    notify.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Top)
    gui.Label size = settings.add(gui.Label("Text size: 100%", 16, 64, 358, 20))
    gui.Slider zoom = settings.add(gui.Slider(80, 150, 16, 88, 358, 32))
    zoom.setValue(100)
    zoom.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Top)
    zoom.onChange([size](i64 value) { size.setText(format("Text size: {}%", value)) })

    bar.onChange([top](i64 index) { top.setTitle(index == 0 ? "Inbox" : "Settings") })
    w.start()
    ret 0
}
```

![Lo stesso programma su Windows e su un telefono Android](images/mail.png)

- `gui.AppBar(title)` è la barra in alto, messa nella finestra con `add`: occupa la parte alta del contenuto, tutta la larghezza e 44 pixel, 56 su un telefono, e gli altri componenti vanno sotto di essa. `setTitle` e `title()`; `setNavigation(icon)` mette un'icona alla sua sinistra, come `gui.Icon.Menu` o `gui.Icon.ArrowLeft`, i cui clic vanno a `onNavigation`; `addAction(icon, handler)` aggiunge le azioni alla sua destra, la prima sul bordo destro, e `setActionTooltip(index, text)` dà un tooltip a una di esse. Una finestra ha al massimo una barra dell'app: una seconda, o una messa in un gruppo, in un pannello o in un dialogo, è un panic.
- `w.navigationBar()` dà la barra in basso, creata la prima volta, alta 56 pixel, 64 su un telefono. Ogni `addPage(text, icon)` le aggiunge una voce e ne restituisce la pagina, un `gui.Panel` che riempie lo spazio fra le due barre; la barra mostra la pagina della voce scelta. `onChange` dice quale voce ha scelto l'utente, e `setSelected(index)` e `selected()` la scelgono e la leggono, la prima all'inizio.
- La lista riempie la sua pagina, e `onChoose` apre un messaggio. Il pulsante `Write` è ancorato in basso a destra della sua pagina: resta lì mentre la lista gli scorre sotto.
- Sul desktop `std.gui` è compatto, come le applicazioni desktop; su un telefono il testo, le righe e le barre sono più grandi, per le dita. Una pagina è più bassa su un telefono, dove le barre sono più alte: la lista e il pulsante prendono il loro posto da `inbox.width()` e `inbox.height()`, così vanno bene in entrambi i casi.

## Dialoghi e snackbar

```cromo norun
import std.gui

[[window]]
i32 main() {
#     gui.Window w = gui.Window("Mail", 390, 760)
#     gui.AppBar top = w.add(gui.AppBar("Inbox"))
#     gui.ListView messages = w.add(gui.ListView(0, top.height(), 390, 725 - top.height()))
#     messages.setAnchor(gui.Horizontal.Stretch, gui.Vertical.Stretch)
#     messages.addItem(gui.Icon.Mail, "The workshop", "The report of the day", "10:42")
#     messages.addItem(gui.Icon.Bell, "Reminder", "Send the letters", "Sun")
#
    gui.Dialog ask = gui.Dialog("Delete the message?", 320, 156)
    ask.add(gui.Label("It can be undone for a few seconds.", 0, 0, 292, 20))
    ask.addButton("Cancel", [ask]() { ask.close() })
    ask.addButton("Delete", [w, ask, messages]() {
        ask.close()
        i64 index = messages.selected()
        string title = messages.itemTitle(index)
        string line = messages.itemSubtitle(index)
        string when = messages.itemDetail(index)
        gui.Icon icon = messages.itemIcon(index)
        messages.removeItem(index)
        w.showSnackbarAction("Message deleted", "Undo", [messages, index, icon, move title, move line, move when]() {
            messages.insertItem(index, icon, title, line, when)
        })
    })
    top.addAction(gui.Icon.Trash2, [w, ask, messages]() {
        if (messages.selected() < 0) {
            w.showSnackbar("Choose a message first")
        } else {
            w.showDialog(ask)
        }
    })
#     w.start()
#     ret 0
}
```

![Un dialogo, e la snackbar dopo Delete](images/dialog.png)

- `gui.Dialog(title, width, height)` è una card con un titolo, i suoi componenti e i suoi pulsanti, disegnata dentro la finestra, uguale sul desktop e su un telefono. Il dialogo si crea una volta, e si può mostrare di nuovo dopo che si è chiuso.
- `add` mette un componente nel suo corpo, che comincia a 14 pixel dal bordo sinistro della card e 14 sotto la fascia del titolo, alta 32 pixel, e finisce 14 pixel sopra la fascia dei pulsanti, alta 44 pixel: il dialogo qui sopra ha un corpo di 292 per 52. Su un telefono le fasce sono alte 40 e 60 pixel, e i margini 16. In una finestra più piccola della card, la card si restringe, e i suoi componenti seguono le loro ancore.
- `addButton(text, handler)` aggiunge un pulsante in basso, a destra di quelli aggiunti prima: l'ultimo è quello principale, nell'accento. I pulsanti non chiudono il dialogo da sé: sono i loro handler a chiamare `close()`.
- `w.showDialog(d)` lo apre al centro della finestra, sopra il resto scurito, che il mouse e i tasti non raggiungono finché non si chiude, e ritorna subito. La tastiera va al primo componente del dialogo che la prende, e torna al componente che l'aveva quando il dialogo si chiude. Una finestra mostra un dialogo alla volta: mostrarne uno mentre un altro è aperto è un panic, `the window shows a dialog already`.
- `d.close()` lo chiude, e così fanno Esc e il tasto indietro di un telefono; poi vengono chiamati gli handler di `d.onClose`, in qualunque modo si sia chiuso. `d.shown()` dice se è aperto, e `setTitle` ne cambia il titolo.
- `w.showSnackbar(text)` mostra un breve messaggio in basso per 4 secondi, sopra la barra di navigazione e sopra i pulsanti ancorati lì, che restano visibili e si possono premere; una nuova snackbar prende il posto di quella mostrata. `w.showSnackbarAction(text, action, handler)` aggiunge un'azione, come `Undo`, che chiama l'handler e nasconde il messaggio.
- La lambda di `Undo` prende le stringhe della voce rimossa con `move`: la finestra la conserva, e una lambda conservata deve possedere ciò che cattura, come spiega [Funzioni anonime](lambdas.md).

## Aprire e salvare file

```cromo norun
import std.fs
import std.gui

[[window]]
i32 main() {
    gui.Window w = gui.Window("Notes", 480, 300)
    gui.Button open = w.add(gui.Button("Open", 16, 16, 96, 28))
    gui.TextArea text = w.add(gui.TextArea("", 16, 56, 448, 193))
    open.onClick([w, text]() {
        w.openFile([gui.FileType("Text", "txt;md")], [text](list<string> files) {
            if (files.len() > 0) {
                text.setText(fs.readText(files[0]))
            }
        })
    })
    w.start()
    ret 0
}
```

- `w.openFile(types, handler)` mostra il dialogo del sistema per aprire un file: quello di Windows, quello del desktop su Linux, il selettore dei documenti su Android. Ritorna subito, e l'handler viene chiamato più tardi con il percorso scelto in una lista, o con una lista vuota quando l'utente annulla.
- `gui.FileType("Text", "txt;md")` è un tipo di file che il dialogo mostra: il suo nome, e le sue estensioni senza il punto, separate da `;`, o `"*"` per ogni file. All'inizio è scelto il primo tipo, e una lista vuota mostra tutti i file.
- `w.openFiles` lascia scegliere più file, `w.saveFile(types, "notes.txt", handler)` chiede dove salvare, con un nome proposto, e `w.openFolder(handler)` chiede una cartella; l'handler di questi due riceve il percorso, o `""`.
- Il programma legge e scrive i file con `std.fs`. Su Android il selettore dà nomi come `content://...` invece di percorsi: `fs.readText`, `fs.readBytes`, `fs.writeText` e `fs.writeBytes` leggono e scrivono anche quelli, in un programma con finestre.
- `examples/23_notepad.cro` è un blocco note che apre e salva il suo testo.

## Programmi con finestre

### `[[window]]`

`[[window]]` prima di `main` rende l'eseguibile per Windows un programma a finestre, che non apre una console quando parte: un programma con finestre non ha una console dietro di esse. `cromo build` e `cromo run` non hanno bisogno di opzioni per questo. Su Linux non cambia nulla.

- Lo standard output e lo standard error di un programma così non vanno da nessuna parte, a meno che non siano rediretti, come in `notes.exe > log.txt 2> errors.txt`. Mentre si scrive un programma, conviene lasciare fuori `[[window]]`, per vedere i suoi `println` nella console.
- Solo `main` può averlo, una volta sola, e non prende argomenti:

```cromo error
[[window]]
void open() {
}

i32 main() {
    open()
    ret 0
}
```

```output
error: [[window]] is only for main of the program, whose executable opens no console
```

### Errori fatali

Un programma con `[[window]]` non ha una console dove mandare i suoi errori. Perciò quando fallisce, con un panic, un'eccezione che esce da `main` o uno stack overflow, una finestra di messaggio di Windows mostra il messaggio e lo stack trace, e il programma termina con il suo solito codice di uscita, 101 per un panic e 1 per un'eccezione, quando la finestra di messaggio viene chiusa. Se lo standard error è rediretto su un file o su una pipe, il testo va lì, come senza `[[window]]`.

Un'eccezione che esce da un handler ferma il programma nello stesso modo, con `uncaught exception in a thread:`, il messaggio e lo stack trace: nella finestra di messaggio con `[[window]]`, sullo standard error senza.

### L'icona

Quando il programma è compilato per Windows, un'icona accanto al file con `main`, con il nome di quel file, `notes.ico` per `notes.cro`, o altrimenti `main.ico`, entra nell'eseguibile: Esplora file e la barra delle applicazioni la mostrano. `cromo build` e `cromo run` la prendono da sé. Un file che non è un'icona è un errore: `cromo: the icon notes.ico is not a .ico file`. Su Linux un eseguibile non ha icona, e il file non viene letto.

### Ciò che serve a un eseguibile

Un eseguibile con finestre porta dentro di sé l'intero `std.gui`: non c'è nessuna DLL né alcun `.so` da distribuire insieme, e anche i font e le icone sono al suo interno. Su Linux, quando parte, carica le parti del sistema che disegnano le finestre, X11 o Wayland, perciò lo stesso eseguibile gira su entrambi; richiede glibc 2.29 o successiva, come la maggior parte delle distribuzioni dal 2019, e non gira su quelle con musl. I programmi con finestre non si possono ancora compilare per `--target linux-arm64`.

## Lo stesso programma su Android

```sh
cromo build --target android mail.cro    # writes mail.apk
cromo run --target android mail.cro      # installs it on the phone, starts it, shows its output
```

- `cromo build --target android` scrive l'APK da sé, con il programma per i telefoni, su ARM64, e per gli emulatori, su x86-64, firmato con una chiave di debug che `cromo` crea la prima volta in `~/.cromo/android`. Non servono né l'SDK di Android né Java. Le app girano da Android 8.
- `[[android("org.example.mail", "Mail")]]` prima di `main` dà il package dell'app e il nome sotto la sua icona; senza, il package è `org.cromo.` seguito dal nome del file, e il nome è quello del file. L'icona è `mail.png`, o `main.png`, accanto al file.
- `cromo run --target android` installa l'app sul telefono o sull'emulatore che `adb` vede, la avvia, e mostra ciò che stampa finché non finisce; Ctrl+C la chiude. `adb` fa parte dei platform tools di Android: `cromo` lo cerca in `ANDROID_HOME` e nel `PATH`, e con più dispositivi collegati `ANDROID_SERIAL` ne sceglie uno.
- Un'app che usa `std.net`, direttamente o tramite `std.http`, riceve da sé il permesso della rete.

Il package è fatto di nomi composti da lettere, cifre e `_`, ognuno dei quali comincia con una lettera, uniti da punti, almeno due:

```cromo error
[[android("mail", "Mail")]]
i32 main() {
    ret 0
}
```

```output
error: the package of an app is names of letters, digits and _, each starting with a letter, joined by dots, as org.example.mail
```

Su un telefono lo stesso programma si comporta come un'app:

- Un'app ha una sola finestra, che riempie lo schermo fra le barre del sistema, senza barra del titolo. Avviare una seconda finestra è un panic: `an app of Android has one window: this is a second one`. Le pagine di un'app sono quelle della sua barra di navigazione, o pannelli mostrati e nascosti.
- I componenti, scritti per la dimensione data a `gui.Window`, come 390 per 760, seguono lo schermo con le loro ancore, anche quando il telefono ruota. `setSize`, `setResizable`, `minimize`, `maximize` e `restore` lì non fanno nulla, il titolo non è mostrato, e nemmeno il pannello di `titleBar()`.
- La tastiera sullo schermo accorcia il contenuto, e una pagina scorre per mostrare la casella di testo in cui si sta scrivendo. La barra di navigazione resta sotto la tastiera.
- Il tasto indietro, o il suo gesto, chiude un dialogo aperto, altrimenti la finestra, e quindi l'app, a meno che `w.onBack(handler)` non decida altrimenti, per esempio tornando alla prima pagina. Sul desktop gli handler di `onBack` non vengono mai chiamati.
- Una pressione lunga vale come tasto destro su un pezzo di testo ricco, e mostra il tooltip sugli altri componenti.

## Riepilogo

- Una finestra e i suoi componenti sono handle, che le lambda catturano per copia. Un solo thread disegna tutte le finestre ed esegue gli handler, uno alla volta; i metodi si possono chiamare da qualunque thread, e la fine di `main` aspetta le finestre.
- Un handler deve ritornare presto: il lavoro lungo va a un thread, che cambia i componenti quando ha finito. Un'eccezione che esce da un handler ferma il programma.
- La finestra ha il suo titolo, la sua dimensione e il suo stato, i suoi eventi del mouse, dei tasti, della dimensione e della chiusura, una barra del titolo disegnata da `std.gui` dove possono andare dei componenti, e i temi chiaro, scuro e del sistema.
- Ogni componente ha il suo posto, le ancore, la visibilità, lo stato e il tooltip. Etichette, pulsanti, caselle e aree di testo, caselle di spunta, interruttori, slider, combo box, gruppi di scelte, barre di avanzamento, immagini e icone compongono i form; gruppi, pannelli e split li contengono; liste, tabelle, schede, controlli segmentati, testo ricco e canvas mostrano i dati; menu, dialoghi, snackbar e i dialoghi dei file parlano con l'utente.
- `embed` mette i file nell'eseguibile; `[[window]]` produce un programma senza console, i cui errori fatali compaiono in una finestra di messaggio, e un'icona accanto al file entra nell'eseguibile.
- Lo stesso programma è un'app per Android con `--target android`: una sola finestra che riempie lo schermo, con una barra dell'app, una barra di navigazione e delle pagine.
