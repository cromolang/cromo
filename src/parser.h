#pragma once

#include "ast.h"
#include "lexer.h"

#include <set>
#include <vector>

namespace cromo {

// Recursive descent parser. On an error it reports it and tries to resume from
// the next statement or declaration. `genericAngles`, if given, receives the
// offsets of the `<` and `>` that enclose type arguments and parameters, which
// cromo fmt writes without spaces (D161).
Module parse(const std::vector<Token>& tokens, Diagnostics& diag, std::set<size_t>* genericAngles = nullptr);

}  // namespace cromo
