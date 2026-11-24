# Changes

## 0.18.0

- Run and Debug: the buttons at the top right of a `.cro` file, the lines above `main`, the status bar, F5 and Ctrl+F5. A program runs with `cromo run`; it is debugged with the debugger of the C/C++ extension of Microsoft, which the extension offers to install.
- In the debugger a `string` is its text, a `list` its elements and a `map` its keys and values.
- Run test and Debug test above each `[[test]]`, and the tests of a file from the button of the title. Debugging a test needs cromo 0.98.1, for `cromo test --list`.
- The Cromo Guide in a panel next to the code, read from cromolang.org: the book of the title of the editor, or *Cromo: Open the Cromo Guide*. It is in the language of Visual Studio Code when the guide has it, or in the one of the setting `cromo.guideLanguage`.
- A breakpoint can be set in a `.cro` file.

## 0.15.0

- The color of `fma`.

## 0.14.0

- The colors of the vectors of SIMD, as `i16x16` and `f32x8`.

## 0.13.0

- The colors of the functions of the bits: `countOnes`, `trailingZeros`, `leadingZeros`, `rotateLeft` and `rotateRight`.

## 0.12.0

- The documentation of the names, from the `//` comments above their declarations, in the hover and in the completion, with the module of the name; and of the built-in functions and types, as `println` and `i64`.
- The names of a module after its name and a dot, as `fs.`, with their documentation.
- The help on the signature while the arguments of a call are written, with the parameter being written in evidence.

## 0.11.0

- The color of `embed`.

## 0.10.0

- The first version in the Visual Studio Marketplace and in Open VSX, as `cromolang.cromo`, with its icon and the links to cromolang.org, the guide and GitHub. It takes the place of `cromo.cromo-lang`, which came with the archives of Cromo up to 0.27.
- The colors of `wrapAdd`, `wrapSub` and `wrapMul`.

## 0.9

- The colors of the attributes, as `[[test]]` and `[[json("name")]]`.
- The language server `cromo lsp`: the errors while writing, hover, Go to Definition, the outline of the file, the fields and the methods after a dot, and formatting, also on save.
- `cromo.path` and `cromo.trace`.

## 0.1

- Syntax highlighting of the `.cro` files, comment toggling, closing brackets and indentation.
