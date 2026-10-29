# Cromo for VS Code

Syntax highlighting for `.cro` files of the Cromo language: keywords, types, numbers, strings with format placeholders (`{}`, `{:.2f}`), comments, functions and destructors. It also provides comment toggling (`Ctrl+/`), auto-closing brackets and indentation after `{`.

It starts the language server `cromo lsp` (D170): the errors while writing, the type or the signature under the mouse, *Go to Definition* (F12), the outline of the file, the fields and methods proposed after a dot (D179) and formatting, also when a file is saved (D171, D164). It runs the `cromo.exe` of the setting `cromo.path`; without it, `build\cromo.exe` in a folder of the workspace, as in the folder of cromo itself, or else `cromo` from PATH. If it cannot start, a message says so. On Windows the server runs from a copy of `cromo.exe` in the temporary folder, so that `cromo.exe` can be built again while VS Code is open; when it changes, the server starts again. The messages of the server are in the *Cromo* output panel; with `cromo.trace` also every message of the protocol.

## Installation

From PowerShell, in the project folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vscode-cromo\build-vsix.ps1 -Install
```

The script creates `tools/vscode-cromo/dist/cromo-lang-<version>.vsix` and installs it with `code --install-extension`. If VS Code is open, reload the window (command *Developer: Reload Window*).

## Maintenance

The grammar is in `syntaxes/cromo.tmLanguage.json`. When keywords or built-in types change in `docs/decisions.md`, update it too; then bump `version` in `package.json` and run the script again.
