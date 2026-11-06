# Windows and apps

`std.gui` makes windows with their components: labels, buttons, text boxes, lists, dialogs. It draws them itself, with SDL3 inside the program, so a window looks the same on Windows and on Linux: flat and quiet, with an ochre accent, light or dark, compact as a desktop application. The same program, compiled with `--target android`, is an app for Android.

> How `std.gui` works is decided: components as handles, handlers on the thread of the window, positions in pixels with anchors. The names of the components and of their methods are still a draft, and can change before version 1.0.

## A first window

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

![The window, after a click on Greet](images/first-window.png)

- `gui.Window(title, width, height)` makes a window, and `w.start()` shows it. When `main` returns, the program waits until its windows are closed.
- `w.add(...)` puts a component in the window and gives it back. A component is a *handle*: a small value that names it, and that can be copied freely. The lambda of `onClick` captures `name` and `answer` by copy, and reads and changes the components on the screen.
- Positions and sizes are in logical pixels, from the top left corner of the content, under the title bar: `std.gui` scales them with the density of the screen.
- The handlers run on the thread of the window, one at a time, and can change any component. A handler that takes long freezes the window: long work goes to a task, as in [Threads, tasks and channels](concurrency.md).
- `setStyle(gui.ButtonStyle.Primary)` fills the button of the main action with the accent; `gui.ButtonStyle.Danger` is for an action that destroys, and the other buttons are light with a thin border.
- `[[window]]` makes on Windows a program without a console, as programs with windows are; without it the console stays, which is handy for `println` while the program is being written. An icon `greet.ico` next to `greet.cro` goes into the executable.

## The components

| Component | What it is |
|---|---|
| `gui.Label(text, x, y, width, height)` | a line of text |
| `gui.Button(text, ...)` | a button: `onClick`, `setIcon` for an icon before the text, and `setStyle` |
| `gui.TextBox(text, ...)` | a line to write in: `text()`, `setPlaceholder`, `onChange`, and `onEnter` for the Enter key |
| `gui.CheckBox(text, ...)`, `gui.Switch(text, ...)` | on or off: `checked()` and `onChange` |
| `gui.Slider(minimum, maximum, ...)` | an integer between the two: `value()` and `onChange` |
| `gui.ComboBox(items, ...)` | one item of a list: `selected()`, `text()` and `onChange` |
| `gui.ImageBox(file, ...)` | an image in PNG, JPEG, BMP, GIF or SVG, from a file, or from memory with `setBytes` |
| `gui.IconBox(icon, ...)` | an icon |
| `gui.GroupBox(text, ...)` | a card with its title in a band at the top, which holds other components |
| `gui.Panel(...)` | a container with no look of its own, which scrolls when its components go below its bottom |
| `gui.ListView(...)` | a list of items, each with an icon or an image, a title, a line and a detail |
| `gui.Canvas(...)` | a surface on which the program draws: `setDrawing`, and the mouse on it |

Every component has `setPosition`, `setSize`, `setVisible`, `setEnabled` and `setAnchor`. The components of a group or of a panel take their positions from its top left corner.

## Anchors

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

When the window grows, the text box grows with it, the button stays in the bottom right corner and the status in the bottom left one.

- `setAnchor(gui.Horizontal, gui.Vertical)` says which edges a component follows: `Left`, the default, `Right`, `Stretch` for both and `Center`; `Top`, the default, `Bottom`, `Stretch` and `Center`.
- A component keeps the distances from the edges that it has when it is anchored. So a window is written for one size, and the anchors adapt it to the others, also to the screen of a phone.
- `gui.Icon` has the icons of Lucide, drawn with lines at every size: `gui.Icon.Save`, `gui.Icon.Mail`, `gui.Icon.Trash2`. A wrong name is a compile error, and the editor completes the names. `setIcon` puts one on a button, and `gui.IconBox` shows one alone.

## Files inside the program

A program that shows images needs them where it runs. `embed("file")` reads a file when the program is compiled, and puts its bytes in the executable:

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

- The path starts from the folder of the `.cro` file that writes `embed`, as `#include` does in C: a module finds its files next to itself, whoever imports it.
- A file that is not there is a compile error, and a file that changes needs the program compiled again.
- Each call gives a `list<u8>` of its own, which the program can change; `string(...)` makes it a text.
- An image goes to an `ImageBox` with `setBytes(embed("images/logo.png"))`, and to an item of a list with `setItemImageBytes`. On a phone, where an app has no folder of files next to it, this is how it carries its images.

## An app: bars, pages and lists

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

![The same program on Windows and on an Android phone](images/mail.png)

