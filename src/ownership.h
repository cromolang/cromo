#pragma once

#include "ast.h"

namespace cromo {

// Checks that no variable is used after a `move`, following every path through
// branches and loops (D23, D77, D81). Runs on the AST checked by sema. Returns
// false if there are errors.
bool checkMoves(const Module& module, Diagnostics& diag);

}  // namespace cromo
