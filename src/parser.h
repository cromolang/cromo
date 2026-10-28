#pragma once

#include "ast.h"
#include "lexer.h"

#include <vector>

namespace cromo {

// Recursive descent parser. On an error it reports it and tries to resume from
// the next statement or declaration.
Module parse(const std::vector<Token>& tokens, Diagnostics& diag);

}  // namespace cromo
