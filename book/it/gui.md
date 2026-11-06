# Finestre e app

`std.gui` crea finestre con i loro componenti: etichette, pulsanti, caselle di testo, liste, dialoghi. Li disegna da sé, con SDL3 dentro il programma, perciò una finestra appare uguale su Windows e su Linux: piatta e sobria, con un accento ocra, chiara o scura, compatta come un'applicazione desktop. Lo stesso programma, compilato con `--target android`, è un'app per Android.

> Il funzionamento di `std.gui` è deciso: componenti come handle, gestori sul thread della finestra, posizioni in pixel con le ancore. I nomi dei componenti e dei loro metodi sono ancora una bozza, e possono cambiare prima della versione 1.0.

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

- `gui.Window(titolo, larghezza, altezza)` crea una finestra, e `w.start()` la mostra. Quando `main` ritorna, il programma aspetta che le sue finestre siano chiuse.
- `w.add(...)` mette un componente nella finestra e lo restituisce. Un componente è un *handle*: un piccolo valore che lo indica, e che si può copiare liberamente. La lambda di `onClick` cattura `name` e `answer` per copia, e legge e cambia i componenti sullo schermo.
- Posizioni e dimensioni sono in pixel logici, dall'angolo in alto a sinistra del contenuto, sotto la barra del titolo: `std.gui` le scala con la densità dello schermo.
- I gestori girano sul thread della finestra, uno alla volta, e possono cambiare qualunque componente. Un gestore che impiega molto blocca la finestra: il lavoro lungo va a un task, come in [Thread, task e canali](concurrency.md).
- `setStyle(gui.ButtonStyle.Primary)` riempie con l'accento il pulsante dell'azione principale; `gui.ButtonStyle.Danger` è per un'azione che distrugge, e gli altri pulsanti sono chiari con un bordo sottile.
- `[[window]]` produce su Windows un programma senza console, come sono i programmi con finestre; senza, la console resta, comoda per `println` mentre si scrive il programma. Un'icona `greet.ico` accanto a `greet.cro` entra nell'eseguibile.

## I componenti

| Componente | Che cos'è |
|---|---|
| `gui.Label(testo, x, y, larghezza, altezza)` | una riga di testo |
| `gui.Button(testo, ...)` | un pulsante: `onClick`, `setIcon` per un'icona prima del testo, e `setStyle` |
| `gui.TextBox(testo, ...)` | una riga in cui scrivere: `text()`, `setPlaceholder`, `onChange`, e `onEnter` per il tasto Invio |
| `gui.CheckBox(testo, ...)`, `gui.Switch(testo, ...)` | acceso o spento: `checked()` e `onChange` |
| `gui.Slider(minimo, massimo, ...)` | un intero tra i due: `value()` e `onChange` |
| `gui.ComboBox(voci, ...)` | una voce di una lista: `selected()`, `text()` e `onChange` |
| `gui.ImageBox(file, ...)` | un'immagine PNG, JPEG, BMP, GIF o SVG, da un file, o dalla memoria con `setBytes` |
| `gui.IconBox(icona, ...)` | un'icona |
| `gui.GroupBox(testo, ...)` | una scheda con il titolo in una fascia in alto, che contiene altri componenti |
| `gui.Panel(...)` | un contenitore senza aspetto proprio, che scorre quando i suoi componenti vanno oltre il fondo |
| `gui.ListView(...)` | una lista di voci, ognuna con un'icona o un'immagine, un titolo, una riga e un dettaglio |
| `gui.Canvas(...)` | una superficie su cui il programma disegna: `setDrawing`, e il mouse su di essa |

Ogni componente ha `setPosition`, `setSize`, `setVisible`, `setEnabled` e `setAnchor`. I componenti di un gruppo o di un pannello prendono le posizioni dal suo angolo in alto a sinistra.

## Le ancore

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

