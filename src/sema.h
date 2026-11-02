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

}  // namespace cromo
