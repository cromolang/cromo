# Cromo for Visual Studio Code

Support for [Cromo](https://cromolang.org), a general-purpose language with the syntax of C and C++, ownership without a garbage collector, lightweight tasks, and one tool that compiles and links.

## Features

- **Syntax highlighting** of the `.cro` files: keywords, types, numbers, strings with their format placeholders (`{}`, `{:.2f}`), comments, attributes and functions.
- **Errors as you type**, from the compiler itself.
- **Hover**: the type of a value or the signature of a function under the mouse, with its documentation: the `//` comments above its declaration, for the standard library and for your code.
- **Go to Definition** (F12) and the **outline** of the file.
- **Completion** of the fields and the methods after a dot, and of the names of a module after its name, as `fs.`, with their documentation.
- **Signature help** while the arguments of a call are written, with the parameter being written in evidence.
- **Formatting** with the rules of `cromo fmt`, also when a file is saved.
- Comment toggling (`Ctrl+/`), closing brackets and indentation after `{`.

## Requirements

Everything but the colors comes from the language server of the compiler, `cromo lsp`, so the extension needs `cromo`. It is one command:

```powershell
irm https://cromolang.org/install.ps1 | iex          # Windows, in PowerShell
```

```sh
curl -sSf https://cromolang.org/install.sh | sh     # Linux
```

The installers also install this extension, when the command `code` is there.

## Settings

| Setting | Meaning |
|---|---|
| `cromo.path` | The `cromo` executable that runs the language server. Empty: `build\cromo.exe` in a folder of the workspace, as while working on Cromo itself, else `cromo` from `PATH`. |
| `cromo.trace` | Writes every message between VS Code and `cromo lsp` in the *Cromo* output panel. |

Files of Cromo are formatted when they are saved (`editor.formatOnSave`); turn it off for `[cromo]` in the settings, if you prefer.

If `cromo` cannot start, a message says so. On Windows the server runs from a copy of `cromo.exe` in the temporary folder, so that `cromo.exe` can be built again while VS Code is open; when it changes, the server starts again.

## Learn more

- [The Cromo Guide](https://cromolang.org/book/), in English and in Italian, also as a PDF.
- The sources of the language, of this extension and the issues are on [GitHub](https://github.com/cromolang/cromo).

## Building the extension

From the folder of the repository, in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File tools\vscode-cromo\build-vsix.ps1 -Install
```

The script writes `tools/vscode-cromo/dist/cromo-<version>.vsix`, without npm, and with `-Install` installs it with `code --install-extension`. The grammar is in `syntaxes/cromo.tmLanguage.json`: when the keywords or the built-in types change, update it and raise `version` in `package.json`.