- `setAnchor(gui.Horizontal, gui.Vertical)` dice quali bordi segue un componente: `Left`, il predefinito, `Right`, `Stretch` per entrambi e `Center`; `Top`, il predefinito, `Bottom`, `Stretch` e `Center`.
- Un componente mantiene le distanze dai bordi che ha quando viene ancorato. Così una finestra si scrive per una dimensione, e le ancore la adattano alle altre, anche allo schermo di un telefono.
- `gui.Icon` ha le icone di Lucide, disegnate a linee a ogni dimensione: `gui.Icon.Save`, `gui.Icon.Mail`, `gui.Icon.Trash2`. Un nome sbagliato è un errore di compilazione, e l'editor completa i nomi. `setIcon` ne mette una su un pulsante, e `gui.IconBox` ne mostra una da sola.

## File dentro il programma

Un programma che mostra immagini ne ha bisogno dove gira. `embed("file")` legge un file quando il programma viene compilato, e ne mette i byte nell'eseguibile:

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

- Il percorso parte dalla cartella del file `.cro` che scrive `embed`, come fa `#include` in C: un modulo trova i suoi file accanto a sé, chiunque lo importi.
- Un file che non c'è è un errore di compilazione, e un file che cambia richiede di ricompilare il programma.
- Ogni chiamata dà una `list<u8>` tutta sua, che il programma può cambiare; `string(...)` la rende un testo.
- Un'immagine va a un `ImageBox` con `setBytes(embed("images/logo.png"))`, e a una voce di una lista con `setItemImageBytes`. Su un telefono, dove un'app non ha accanto una cartella di file, è così che porta con sé le sue immagini.

## Un'app: barre, pagine e liste

```cromo norun
import std.gui

[[window]]
[[android("org.example.mail", "Mail")]]
i32 main() {
    gui.Window w = gui.Window("Mail", 390, 760)
    gui.AppBar top = w.add(gui.AppBar("Inbox"))
    gui.NavigationBar bar = w.navigationBar()

    // Ogni pagina riempie lo spazio tra le due barre, e ha subito la sua misura.
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

- `gui.AppBar(titolo)` è la barra in alto; `setNavigation(icona)` mette un'icona alla sua sinistra, e `addAction(icona, gestore)` le azioni alla sua destra.
- `w.navigationBar()` dà la barra in basso. Ogni `addPage(testo, icona)` le aggiunge una voce e ne restituisce la pagina, un `gui.Panel` che riempie lo spazio tra le due barre; la barra mostra la pagina della voce scelta, e `onChange` dice quale.
- `gui.ListView` mostra le sue voci in righe: `addItem(icona, titolo, sottotitolo, dettaglio)`, con `gui.Icon.None` e `""` per ciò che una voce non ha, `setItemImageBytes` per un'immagine rotonda al posto dell'icona, e `onChoose` quando l'utente ne sceglie una. Una lista scorre con la rotella del mouse, con la barra alla sua destra o con un dito; lo stesso fa una pagina i cui componenti vanno oltre il fondo.
- Il pulsante `Write` è ancorato in basso a destra della sua pagina: resta lì mentre la lista gli scorre sotto.
- Sul desktop `std.gui` è compatto, come le applicazioni desktop; sul telefono il testo, le righe e le barre sono più grandi, per le dita. Una pagina è più bassa sul telefono, dove le barre sono più alte: la lista e il pulsante prendono il loro posto da `inbox.width()` e `inbox.height()`, così vanno bene in entrambi i casi.

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

- `gui.Dialog(titolo, larghezza, altezza)` è una scheda con un titolo, i suoi componenti, aggiunti con `add`, e i suoi pulsanti, aggiunti con `addButton(testo, gestore)`: l'ultimo è quello principale. `w.showDialog(d)` la apre al centro della finestra, sopra il resto scurito, che mouse e tasti non raggiungono finché non si chiude. `d.close()` la chiude, e così Esc e il tasto indietro di un telefono.
- `w.showSnackbar(testo)` mostra un breve messaggio in basso per 4 secondi, sopra i pulsanti ancorati lì; `w.showSnackbarAction(testo, azione, gestore)` aggiunge un'azione, come `Undo`, che chiama il gestore.
- La lambda di `Undo` prende le stringhe della voce rimossa con `move`: la finestra la conserva, e una lambda conservata deve possedere ciò che cattura, come spiega [Funzioni anonime](lambdas.md).

## Disegnare su un canvas

```cromo norun
import std.gui

