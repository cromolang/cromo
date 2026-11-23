#pragma once

#include "ast.h"

namespace cromo {

// Resolves names and types, folds negative literals, inserts the lossless
// conversions (D33), parses the format strings of print/println (D38) and checks
// the rules of the language. Returns false if there are errors. The language
// server checks a module without `main` with requireMain false (D172).
bool check(Module& module, Diagnostics& diag, bool requireMain = true);

// Only the names of the modules (D423): what the first part of sema in Cromo checks.
bool checkNames(Module& module, Diagnostics& diag);

// The names and the types of the declarations, without the bodies (D423): the second
// part of sema in Cromo. With `list`, what it knows of them is printed (P89).
bool checkDeclarations(Module& module, Diagnostics& diag, bool list);

// What the analysis wrote on the bodies of the functions, on the standard output
// (P90): the third part of sema in Cromo prints the same.
void listTypes(const Module& module);

}  // namespace cromo