- `gui.AppBar(title)` is the bar at the top; `setNavigation(icon)` puts an icon on its left, and `addAction(icon, handler)` the actions on its right.
- `w.navigationBar()` gives the bar at the bottom. Each `addPage(text, icon)` adds an item to it and gives its page, a `gui.Panel` that fills the room between the two bars; the bar shows the page of the item chosen, and `onChange` says which one.
- `gui.ListView` shows its items in rows: `addItem(icon, title, subtitle, detail)`, with `gui.Icon.None` and `""` for what an item does not have, `setItemImageBytes` for a round image in place of the icon, and `onChoose` when the user chooses one. A list scrolls with the wheel of the mouse, with the bar on its right or with a finger; so does a page whose components go below its bottom.
- The button `Write` is anchored at the bottom right of its page: it stays there while the list scrolls under it.
- On the desktop `std.gui` is compact, as the desktop applications; on a phone the text, the rows and the bars are larger, for the fingers. A page is lower on a phone, where the bars are higher: the list and the button take their places from `inbox.width()` and `inbox.height()`, so they are right on both.

## Dialogs and snackbars

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

![A dialog, and the snackbar after Delete](images/dialog.png)

- `gui.Dialog(title, width, height)` is a card with a title, its components, added with `add`, and its buttons, added with `addButton(text, handler)`: the last one is the main one. `w.showDialog(d)` opens it at the center of the window, over the rest darkened, which the mouse and the keys do not reach until it closes. `d.close()` closes it, and so do Esc and the back key of a phone.
- `w.showSnackbar(text)` shows a short message at the bottom for 4 seconds, above the buttons anchored there; `w.showSnackbarAction(text, action, handler)` adds an action, as `Undo`, which calls the handler.
- The lambda of `Undo` takes the strings of the item removed with `move`: the window keeps it, and a lambda that is kept must own what it captures, as [Anonymous functions](lambdas.md) explains.

## Drawing on a canvas

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

![The curve, with the mouse on its fourth point](images/canvas.png)

- `gui.Canvas(x, y, width, height)` is a surface on which the program draws: a chart, a map, a diagram, a game. It shows a `gui.Drawing`, which the program builds and hands over with `setDrawing(move d)`; the canvas keeps it and draws it again by itself, until a new drawing takes its place.
- A drawing has rectangles, `rect` and `roundRect`, outlines, `frame` and `ring`, circles, segments, `line`, `polygon` and `polyline`, images and texts, each drawn over the ones before. The positions are in logical pixels from the top left corner of the canvas, and `gui.rgb(0xB4761C)` and `gui.rgba(0x2F6E8F, 115)` give the colors, the second with its opacity.
- `gui.Image(bytes)` reads an image once: PNG, JPEG, BMP, GIF or SVG. An SVG image is drawn at the size where it is shown, sharp at every size, and `ImageBox` takes it too.
- `onMouseDown`, `onMouseMove` and `onMouseUp` give the position from the top left corner of the canvas. While a button pressed on the canvas is held, its moves and its release come to the canvas also outside it: so the program can drag what it draws.
- The handler cannot change the variables of `main`: it owns its copies, moved into it with `move`, and at each move it draws again from them and from the mouse.

A state that the handlers change goes in a `thread.Shared`, which every handler captures by copy. Here the circle follows the mouse while its button is held:

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

## The same program on Android

```sh
cromo build --target android mail.cro    # writes mail.apk
cromo run --target android mail.cro      # installs it on the phone, starts it, shows its output
```

- `cromo build --target android` writes the APK by itself, with the program for phones, on ARM64, and for emulators, on x86-64, signed with a key for debugging that `cromo` makes the first time in `~/.cromo/android`. Neither the Android SDK nor Java is needed. The apps run from Android 8.
- `[[android("org.example.mail", "Mail")]]` before `main` gives the package of the app and the name under its icon; without it, both come from the name of the file. The icon is `mail.png`, or `main.png`, next to the file.
- `cromo run --target android` installs the app on the phone or on the emulator that `adb` sees, starts it, and shows what it prints until it ends; Ctrl+C closes it. `adb` comes with the platform tools of Android: `cromo` looks for it in `ANDROID_HOME` and in the `PATH`, and with more devices connected `ANDROID_SERIAL` chooses one.
- On the phone the window fills the screen between the bars of the system, and the anchors adapt the components written for 390 by 760 to it, also when the phone turns. The keyboard on the screen makes the content shorter, and a page scrolls to show the text box being written in. The back key closes the app, unless `w.onBack(handler)` decides otherwise, as going back to the first page.
- An app that uses `std.net`, directly or through `std.http`, gets the permission of the network by itself.