// Una scheda bianca, una curva per i valori, il logo e un anello sul punto acceso.
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

- `gui.Canvas(x, y, larghezza, altezza)` è una superficie su cui il programma disegna: un grafico, una mappa, un diagramma, un gioco. Mostra un `gui.Drawing`, che il programma costruisce e consegna con `setDrawing(move d)`; il canvas lo tiene e lo ridisegna da sé, finché un nuovo disegno non prende il suo posto.
- Un disegno ha rettangoli, `rect` e `roundRect`, contorni, `frame` e `ring`, cerchi, segmenti, `line`, `polygon` e `polyline`, immagini e testi, ciascuno disegnato sopra i precedenti. Le posizioni sono in pixel logici dall'angolo in alto a sinistra del canvas, e `gui.rgb(0xB4761C)` e `gui.rgba(0x2F6E8F, 115)` danno i colori, il secondo con la sua opacità.
- `gui.Image(byte)` legge un'immagine una volta sola: PNG, JPEG, BMP, GIF o SVG. Un'immagine SVG è disegnata alla misura in cui è mostrata, nitida a ogni misura, e anche `ImageBox` la accetta.
- `onMouseDown`, `onMouseMove` e `onMouseUp` danno la posizione dall'angolo in alto a sinistra del canvas. Mentre un pulsante premuto sul canvas resta premuto, i suoi movimenti e il suo rilascio arrivano al canvas anche fuori da esso: così il programma può trascinare ciò che disegna.
- Il gestore non può cambiare le variabili di `main`: possiede le sue copie, spostate dentro di lui con `move`, e a ogni movimento ridisegna da quelle e dalla posizione del mouse.

Uno stato che i gestori cambiano va in un `thread.Shared`, che ogni gestore cattura per copia. Qui il cerchio segue il mouse mentre il suo pulsante resta premuto:

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

## Lo stesso programma su Android

```sh
cromo build --target android mail.cro    # scrive mail.apk
cromo run --target android mail.cro      # lo installa sul telefono, lo avvia, ne mostra l'output
```

- `cromo build --target android` scrive l'APK da sé, con il programma per i telefoni, su ARM64, e per gli emulatori, su x86-64, firmato con una chiave di debug che `cromo` crea la prima volta in `~/.cromo/android`. Non servono né l'SDK di Android né Java. Le app girano da Android 8.
- `[[android("org.example.mail", "Mail")]]` prima di `main` dà il package dell'app e il nome sotto la sua icona; senza, entrambi vengono dal nome del file. L'icona è `mail.png`, o `main.png`, accanto al file.
- `cromo run --target android` installa l'app sul telefono o sull'emulatore che `adb` vede, la avvia, e mostra ciò che stampa finché non finisce; Ctrl+C la chiude. `adb` fa parte dei platform tools di Android: `cromo` lo cerca in `ANDROID_HOME` e nel `PATH`, e con più dispositivi collegati `ANDROID_SERIAL` ne sceglie uno.
- Sul telefono la finestra riempie lo schermo tra le barre del sistema, e le ancore vi adattano i componenti scritti per 390 per 760, anche quando il telefono ruota. La tastiera sullo schermo accorcia il contenuto, e una pagina scorre per mostrare la casella in cui si scrive. Il tasto indietro chiude l'app, a meno che `w.onBack(gestore)` decida altrimenti, come tornare alla prima pagina.
- Un'app che usa `std.net`, direttamente o tramite `std.http`, riceve da sé il permesso della rete.
