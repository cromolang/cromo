# Windows and apps

`std.gui` makes windows with their components: labels, buttons, text boxes, lists, tables, dialogs. It draws them itself, inside the program, so a window looks the same on Windows and on Linux: flat and quiet, with an ochre accent, light or dark, compact as a desktop application. The same program, compiled with `--target android`, is an app for Android.

This chapter explains how the module works, then each family of components with its main methods and a short example. [std.gui](std.gui.html), in the reference of the library, lists every method.

> How `std.gui` works is decided: components as handles, handlers on the thread of the windows, positions in pixels with anchors. The names of the components and of their methods are still a draft, and can change before version 1.0.

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
- `setStyle(gui.ButtonStyle.Primary)` fills the button of the main action with the accent.
- `[[window]]` makes on Windows a program without a console, as programs with windows are; without it the console stays, which is handy for `println` while the program is being written. [Programs with windows](#programs-with-windows) explains it, with the icon of the executable.

## How std.gui works

Five rules explain the whole module.

- **Windows and components are handles.** A `gui.Window`, a `gui.Button` or a `gui.Table` is a small value that names a window or a component kept by the library. Its copies name the same one, and a lambda captures it by copy. A window has no destructor: it stays open when its variable ends, until the user or the program closes it.
- **One thread draws all the windows.** The first `start()` starts the *thread of the windows*: it draws every window of the program, at 60 frames a second by default, and runs the handlers of the events, one at a time, in the order of the events. A handler can change any component of any window.
- **A handler must return soon.** While a handler runs, no window is drawn and no other handler runs: a handler that takes long freezes all the windows. Long work goes to another thread, which changes the components when it has the result.
- **The methods can be called from any thread.** The methods of the windows and of the components take a lock, so `main`, a thread or a task can change a label or a progress bar while the user works: the change shows at the next frame. When the program changes a component, as with `setText` or `setChecked`, the handlers are not called: they are for what the user does.
- **The end of `main` waits for the windows.** When `main` returns, the program waits until all its windows are closed, and then ends with the exit code of `main`. `os.exit` ends it at once.

The methods work before the window is shown too. This program never starts its window, so it shows nothing: it only prints what its components hold.

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

Here `main` does the work after it has shown the window, and changes the components from its own thread. The window stays responsive: the user can move it, resize it or close it while the work goes on.

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

When the work starts from a handler, as after a click, the handler starts a thread for it, detaches it and returns at once ([Threads, tasks and channels](concurrency.md)). The thread gives the result to the components, whose methods work from any thread:

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

- The handler of `Count` only starts the thread and returns: the window keeps drawing while the thread counts.
- The thread captures only the handles of the components, which are simple values: it owns all that it uses, so it can be detached, and nothing waits for it. A thread that was not detached would be waited for by its destructor at the end of the handler, and the window would freeze.
- The end of `main` waits for the windows, not for the detached threads: a window closed while the thread counts ends the program.
- For work that waits, as a request to the network, a `thread.Task` is detached in the same way.

An exception that leaves a handler stops the program, as one that leaves a thread that nobody joins: it prints `uncaught exception in a thread:` with the message and the stack trace, and the program ends with exit code 1. A handler that can fail catches its exceptions, and shows the error in the window, as with a snackbar.

## The window

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

### Size and state

- `gui.Window(title, width, height)` takes the size of the whole window in logical pixels, its title bar of 35 pixels included: the content, where the components go, is `width` wide and `height - 35` high.
- `setTitle(text)` and `title()`: the title is shown at the center of the title bar and by the system, as in the taskbar.
- `setSize(width, height)`, `width()` and `height()`: the size of the whole window, which follows the user who resizes it. Before `start`, `setSize` gives the size the window starts with.
- `setResizable(false)` stops the user from resizing the window with its edges and from maximizing it; `resizable()` reads it, true by default.
- `minimize()`, `maximize()` and `restore()` change the state of the window, and `minimized()` and `maximized()` read it. Called before `start`, they choose how the window starts.
- `setFps(n)` sets how many frames are drawn each second, from 1 to 240, and `fps()` reads it: 60 by default. All the windows are drawn together, at the most frames among those open. A value out of the range is a panic.

### Events of the mouse and of the keys

- `onMouseMove`, `onMouseDown` and `onMouseUp` receive a `gui.Mouse`: `x` and `y` in logical pixels from the top left corner of the content, `button`, 1 left, 2 middle, 3 right and 0 while the mouse moves, and `shift`, `control` and `alt`, the keys held. The presses and the releases on the components come here too, after the components. On a phone a finger is the left button.
- `onKeyDown` receives a `gui.Key` for each key that the component with the keyboard does not use, as F5 in a text box; a key held down comes again as it repeats. Its `code` is the character of the key without Shift for the letters, the digits and the signs, 97 for A, also with Shift, and 49 for 1; then 13 is Enter, 27 Esc, 32 Space, 8 Backspace, 127 Delete, and the arrows are `0x40000050` left, `0x4000004F` right, `0x40000052` up and `0x40000051` down. It has `shift`, `control` and `alt` too. Tab, which moves the keyboard to the next component, and the keys pressed while a dialog is shown, do not come here.
- `onResize` receives the new width and height of the whole window when its size changes. The anchors move the components by themselves: the handler is for what they cannot do.
- `onClose` is called after the window is closed: by its button, by the keys of the system, by the back key of a phone, or by `close()`.

Every `on...` method, here and in the components, adds a handler: a window or a component can have several, called in the order they were added.

### The title bar

A window has no title bar of the system: `std.gui` draws its own, with the title at the center and the buttons to minimize, to maximize or restore, and to close on the right, in the colors of the theme. It behaves as the bar of the system: dragging it moves the window, a double click maximizes it, the edges of the window resize it, and on Windows the window snaps to the edges of the screen.

`w.titleBar()` gives a panel in the bar, where the program puts its own components, as a menu button or the views of a document:

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

![Components in the title bar, in the dark theme](images/title-bar.png)

- The panel takes the bar from its left edge to the buttons of the window, and is as high as the bar, 35 pixels. Its components are drawn over the bar; where there is none, the bar still moves the window, and the title stays at its center, under them.
- Each call gives the same panel, made the first time. On a phone, where a window has no title bar, the panel is not shown.

### Themes

`w.setTheme(theme)` sets the colors of the window and of its components, at once: `gui.Theme.Light`, dark text on light grays; `gui.Theme.Dark`, light text on dark grays; and `gui.Theme.System`, the default, which follows the theme of Windows, of the desktop of Linux or of the phone, and changes with it while the program runs. `w.theme()` reads it. On a phone the bars of the system take the colors of the theme too.

The rich text follows the theme by itself, since its colors are roles of the theme; a drawing on a canvas has the colors that the program gives it.

### Several windows, closing and waiting

A program can have several windows: each is made and started as the first, and all share the thread of the windows, so a handler of one can change the components of another. A window can be made and started also inside a handler:

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

- `w.close()` closes the window, as its button does, and then calls its handlers of `onClose`. A window closed before `start` is never shown.
- `w.closed()` is true once the window is closed and its handlers of `onClose` have run.
- `w.wait()` waits until the window is closed, for a program that has more to do after it. It waits forever for a window never started, and when it is called from a handler, which holds the thread that would close the window.
- Closing a window does not close the others: the program ends when all of them are closed. A window that must close the others does it in its `onClose`.
- `w.start()` of a window already started is a panic.

## What every component has

A component is made with its text, when it has one, and its place: `gui.Button("OK", x, y, width, height)`. Those without a text take only the place, as `gui.Panel(x, y, width, height)`, and those that choose among items take the list first, as `gui.ComboBox(items, x, y, width, height)`.

`add` puts a component in a window, a group box, a panel or a dialog, and gives back its handle. A component is in one place only: adding it a second time is a panic, `the component is already in a window or in a group`. Components can be added before or after `start`, and a group can be put in its window before or after its components are put in it.

Every component has these methods:

- `setPosition(x, y)` and `setSize(width, height)`; `x()`, `y()`, `width()` and `height()` give where its anchors have placed it now.
- `setVisible(bool)` and `visible()`: a hidden component is not drawn and takes neither the mouse nor the keys.
- `setEnabled(bool)` and `enabled()`: a disabled component is drawn at half its opacity and takes neither the mouse nor the keys.
- `setTooltip(text)` and `tooltip()`: a text shown in a small card when the mouse stays on the component for a moment, or after a long press on a phone; `""` for none, the default, and `"\n"` starts a new line.
- `setAnchor(horizontal, vertical)`, `horizontalAnchor()` and `verticalAnchor()`, explained in the next section.

The app bar and the navigation bar take their place by themselves, so of these methods they have only `setVisible`, `setEnabled` and those that read the place; a split has no tooltip of its own, its panels can.

The handlers take a `move func`: the component keeps the lambda, so the lambda must own what it captures. Handles and numbers are captured by copy, strings and lists with `move`, and a state that several handlers change goes in a `thread.Shared`, as in [Drawing on a canvas](#drawing-on-a-canvas). [Anonymous functions](lambdas.md) explains the rules.

## Positions and anchors

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

- A position is in logical pixels from the top left corner of the parent: the content of the window, under the title bar, or the group box, the panel or the body of the dialog that holds the component. `std.gui` scales logical pixels with the density of the screen, so a window has the same size on a screen with more pixels.
- `setAnchor(gui.Horizontal, gui.Vertical)` says which edges of its parent a component follows when the parent changes size: `Left`, the default, keeps the distance from the left edge, `Right` from the right edge, `Stretch` from both, changing the width, and `Center` keeps its center at the same distance from the center. `Top`, the default, `Bottom`, `Stretch` and `Center` do the same vertically.
- A component keeps the distances that it has when its anchors, its position or its size are set. So a window is written for one size, and the anchors adapt it to the others, also to the screen of a phone.
- A width or a height made smaller by a `Stretch` anchor does not go below 0.

## Labels, buttons and icons

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

![A button with two states and an icon, a primary button, a button of danger, and an icon box](images/buttons.png)

- `gui.Label(text, ...)` is a line of text, on the left of its box and in the middle of its height: `setText` and `text()`. It takes neither the mouse nor the keys.
- `gui.Button(text, ...)` calls its handlers of `onClick` at a click, a touch, or Space and Enter while it has the keyboard. `setText` and `text()`; `setIcon(icon)` puts an icon of 16 pixels before the text, or alone in the middle when the text is `""`, and `gui.Icon.None` removes it.
- `setStyle` changes its look: `gui.ButtonStyle.Normal`, the default, light with a thin border; `Primary`, filled with the accent, for the main action of a window; `Danger`, for an action that destroys.
- A button with `setToggle(true)` has two states: each click presses or releases it, and pressed it is in the soft accent. `checked()` and `setChecked(bool)` read and set the state, and `onChange` receives the new state, before the handlers of `onClick`.
- `gui.IconBox(icon, ...)` shows an icon alone, at the center of its box, as large as the smaller side of the box: `setIcon` and `icon()`.

`gui.Icon` has the icons of Lucide, drawn with lines, sharp at every size and in the colors of the component that shows them. About 150 are in the library. The names are those of the site of Lucide, with capitals and without hyphens: `message-circle` is `gui.Icon.MessageCircle`, `trash-2` is `gui.Icon.Trash2`. The editor completes the names, and a name that is not there is a compile error:

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

## Text boxes and text areas

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

- `gui.TextBox(text, ...)` is one line that the user writes, with the input method of the system and, on a phone, the keyboard on the screen. `text()` gives it, `setText` replaces it, `setPlaceholder` shows a hint in gray while it is empty.
- `onChange` receives the text at each change that the user makes, a character written or deleted, a text cut or pasted; `onEnter` receives it when the user presses Enter.
- `gui.TextArea(text, ...)` is a text on more lines: Enter starts a new line, a line longer than the area goes on the next one at its spaces, and the text scrolls up and down. It has `text()`, with `"\n"` between the lines, `setText`, `setPlaceholder` and `onChange`. Tab gives the keyboard to the next component.
- In both, the arrows, Home and End move, with Ctrl by a word, and with Shift they select, as the mouse does; Backspace, Delete, Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V do what they do in every program, and Ctrl+Z and Ctrl+Y undo and redo. A text pasted in a text box becomes one line. `setText` puts the cursor at the end and forgets what Ctrl+Z could undo.
- `examples/23_notepad.cro` is a notepad.

## Check boxes, switches, sliders and combo boxes

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

![Two groups, with a combo box, a check box, a switch and a slider](images/form.png)

- `gui.CheckBox(text, ...)` is a box to check with its text on its right; a click on the box or on the text, or Space, changes it. `checked()` and `setChecked(bool)`, false at the start, and `onChange` with the new state; `setText` and `text()`.
- `gui.Switch(text, ...)` is the same choice with the look of the switches of a phone: its text on the left and the switch at the right edge of its box. It has the same methods as a check box.
- `gui.Slider(minimum, maximum, ...)` chooses an integer between the two, both included, by dragging the knob, with a click on the track, or with the keys: Left and Down take 1, Right and Up add 1, Page Down and Page Up move by a tenth, Home and End go to the ends. `value()`, `setValue`, which keeps the value between the two, `minimum()` and `maximum()`; `onChange` receives the value also while the knob is dragged. A value with decimals comes from dividing it: a slider from 0 to 100 gives the percents. A minimum above the maximum is a panic.
- `gui.ComboBox(items, ...)` chooses one item of a list, which the user cannot write, as the `<select>` of HTML. A click opens the list over the other components, below the box or above it, with up to 8 items and the others with the wheel. With the keyboard, Up and Down, Home, End and the first letter of an item choose, and Space, Enter or Alt+Down open the list.
- `selected()` is the index of the item chosen, or -1, the default, and `text()` is the item, or `""`. `setSelected(index)` chooses one, -1 for none; `setItems(items)` replaces the items and chooses none; `addItem(item)` adds one at the end. `setPlaceholder` is the text shown in gray while no item is chosen, and `onChange` receives the index that the user chooses.

The groups that hold these components are explained in [Groups and panels](#groups-and-panels).

## Radio groups, toggle buttons and progress bars

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

![A radio group, buttons with two states and progress bars](images/choices.png)

- `gui.RadioGroup(choices, ...)` is a choice among a few values, one under the other, each taking 28 pixels of the height, 44 on a phone. The first is chosen at the start. `selected()`, `setSelected(index)`, `text()`, `setItems(choices)`, which chooses the first again, and `onChange` with the index; while it has the keyboard, the arrows, Home and End choose.
- The buttons `Bold` and `Italic` have two states, as [Labels, buttons and icons](#labels-buttons-and-icons) explains. With an icon only, such a button is the mode of a view in a bar.
- `gui.ProgressBar(...)` shows the part of a work that is done: `setValue` from 0 to 1, a value out of the range taken to its nearest end, and `value()`. With `setBusy(true)` it shows instead a segment that runs, for a wait without a measure; `busy()` reads it. The bar is at most 6 pixels high, in the middle of its box. As every component, it can be changed from any thread, as from the thread that does the work.

## Groups and panels

A group box and a panel hold other components, put in them with their `add`: the positions of those components start from the top left corner of their group, they move, hide and are disabled with it, they follow its edges with their anchors, and they are drawn only inside it.

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

- `gui.GroupBox(title, ...)` is a card with its title in a band at its top, 32 pixels high, 40 on a phone, so its first components go under the band, at about 42 pixels. Without a title it has no band. `setText` and `text()` change and read the title.
- `gui.Panel(...)` is a container with no look of its own. A page of a navigation bar, of a tab view and of a split is a panel.
- Groups and panels hold groups and panels. A group put in itself, or in a group that it holds, is a panic: `a group cannot contain itself`.

### Scrolling

A panel scrolls up and down when its components anchored to the top go below its bottom edge, as the twelve options above: with a finger, with the wheel of the mouse, or with the thin bar that shows on its right. There is nothing to write for it.

- The components anchored otherwise, to the bottom, stretched or centered, stay where they are on the part that is seen while the others scroll, as a button over a list.
- `scrollY()` says how far the panel is scrolled, in logical pixels, and `scrollTo(y)` scrolls it: `scrollTo(0)` goes back to the top.
- A group box does not scroll: what goes beyond its edges is cut. A panel in it, as above, scrolls.
- The lists, the tables, the text areas and the text views scroll by themselves in the same way. A panel in a panel scrolls first.
- On a phone, when the keyboard on the screen opens, the content of the window ends above it, and the panels scroll to show the text box being written in.

## Splitting the room

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

![A window split in three, with a list, a table and a text](images/splits.png)

- `gui.Split(direction, x, y, width, height)` shares its room between two panels, `first()` and `second()`: side by side with `gui.Direction.Across`, the first on the left, one over the other with `Down`, the first on top. `direction()` reads it.
- The first panel takes half of the room, or the part that `setRatio` gives, from 0 to 1; `ratio()` reads it, and a value out of the range is a panic. `setFirstSize(pixels)` or `setSecondSize(pixels)` keep one panel at a size in pixels, as a side bar, and the other takes the rest; `setRatio` goes back to a part.
- `setMinimums(first, second)` keeps both from becoming too small: the first has at least its size, then the second, as far as the room allows. `setGap(pixels)` changes the room between the panels, 8 pixels by default. A negative size is a panic.
- When the split changes size, as with a `Stretch` anchor, its panels follow, and the components in them follow with their anchors. Splits in the panels of other splits make the grid of a whole page.
- With `setDraggable(true)` the user moves the line between the panels with the mouse or a finger, within the least sizes; `onChange` receives the part of the first panel at each move, to keep it for the next time.
- A `gui.TextStyle` with `spacing` spaces its letters, as a part of their size: `0.06` for small capitals, as the titles of the tables. [Rich text](#rich-text) explains the styles.

## Focus and the keyboard

One component of a window at a time has the keyboard, the *focus*: the keys go to it.

- A click or a touch gives the keyboard to the component under it. Tab and Shift+Tab move it to the next and to the previous component, in the order they were added; the components hidden or disabled are skipped.
- The components that take the keyboard are the buttons, the text boxes and the text areas, the check boxes, the switches, the sliders, the combo boxes, the radio groups, the segmented controls, the tab views, the lists and the tables. The labels, the icons, the images, the groups, the panels, the progress bars, the canvases and the text views do not.
- After a key, the component with the keyboard has a ring around it; after a click or a touch it has none. A text box with the keyboard always shows it, with its border and its cursor.
- Space and Enter press a button, Space changes a check box or a switch, and the arrows move a slider, a radio group, a segmented control, the tabs, a list or a table. Each section of this chapter says the keys of its components.
- The keys that the component does not use go to `onKeyDown` of the window, as F5 or Ctrl+S in a text box.
- A dialog takes the keyboard while it is open, and gives it to its first component that takes it; when it closes, the keyboard goes back to the component that had it. Esc closes a dialog, a menu and the list of a combo box.

## Images

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

- `gui.ImageBox(file, ...)` shows an image in PNG, JPEG, BMP, GIF, of which it shows the first frame, or SVG. `setFile(path)` reads it from a file, `setBytes(bytes)` from memory, as `embed` and `fs.readBytes` give it, and `""` or an empty list remove it. `file()` is the path, and `imageWidth()` and `imageHeight()` the size of the image in its own pixels.
- The image is read and decoded by the thread that sets it, which receives the errors as exceptions: a file that cannot be read, or bytes that are not an image, throw an `Error`, and the image shown stays. The constructor throws in the same way.
- `setScale` says how the image fits the box, at its center: `gui.Scale.Fit`, the default, the whole image as large as the box allows; `Fill`, the whole box covered, the image cut at the edges; `Stretch`, the size of the box, without the proportions of the image; `Center`, the size of the image, one of its pixels for each logical pixel.
- An SVG image is drawn at the size where it is shown, sharp at every size and density: the best for logos and drawings.
- `gui.Image(bytes)` reads an image once for a drawing on a canvas or for rich text, and its `width()` and `height()` give its size. An icon goes in a `gui.IconBox`.

## Files inside the program

A program that shows images needs them where it runs. `embed("file")` reads a file when the program is compiled, and puts its bytes in the executable. It is a feature of the language, not of `std.gui`, and works in any program; it is here because a window is where a program most often needs its files:

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

- The path starts from the folder of the `.cro` file that writes `embed`, as `#include` does in C: a module finds its files next to itself, whoever imports it and wherever the compiler runs. An absolute path is taken as it is.
- Each call gives a `list<u8>` of its own, which the program can change; `string(...)` makes it a text. The file is read as it is, byte by byte, and its bytes are in the executable once, however many calls name it.
- A file that changes needs the program compiled again.
- An image goes to an `ImageBox` with `setBytes(embed("images/logo.png"))`, to an item of a list with `setItemImageBytes`, and to a drawing with `gui.Image(embed(...))`. On a phone, where an app has no folder of files next to it, this is how it carries its images.

The compiler reads the file when it checks the call, so the argument must be a string literal, and the file must be there:

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

A folder is an error too, `cannot embed images: images is a folder`. And `embed` is a built-in name, as `format`: a function of the program called `embed` is an error, `'embed' is a built-in name`.

## Lists

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

A `gui.ListView` shows its items in rows of 44 pixels, 56 on a phone, as the messages of an app of mail. Only the rows that are seen are drawn, so a list can have thousands of items.

- `addItem(icon, title, subtitle, detail)` adds an item: an icon, a title, a line under it and a text on its right, as a time, with `gui.Icon.None` and `""` for what the item does not have. `insertItem(index, ...)` puts one before the item at `index`, `setItem(index, ...)` replaces one, `removeItem(index)` and `clear()` remove them, and `count()` says how many there are.
- `itemTitle`, `itemSubtitle`, `itemDetail` and `itemIcon` read an item. An index out of the items is a panic.
- The item chosen stays lit: `selected()` is its index, or -1, and `setSelected` chooses one. Inserting and removing items keep the same item chosen, at its new index; removing it leaves none chosen, as above.
- `onChoose` receives the index of the item that the user chooses, with a click, a touch or the keys, Up, Down, Home, End, Page Up and Page Down, also when it was chosen already.
- `setItemImage(index, file)` and `setItemImageBytes(index, bytes)` show a round image of 40 pixels in place of the icon, as the photo of a contact: the square at the center of the image.
- `scrollTo(index)` scrolls as little as it takes to show an item. The list scrolls as a panel.

[An app: bars, pages and lists](#an-app-bars-pages-and-lists) shows a list in its window.

## Tables

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

![A table of a million rows](images/table.png)

- `gui.Table` shows rows of cells in columns: `addColumn(title, width, align)`, with `gui.Horizontal.Left`, `Center` or `Right` for the title and the cells, and a width of 0 for the room that the other columns leave, shared among the columns of width 0. The user cannot drag the widths. `setColumnVisible(index, visible)` hides a column and shows it again, and `columnCount()` counts them all.
- It can have millions of rows: `setRowCount` says how many, and the table asks with `onNeedRows(first, count)` for those it shows and does not have, in blocks of 100. The program gives them with `setRows(first, rows)`, also later and from another thread that reads them from a file or a database; a row not given yet is empty. A small table gives all its rows at once, before `start`. `cell(row, column)` reads a cell, `""` for a row not given.
- A click on a title calls `onSort(column, descending)`, ascending at the first click and descending at the second: the program sorts its data, calls `clearRows()`, and the table asks again for its rows, showing the order with an arrow. `setSort(column, descending)` shows an order without calling the handlers, -1 for none, and `sortColumn()` and `sortDescending()` read it.
- A click chooses a row, Ctrl and a click add or remove one, Shift and a click choose those between, and Ctrl+A all of them; the arrows, Page Up, Page Down, Home and End move. `selected()` is the row chosen last, `selectedRows()` all of them in order, `setSelected(index)` chooses one and scrolls to it, and `onChange` says when they change. A double click or Enter calls `onChoose`. `scrollTo(index)` shows a row.
- The data that the handlers read and sort go in a `thread.Shared`, as in `examples/20_table.cro`.

## Tabs and segmented controls

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

![Tabs and a segmented control](images/tabs.png)

- `gui.TabView` is a card with its tabs at the top: `addPage(title)` adds a tab and gives back its page, a panel under the tabs that follows the size of the card, where the components of that tab go. A click on a tab shows its page, and `onChange` says which one the user chose.
- `setSelected(index)` and `selected()` choose and read the tab shown, the first at the start; `pageCount()`, `setPageTitle(index, title)` and `pageTitle(index)` count and rename the tabs.
- `gui.Segmented` is a row of buttons joined together, one chosen, for a choice among a few values that are all seen: the segments share its width in equal parts. `selected()` and `text()` say which one is chosen, the first at the start, `setSelected` chooses one, `setItems` replaces them, and `onChange` says which one the user chose.
- Both take the keyboard: Left and Right choose the tab or the segment next to it, Home and End the first and the last.
- `examples/22_agenda.cro` is a small agenda with both.

## Rich text

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

![Rich text, with a piece marked](images/rich-text.png)

- `gui.TextView` shows a `gui.RichText`, which the program builds, on any thread, and hands over with `setText(move t)`: to change it, it builds it again, and the view stays where it was scrolled. It goes to the next line at its width, and scrolls when the text is longer than its height.
- `text(value, style)` adds a text, which goes to the next line by itself at the spaces; `"\n"` and `newLine()` start a new line. `startBlock(indent, line)` and `endBlock()` make a block, with a margin and a line on its left, `gui.BlockLine.None`, `Plain`, `Selection` or `Accent`, also inside another block. `image(picture, width, height)` puts a small image among the words, and `gap(width)` a space of some pixels. `clear()` empties it.
- A `gui.TextStyle(ink)` has a color that is a role of the theme, a `gui.Ink`, `Normal`, `Secondary`, `Faint`, `Accent`, `Selection` or `Danger`, so that the text follows the light and the dark theme by itself. Its fields are `strong` for semibold, `italic`, `mono` for a monospaced font, `scale` for the size, 1 by default, and `spacing`, the room after each letter as a part of its size, from -0.5 to 1: a value out of the range is a panic when the text is added.
- `link(piece, value, style)` and `linkImage(piece, picture, width, height)` add a piece that the user clicks, with its number: the texts and images with the same number one after the other are one piece, which does not break into lines. `onClick` gives the number and the mouse, with its button. `setMarked(piece)` marks a piece in the accent and scrolls to show it, -1 for none, and `marked()` reads it; `scrollTo(piece)` only scrolls. `tooltip(piece, text)` gives a piece a tooltip.
- `setTextSize(size)` sets the size of the text in logical pixels, from 6 to 72, 13 by default on the desktop and 14 on a phone, and `setLineSpacing(value)` the height of the lines, from 1 to 3 times the size, 1.5 by default.
- `examples/21_thread.cro` shows a discussion with its replies, one inside the other.

## Menus and tooltips

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

![A menu under a button](images/menu.png)

- `gui.Menu` is a value that the program builds when it needs it: `addItem(text, handler)`, `addIconItem(icon, text, handler)` with an icon, `addSeparator()`, and `setEnabled(index, false)` for an item that cannot be chosen now; the index counts the separators too, so `Delete` is 3. `len()` counts the entries, and `clear()` removes them. Since the menu is built at that moment, its handlers capture what it is about, as the row or the piece clicked.
- `w.showMenu(move menu)` opens it where the mouse is: the place of the menu of the right button. `w.showMenuUnder(move menu, component)` opens it under a component, as the list of a button. A new menu takes the place of the one open, and `w.closeMenu()` closes it.
- The menu goes away when an item is chosen, and then its handler is called; with Esc; or with a click out of it, which does nothing else. Up, Down and Enter choose with the keys. On a phone a long press on a piece of rich text is the right button.

An item made with `addSubmenu(text, move submenu)` opens another menu on its right, when the mouse is on it or with the Right arrow; Left closes it. A submenu can have submenus of its own, at any depth:

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

- `len()` and `setEnabled` of a menu count its own entries, the items with a submenu among them, not those of the submenus.

The tooltips are the small cards that explain what something does. Every component but the app bar, the navigation bar and the split has `setTooltip(text)`; `RichText.tooltip(piece, text)` gives one to a piece of rich text, and `AppBar.setActionTooltip(index, text)` to an action of the app bar. On the desktop the tooltip shows when the mouse stays on the component for a moment, and on a phone after a long press.

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

- `gui.Canvas(x, y, width, height)` is a surface on which the program draws: a chart, a map, a diagram, a game. It shows a `gui.Drawing`, which the program builds, on any thread, and hands over with `setDrawing(move d)`; the canvas keeps it and draws it again by itself, until a new drawing takes its place. Under the shapes, the parent of the canvas shows.
- A drawing has its shapes, each drawn over the ones before: `rect` and `roundRect` filled, `frame` and `ring` outlines of a rectangle and of a circle, `circle`, `line` from a point to another with round ends, `polygon` filled and `polyline` through points given as x and y one after the other, `image` in a rectangle with its proportions, and `text`, a line whose `y` is its middle and whose `x` is its left edge, its center or its right edge. `clear()` empties it, and `len()` counts the shapes.
- The positions are in logical pixels from the top left corner of the canvas. `gui.rgb(0xB4761C)` and `gui.rgba(0x2F6E8F, 115)` give the colors, a `gui.Color`, the second with its opacity from 0 to 255.
- `gui.Image(bytes)` reads an image once: PNG, JPEG, BMP, GIF or SVG. An SVG image is drawn at the size where it is shown, sharp at every size.
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

`onWheel` receives the wheel of the mouse over the canvas: where the mouse is, with the keys held, and the steps, more than 0 when the wheel turns away from the user. Here it zooms a circle, faster with Ctrl:

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

- Without handlers of `onWheel`, the wheel scrolls the panel that holds the canvas; with them, it does not.
- The `gui.Mouse` of every handler has `shift`, `control` and `alt`, the keys held, so a click with Ctrl can add to a selection and a drag with Shift can keep a line straight.

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

- `gui.AppBar(title)` is the bar at the top, put in the window with `add`: it takes the top of the content, the whole width and 44 pixels, 56 on a phone, and the other components go under it. `setTitle` and `title()`; `setNavigation(icon)` puts an icon on its left, as `gui.Icon.Menu` or `gui.Icon.ArrowLeft`, whose clicks go to `onNavigation`; `addAction(icon, handler)` adds the actions on its right, the first at the right edge, and `setActionTooltip(index, text)` gives one a tooltip. A window has one app bar at most: a second one, or one put in a group, a panel or a dialog, is a panic.
- `w.navigationBar()` gives the bar at the bottom, made the first time, 56 pixels high, 64 on a phone. Each `addPage(text, icon)` adds an item to it and gives its page, a `gui.Panel` that fills the room between the two bars; the bar shows the page of the item chosen. `onChange` says which item the user chose, and `setSelected(index)` and `selected()` choose and read it, the first at the start.
- The list fills its page, and `onChoose` opens a message. The button `Write` is anchored at the bottom right of its page: it stays there while the list scrolls under it.
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

- `gui.Dialog(title, width, height)` is a card with a title, its components and its buttons, drawn inside the window, the same on the desktop and on a phone. It is made once, and can be shown again after it closes.
- `add` puts a component in its body, which starts 14 pixels from the left edge of the card and 14 under the band of the title, 32 pixels high, and ends 14 pixels above the band of the buttons, 44 pixels high: the dialog above has a body of 292 by 52. On a phone the bands are 40 and 60 pixels high, and the margins 16. In a window smaller than the card, the card shrinks, and its components follow their anchors.
- `addButton(text, handler)` adds a button at the bottom, on the right of those added before: the last one is the main one, in the accent. The buttons do not close the dialog by themselves: their handlers call `close()`.
- `w.showDialog(d)` opens it at the center of the window, over the rest darkened, which the mouse and the keys do not reach until it closes, and returns at once. The keyboard goes to the first component of the dialog that takes it, and back to the component that had it when the dialog closes. A window shows one dialog at a time: showing one while another is open is a panic, `the window shows a dialog already`.
- `d.close()` closes it, and so do Esc and the back key of a phone; then the handlers of `d.onClose` are called, however it closed. `d.shown()` says whether it is open, and `setTitle` changes its title.
- `w.showSnackbar(text)` shows a short message at the bottom for 4 seconds, above the navigation bar and above the buttons anchored there, which stay visible and can be pressed; a new one takes the place of the one shown. `w.showSnackbarAction(text, action, handler)` adds an action, as `Undo`, which calls the handler and hides the message.
- The lambda of `Undo` takes the strings of the item removed with `move`: the window keeps it, and a lambda that is kept must own what it captures, as [Anonymous functions](lambdas.md) explains.

## Opening and saving files

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

- `w.openFile(types, handler)` shows the dialog of the system to open a file: that of Windows, of the desktop on Linux, the picker of the documents on Android. It returns at once, and the handler is called later with the path chosen in a list, or with an empty list when the user cancels.
- `gui.FileType("Text", "txt;md")` is a kind of file that the dialog shows: its name, and its extensions without the dot, between `;`, or `"*"` for every file. The first kind is chosen at the start, and an empty list shows every file.
- `w.openFiles` lets the user choose more files, `w.saveFile(types, "notes.txt", handler)` asks where to save, with a name proposed, and `w.openFolder(handler)` asks for a folder; the handler of these two receives the path, or `""`.
- The program reads and writes the files with `std.fs`. On Android the picker gives names as `content://...` instead of paths: `fs.readText`, `fs.readBytes`, `fs.writeText` and `fs.writeBytes` read and write them too, in a program with windows.
- `examples/23_notepad.cro` is a notepad that opens and saves its text.

## Programs with windows

### `[[window]]`

`[[window]]` before `main` makes the executable for Windows a program of the windows, which opens no console when it starts: a program with windows has no console behind them. `cromo build` and `cromo run` need no option for it. On Linux it changes nothing.

- The standard output and the standard error of such a program go nowhere, unless they are redirected, as in `notes.exe > log.txt 2> errors.txt`. While a program is being written, leave `[[window]]` out, to see its `println` in the console.
- Only `main` can have it, once, and it takes no arguments:

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

### Fatal errors

A program with `[[window]]` has no console where its errors would go. So when it fails, with a panic, an exception that leaves `main` or a stack overflow, a message box of Windows shows the message and the stack trace, and the program ends with its usual exit code, 101 for a panic and 1 for an exception, when the box is closed. If the standard error is redirected to a file or a pipe, the text goes there instead, as without `[[window]]`.

An exception that leaves a handler stops the program in the same way, with `uncaught exception in a thread:`, the message and the stack trace: in the message box with `[[window]]`, on the standard error without it.

### The icon

When the program is compiled for Windows, an icon next to the file with `main`, with the name of that file, `notes.ico` for `notes.cro`, or else `main.ico`, goes into the executable: Explorer and the taskbar show it. `cromo build` and `cromo run` take it by themselves. A file that is not an icon is an error: `cromo: the icon notes.ico is not a .ico file`. On Linux an executable has no icon, and the file is not read.

### What an executable needs

An executable with windows carries the whole of `std.gui` inside it: there is no DLL or `.so` to bring with it, and the fonts and the icons are in it too. On Linux it loads the parts of the system that draw the windows, X11 or Wayland, when it starts, so the same executable runs on both; it needs glibc 2.29 or later, as most distributions since 2019 have, and does not run on those with musl. Programs with windows cannot be compiled yet for `--target linux-arm64`.

## The same program on Android

```sh
cromo build --target android mail.cro    # writes mail.apk
cromo run --target android mail.cro      # installs it on the phone, starts it, shows its output
```

- `cromo build --target android` writes the APK by itself, with the program for phones, on ARM64, and for emulators, on x86-64, signed with a key for debugging that `cromo` makes the first time in `~/.cromo/android`. Neither the Android SDK nor Java is needed. The apps run from Android 8.
- `[[android("org.example.mail", "Mail")]]` before `main` gives the package of the app and the name under its icon; without it, the package is `org.cromo.` followed by the name of the file, and the name is that of the file. The icon is `mail.png`, or `main.png`, next to the file.
- `cromo run --target android` installs the app on the phone or on the emulator that `adb` sees, starts it, and shows what it prints until it ends; Ctrl+C closes it. `adb` comes with the platform tools of Android: `cromo` looks for it in `ANDROID_HOME` and in the `PATH`, and with more devices connected `ANDROID_SERIAL` chooses one.
- An app that uses `std.net`, directly or through `std.http`, gets the permission of the network by itself.

The package is names of letters, digits and `_`, each starting with a letter, joined by dots, at least two of them:

```cromo error
[[android("mail", "Mail")]]
i32 main() {
    ret 0
}
```

```output
error: the package of an app is names of letters, digits and _, each starting with a letter, joined by dots, as org.example.mail
```

On a phone the same program behaves as an app:

- An app has one window, which fills the screen between the bars of the system, without a title bar. Starting a second window is a panic: `an app of Android has one window: this is a second one`. The pages of an app are those of its navigation bar, or panels shown and hidden.
- The components, written for the size given to `gui.Window`, as 390 by 760, follow the screen with their anchors, also when the phone turns. `setSize`, `setResizable`, `minimize`, `maximize` and `restore` do nothing there, the title is not shown, and neither is the panel of `titleBar()`.
- The keyboard on the screen makes the content shorter, and a page scrolls to show the text box being written in. The navigation bar stays under the keyboard.
- The back key, or its gesture, closes a dialog that is open, else the window, and so the app, unless `w.onBack(handler)` decides otherwise, as going back to the first page. On the desktop the handlers of `onBack` are never called.
- A long press is the right button on a piece of rich text, and shows the tooltip on the other components.

## Summary

- A window and its components are handles, which lambdas capture by copy. One thread draws all the windows and runs the handlers, one at a time; the methods can be called from any thread, and the end of `main` waits for the windows.
- A handler must return soon: long work goes to a thread, which changes the components when it is done. An exception that leaves a handler stops the program.
- The window has its title, size and state, its events of the mouse, of the keys, of the size and of the closing, a title bar drawn by `std.gui` where components can go, and the themes light, dark and of the system.
- Every component has its place, anchors, visibility, state and tooltip. Labels, buttons, text boxes and areas, check boxes, switches, sliders, combo boxes, radio groups, progress bars, images and icons make the forms; groups, panels and splits hold them; lists, tables, tabs, segmented controls, rich text and canvases show the data; menus, dialogs, snackbars and the dialogs of the files talk with the user.
- `embed` puts files in the executable; `[[window]]` makes a program without a console, whose fatal errors show in a message box, and an icon next to the file goes into it.
- The same program is an app for Android with `--target android`: one window that fills the screen, with an app bar, a navigation bar and pages.
