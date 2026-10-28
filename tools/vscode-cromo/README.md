# Cromo for VS Code

Syntax highlighting for `.cro` files of the Cromo language: keywords, types, numbers, strings with format placeholders (`{}`, `{:.2f}`), comments, functions and destructors. It also provides comment toggling (`Ctrl+/`), auto-closing brackets and indentation after `{`.

## Installation

From PowerShell, in the project folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vscode-cromo\build-vsix.ps1 -Install
```

The script creates `tools/vscode-cromo/dist/cromo-lang-<version>.vsix` and installs it with `code --install-extension`. If VS Code is open, reload the window (command *Developer: Reload Window*).

## Maintenance

The grammar is in `syntaxes/cromo.tmLanguage.json`. When keywords or built-in types change in `docs/decisions.md`, update it too; then bump `version` in `package.json` and run the script again.
